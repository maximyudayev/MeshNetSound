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
        VII.----split AudioBuffer into 2 child classes, SinkBuffer and SourceBuffer, which inherit common
                function from AudioBuffer.
        IIX.----add additional offset variable to account for time delay adjustment.
*/

#include "AudioBuffer.h"

/// <summary>
/// <para>Static class member variable.</para>
/// <para>Keeps track of the number of instances created and is useful
/// for objects to know their order of indices.</para>
/// </summary>
UINT32* AudioBuffer::pNewChannelOffset{ NULL };
UINT32* AudioBuffer::pGroupId{ NULL };
UINT32 AudioBuffer::nNewInstance{ 0 };
UINT32 AudioBuffer::nNewGroupId{ 0 };
UINT32 AudioBuffer::nGroups{ 0 };

/// <summary>
/// <para>AudioBuffer constructor.</para>
/// <para>Calling thread must call AudioBuffer::SetFormat() and AudioBuffer::InitBuffer()
/// before using any other functionality of the class.</para>
/// <para>Note: The AudioBuffer does not limit, but assumes the maximum number
/// of channels per arbitrary device to not exceed 2^32. That includes
/// "virtual" devices (i.e. aggregated WASAN devices connected to another,
/// each having aggregated own devices). In case larger number of aggregated
/// channels is required for a truly enormous WASAN (i.e. up to 2^64 aggregated channels
/// in the network, composed of N multichanneled connected devices, PCs, BT sets, etc.)
/// only the data type of all variables (i.e. in loops) operating with nChannels
/// variable, along with the variable itself, must be updated to the desired bit width
/// (i.e. UINT8, UINT16, UINT32, UINT64).</para>
/// </summary>
/// <param name="filename">- stores filename used for writing WAV files.</param>
/// <param name="nMember">- stores AudioBuffer group membership ID to cluster by ring buffer membership.</param>
AudioBuffer::AudioBuffer(std::string filename, UINT32 nMember)
{
    sFilename = filename;
    
    // Indicate affiliation of this AudioBuffer instance to a group
    // for clustering AudioBuffers by membership to a ring buffer
    nMemberId = nMember;

    // Indicate to this AudioBuffer instance its identifier
    nInstance = AudioBuffer::nNewInstance;
    // Update the new instance identifier for the next Class member
    AudioBuffer::nNewInstance++;

    pResampler = new Resampler();
}

/// <summary>
/// <para>AudioBuffer destructor.</para>
/// <para>Closes WAV files, if used, and frees alloc'ed memory of FILE arrays and file lengths.</para>
/// </summary>
AudioBuffer::~AudioBuffer()
{
    // Cleans up WAV files
    if (bOutputWAV)
    {
        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
        {
            // Gets complete file length
            nOriginalFileLength[i] = ftell(fOriginalOutputFiles[i]);

            // Fills missing data chunk size data in the WAV file (Data) headers
            fseek(fOriginalOutputFiles[i], 64, SEEK_SET); // updates file pointer into the byte identifying data chunk size
            DWORD dataChunkSize = nOriginalFileLength[i] - 68;
            fwrite(&dataChunkSize, sizeof(DWORD), 1, fOriginalOutputFiles[i]);

            // Fills missing file size data in the WAV file (RIFF) headers
            fseek(fOriginalOutputFiles[i], 4, SEEK_SET); // updates file pointer into the byte identifying file size
            DWORD fileChunkSize = nOriginalFileLength[i] - 8;
            fwrite(&fileChunkSize, sizeof(DWORD), 1, fOriginalOutputFiles[i]);

            // Closes WAV files
            fclose(fOriginalOutputFiles[i]);
        }
        free(fOriginalOutputFiles);
        free(nOriginalFileLength);

        if (tResampleFmt.nUpsample > 1 || tResampleFmt.nDownsample > 1)
        {
            for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
            {
                // Gets complete file length
                nResampledFileLength[i] = ftell(fResampledOutputFiles[i]);

                // Fills missing data chunk size data in the WAV file (Data) headers
                fseek(fResampledOutputFiles[i], 64, SEEK_SET); // updates file pointer into the byte identifying data chunk size
                DWORD dataChunkSize = nResampledFileLength[i] - 68;
                fwrite(&dataChunkSize, sizeof(DWORD), 1, fResampledOutputFiles[i]);

                // Fills missing file size data in the WAV file (RIFF) headers
                fseek(fResampledOutputFiles[i], 4, SEEK_SET); // updates file pointer into the byte identifying file size
                DWORD fileChunkSize = nResampledFileLength[i] - 8;
                fwrite(&fileChunkSize, sizeof(DWORD), 1, fResampledOutputFiles[i]);

                // Closes WAV files
                fclose(fResampledOutputFiles[i]);
            }
            free(fResampledOutputFiles);
            free(nResampledFileLength);
        }
    }

    delete pResampler;
}

/// <summary>
/// <para>Creates an alias for a group of AudioBuffer's to separate each by
/// membership to a particular ring buffer.</para>
/// </summary>
/// <param name="pGroup">- pointer to the location to store group membership ID.</param>
/// <returns></returns>
HRESULT AudioBuffer::CreateBufferGroup(UINT32* pGroup)
{
    if (AudioBuffer::nGroups == 0)
    {
        pNewChannelOffset   = (UINT32*)malloc(sizeof(UINT32));
        pGroupId            = (UINT32*)malloc(sizeof(UINT32));
        
        if (pNewChannelOffset == NULL ||
            pGroupId == NULL)
            return ENOMEM;
    }
    else
    {
        // Resize array of new channel offsets
        UINT32* dummyChannel    = (UINT32*)realloc(pNewChannelOffset, (AudioBuffer::nGroups + 1) * sizeof(UINT32));
        UINT32* dummyGroup      = (UINT32*)realloc(pGroupId, (AudioBuffer::nGroups + 1) * sizeof(UINT32));
        
        if (dummyChannel != NULL &&
            dummyGroup != NULL)
        {
            pNewChannelOffset = dummyChannel;
            pGroupId = dummyGroup;
        }
        else
            return ENOMEM;
    }
    // Set the channel offset of the new group to 0
    pNewChannelOffset[AudioBuffer::nGroups] = 0;

    // Write the "GUID" of the group into the user variable
    *pGroup = AudioBuffer::nNewGroupId++;

    // Write the index of the new group into the array of "GUID's"
    pGroupId[AudioBuffer::nGroups] = *pGroup;
    
    // Increment the number of existing groups
    AudioBuffer::nGroups++;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Retreives static array index corresponding to nGroup "GUID"
/// to index into corresponding static channel offset.</para>
/// </summary>
/// <param name="nGroup">- the group "GUID" to look up array index for.</param>
/// <returns></returns>
UINT32 AudioBuffer::GetBufferGroupIndex(UINT32 nGroup)
{
    UINT32 id = 0;
    while (AudioBuffer::pGroupId[id] != nGroup && id < AudioBuffer::nGroups) id++;

    return id;
}

/// <summary>
/// <para>Removes AudioBuffer group's index from array of existing ones,
/// decrements the number of groups remaining.</para>
/// </summary>
/// <param name="nGroup">- the group "GUID" to remove.</param>
/// <returns></returns>
HRESULT AudioBuffer::RemoveBufferGroup(UINT32 nGroup)
{
    if (AudioBuffer::nGroups == 1)
    {
        free(AudioBuffer::pNewChannelOffset);
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
        UINT32* dummyChannel = (UINT32*)realloc(pNewChannelOffset, (AudioBuffer::nGroups - 1) * sizeof(UINT32));
        UINT32* dummyGroup = (UINT32*)realloc(pGroupId, (AudioBuffer::nGroups - 1) * sizeof(UINT32));

        if (dummyChannel != NULL &&
            dummyGroup != NULL)
        {
            pNewChannelOffset = dummyChannel;
            pGroupId = dummyGroup;
        }
        else
            return ENOMEM;
    }

    AudioBuffer::nGroups--;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Copies WAVEFORMATEX data received from WASAPI for all audio related operations.</para>
/// </summary>
/// <remarks>
/// WAVEFORMATEX uses 16bit value for channels, hence maximum intrinsic number of virtual channels is 65'536.
/// </remarks>
/// <param name="pwfx">- WAVEFORMATEX pointer obtained from calling thread after WASAPI inits AudioClient</param>
/// <returns>ERROR_SUCCESS</returns>
HRESULT AudioBuffer::SetFormat(WAVEFORMATEX* pwfx)
{
    std::cout << "Samples per second: " << pwfx->nSamplesPerSec << '\n';
    std::cout << "Number of channels: " << pwfx->nChannels << '\n';
    std::cout << "Block alignment: " << pwfx->nBlockAlign << '\n';
    std::cout << "Bits per sample: " << pwfx->wBitsPerSample << '\n';
    std::cout << "Extra info size: " << pwfx->cbSize << '\n';
    
    tEndpointFmt.nBlockAlign = pwfx->nBlockAlign;
    tEndpointFmt.nChannels = pwfx->nChannels;
    tEndpointFmt.wBitsPerSample = pwfx->wBitsPerSample;
    tEndpointFmt.nBytesInSample = pwfx->wBitsPerSample / 8;
    tEndpointFmt.wFormatTag = pwfx->wFormatTag;
    tEndpointFmt.nSamplesPerSec = pwfx->nSamplesPerSec;
    tEndpointFmt.nAvgBytesPerSec = pwfx->nAvgBytesPerSec;
    tEndpointFmt.cbSize = pwfx->cbSize;

    if (pwfx->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        tEndpointFmt.subFormat = waveFormatExtensible->SubFormat;
        tEndpointFmt.channelMask = waveFormatExtensible->dwChannelMask;
        tEndpointFmt.wValidBitsPerSample = waveFormatExtensible->Samples.wValidBitsPerSample;

        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) printf("Variable type is: float\n\n");
        else if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) printf("Variable type is: PCM\n\n");
    }
    
    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Initializes stream resample properties and endpoint buffer size.</para>
/// <para>Note: currently not thread-safe since AudioBuffer::nNextChannelOffset
/// does not yet have a mutex. Each AudioBuffer should call InitBuffer sequentially
/// in order to properly setup circular buffer channel offsets for each channel.</para>
/// </summary>
/// <param name="nEndpointBufferSize">- length of endpoint buffer per channel</param>
/// <param name="pCircularBuffer">- pointer to memory of the first channel of the corresponding 
/// device in the circular buffer of the aggregator</param>
/// <param name="nCircularBufferSize">- size of circular buffer of the aggregator</param>
/// <param name="nUpsample">- upsampling factor</param>
/// <param name="nDownsample">- downsampling factor</param>
/// <returns>
/// ERROR_SUCCESS if buffer set up succeeded.
/// </returns>
HRESULT AudioBuffer::InitBuffer(UINT32* nEndpointBufferSize, FLOAT** pCircularBuffer,
    UINT32* nCircularBufferSize, DWORD nUpsample, DWORD nDownsample)
{
    tEndpointFmt.nBufferSize = nEndpointBufferSize;

    tResampleFmt.pBuffer = pCircularBuffer;
    tResampleFmt.nBufferSize = nCircularBufferSize;
    tResampleFmt.nWriteOffset = 0;
    tResampleFmt.nReadOffset = 0;
    tResampleFmt.nUpsample = nUpsample;
    tResampleFmt.nDownsample = nDownsample;
    tResampleFmt.fFactor = (FLOAT)nUpsample / (FLOAT)nDownsample;
    
    // Set LP scaling factor in the associated resampler according to the resampling factor
    pResampler->SetLPScaling(tResampleFmt.fFactor);
    
    std::cout << MSG "The resample factor of device " << nInstance << " is: " << tResampleFmt.fFactor << std::endl;

    UINT32 id = AudioBuffer::GetBufferGroupIndex(nMemberId);
    // Indicate to this AudioBuffer instance its absolute buffer channel position
    nChannelOffset = AudioBuffer::pNewChannelOffset[id];
    // Update the new channel position for the next Class member
    AudioBuffer::pNewChannelOffset[id] += tEndpointFmt.nChannels;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Initializes .WAV file headers for each channel of a device.
/// If original stream is resampled, also initializes .WAV file headers 
/// for the resampled version of the dat.</para>
/// </summary>
/// <returns>
/// <para>ERROR_TOO_MANY_OPEN_FILES if fopen fails for FILE_OPEN_ATTEMPTS times.</para>
/// <para>ERROR_SUCCESS if WAV file for each channel is properly initialized.</para>
/// </returns>
HRESULT AudioBuffer::InitWAV()
{
    bOutputWAV = TRUE;
    fOriginalOutputFiles = (FILE**)malloc(tEndpointFmt.nChannels * sizeof(FILE*));
    nOriginalFileLength = (DWORD*)malloc(tEndpointFmt.nChannels * sizeof(DWORD));

    WORD ch = 1;
    DWORD fmtLength = 40;
    DWORD newAvgBytesPerSec = tEndpointFmt.nAvgBytesPerSec / tEndpointFmt.nChannels;
    WORD newBlockAlign = tEndpointFmt.nBlockAlign / tEndpointFmt.nChannels;

    for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
    {
        for (UINT8 attempts = 0; attempts < WAV_FILE_OPEN_ATTEMPTS; attempts++)
        {
            fOriginalOutputFiles[i] = fopen(("Audio Files/" + sFilename + std::to_string(i + 1) + ".wav").c_str(), "wb");

            if (fOriginalOutputFiles[i] != NULL) break;
            else if (attempts == WAV_FILE_OPEN_ATTEMPTS - 1 && fOriginalOutputFiles[i] == NULL) return ERROR_TOO_MANY_OPEN_FILES;
        }

        // RIFF Header
        fputs("RIFF----WAVEfmt ", fOriginalOutputFiles[i]);
        // Format-Section
        fwrite(&fmtLength, sizeof(DWORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.wFormatTag, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&ch, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.nSamplesPerSec, sizeof(DWORD), 1, fOriginalOutputFiles[i]);
        fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, fOriginalOutputFiles[i]);
        fwrite(&newBlockAlign, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.wBitsPerSample, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.cbSize, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.wValidBitsPerSample, sizeof(WORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.channelMask, sizeof(DWORD), 1, fOriginalOutputFiles[i]);
        fwrite(&tEndpointFmt.subFormat, sizeof(GUID), 1, fOriginalOutputFiles[i]);
        // Data-Section
        fputs("data----", fOriginalOutputFiles[i]);
    }

    if (tResampleFmt.nUpsample > 1 || tResampleFmt.nDownsample > 1)
    {
        fResampledOutputFiles = (FILE**)malloc(tEndpointFmt.nChannels * sizeof(FILE*));
        nResampledFileLength = (DWORD*)malloc(tEndpointFmt.nChannels * sizeof(DWORD));
    
        newAvgBytesPerSec = newAvgBytesPerSec * tResampleFmt.nUpsample / tResampleFmt.nDownsample;
        DWORD newResampledSamplesPerSec = tEndpointFmt.nSamplesPerSec * tResampleFmt.nUpsample / tResampleFmt.nDownsample;

        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
        {
            for (UINT8 attempts = 0; attempts < WAV_FILE_OPEN_ATTEMPTS; attempts++)
            {
                fResampledOutputFiles[i] = fopen(("Audio Files/" + sFilename + std::to_string(i + 1) + " Resampled.wav").c_str(), "wb");

                if (fResampledOutputFiles[i] != NULL) break;
                else if (attempts == WAV_FILE_OPEN_ATTEMPTS - 1 && fResampledOutputFiles[i] == NULL) return ERROR_TOO_MANY_OPEN_FILES;
            }

            // RIFF Header
            fputs("RIFF----WAVEfmt ", fResampledOutputFiles[i]);
            // Format-Section
            fwrite(&fmtLength, sizeof(DWORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.wFormatTag, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&ch, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&newResampledSamplesPerSec, sizeof(DWORD), 1, fResampledOutputFiles[i]);
            fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, fResampledOutputFiles[i]);
            fwrite(&newBlockAlign, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.wBitsPerSample, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.cbSize, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.wValidBitsPerSample, sizeof(WORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.channelMask, sizeof(DWORD), 1, fResampledOutputFiles[i]);
            fwrite(&tEndpointFmt.subFormat, sizeof(GUID), 1, fResampledOutputFiles[i]);
            // Data-Section
            fputs("data----", fResampledOutputFiles[i]);
        }
    }

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>The main audio data decimating method.</para>
/// <para>Splits original WASAPI stream channelwise and stores decimated resampled
/// data in the aggregator's circular buffer previously passed by the calling thread
/// in AudioBuffer::InitBuffer(). Resamples in-place in time domain the original signal
/// as it is being pushed onto the buffer to the calling thread's desired frequency.</para>
/// <para>Goes over each audio packet only once. Decimates blocks by offsetting and 
/// casting as FLOAT pointers.</para>
/// <para>Current implementation promotes and employs in-place buffer processing to reduce latency and
/// reduce technically unnecessary latency as the result of writing several intermediate buffers.</para>
/// <para>Writes data to a WAV file if calling thread invoked AudioBuffer::InitWAV() prior.</para>
/// <para>Calling thread must ensure integrity of pBuffer and allocate enough
/// memory in first dimension to fit all channels for all devices.</para>
/// <para>Each AudioBuffer object writes resampled channelwise data decimated from
/// captured endpoint data to consecutive row vectors of RESAMPLEFMT_T.pBuffer.</para>
/// <para>Note: It might be better in the future to use DMA.</para>
/// </summary>
/// <param name="pData">- pointer to the first byte into the endpoint's newly captured packet</param>
/// <param name="bDone">- address of the variable responsible for terminating the program</param>
/// <returns></returns>
HRESULT AudioBuffer::PullData(BYTE* pData, BOOL* bDone)
{
    BYTE* pDataDummy = pData;
    UINT32 nSamplesWritten = 0;
#ifdef DEBUG
    durationCounter++;
#endif // DEBUG
    
    // Modulo operator allows to go in circular fashion so no code duplication is required

    // When user calls AudioBuffer::PullData with pData = NULL, AUDCLNT_BUFFERFLAGS_SILENT flag is set
    // results in keeping 0's bulk set in previous step in the audio buffer data structure
    if (pData != NULL)
    {
        // Save original signal to file if user requested
        if (bOutputWAV) 
            for (UINT32 j = 0; j < *tEndpointFmt.nBufferSize; j++, pDataDummy += tEndpointFmt.nBlockAlign)
                for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                    fwrite(((FLOAT*)pDataDummy) + i, sizeof(FLOAT), 1, fOriginalOutputFiles[i]);

        // Perform resampling if resampling factor is other than 1
        if (tResampleFmt.fFactor != 1.0)
        {   
            // Sample rate convert the packet and place in circular buffer
            nSamplesWritten = pResampler->Resample(tResampleFmt, tEndpointFmt, nChannelOffset, pData);
            
            // Write freshly resampled stream into file if user requested
            if (bOutputWAV) 
            {
                for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                {
                    // If data was filled at most up to the end of memory allocated for ring buffer
                    if (tResampleFmt.nWriteOffset + nSamplesWritten <= *(tResampleFmt.nBufferSize))
                        // Write to file data from ring buffer from the previous offset up till the number of resampled frames
                        fwrite(tResampleFmt.pBuffer[nChannelOffset + i] + tResampleFmt.nWriteOffset, 
                            sizeof(FLOAT), 
                            nSamplesWritten, 
                            fResampledOutputFiles[i]);
                    // If more data was filled in the buffer than the amount of free contigious memory in the ring buffer, go circularly
                    else
                    {
                        // Write data to file from ring buffer's offset up till the end of the ring buffer
                        fwrite(tResampleFmt.pBuffer[nChannelOffset + i] + tResampleFmt.nWriteOffset,
                            sizeof(FLOAT),
                            *tResampleFmt.nBufferSize - tResampleFmt.nWriteOffset,
                            fResampledOutputFiles[i]);
                        
                        // Write data to file from ring buffer's beginnig up till the remaining number of resampled frames
                        fwrite(tResampleFmt.pBuffer[nChannelOffset + i],
                            sizeof(FLOAT),
                            (tResampleFmt.nWriteOffset + nSamplesWritten) % *tResampleFmt.nBufferSize, 
                            fResampledOutputFiles[i]);
                    }
                }
            }
        }
        else // If factor is 1, right data straight into the ring buffer, don't write resampled file
        {
            nSamplesWritten = *tEndpointFmt.nBufferSize;
            pDataDummy = pData;
            for (UINT32 j = 0; j < *tEndpointFmt.nBufferSize; j++, pDataDummy += tEndpointFmt.nBlockAlign)
                for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                    *(tResampleFmt.pBuffer[nChannelOffset + i] + (tResampleFmt.nWriteOffset + j) % *tResampleFmt.nBufferSize) = *(((FLOAT*)pData) + i);
        }
    }
    else
    {
        // TODO: provide for case when silence was written to file
    }

    //-------------------- End --------------------//
    UINT32 dummy = tResampleFmt.nWriteOffset;
    // Update the offset to position after the last frame of the current chunk
    tResampleFmt.nWriteOffset = (tResampleFmt.nWriteOffset + nSamplesWritten) % *tResampleFmt.nBufferSize;

    // If write offset exceeded read offset by a whole lap, update read offset to the position of write offset,
    // hence drop frames that were delayed and try to catch up from the new position
    if ((dummy >= tResampleFmt.nReadOffset && tResampleFmt.nWriteOffset > tResampleFmt.nReadOffset && tResampleFmt.nWriteOffset < dummy) ||
        (dummy <= tResampleFmt.nReadOffset && tResampleFmt.nWriteOffset < tResampleFmt.nReadOffset && tResampleFmt.nWriteOffset < dummy) ||
        (dummy <= tResampleFmt.nReadOffset && tResampleFmt.nWriteOffset > tResampleFmt.nReadOffset))
        tResampleFmt.nReadOffset = tResampleFmt.nWriteOffset;

#ifdef DEBUG
    // Stops capture, used for debugging
    if (durationCounter >= 1000) *bDone = TRUE;
#endif // DEBUG

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Pushes data of the AudioBuffer into a corresponding render endpoint device.</para>
/// <para>Note: data in the buffer must already be resampled, in PullData step, to adjust sample
/// rate specifically to the device to which AudioBuffer will be outputting.</para>
/// <para>Note: does not yet enforce matching dimensions between AudioBuffer and destination
/// render endpoint device.</para>
/// </summary>
/// <param name="pData"></param>
/// <param name="nFrames"></param>
/// <returns></returns>
HRESULT AudioBuffer::PushData(BYTE* pData, UINT32 nFrames)
{
    // Push nFrames from the ring buffer into the endpoint buffer for playback
    for (UINT32 j = 0; j < nFrames; j++, pData += tEndpointFmt.nBlockAlign)
        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
            *(((FLOAT*)pData) + i) = *(tResampleFmt.pBuffer[nChannelOffset + i] + (tResampleFmt.nReadOffset + j) % *tResampleFmt.nBufferSize);

    // Update read pointer respecting the circular buffer traversal
    tResampleFmt.nReadOffset = (tResampleFmt.nReadOffset + nFrames) % *tResampleFmt.nBufferSize;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Gets the number of frames in the buffer available for reading.</para>
/// </summary>
/// <returns></returns>
UINT32 AudioBuffer::FramesAvailable()
{   
    return (tResampleFmt.nWriteOffset > tResampleFmt.nReadOffset) ? 
                    (tResampleFmt.nWriteOffset - tResampleFmt.nReadOffset) :                            // data to read is linear
                    (*tResampleFmt.nBufferSize - tResampleFmt.nReadOffset + tResampleFmt.nWriteOffset); // data to read is circular
}