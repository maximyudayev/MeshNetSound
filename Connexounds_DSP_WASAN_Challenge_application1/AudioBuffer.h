#pragma once
#include <windows.h>
#include <iostream>
#include <fstream>
#include <mmsystem.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <mmreg.h>
#include <stdio.h>
#include <string>
#include "config.h"

typedef struct resamplefmt {
	FLOAT** pBuffer;
	UINT32 nBufferOffset;
	UINT32* nBufferSize; 
	DWORD nUpsample;
	DWORD nDownsample;
} RESAMPLEFMT;

typedef struct endpointfmt {
	UINT32* nBufferSize; // not fixed because number of frames is dictated by WASAPI due to AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
	WORD nBlockAlign;
	WORD nChannels;
	WORD wBitsPerSample;
	WORD wValidBitsPerSample;
	WORD nBytesInSample;
	WORD wFormatTag;
	WORD cbSize;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	DWORD channelMask;
	GUID subFormat;
} ENDPOINTFMT;

class AudioBuffer
{
	public:
		AudioBuffer(std::string filename);
		~AudioBuffer();
		HRESULT SetFormat(WAVEFORMATEX* pwfx);
		HRESULT InitBuffer(UINT32* nEndpointBufferSize, FLOAT** pCircularBuffer, 
							UINT32* nCircularBufferSize, DWORD nUpsample, DWORD nDownsample);
		HRESULT InitWAV();
		HRESULT CopyData(BYTE* pData, BOOL* bDone);

		static const FLOAT sinc(UINT32 index)
		{
			static const FLOAT a[] = { 1, -1 };
			return a[index];
		};
		static const UINT32 nSincWeights = 2;

	private:
		// WAV file output related variables
		FILE** fOriginalOutputFiles;
		FILE** fResampledOutputFiles;
		DWORD* nOriginalFileLength;
		DWORD* nResampledFileLength;
		std::string sFilename;
		BOOL bOutputWAV = FALSE;
	
		// Circular buffer related variables
		RESAMPLEFMT tResampleFmt;

		// Endpoint buffer related variables
		ENDPOINTFMT tEndpointFmt;

		UINT32 nChannelOffset;
		static UINT32 nNewChannelOffset;

#ifdef DEBUG
		UINT32 durationCounter{ 0 };
#endif // DEBUG
};