#pragma once
#define _USE_MATH_DEFINES
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"

//-------- Type Definitions of AudioBuffer (placed here to avoid circular dependency error)
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

typedef struct resamplefmt {
	RingBufferChannel** pBuffer;
	UINT32* nBufferSize;
	DWORD nUpsample;
	DWORD nDownsample;
	FLOAT fFactor;
} RESAMPLEFMT;

//-------- Type Definitions of Resampler
typedef struct resampler {
	FLOAT* pImp;	// Filter coefficients
	FLOAT* pImpD;	// Filter coefficient deltas
	UINT32 nNz;
	UINT32 nNh;
	UINT32 nNl;
	UINT32 nTwosExp;
	FLOAT fFrq;
	BOOL bHighQuality;
	DOUBLE fRollOff;
	DOUBLE fBeta;
} RESAMPLERPARAMS;

/// <summary>
/// Class performing sample rate conversion on its associated AudioBuffer object
/// in the data flow pipe in front of and right after the DSP processor.
/// </summary>
class Resampler
{
	public:
		/// <summary>
		/// <para>Resampler constructor.</para>
		/// <para>Does not perform any function.</para>
		/// </summary>
		Resampler();

		/// <summary>
		/// <para>Resampler destructor.</para>
		/// <para>Does not perform any function.</para>
		/// </summary>
		~Resampler();

		/// <summary>
		/// <para>Initializes memory for FIR filter coefficientsand saves filter desired parameters.</para>
		/// <para>Computes the coeffs of a Kaiser-windowed low pass filter with
		/// the following characteristics.</para>
		/// </summary>
		/// <param name="bHighQuality">- boolean indicating if standard or higher number of zero crossings to use.</param>
		/// <param name="fRollOff">- LP filter roll-off frequency.</param>
		/// <param name="fBeta">- Kaiser window parameter.</param>
		/// <param name="nNl">- number of filter coefficients between each zero-crossing.</param>		
		static void InitLPFilter(BOOL bHighQuality, DOUBLE fRollOff, DOUBLE fBeta, UINT32 nTwosExp);

		/// <summary>
		/// <para>Static method releasing memory allocated for FIR filter
		/// coefficients.</para>
		/// <para>Note: must be called only once from Aggregator or double freeing will occur.</para>
		/// </summary>		
		static void FreeLPFilter();

		/// <summary>
		/// <para>Adjusts scaling factor for its parent's AudioBuffer's Resampler
		/// to account for unity gain using the resampling factor.</para>
		/// <para>Note: must be called from one of AudioBuffer's initialization
		/// functions before any resampling is performed.</para>
		/// </summary>
		/// <param name="fFactor">- resampling factor</param>		
		void SetLPScaling(FLOAT fFactor);

		/// <summary>
		/// <para>Performs SRC on the WASAPI audio packet and stores output
		/// into the AudioBuffer's corresponding ring buffer.</para>
		/// <para>Note: the SRC implementation might create slight artifacts
		/// because each packet is treated independently and instead of actual Nz*Fs/F's or Nz
		/// extra input samples before and after current packet, padds with 0's.</para>
		/// <para>Note: for each sample, starts from largest coefficients in both wings
		/// and traverses toward the smallest - potentially reduces precision of float results.</para>
		/// </summary>
		/// <param name="tResampleFmt">- alias of the corresponding endpoint's resampling details.</param>
		/// <param name="tEndpointFmt">- alias of the corresponding endpoint's device details.</param>
		/// <param name="nChannelOffset">- first channel index into the ring buffer of the Resampler's parent AudioBuffer.</param>
		/// <param name="pDataSrc">- pointer to corresponding devices linear buffer to read from.</param>
		/// <param name="pDataDst">- pointer to corresponding devices linear buffer to write into.</param>
		/// <param name="nFramesLimit">- if bIn is set to FALSE (writing to a render device buffer) used to tell the function when
		/// to stop resampling to still fit within the size of the device's render buffer; ignored otherwise.</param>
		/// <param name="bIn">- indicator if pDataSrc is a pointer to a capture buffer (moving captured data into ring buffer) 
		/// or to a ring buffer (moving processed data out of ring buffer into linear render buffer).</param>
		/// <returns>Returns the number of resampled values written to the buffer.</returns>		
		UINT32 Resample(RESAMPLEFMT& tResampleFmt, ENDPOINTFMT& tEndpointFmt, void* pDataSrc, void* pDataDst, UINT32 nFramesLimit, BOOL bIn);

	private:
		/// <summary>
		/// Computes the 0th order modified bessel function of the first kind.
		/// (Needed to compute Kaiser window).
		/// </summary>
		/// <param name="x"></param>
		/// <returns></returns>		
		static DOUBLE Izero(DOUBLE x);

	private:
		// Variables
		FLOAT						fLPScale				{ 1.0 };
		static RESAMPLERPARAMS		tResamplerParams;
};
