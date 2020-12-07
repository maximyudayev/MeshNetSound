#pragma once
#define _USE_MATH_DEFINES
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"

// Type Definitions of AudioBuffer
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
	FLOAT** pBuffer;
	UINT32 nBufferOffset;
	UINT32* nBufferSize;
	DWORD nUpsample;
	DWORD nDownsample;
	FLOAT fFactor;
} RESAMPLEFMT;

// Type Definitions of Resampler
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
/// Ring Buffer Resampler.
/// </summary>
class Resampler
{
public:
	Resampler();
	~Resampler();
	static void InitLPFilter(BOOL bHighQuality, DOUBLE fRollOff, DOUBLE fBeta, UINT32 nTwosExp);
	static void FreeLPFilter();
	void SetLPScaling(FLOAT fFactor);
	UINT32 Resample(RESAMPLEFMT& pResampleFmt, ENDPOINTFMT& pEndpointFmt, UINT32 nChannelOffset, BYTE* pData);

private:
	static DOUBLE Izero(DOUBLE x);

	// Variables
	FLOAT						fLPScale				{ 1.0 };
    static RESAMPLERPARAMS		tResamplerParams;
};