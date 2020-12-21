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

#include "config.h"
#include "AudioBuffer.h"
#include "UDPAudioBuffer.h"
#include "Resampler.h"
#include "AudioEffect.h"

typedef struct UDPCaptureThreadParam {
	CHAR* sUDPServerIP;
	UDPAudioBuffer** pUDPAudioBuffer;
	UINT32 nWASANNodes;
	BOOL* bDone;
} UDPCAPTURETHREADPARAM;

typedef struct WASAPICaptureThreadParam {
	AudioBuffer** pAudioBuffer;
	UINT32 nDevices;
	BOOL* bDone;
	DWORD** flags;
	BYTE** pData;
	UINT32** nEndpointBufferSize;
	UINT32** nEndpointPackets;
	IAudioCaptureClient** pCaptureClient;
} WASAPICAPTURETHREADPARAM;

typedef struct RenderThreadParam {
	BOOL* bDone;
	UINT32 nDevices;
	AudioBuffer** pAudioBuffer;
	UINT32** nEndpointBufferSize;
	IAudioRenderClient** pRenderClient;
	BYTE** pData;
	DWORD** flags;
	UDPAudioBuffer** pUDPAudioBuffer;
	UINT32 nWASANNodes;
} RENDERTHREADPARAM;

/// <summary>
/// <para>Runs UDP server listener thread.</para>
/// </summary>
/// <param name="lpParam">- pointer to struct UDPCAPTURETHREADPARAM.</param>
/// <returns></returns>
DWORD WINAPI UDPCaptureThread(LPVOID lpParam);

/// <summary>
/// <para>Runs WASAPI endpoint data capture thread.</para>
/// </summary>
/// <param name="lpParam">- pointer to struct WASAPICAPTURETHREADPARAM.</param>
/// <returns></returns>
DWORD WINAPI WASAPICaptureThread(LPVOID lpParam);

/// <summary>
/// <para>Runs playback thread for WASAPI devices and WASAN UDP nodes.</para>
/// </summary>
/// <param name="lpParam">- pointer to struct RENDERTHREADPARAM.</param>
/// <returns></returns>
DWORD WINAPI RenderThread(LPVOID lpParam);

/// <summary>
/// <para>Thread running DSP algorithms on the input ring buffer and outputting to
/// the output ring buffer.</para>
/// <para>Combines audio effect thread pool together with an interactive CLI
/// functionality for audio effect object manipulation.</para>
/// </summary>
/// <param name="lpParam"></param>
/// <returns></returns>
DWORD WINAPI DSPThread(LPVOID lpParam);

/// <summary>
/// Class presenting top-level API to the caller that arranges all the configuration
/// with the user and his/her choice of WASAN topology to create a data pipe into and
/// out of the DSP processor.
/// </summary>
class Aggregator
{
	public:
		/// <summary>
		/// <para>Aggregator constructor.</para>
		/// <para>High level, clean interface to perform all the cumbersome setup
		/// and negotiation with WASAPI, user parameters, etc.</para>
		/// <para>Note: not thread-safe, must be instantiated only once.</para>
		/// </summary>
		Aggregator();

		/// <summary>
		/// <para>Aggregator destructor.</para>
		/// <para>Frees all alloc'ed memory and cleans up underlying classes.</para>
		/// </summary>
		~Aggregator();

		/// <summary>
		/// <para>Alloc's memory and instantiates WASAPI interface
		/// to stream capture device data.</para>
		/// <para>Note: if at some point crashes, likely it is because of
		/// double deletion of pAudioBuffer or pResampler. Check if EXIT_ON_ERROR
		/// is triggered before either is instantiated using "new" and if so, provide
		/// extra safety checks.</para>
		/// </summary>
		/// <returns></returns>
		HRESULT Initialize();
		
		/// <summary>
		/// <para>Wrapper for starting capture and render services.</para>
		/// </summary>
		/// <returns></returns>
		HRESULT Start();

		/// <summary>
		/// <para>Wrapper for stopping capture and render services.</para>
		/// </summary>
		/// <returns></returns>
		HRESULT Stop();

	private:
		/// <summary>
		/// <para>Pipes all active chosen type devices into console.</para> 
		/// <para>Records each device into pDevice data structure to 
		/// later retreive the desired ones based on user input.</para>
		/// </summary>
		/// <returns>
		/// <para>S_OK if successful.</para>
		/// <para>ENOMEM if memory (re)allocation for IMMDevice array fails.</para>
		/// <para>IMMDevice/IMMDeviceCollection/IMMDeviceEnumerator/IPropertyStore
		/// specific error otherwise.</para>
		/// </returns>
		HRESULT ListAvailableDevices(UINT8 nDeviceType);

		/// <summary>
		/// <para>Prompts user to connect to WASAN nodes given user's IP selection.</para>
		/// </summary>
		/// <param name="nDeviceType">- indicator of capture vs. render device role.</param>
		/// <returns></returns>
		HRESULT GetWASANNodes(UINT8 nDeviceType);

		/// <summary>
		/// <para>Prompts user to choose from devices available to the system.</para>
		/// <para>Must be called after Aggregator::ListAvailableDevices.</para>
		/// </summary>
		/// <param name="nDeviceType">- indicator of capture vs. render device role.</param>
		/// <returns></returns>
		HRESULT GetWASAPIDevices(UINT8 nDeviceType);
		
		/// <summary>
		/// <para>Wrapper for all initialization steps on the capturing side.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT InitializeCapture();
		
		/// <summary>
		/// <para>Wrapper for all initialization steps on the rendering side.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT InitializeRender();
		
		/// <summary>
		/// <para>Starts capturing audio from user-selected devices on a poll basis.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT StartCapture();
		
		/// <summary>
		/// <para>Flags to each AudioBuffer to not request new frames from WASAPI
		/// and stops each WASAPI stream.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT StopCapture();
		
		/// <summary>
		/// <para>Starts rendering audio on user-selected devices on a poll basis.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT StartRender();
		
		/// <summary>
		/// <para>Flags to WASAPI to not expect new frames from AudioBuffer
		/// and stops each WASAPI stream.</para>
		/// </summary>
		/// <returns></returns>		
		HRESULT StopRender();

		/// <summary>
		/// <para>Finds Greatest Common Divisor of two integers.</para>
		/// </summary>
		/// <param name="a">- first integer</param>
		/// <param name="b">- second integer</param>
		/// <returns>The greatest common devisor of "a" and "b"</returns>
		DWORD gcd(DWORD a, DWORD b);

	private:
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
								nWASANNodes[2]			{ 0 },
								* nEndpointBufferSize[2]{ NULL },
								* nEndpointPackets[2]	{ NULL },
								* pAudioBufferGroupId	{ NULL },
								nCaptureThread			{ 0 },
								nRenderThread			{ 0 };

		DWORD					* flags[2]				{ NULL },
								* nUpsample[2]			{ NULL },
								* nDownsample[2]		{ NULL },
								* nGCD[2]				{ NULL },
								* nGCDDiv[2]			{ NULL },
								* nGCDTFreqDiv[2]		{ NULL },
								* dwCaptureThreadId		{ NULL },
								* dwRenderThreadId		{ NULL };

		HANDLE					* hCaptureThread		{ NULL },
								* hRenderThread			{ NULL };

		CHAR					* pWASANNodeIP[2]		{ NULL },
								UDPServerIP[16];
};
