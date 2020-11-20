#include "AudioBuffer.h"
#include <stdio.h>
#include <string>

AudioBuffer::AudioBuffer(std::string filename)
{
    file_name = filename;
    output_file1.open(filename + "1.txt");
    output_file2.open(filename + "2.txt");
}

AudioBuffer::~AudioBuffer()
{
    /*for (UINT8 i = 0; i < channels; i++) free(buffer[i]);
    free(buffer);*/
    //free(output_files);
}

/*
 * WAVEFORMATEX: https://docs.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex
 * A structure wich will define the format of the waveform of the audio
 * will give the following data:
 * typedef struct tWAVEFORMATEX {
 *   WORD  wFormatTag;
 *   WORD  nChannels;
 *   DWORD nSamplesPerSec;
 *   DWORD nAvgBytesPerSec;
 *   WORD  nBlockAlign;
 *   WORD  wBitsPerSample;
 *   WORD  cbSize;
 * }
 */
HRESULT AudioBuffer::SetFormat(WAVEFORMATEX* pwfx)
{
    std::cout << "samples per second " << pwfx->nSamplesPerSec << '\n';
    std::cout << "amount of channels " << pwfx->nChannels << '\n';
    std::cout << "block allignment " << pwfx->nBlockAlign << '\n';
    std::cout << "bits per sample " << pwfx->wBitsPerSample << '\n';
    std::cout << "extra info size " << pwfx->cbSize << '\n';

    block_sz = pwfx->nBlockAlign;
    channels = pwfx->nChannels;
    sample_octet_num = pwfx->wBitsPerSample / 8;

    if (pwfx->cbSize >= 22) {
        WAVEFORMATEXTENSIBLE* waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            printf("the variable type is a float\n");
        }
        if (waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            printf("the term is a PCM\n");
        }
    }

    //-------- This part does not want to cooperate. Unable to create an array of files or streams to write data for each independent channel
   /* char strp[10];
    output_files = (FILE**)malloc(channels * sizeof(FILE*));

    for (UINT8 i = 0; i < channels; i++) fopen_s(&output_files[i], (file_name + std::to_string(i) + ".txt").c_str(), "w");
    */
    return 0;
}

HRESULT AudioBuffer::CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone)
{
    duration_counter++;
    buffer_samples_available = numFramesAvailable;
    FLOAT* tData;

    for (UINT32 j = 0; numFramesAvailable > 0; j++)
    {
        /*for (UINT8 i = 0; i < channels; i++)
        {*/
            tData = (FLOAT*)pData;
            //buffer[i][j] = *(((FLOAT*)pData) + i);

            output_file1 << *tData << std::endl;
            output_file2 << *(tData+1) << std::endl;
        /*}*/

        pData += block_sz;
        numFramesAvailable--;
    }

    if (duration_counter >= 1000)
    {
        *bDone = TRUE;
        output_file1.close();
        output_file2.close();
        /*for (UINT8 i = 0; i < channels; i++) fclose(output_files[i]);*/
    }

    return 0;
}

/*
 * TODO: provide safety in case allocation fails, buffer is not NULL, buffer size is reset by user, etc
 */
HRESULT AudioBuffer::SetBufferSize(UINT32* pSize)
{
    buffer = (FLOAT**)malloc(channels * sizeof(FLOAT*));

    for (UINT8 i = 0; i < channels; i++) buffer[i] = (FLOAT*)malloc(*pSize * sizeof(FLOAT));

    return 0;
}