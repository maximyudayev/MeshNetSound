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
#include "Resampler.h"

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

	private:
		// WAV file output related variables
		FILE				** fOriginalOutputFiles,
							** fResampledOutputFiles;
		DWORD				* nOriginalFileLength,
							* nResampledFileLength;
		std::string			sFilename;
		BOOL				bOutputWAV						{ FALSE };
	
		// Circular buffer related variables
		RESAMPLEFMT			tResampleFmt;
		Resampler			 *pResampler;

		// Endpoint buffer related variables
		ENDPOINTFMT			tEndpointFmt;

		UINT32				nChannelOffset;
		static UINT32		nNewChannelOffset;

		UINT32				nInstance;
		static UINT32		nNewInstance;

#ifdef DEBUG
		UINT32				durationCounter					{ 0 };
#endif // DEBUG
};