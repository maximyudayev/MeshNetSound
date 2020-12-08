#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <windows.h>
#include <MMDeviceAPI.h>
#include <stdio.h>
#include <string>
#include <memory>
#include <functiondiscoverykeys_devpkey.h>
#include <audiopolicy.h>

#include "AudioBuffer.h"
#include "Resampler.h"
#include "config.h"

class Aggregator
{
	public:
		Aggregator();
		~Aggregator();
		HRESULT Initialize();
		HRESULT StartCapture();
		HRESULT StopCapture();
		HRESULT StartRender();
		HRESULT StopRender();

	private:
		HRESULT ListAvailableDevices(UINT8 nDeviceType);
		HRESULT GetUserChoiceDevices(UINT8 nDeviceType);
		HRESULT InitializeCapture();
		HRESULT InitializeRender();
		DWORD gcd(DWORD a, DWORD b);

		// WASAPI related variables
		IMMDeviceEnumerator		* pEnumerator			{ NULL };
		IMMDeviceCollection		* pCollection[2]		{ NULL };
		
		IMMDevice				** pDeviceAll[2]		{ NULL },
								** pDevice[2]			{ NULL };

		IAudioClient			** pAudioClient[2]		{ NULL };
		IAudioCaptureClient		** pCaptureClient		{ NULL };
		IAudioRenderClient		** pRenderClient		{ NULL };
		WAVEFORMATEX			** pwfx[2]				{ NULL };



		// Aggregator related variables
		AudioBuffer				** pAudioBuffer[2]		{ NULL };

		Resampler				** pResampler			{ NULL };

		FLOAT					** pCircularBuffer[2]	{ NULL }; 

		BOOL					bDone[2]				{ FALSE, FALSE };

		BYTE					** pData[2]				{ NULL };

		UINT32					nAggregatedChannels[2]	{ 0 },
								nCircularBufferSize[2]	{ AGGREGATOR_CIRCULAR_BUFFER_SIZE, AGGREGATOR_CIRCULAR_BUFFER_SIZE },
								nAllDevices[2]			{ 0 },
								nDevices[2]				{ 0 },
								* nEndpointBufferSize[2]{ NULL },
								* nEndpointPackets[2]	{ NULL },
								* pAudioBufferGroupId	{ NULL };

		DWORD					* flags[2]				{ NULL },
								* nUpsample[2]			{ NULL },
								* nDownsample[2]		{ NULL },
								* nGCD[2]				{ NULL },
								* nGCDDiv[2]			{ NULL },
								* nGCDTFreqDiv[2]		{ NULL };
};