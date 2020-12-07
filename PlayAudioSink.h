#include <windows.h>
#include <iostream>
#include <mmsystem.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <mmreg.h>
#include <sstream>
#include <fstream>
#include <vector>

class PlayAudioSink {
public:
  // methods
  PlayAudioSink();
  HRESULT SetFormat(WAVEFORMATEX *pwfx);
  HRESULT LoadData(UINT32 bufferFrameCount, BYTE *pData, DWORD *flags);
  void OpenFile();
  void CloseFile();

};
