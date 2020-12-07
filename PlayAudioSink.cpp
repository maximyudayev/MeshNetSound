#include "PlayAudioSink.h"

int blocksize;
int channels;
std::vector<std::vector<float>> dataVector;
UINT64 distance = 0;


PlayAudioSink::PlayAudioSink(){

}
/*
 * WAVEFORMATEX: https://docs.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex
 * A structure wich will define the format of the waveform of the audio
 * will give the following data:
 * typedef struct tWAVEFORMATEX {
 * WORD  wFormatTag;
 * WORD  nChannels;
 * DWORD nSamplesPerSec;
 * DWORD nAvgBytesPerSec;
 * WORD  nBlockAlign;
 * WORD  wBitsPerSample;
 * WORD  cbSize;
 * }
 */
HRESULT PlayAudioSink::SetFormat(WAVEFORMATEX *pwfx){
  std::cout << "samples per second " << pwfx->nSamplesPerSec <<'\n';
  std::cout << "amount of channels " << pwfx->nChannels <<'\n';
  std::cout << "block allignment " << pwfx->nBlockAlign <<'\n';
  std::cout << "bits per sample " << pwfx->wBitsPerSample <<'\n';
  std::cout << "extra info size " << pwfx->cbSize <<'\n';

  blocksize = pwfx->nBlockAlign;
  channels = pwfx->nChannels;

  if(pwfx->cbSize >= 22){
    WAVEFORMATEXTENSIBLE *waveFormatExtensible = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(pwfx);
    if(waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT){
        printf("the variable type is a float\n");
    }
    if(waveFormatExtensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM){
        printf("the term is a PCM\n");
    }
  }

  return 0;
}



HRESULT PlayAudioSink::LoadData(UINT32 bufferFrameCount, BYTE *pData, DWORD *flags){

  union{
    float floatData;
    BYTE byteData[4];
  } data;

  while (bufferFrameCount != 0){
    if(distance >= dataVector.size()){
      *flags = AUDCLNT_BUFFERFLAGS_SILENT;
      return 0;
    }
    //printf("here in play now\n");

    data.floatData = dataVector[distance][0];

    // writing the same data after each other twice because two channels
    int size = 0;
    while (size < 4){
      *pData = data.byteData[size];
      pData ++;
      size ++;
    }
    size = 0;
    while (size < 4){
      *pData = data.byteData[size];
      pData ++;
      size ++;
    }

    distance ++;
    bufferFrameCount --;
  }


  return 0;
}

void PlayAudioSink::OpenFile(){
    std::fstream in("C:/Users/Yannick/Desktop/Mic-capture0.txt");
    std::string line;

    int i = 0;

    while (std::getline(in, line))
    {
        float value;
        std::stringstream ss(line);

        dataVector.push_back(std::vector<float>());

        while (ss >> value)
        {
            dataVector[i].push_back(value);
        }
        ++i;
    }

    i = 0;

    /*
    while (i < dataVector.size()){
      std::cout << dataVector[i][0] << '\n';
      ++i;
    }
    */

}

void PlayAudioSink::CloseFile(){

}
