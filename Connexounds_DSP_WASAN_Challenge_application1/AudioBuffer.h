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
		HRESULT GetResampled(FLOAT** pBuffer, UINT32* pOffset, UINT32 nCircularBufferSize, DWORD nUpsample, DWORD nDownsample);

		static const FLOAT sinc(UINT32 index)
		{
			static const FLOAT a[] = { 1, -1 };
			return a[index];
		};
		static const UINT32 nSincWeights = 2;

	private:
		FILE** outputFiles;
		DWORD* dataSectionOffset;
		DWORD* fileLength;
		std::string sFilename;
		GUID subFormat;
		DWORD channelMask;
		WORD wValidBitsPerSample;
		BOOL bOutputWAV = FALSE;
	
		FLOAT** dBuffer = NULL;
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