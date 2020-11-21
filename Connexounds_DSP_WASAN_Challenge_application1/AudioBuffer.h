#pragma once
#include <windows.h>
#include <iostream>
#include <fstream>
#include <mmsystem.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <mmreg.h>

class AudioBuffer
{
public:
	AudioBuffer(std::string filename);
	~AudioBuffer();
	HRESULT SetFormat(WAVEFORMATEX* pwfx);
	HRESULT CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone);
	HRESULT SetBufferSize(UINT32* pSize);
	HRESULT WriteWAV();

private:
	FILE** outputFiles;
	DWORD* dataSectionOffset;
	DWORD* fileLength;
	std::string sFilename;
	GUID subFormat;
	DWORD channelMask;
	WORD wValidBitsPerSample;
	
	FLOAT** dBuffer = NULL;
	UINT32 nFramesAvailable{ 0 };
	UINT32 nBufferSize{ 0 };
	UINT32 durationCounter{ 0 };
	WORD nBlockAlign{ 0 };
	WORD nChannels{ 0 };
	WORD nBytesInSample{ 0 };
	WORD wFormatTag{ 0 };
	WORD wBitsPerSample{ 0 };
	WORD cbSize{ 0 };
	DWORD nSamplesPerSec{ 0 };
	DWORD nAvgBytesPerSec{ 0 };
};