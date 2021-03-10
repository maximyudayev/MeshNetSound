/*
    TODO:
        I.------make possible to update endpoint buffer length on demand.
        II.-----expand versatility by checking the actual audio format.
        III.----at compile or run time request user about the intended size of WASAN
                and choose bit width of the nChannels and all corresponding 
                supporting variables accordingly.
        IV.-----add intelligence to the function to choose time- vs. frequency-based resampling,
                depending on the size of the RESAMPLEFMT_T.pBuffer.
        V.------provide for case when silence was written to file.
        VI.-----convert implementation of grouping into a separate class that contains AudioBuffer instances.
        VII.----support thread pool for concurrent AudioBuffer operations like PullData, PullData and Resample
                for improved performance.
        IIX.----add synchronization mechanisms on buffer offset variables.
        IX.-----convert WASAPI reading into event-driven.
        X.------add support for condition variable thread sync between ring buffer reading and writing threads.
        XI.-----add source and sink AudioBuffer and UDPAudioBuffer derived classes for modularity.
                sink version should have channel mask to multiplex between ring buffer channels:
        XII.----add logic to temporarily prevent writer from writing data into the buffer where it may
                overwrite data currently being processed by reader, i.e case when loading a packet into a full
                buffer with lagging reading which might overwrite oldest samples while reader is resampling
                or outputing to render device or other consumer: for now assumed that consumer is as fast
                as producer.
        XIII.---make a comprehensive drawing of the complex RW lock mechanism and proof that it ensures
                safety for buffer read and write offsets as well as does not create a deadlock.
        XIV.----consider changing pBuffer from 2D array into a 1D array of size nBufferSize-by-nChannels:
                a. potentially better performance and caching.
                b. might be difficult to allocate single heap chunk for a large buffer.
*/

#include "AudioBuffer.h"
#include "AudioEffect.h"
#include "RingBufferChannel.h"

UINT32* AudioBuffer::pGroupId{ NULL };
UINT32 AudioBuffer::nNewInstance{ 0 };
UINT32 AudioBuffer::nNewGroupId{ 0 };
UINT32 AudioBuffer::nGroups{ 0 };

AudioBuffer::AudioBuffer(std::string filename, UINT32 nMember)
{
    this->sFilename = filename;
    
    // Indicate affiliation of this AudioBuffer instance to a group
    // for clustering AudioBuffers by membership to a ring buffer
    this->nMemberId = nMember;

    // Indicate to this AudioBuffer instance its identifier
    this->nInstance = AudioBuffer::nNewInstance;
    // Update the new instance identifier for the next Class member
    AudioBuffer::nNewInstance++;

    this->pResampler = new Resampler();
}

AudioBuffer::~AudioBuffer()
{
    // Cleans up WAV files
    if (this->bOutputWAV)
    {
        for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        {
            // Gets complete file length
            this->nOriginalFileLength[i] = ftell(this->fOriginalOutputFiles[i]);

            // Fills missing data chunk size data in the WAV file (Data) headers
            fseek(this->fOriginalOutputFiles[i], 64, SEEK_SET); // updates file pointer into the byte identifying data chunk size
            DWORD dataChunkSize = this->nOriginalFileLength[i] - 68;
            fwrite(&dataChunkSize, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);

            // Fills missing file size data in the WAV file (RIFF) headers
            fseek(this->fOriginalOutputFiles[i], 4, SEEK_SET); // updates file pointer into the byte identifying file size
            DWORD fileChunkSize = this->nOriginalFileLength[i] - 8;
            fwrite(&fileChunkSize, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);

            // Closes WAV files
            fclose(this->fOriginalOutputFiles[i]);
        }
        free(this->fOriginalOutputFiles);
        free(this->nOriginalFileLength);

        if (this->tResampleFmt.nUpsample > 1 || this->tResampleFmt.nDownsample > 1)
        {
            for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
            {
                // Gets complete file length
                this->nResampledFileLength[i] = ftell(this->fResampledOutputFiles[i]);

                // Fills missing data chunk size data in the WAV file (Data) headers
                fseek(this->fResampledOutputFiles[i], 64, SEEK_SET); // updates file pointer into the byte identifying data chunk size
                DWORD dataChunkSize = this->nResampledFileLength[i] - 68;
                fwrite(&dataChunkSize, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);

                // Fills missing file size data in the WAV file (RIFF) headers
                fseek(this->fResampledOutputFiles[i], 4, SEEK_SET); // updates file pointer into the byte identifying file size
                DWORD fileChunkSize = this->nResampledFileLength[i] - 8;
                fwrite(&fileChunkSize, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);

                // Closes WAV files
                fclose(this->fResampledOutputFiles[i]);
            }
            free(this->fResampledOutputFiles);
            free(this->nResampledFileLength);
        }
    }

    free(this->tResampleFmt.pBuffer);

    delete this->pResampler;
}

HRESULT AudioBuffer::CreateBufferGroup(UINT32* pGroup)
{
    if (AudioBuffer::nGroups == 0)
    {
        pGroupId            = (UINT32*)malloc(sizeof(UINT32));
        
        if (pGroupId == NULL)
            return ENOMEM;
    }
    else
    {
        // Resize array of new channel offsets
        UINT32* dummyGroup      = (UINT32*)realloc(pGroupId, (AudioBuffer::nGroups + 1) * sizeof(UINT32));
        
        if (dummyGroup != NULL)
        {
            pGroupId = dummyGroup;
        }
        else
            return ENOMEM;
    }

    // Write the "GUID" of the group into the user variable
    *pGroup = AudioBuffer::nNewGroupId++;

    // Write the index of the new group into the array of "GUID's"
    pGroupId[AudioBuffer::nGroups] = *pGroup;
    
    // Increment the number of existing groups
    AudioBuffer::nGroups++;

    return ERROR_SUCCESS;
}

UINT32 AudioBuffer::GetBufferGroupIndex(UINT32 nGroup)
{
    UINT32 id = 0;
    while (AudioBuffer::pGroupId[id] != nGroup && id < AudioBuffer::nGroups) id++;

    return id;
}

HRESULT AudioBuffer::RemoveBufferGroup(UINT32 nGroup)
{
    if (AudioBuffer::nGroups == 1)
    {
        free(AudioBuffer::pGroupId);
        AudioBuffer::nNewGroupId = 0;
    }
    else
    {
        UINT32 id = GetBufferGroupIndex(nGroup);

        // Remove this GUID and align all other GUID positions with the new length of the array
        for (UINT32 i = 0, j = 0; j < AudioBuffer::nGroups; i++, j++)
        {
            if (j == id) j++;
            AudioBuffer::pGroupId[i] = AudioBuffer::pGroupId[j];
        }

        // Reduce heap space for the arrays
        UINT32* dummyGroup = (UINT32*)realloc(pGroupId, (AudioBuffer::nGroups - 1) * sizeof(UINT32));

        if (dummyGroup != NULL)
        {
            pGroupId = dummyGroup;
        }
        else
            return ENOMEM;
    }

    AudioBuffer::nGroups--;

    return ERROR_SUCCESS;
}

UINT32 AudioBuffer::GetChannelNumber()
{
    return this->tEndpointFmt.nChannels;
}

RingBufferChannel** AudioBuffer::GetRingBufferChannel()
{
    return this->tResampleFmt.pBuffer;
}

HRESULT AudioBuffer::SetRingBufferChannel(RingBufferChannel** pChannelArray)
{
    this->tResampleFmt.pBuffer = pChannelArray;
    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::SetFormat(WAVEFORMATEX* pwfx)
{
    std::cout << "Samples per second: " << pwfx->nSamplesPerSec << '\n';
    std::cout << "Number of channels: " << pwfx->nChannels << '\n';
    std::cout << "Block alignment: " << pwfx->nBlockAlign << '\n';
    std::cout << "Bits per sample: " << pwfx->wBitsPerSample << '\n';
    std::cout << "Extra info size: " << pwfx->cbSize << '\n';
    
    this->tEndpointFmt.nBlockAlign = pwfx->nBlockAlign;
    this->tEndpointFmt.nChannels = pwfx->nChannels;
    this->tEndpointFmt.wBitsPerSample = pwfx->wBitsPerSample;
    this->tEndpointFmt.nBytesInSample = pwfx->wBitsPerSample / 8;
    this->tEndpointFmt.wFormatTag = pwfx->wFormatTag;
    this->tEndpointFmt.nSamplesPerSec = pwfx->nSamplesPerSec;
    this->tEndpointFmt.nAvgBytesPerSec = pwfx->nAvgBytesPerSec;
    this->tEndpointFmt.cbSize = pwfx->cbSize;

    if (pwfx->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        this->tEndpointFmt.subFormat = waveFormatExtensible->SubFormat;
        this->tEndpointFmt.channelMask = waveFormatExtensible->dwChannelMask;
        this->tEndpointFmt.wValidBitsPerSample = waveFormatExtensible->Samples.wValidBitsPerSample;

        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) printf("Variable type is: float\n\n");
        else if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) printf("Variable type is: PCM\n\n");
    }
    
    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::InitBuffer(UINT32* nEndpointBufferSize, RingBufferChannel** pCircularBuffer,
    DWORD nUpsample, DWORD nDownsample)
{
    this->tEndpointFmt.nBufferSize = nEndpointBufferSize;

    this->pRingBufferChannel = pCircularBuffer;
    this->tResampleFmt.nUpsample = nUpsample;
    this->tResampleFmt.nDownsample = nDownsample;
    this->tResampleFmt.fFactor = (FLOAT)nUpsample / (FLOAT)nDownsample;
    
    // Set LP scaling factor in the associated resampler according to the resampling factor
    this->pResampler->SetLPScaling(this->tResampleFmt.fFactor);
    // Update the minimum number of ring buffer samples required for safe SRC prior to output to render device
    // value is unused if the device is a capture device
    this->UpdateMinFramesOut();

    std::cout   << MSG "The resample factor of device " 
                << this->nInstance 
                << " is: " 
                << this->tResampleFmt.fFactor 
                << std::endl;

    UINT32 id = AudioBuffer::GetBufferGroupIndex(this->nMemberId);

    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::InitWAV()
{
    this->bOutputWAV = TRUE;
    this->fOriginalOutputFiles = (FILE**)malloc(this->tEndpointFmt.nChannels * sizeof(FILE*));
    this->nOriginalFileLength = (DWORD*)malloc(this->tEndpointFmt.nChannels * sizeof(DWORD));

    WORD ch = 1;
    DWORD fmtLength = 40;
    DWORD newAvgBytesPerSec = this->tEndpointFmt.nAvgBytesPerSec / this->tEndpointFmt.nChannels;
    WORD newBlockAlign = this->tEndpointFmt.nBlockAlign / this->tEndpointFmt.nChannels;

    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
    {
        for (UINT8 attempts = 0; attempts < WAV_FILE_OPEN_ATTEMPTS; attempts++)
        {
            this->fOriginalOutputFiles[i] = fopen(("Audio Files/" + this->sFilename + std::to_string(i + 1) + ".wav").c_str(), "wb");

            if (this->fOriginalOutputFiles[i] != NULL) break;
            else if (attempts == WAV_FILE_OPEN_ATTEMPTS - 1 && this->fOriginalOutputFiles[i] == NULL) 
                return ERROR_TOO_MANY_OPEN_FILES;
        }

        // RIFF Header
        fputs("RIFF----WAVEfmt ", this->fOriginalOutputFiles[i]);
        // Format-Section
        fwrite(&fmtLength, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.wFormatTag, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&ch, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.nSamplesPerSec, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&newBlockAlign, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.wBitsPerSample, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.cbSize, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.wValidBitsPerSample, sizeof(WORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.channelMask, sizeof(DWORD), 1, this->fOriginalOutputFiles[i]);
        fwrite(&this->tEndpointFmt.subFormat, sizeof(GUID), 1, this->fOriginalOutputFiles[i]);
        // Data-Section
        fputs("data----", this->fOriginalOutputFiles[i]);
    }

    if (this->tResampleFmt.nUpsample > 1 || this->tResampleFmt.nDownsample > 1)
    {
        this->fResampledOutputFiles = (FILE**)malloc(this->tEndpointFmt.nChannels * sizeof(FILE*));
        this->nResampledFileLength = (DWORD*)malloc(this->tEndpointFmt.nChannels * sizeof(DWORD));

        newAvgBytesPerSec = newAvgBytesPerSec * this->tResampleFmt.nUpsample / this->tResampleFmt.nDownsample;
        DWORD newResampledSamplesPerSec = this->tEndpointFmt.nSamplesPerSec * this->tResampleFmt.nUpsample / this->tResampleFmt.nDownsample;

        for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        {
            for (UINT8 attempts = 0; attempts < WAV_FILE_OPEN_ATTEMPTS; attempts++)
            {
                this->fResampledOutputFiles[i] = fopen(("Audio Files/" + this->sFilename + std::to_string(i + 1) + " Resampled.wav").c_str(), "wb");

                if (this->fResampledOutputFiles[i] != NULL) break;
                else if (attempts == WAV_FILE_OPEN_ATTEMPTS - 1 && this->fResampledOutputFiles[i] == NULL) return ERROR_TOO_MANY_OPEN_FILES;
            }

            // RIFF Header
            fputs("RIFF----WAVEfmt ", this->fResampledOutputFiles[i]);
            // Format-Section
            fwrite(&fmtLength, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.wFormatTag, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&ch, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&newResampledSamplesPerSec, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&newBlockAlign, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.wBitsPerSample, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.cbSize, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.wValidBitsPerSample, sizeof(WORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.channelMask, sizeof(DWORD), 1, this->fResampledOutputFiles[i]);
            fwrite(&this->tEndpointFmt.subFormat, sizeof(GUID), 1, this->fResampledOutputFiles[i]);
            // Data-Section
            fputs("data----", this->fResampledOutputFiles[i]);
        }
    }

    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::PushData(BYTE* pData)
{
    BYTE* pDataDummy = pData;
    UINT32 nSamplesWritten = 0;
    BOOL bReadOffsetLock = FALSE;
    
    // Modulo operator allows to go in circular fashion so no code duplication is required   

    if (pData != NULL)
        // Save original signal to file if user requested
        if (this->bOutputWAV)
            for (UINT32 j = 0; j < *this->tEndpointFmt.nBufferSize; j++, pDataDummy += this->tEndpointFmt.nBlockAlign)
                for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
                    fwrite(((FLOAT*)pDataDummy) + i, sizeof(FLOAT), 1, this->fOriginalOutputFiles[i]);

    // Obtain all necessary locks for thread safety
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        this->pRingBufferChannel[i]->PrepareToPullDataIn();

    // When user calls AudioBuffer::PullData with pData = NULL, AUDCLNT_BUFFERFLAGS_SILENT flag is set
    // results in keeping 0's bulk set in previous step in the audio buffer data structure
    if (pData != NULL)
    {
        // Perform resampling if resampling factor is other than 1
        if (this->tResampleFmt.fFactor != 1.0)
        {
            // Sample rate convert the packet and place in circular buffer
            nSamplesWritten = this->pResampler->Resample(
                this->tResampleFmt, 
                this->tEndpointFmt,
                &pData, 
                (void*)this->pRingBufferChannel,
                0,
                TRUE);

            // Write freshly resampled stream into file if user requested
            if (this->bOutputWAV)
            {
                for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
                {
                    UINT32 nBufferSize = this->pRingBufferChannel[i]->GetBufferSize();
                    UINT32 nWriteOffset = this->pRingBufferChannel[i]->GetWriteOffset();
                    FLOAT* pBuffer = this->pRingBufferChannel[i]->GetBufferPointer();

                    // If data was filled at most up to the end of memory allocated for ring buffer
                    if (nWriteOffset + nSamplesWritten <= nBufferSize)
                        // Write to file data from ring buffer from the previous offset up till the number of resampled frames
                        fwrite(pBuffer + nWriteOffset,
                            sizeof(FLOAT),
                            nSamplesWritten,
                            this->fResampledOutputFiles[i]);
                    // If more data was filled in the buffer than the amount of free contigious memory in the ring buffer, go circularly
                    else
                    {
                        // Write data to file from ring buffer's offset up till the end of the ring buffer
                        fwrite(pBuffer + nWriteOffset,
                            sizeof(FLOAT),
                            nBufferSize - nWriteOffset,
                            this->fResampledOutputFiles[i]);

                        // Write data to file from ring buffer's beginnig up till the remaining number of resampled frames
                        fwrite(pBuffer,
                            sizeof(FLOAT),
                            (nWriteOffset + nSamplesWritten) % nBufferSize,
                            this->fResampledOutputFiles[i]);
                    }
                }
            }
        }
        else // If factor is 1, right data straight into the ring buffer, don't write resampled file
        {
            nSamplesWritten = *this->tEndpointFmt.nBufferSize;
            pDataDummy = pData;

            for (UINT32 j = 0; j < *this->tEndpointFmt.nBufferSize; j++, pDataDummy += this->tEndpointFmt.nBlockAlign)
                for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
                    *(this->pRingBufferChannel[i]->GetBufferPointer() + (this->pRingBufferChannel[i]->GetWriteOffset() + j) % this->pRingBufferChannel[i]->GetBufferSize()) = *(((FLOAT*)pData) + i);
        }
    }
    else
    {
        // TODO: provide for case when silence was written to file
    }

    //-------------------- End --------------------//
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
    {
        UINT32 nWriteOffsetOld = this->pRingBufferChannel[i]->GetWriteOffset(), nReadOffsetOld = this->pRingBufferChannel[i]->GetReadOffset();
        UINT32 nWriteOffsetNew = (nWriteOffsetOld + nSamplesWritten) % this->pRingBufferChannel[i]->GetBufferSize();
        BOOL bWriteAheadReadByLap = this->pRingBufferChannel[i]->GetWriteAheadReadByLap();

        // Update the offset to position after the last frame of the current chunk
        this->pRingBufferChannel[i]->SetWriteOffset(nWriteOffsetNew);

        // If write offset exceeded read offset by a whole lap, update read offset to the position of write offset,
        // hence drop frames that were delayed and try to catch up from the new position
        if (((nWriteOffsetOld > nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
                nWriteOffsetNew > nReadOffsetOld&&
                nWriteOffsetNew < nWriteOffsetOld) ||
            ((nWriteOffsetOld < nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
                nWriteOffsetNew < nReadOffsetOld &&
                nWriteOffsetNew < nWriteOffsetOld) ||
            ((nWriteOffsetOld < nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
                nWriteOffsetNew > nReadOffsetOld))
        {
            // Set read offset from the same point as the last non-overwritten sample
            this->pRingBufferChannel[i]->SetReadOffset(nWriteOffsetNew);
        }

        // Write offset catching up on read offset from the left (lower array indices)
        // indicating that if both match and write offset increased, then read offset is 
        // about to read overwritten newest data - not good, jump to the oldest valid instead
        this->pRingBufferChannel[i]->SetWriteAheadReadByLap(
            (nWriteOffsetNew <= nReadOffsetOld && nWriteOffsetOld > nReadOffsetOld) ||
            (nWriteOffsetNew <= nReadOffsetOld && nWriteOffsetOld < nWriteOffsetNew)
        );
    }

    // Release all necessary locks for thread safety
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        this->pRingBufferChannel[i]->FinishToPullDataIn();
    
    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::PullData(BYTE* pData, UINT32 nFrames)
{
    UINT32 nSamplesRead = nFrames;

    // Obtain all necessary locks for thread safety
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        this->pRingBufferChannel[i]->PrepareToPushDataOut();

    // Perform resampling if resampling factor is other than 1
    if (this->tResampleFmt.fFactor != 1.0)
    {
        // Sample rate convert the packet and place in output device's buffer
        nSamplesRead = this->pResampler->Resample(
            this->tResampleFmt,
            this->tEndpointFmt,
            (void*)this->pRingBufferChannel,
            &pData,
            nFrames,
            FALSE);
    }
    else // If factor is 1, right data straight into the device's buffer
    {
        // Push nFrames from the ring buffer into the endpoint buffer for playback
        for (UINT32 j = 0; j < nFrames; j++, pData += this->tEndpointFmt.nBlockAlign)
            for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
                *(((FLOAT*)pData) + i) = *(this->pRingBufferChannel[i]->GetBufferPointer() + (this->pRingBufferChannel[i]->GetReadOffset() + j) % this->pRingBufferChannel[i]->GetBufferSize());
    }

    // Update read pointer respecting the circular buffer traversal
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
    {
        this->pRingBufferChannel[i]->SetReadOffset((this->pRingBufferChannel[i]->GetReadOffset() + nSamplesRead) % this->pRingBufferChannel[i]->GetBufferSize());

        // If read offset caught up on the write offset from the left, clear the boolean
        this->pRingBufferChannel[i]->SetWriteAheadReadByLap(this->pRingBufferChannel[i]->GetReadOffset() > this->pRingBufferChannel[i]->GetWriteOffset());
    }

    // Release all necessary locks for thread safety
    for (UINT32 i = 0; i < this->tEndpointFmt.nChannels; i++)
        this->pRingBufferChannel[i]->FinishToPushDataOut();

    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::SetEndpointBufferSize(UINT32 nFrames)
{
    // Set buffer size to the new number
    *this->tEndpointFmt.nBufferSize = nFrames;
    // Update required minimum number of frames for safe SRC prior to output from output ring buffer into render device buffer
    this->UpdateMinFramesOut();
    return ERROR_SUCCESS;
}

HRESULT AudioBuffer::UpdateMinFramesOut()
{
    this->nMinFramesOut = ceil(*this->tEndpointFmt.nBufferSize * this->tResampleFmt.fFactor);
    return ERROR_SUCCESS;
}

UINT32 AudioBuffer::GetMinFramesOut()
{
    return this->nMinFramesOut;
}
