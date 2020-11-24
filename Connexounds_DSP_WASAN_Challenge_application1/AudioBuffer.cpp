#include "AudioBuffer.h"
#include <stdio.h>
#include <string>

/// <summary>
/// <para>AudioBuffer constructor.</para>
/// <para>Calling thread must call AudioBuffer::SetFormat() and AudioBuffer::InitBuffer()
/// before using any other functionality of the class.</para>
/// <para>Note: The AudioBuffer does not limit, but assumes the maximum number
/// of channels per arbitrary device to not exceed 256. That includes
/// "virtual" devices (i.e. aggregated WASAN devices connected to another,
/// each having aggregated own devices). In case larger number of aggregated
/// channels is required for a truly enormous WASAN (i.e. 256+ aggregated channels
/// in the network, composed of N multichanneled connected devices, PCs, BT sets, etc.)
/// only the data type of all variables (i.e. in loops) operating with nChannels
/// variable, along with the variable itself, must be updated to the desired bit width
/// (i.e. UINT16, UINT32, UINT64).</para>
/// <para>TODO: at compile or run time request user about the intended size of WASAN
/// and choose bit width of the nChannels and all corresponding supporting variables
/// accordingly.</para>
/// </summary>
/// <param name="filename">- stores filename used for writing WAV files</param>
AudioBuffer::AudioBuffer(std::string filename)
{
    sFilename = filename;
}

/// <summary>
/// <para>AudioBuffer destructor.</para>
/// <para>Closes WAV files, if used, and frees alloc'ed memory of FILE array and 2D audio buffer array.</para>
/// <para>TODO: expand versatility by checking the actual audio format.</para>
/// </summary>
AudioBuffer::~AudioBuffer()
{
    // Cleans up WAV files
    if (bOutputWAV)
    {
        for (UINT8 i = 0; i < tEndpointFmt.nChannels; i++)
        {
            // Gets complete file length
            fileLength[i] = ftell(outputFiles[i]);

            // Fills missing data chunk size data in the WAV file (Data) headers
            fseek(outputFiles[i], 64, SEEK_SET); // updates file pointer into the byte identifying data chunk size
            DWORD dataChunkSize = fileLength[i] - 68;
            fwrite(&dataChunkSize, sizeof(DWORD), 1, outputFiles[i]);

            // Fills missing file size data in the WAV file (RIFF) headers
            fseek(outputFiles[i], 4, SEEK_SET); // updates file pointer into the byte identifying file size
            DWORD fileChunkSize = fileLength[i] - 8;
            fwrite(&fileChunkSize, sizeof(DWORD), 1, outputFiles[i]);

            // Closes WAV files
            fclose(outputFiles[i]);
        }
        free(outputFiles);
        free(fileLength);
    }
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
    std::cout << "samples per second " << pwfx->nSamplesPerSec << '\n';
    std::cout << "amount of channels " << pwfx->nChannels << '\n';
    std::cout << "block allignment " << pwfx->nBlockAlign << '\n';
    std::cout << "bits per sample " << pwfx->wBitsPerSample << '\n';
    std::cout << "extra info size " << pwfx->cbSize << '\n';
    
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

        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) printf("the variable type is a float\n");
        else if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) printf("the term is a PCM\n");
    }
    
    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Initializes stream resample properties and endpoint buffer size.</para>
/// <para>TODO: make possible to update endpoint buffer length on demand.</para>
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
HRESULT AudioBuffer::InitBuffer(UINT32 nEndpointBufferSize, FLOAT** pCircularBuffer,
    UINT32 nCircularBufferSize, DWORD nUpsample, DWORD nDownsample)
{
    tEndpointFmt.nBufferSize = nEndpointBufferSize;

    tResampleFmt.pBuffer = pCircularBuffer;
    tResampleFmt.nBufferSize = nCircularBufferSize;
    tResampleFmt.nBufferOffset = 0;
    tResampleFmt.nUpsample = nUpsample;
    tResampleFmt.nDownsample = nDownsample;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Initializes .WAV file headers for each channel of a device.</para>
/// </summary>
/// <returns>
/// ERROR_TOO_MANY_OPEN_FILES if fopen fails for FILE_OPEN_ATTEMPTS times.
/// ERROR_SUCCESS if WAV file for each channel is properly initialized.
/// </returns>
HRESULT AudioBuffer::InitWAV()
{
    bOutputWAV = TRUE;
    outputFiles = (FILE**)malloc(tEndpointFmt.nChannels * sizeof(FILE*));
    fileLength = (DWORD*)malloc(tEndpointFmt.nChannels * sizeof(DWORD));

    WORD ch = 1;
    DWORD fmtLength = 40;
    DWORD newAvgBytesPerSec = tEndpointFmt.nAvgBytesPerSec / tEndpointFmt.nChannels;
    WORD newBlockAlign = tEndpointFmt.nBlockAlign / tEndpointFmt.nChannels;

    for (UINT8 i = 0; i < tEndpointFmt.nChannels; i++)
    {
        for (UINT8 attempts = 0; attempts < WAV_FILE_OPEN_ATTEMPTS; attempts++)
        {
            outputFiles[i] = fopen((sFilename + std::to_string(i + 1) + ".wav").c_str(), "wb");

            if (outputFiles[i] != NULL) break;
            else if (attempts == WAV_FILE_OPEN_ATTEMPTS - 1 && outputFiles[i] == NULL) return ERROR_TOO_MANY_OPEN_FILES;
        }

        // RIFF Header
        fputs("RIFF----WAVEfmt ", outputFiles[i]);
        // Format-Section
        fwrite(&fmtLength, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.wFormatTag, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&ch, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.nSamplesPerSec, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&newBlockAlign, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.wBitsPerSample, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.cbSize, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.wValidBitsPerSample, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.channelMask, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&tEndpointFmt.subFormat, sizeof(GUID), 1, outputFiles[i]);
        // Data-Section
        fputs("data----", outputFiles[i]);
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
/// captured endpoint data to consecutive row vectors of RESAMPLEFMT.pBuffer.</para>
/// <para>Note: It might be better in the future to use DMA.</para>
/// <para>TODO: add intelligence to the function to choose time- vs. 
/// frequency-based resampling, depending on the size of the RESAMPLEFMT.pBuffer.</para>
/// </summary>
/// <param name="pData">- pointer to the first byte into the endpoint's newly captured packet</param>
/// <param name="bDone">- address of the variable responsible for terminating the program</param>
/// <returns>
/// 
/// </returns>
HRESULT AudioBuffer::CopyData(BYTE* pData, BOOL* bDone)
{
#ifdef DEBUG
    durationCounter++;
#endif // DEBUG

    //-------------------- Upsampling --------------------//
    // Prefill circular buffer space for next packet of the device with 0's before interpolating

    // Because it is a circular buffer, must check if setting memory to 0 can be done contigiously
    // If the new upsampled packet can fit in the remainder of the circular buffer, set everything ahead to 0
    if (tResampleFmt.nBufferOffset + tEndpointFmt.nBufferSize * tResampleFmt.nUpsample <= tResampleFmt.nBufferSize)
    {
        // Set upsampled number of frames in the circular buffer to 0
        for (UINT8 i = 0; i < tEndpointFmt.nChannels; i++)
        {
            memset(tResampleFmt.pBuffer[i] + tResampleFmt.nBufferOffset, 0, sizeof(FLOAT) * tEndpointFmt.nBufferSize * tResampleFmt.nUpsample);
            
            // If AUDCLNT_BUFFERFLAGS_SILENT is set and capturing WAV files, write silence to file 
            // (takes first tEndpointFmt.nBufferSize 0's from newly cleared tResampleFmt.pBuffer[i] chunk)
            // Will never overflow result of memset
            if (pData == NULL && bOutputWAV) 
                fwrite(tResampleFmt.pBuffer[i] + tResampleFmt.nBufferOffset, sizeof(FLOAT), tEndpointFmt.nBufferSize, outputFiles[i]);
        }
    }
    // If the new upsampled packet overruns the length of the contigious block of memory, go circularly
    else
    {
        // Set all contiguous memory in the circular buffer until the end to 0 and also
        // the first N frames at the beginning of the circular buffer, equalling the remaining frames of the packet
        for (UINT8 i = 0; i < tEndpointFmt.nChannels; i++)
        {
            // Set al frames until the end of circular buffer to 0
            memset(tResampleFmt.pBuffer[i] + tResampleFmt.nBufferOffset, 0, sizeof(FLOAT) * (tResampleFmt.nBufferSize - tResampleFmt.nBufferOffset));
            // Set the next remaining number of frames at the beginning of the circular buffer to 0
            memset(tResampleFmt.pBuffer[i], 0, sizeof(FLOAT) * ((tResampleFmt.nBufferOffset + tEndpointFmt.nBufferSize * tResampleFmt.nUpsample) % tResampleFmt.nBufferSize));

            // If AUDCLNT_BUFFERFLAGS_SILENT is set and capturing WAV files, write silence to file 
            // (takes first tEndpointFmt.nBufferSize 0's from newly cleared tResampleFmt.pBuffer[i] chunk)
            // 
            if (pData == NULL && bOutputWAV)
            {
                if (tResampleFmt.nBufferOffset + tEndpointFmt.nBufferSize <= tResampleFmt.nBufferSize)
                    fwrite(tResampleFmt.pBuffer[i] + tResampleFmt.nBufferOffset,
                        sizeof(FLOAT), 
                        tEndpointFmt.nBufferSize, 
                        outputFiles[i]);
                else
                {
                    fwrite(tResampleFmt.pBuffer[i] + tResampleFmt.nBufferOffset, 
                        sizeof(FLOAT), 
                        tEndpointFmt.nBufferSize - tEndpointFmt.nBufferSize % (tResampleFmt.nBufferSize - tResampleFmt.nBufferOffset), 
                        outputFiles[i]);
                    
                    fwrite(tResampleFmt.pBuffer[i],
                        sizeof(FLOAT),
                        tEndpointFmt.nBufferSize % (tResampleFmt.nBufferSize - tResampleFmt.nBufferOffset),
                        outputFiles[i]);
                }
            }
        }
    }

    // When user calls AudioBuffer::CopyData with pData = NULL, AUDCLNT_BUFFERFLAGS_SILENT flag is set
    // results in keeping 0's bulk set in previous step in the audio buffer data structure
    if (pData != NULL)
    {
        // Insert original frames every nUpsample-1 instances.
        // Modulo operator allows to go in circular fashion so no code duplication is required
        for (UINT32 j = 0; j < tEndpointFmt.nBufferSize; j++, pData += tEndpointFmt.nBlockAlign)
        {
            for (UINT8 i = 0; i < tEndpointFmt.nChannels; i++)
            {
                *(tResampleFmt.pBuffer[i] + (tResampleFmt.nBufferOffset + tResampleFmt.nUpsample * j) % tResampleFmt.nBufferSize) = *(((FLOAT*)pData) + i);
                
                if (bOutputWAV) fwrite(((FLOAT*)pData) + i, sizeof(FLOAT), 1, outputFiles[i]);
            }
        }

        // Convolving with a sinc

    }

    //-------------------- Downsampling --------------------//



    //-------------------- End --------------------//

    // Update the offset to position after the last frame of the current chunk
    tResampleFmt.nBufferOffset = (tResampleFmt.nBufferOffset + tEndpointFmt.nBufferSize * tResampleFmt.nUpsample) % tResampleFmt.nBufferSize;

#ifdef DEBUG
    // Stops capture, used for debugging
    if (durationCounter >= 1000) *bDone = TRUE;
#endif // DEBUG
    
    return ERROR_SUCCESS;
}