#include "AudioBuffer.h"
#include <stdio.h>
#include <string>

#define FILE_OPEN_ATTEMPTS 5

/// <summary>
/// <para>AudioBuffer constructor.</para>
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
/// <param name="filename">
/// - stores filename used for writing WAV files
/// </param>
AudioBuffer::AudioBuffer(std::string filename)
{
    sFilename = filename;
}

/// <summary>
/// <para>AudioBuffer destructor.</para>
/// <para>Closes WAV files, if used, and frees alloc'ed memory of FILE array and 2D audio buffer array.</para>
/// </summary>
AudioBuffer::~AudioBuffer()
{
    // Cleans up audio data buffer array
    for (UINT8 i = 0; i < nChannels; i++) free(dBuffer[i]);
    free(dBuffer);

    // Cleans up WAV files
    if (bOutputWAV)
    {
        for (UINT8 i = 0; i < nChannels; i++)
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

    nBlockAlign = pwfx->nBlockAlign;
    nChannels = pwfx->nChannels;
    wBitsPerSample = pwfx->wBitsPerSample;
    nBytesInSample = pwfx->wBitsPerSample / 8;
    wFormatTag = pwfx->wFormatTag;
    nSamplesPerSec = pwfx->nSamplesPerSec;
    nAvgBytesPerSec = pwfx->nAvgBytesPerSec;
    cbSize = pwfx->cbSize;

    if (pwfx->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        subFormat = waveFormatExtensible->SubFormat;
        channelMask = waveFormatExtensible->dwChannelMask;
        wValidBitsPerSample = waveFormatExtensible->Samples.wValidBitsPerSample;

        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) printf("the variable type is a float\n");
        else if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) printf("the term is a PCM\n");
    }
    
    return ERROR_SUCCESS;
}

/// <summary>
/// <para>The main audio data decimating method.</para>
/// <para>Splits original WASAPI stream channelwise and stores decimated 
/// data in the buffer variable of the object.</para>
/// <para>Writes data to a WAV file if calling thread invoked AudioBuffer::WriteWAV() prior.</para>
/// <para>Goes over each audio packet only once.
/// Decimates blocks by offsetting and casting as FLOAT pointers.</para>
/// <para>Note: It might be better in the future to use DMA.</para>
/// <para>TODO: Merge AudioBuffer::CopyData together with AudioBuffer::GetResampled.</para>
/// </summary>
/// <param name="pData">- pointer to the first byte of the endpoint buffer</param>
/// <param name="numFramesAvailable">- number of audio samples available in the endpoint buffer</param>
/// <param name="bDone">- address of the variable responsible for terminating the program</param>
/// <returns></returns>
HRESULT AudioBuffer::CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone)
{
    durationCounter++;

    // When user calls AudioBuffer::CopyData with pData = NULL, AUDCLNT_BUFFERFLAGS_SILENT flag is set
    // results in writing 0's to the audio buffer data structure
    if (pData == NULL)
    {
        for (UINT8 i = 0; i < nChannels; i++)
        {
            memset(dBuffer[i], 0, numFramesAvailable * sizeof(FLOAT));
            if (bOutputWAV) fwrite(&dBuffer[i], sizeof(FLOAT), numFramesAvailable, outputFiles[i]);
        }
    }
    else
    {
        for (UINT32 j = 0; numFramesAvailable > 0; j++)
        {
            for (UINT8 i = 0; i < nChannels; i++)
            {
                dBuffer[i][j] = *(((FLOAT*)pData) + i);
                if (bOutputWAV) fwrite(&dBuffer[i][j], sizeof(FLOAT), 1, outputFiles[i]);
            }

            pData += nBlockAlign;
            numFramesAvailable--;
        }
    }

    // Stops capture, used for debugging
    if (durationCounter >= 1000) *bDone = TRUE;

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Allocates an nChannels-by-pSize array for use as data buffer.</para>
/// <para>Each device channel is a separate row vector. All channels have same buffer and data lengths.</para>
/// <para>Calling thread should call the method in a loop until returns ERROR_SUCCESS.</para>
/// <para>Note: For now can be envoked succesfully only once.</para>
/// <para>TODO: make possible to update length on demand.</para>
/// </summary>
/// <param name="pSize">- length of buffer per channel</param>
/// <returns>
/// ERROR_SUCCESS if buffer set up succeeded.
/// ENOMEM if allocation of buffer failed.
/// </returns>
HRESULT AudioBuffer::SetBufferSize(UINT32* pSize)
{
    nBufferSize = *pSize;
    
    dBuffer = (FLOAT**)malloc(nChannels * sizeof(FLOAT*));
    if (dBuffer == NULL) return ENOMEM;

    for (UINT8 i = 0; i < nChannels; i++)
    {
        dBuffer[i] = (FLOAT*)malloc(*pSize * sizeof(FLOAT));
        if (dBuffer[i] == NULL)
        {
            for (UINT8 i = 0; i < nChannels; i++)
            {
                if (dBuffer[i] == NULL) free(dBuffer[i]);
            }

            free(dBuffer);

            return ENOMEM;
        }
    }

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Initializes .WAV file headers for each channel of a device.</para>
/// </summary>
/// <returns>
/// ERROR_TOO_MANY_OPEN_FILES if fopen fails for FILE_OPEN_ATTEMPTS times.
/// ERROR_SUCCESS if WAV file for each channel is properly initialized.
/// </returns>
HRESULT AudioBuffer::WriteWAV()
{
    bOutputWAV = TRUE;
    outputFiles = (FILE**)malloc(nChannels * sizeof(FILE*));
    fileLength = (DWORD*)malloc(nChannels * sizeof(DWORD));

    WORD ch = 1;
    DWORD fmtLength = 40;
    DWORD newAvgBytesPerSec = nAvgBytesPerSec / nChannels;
    WORD newBlockAlign = nBlockAlign / nChannels;

    for (UINT8 i = 0; i < nChannels; i++)
    {
        for (UINT attempts = 0; attempts < FILE_OPEN_ATTEMPTS; attempts++)
        {
            outputFiles[i] = fopen((sFilename + std::to_string(i+1) + ".wav").c_str(), "wb");

            if (outputFiles[i] != NULL) break;
            else if (attempts == FILE_OPEN_ATTEMPTS - 1 && outputFiles[i] == NULL) return ERROR_TOO_MANY_OPEN_FILES;
        }
        
        // RIFF Header
        fputs("RIFF----WAVEfmt ", outputFiles[i]);
        // Format-Section
        fwrite(&fmtLength, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&wFormatTag, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&ch, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&nSamplesPerSec, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&newAvgBytesPerSec, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&newBlockAlign, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&wBitsPerSample, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&cbSize, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&wValidBitsPerSample, sizeof(WORD), 1, outputFiles[i]);
        fwrite(&channelMask, sizeof(DWORD), 1, outputFiles[i]);
        fwrite(&subFormat, sizeof(GUID), 1, outputFiles[i]);
        // Data-Section
        fputs("data----", outputFiles[i]);
    }

    return ERROR_SUCCESS;
}

/// <summary>
/// <para>Resamples in time domain original buffer to the calling thread's desired frequency
/// and stores data into the circular buffer passed by the calling thread.</para>
/// <para>Calling thread must ensure integrity of pBuffer and allocate enough
/// memory in first dimension to fit all channels for all devices.</para>
/// <para>Each AudioBuffer instance will write resampled channelwise data to 
/// consecutive row vectors of pBuffer.</para>
/// <para>Calling thread must pass address of the corresponding device's first channel in the buffer.</para>
/// <para>TODO: add intelligence to the function to choose time- vs. 
/// frequency-based resampling, depending on the size of the AudioBuffer.</para>
/// <para>TODO: combine with AudioBuffer::CopyData to reduce latency and reduce
/// technically unnecessary latency as the result of writing several intermediate buffers.
/// Promotes in-place buffer processing.</para>
/// </summary>
/// <param name="pBuffer">- pointer to memory of the first channel of the corresponding 
/// device in the circular buffer of the aggregator</param>
/// <param name="pBufferOffset">- pointer to the offset variable storing position of 
/// the first frame of the next audio chunk in circular buffer for this device</param>
/// <param name="nCircularBufferSize">- size of circular buffer of the aggregator</param>
/// <param name="nUpsample">- upsampling factor</param>
/// <param name="nDownsample">- downsampling factor</param>
/// <returns>
/// ERROR_SUCCESS if AudioBuffer data successfully resampled and was copied into circular buffer.
/// </returns>
HRESULT AudioBuffer::GetResampled(FLOAT** pBuffer, UINT32* pBufferOffset, UINT32 nCircularBufferSize, DWORD nUpsample, DWORD nDownsample)
{
    UINT32 pBufferOffsetDummy = *pBufferOffset;
    
    //-------------------- Upsampling --------------------//
    // Prefill circular buffer space for next packet of the device with 0's before interpolating

    // Because it is a circular buffer, must check if setting memory to 0 can be done contigiously
    // If the new upsampled packet can fit in the remainder of the circular buffer, set everything ahead to 0
    if (*pBufferOffset + nBufferSize * nUpsample <= nCircularBufferSize)
    {
        // Set upsampled number of frames in the circular buffer to 0
        for (UINT8 i = 0; i < nChannels; i++)
            memset(pBuffer[i] + *pBufferOffset, 0, sizeof(FLOAT) * nBufferSize * nUpsample);
    }
    // If the new upsampled packet overruns the length of the contigious block of memory, go circularly
    else
    {
        // Set all contiguous memory in the circular buffer until the end to 0 and also
        // the first N frames at the beginning of the circular buffer, equalling the remaining frames of the packet
        for (UINT8 i = 0; i < nChannels; i++)
        {
            // Set al frames until the end of circular buffer to 0
            memset(pBuffer[i] + *pBufferOffset, 0, sizeof(FLOAT) * (nCircularBufferSize - *pBufferOffset));
            // Set the next remaining number of frames at the beginning of the circular buffer to 0
            memset(pBuffer[i], 0, sizeof(FLOAT) * ((*pBufferOffset + nBufferSize * nUpsample) % nCircularBufferSize));
        }
    }

    // Insert original frames every nUpsample-1 instances.
    // Modulo operator allows to go in circular fashion so no code duplication is required
    for (UINT8 i = 0; i < nChannels; i++)
        for (UINT32 j = 0; j < nBufferSize; j++)
            *(pBuffer[i] + (*pBufferOffset + nUpsample * j) % nCircularBufferSize) = dBuffer[i][j];

    // Convolving with a sinc



    //-------------------- Downsampling --------------------//



    //-------------------- End --------------------//

    // Update the offset to position after the last frame of the current chunk
    *pBufferOffset = (*pBufferOffset + nBufferSize * nUpsample) % nCircularBufferSize;

    return ERROR_SUCCESS;
}