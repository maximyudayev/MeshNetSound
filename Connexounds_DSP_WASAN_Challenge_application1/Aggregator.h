#pragma once
#include <MMDeviceAPI.h>
#include <stdio.h>
#include <string>
#include <memory>

#include "AudioBuffer.h"
#include "config.h"

class Aggregator
{
	public:
		Aggregator();
		~Aggregator();
		
		HRESULT Start();
		HRESULT Stop();

	private:
		HRESULT Initialize();
		HRESULT ListCaptureEndpoints();
		HRESULT GetUserCaptureDevices();
		DWORD gcd(DWORD a, DWORD b);

		// WASAPI related variables
		IMMDeviceCollection		* pCollection;
		IMMDeviceEnumerator		* pEnumerator;
		IMMDevice				** pCaptureDeviceAll,
								** pCaptureDevice;
		IAudioClient			** pAudioClient;
		IAudioCaptureClient		** pCaptureClient;
		WAVEFORMATEX			** pwfx;	

		// Aggregator related variables
		AudioBuffer				** pAudioBuffer;

		FLOAT					** pCircularBuffer;

		BOOL					bDone = FALSE;

		BYTE					** pData;

		UINT32					nAggregatedChannels = 0,
								nCircularBufferSize,
								nAllCaptureEndpoints = 0,
								nCaptureEndpoints = 0,
								* nEndpointBufferSize,
								* nEndpointPackets;

		DWORD					* flags,
								* nUpsample,
								* nDownsample,
								* nGCD,
								* nGCDDiv,
								* nGCDTFreqDiv;
};