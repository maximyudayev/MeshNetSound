#pragma once
#include <MMDeviceAPI.h>
#include <stdio.h>
#include <string>
#include <memory>
#include <functiondiscoverykeys_devpkey.h>

#include "AudioBuffer.h"
#include "config.h"

class Aggregator
{
	public:
		Aggregator();
		~Aggregator();
		HRESULT Initialize();
		HRESULT Start();
		HRESULT Stop();

	private:
		HRESULT ListCaptureDevices();
		HRESULT GetUserCaptureDevices();
		DWORD gcd(DWORD a, DWORD b);

		// WASAPI related variables
		IMMDeviceCollection		* pCollection			{ NULL };
		IMMDeviceEnumerator		* pEnumerator			{ NULL };
		IMMDevice				** pCaptureDeviceAll	{ NULL },
								** pCaptureDevice		{ NULL };
		IAudioClient			** pAudioClient			{ NULL };
		IAudioCaptureClient		** pCaptureClient		{ NULL };
		WAVEFORMATEX			** pwfx					{ NULL };

		// Aggregator related variables
		AudioBuffer				** pAudioBuffer			{ NULL };

		FLOAT					** pCircularBuffer		{ NULL }; 

		BOOL					bDone					{ FALSE };

		BYTE					** pData				{ NULL };

		UINT32					nAggregatedChannels		{ 0 },
								nCircularBufferSize		{ AGGREGATOR_CIRCULAR_BUFFER_SIZE },
								nAllCaptureDevices		{ 0 },
								nCaptureDevices			{ 0 },
								* nEndpointBufferSize	{ NULL },
								* nEndpointPackets		{ NULL };

		DWORD					* flags					{ NULL },
								* nUpsample				{ NULL },
								* nDownsample			{ NULL },
								* nGCD					{ NULL },
								* nGCDDiv				{ NULL },
								* nGCDTFreqDiv			{ NULL };
};