#include "AudioBuffer.h"
#include <stdio.h>
#include <string>

#define FILE_OPEN_ATTEMPTS 5

/// <summary>
/// AudioBuffer constructor.
/// </summary>
/// <param name="filename">
/// Stores filename used for writing WAV files
/// </param>
AudioBuffer::AudioBuffer(std::string filename)
{
    sFilename = filename;
}

/// <summary>
/// AudioBuffer destructor.
/// Closes WAV files and frees alloc'ed memory of FILE array and 2D audio buffer array
/// </summary>
AudioBuffer::~AudioBuffer()
{
    // Cleans up audio data buffer array
    for (UINT8 i = 0; i < nChannels; i++) free(dBuffer[i]);
    free(dBuffer);

    // Cleans up WAV files
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

/// <summary>
/// Copies WAVEFORMATEX data received from WASAPI for all audio related operations.
/// </summary>
/// <param name="pwfx"></param>
/// <remarks>
/// WAVEFORMATEX uses 16bit value for channels, hence maximum intrinsic number of virtual channels is 65'536
/// </remarks>
/// <returns></returns>
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
        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            printf("the variable type is a float\n");
        }
        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            printf("the term is a PCM\n");
        }
    }
    
    return ERROR_SUCCESS;
}

/// <summary>
/// The main audio data decimating method.
/// Splits original WASAPI stream channelwise and stores decimated data in the buffer variable of the object.
/// </summary>
/// <remarks>
/// Goes over each audio packet only once.
/// Decimates blocks by offsetting and casting as FLOAT pointers.
/// </remarks>
/// <param name="pData"></param>
/// <param name="numFramesAvailable"></param>
/// <param name="bDone"></param>
/// <returns></returns>
HRESULT AudioBuffer::CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone)
{
    durationCounter++;
    nFramesAvailable = numFramesAvailable;

    for (UINT32 j = 0; numFramesAvailable > 0; j++)
    {
        for (UINT8 i = 0; i < nChannels; i++)
        {
            dBuffer[i][j] = *(((FLOAT*)pData) + i);
            fwrite(&dBuffer[i][j], sizeof(FLOAT), 1, outputFiles[i]);
        }

        pData += nBlockAlign;
        numFramesAvailable--;
    }

    if (durationCounter >= 1000) *bDone = TRUE;

    return ERROR_SUCCESS;
}

/// <summary>
/// Allocates an nChannels-by-pSize array for use as data buffer.
/// Each device channel is a separate row vector. All channels have same buffer and data lengths.
/// </summary>
/// <remarks>
/// For now can be envoked once. In future should be possible to update length on demand.
/// </remarks>
/// <param name="pSize"></param>
/// <returns></returns>
HRESULT AudioBuffer::SetBufferSize(UINT32* pSize)
{
    dBuffer = (FLOAT**)malloc(nChannels * sizeof(FLOAT*));

    for (UINT8 i = 0; i < nChannels; i++) dBuffer[i] = (FLOAT*)malloc(*pSize * sizeof(FLOAT));

    return ERROR_SUCCESS;
}

/// <summary>
/// Initializes .WAV file headers for each channel of a device
/// </summary>
/// <returns>
/// ERROR_TOO_MANY_OPEN_FILES if fopen fails for FILE_OPEN_ATTEMPTS times.
/// ERROR_SUCCESS if WAV file for each channel is properly initialized
/// </returns>
HRESULT AudioBuffer::WriteWAV()
{
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