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
		AudioBuffer(std::string filename, UINT32 nMember);
		~AudioBuffer();
		static HRESULT CreateBufferGroup(UINT32* pGroup);
		static HRESULT RemoveBufferGroup(UINT32 nGroup);
		static UINT32 GetBufferGroupIndex(UINT32 nGroup);
		HRESULT SetFormat(WAVEFORMATEX* pwfx);
		HRESULT InitBuffer(UINT32* nEndpointBufferSize, FLOAT** pCircularBuffer, 
							UINT32* nCircularBufferSize, DWORD nUpsample, DWORD nDownsample);
		HRESULT InitWAV();
		HRESULT PullData(BYTE* pData, BOOL* bDone);
		HRESULT PushData(BYTE* pData, UINT32 nFrames);
		UINT32 FramesAvailable();

	private:
		// WAV file output related variables
		FILE				** fOriginalOutputFiles			{ NULL },
							** fResampledOutputFiles		{ NULL };
		DWORD				* nOriginalFileLength			{ NULL },
							* nResampledFileLength			{ NULL };
		std::string			sFilename;
		BOOL				bOutputWAV						{ FALSE };
	
		// Circular buffer related variables
		RESAMPLEFMT			tResampleFmt;
		Resampler			* pResampler;

		// Endpoint buffer related variables
		ENDPOINTFMT			tEndpointFmt;
		
		// Auxillary variables for grouping by memership
		// to a particular ring buffer and global identification
		UINT32				nMemberId;						// AudioBuffer group membership alias of the device
		static UINT32		nGroups;						// Counter of the number of AudioBuffer groups
		static UINT32		nNewGroupId;					// Auto-incremented "GUID" of the AudioBuffer group
		static UINT32		* pGroupId;						// Array of AudioBuffer group indices
		UINT32				nChannelOffset;					// Index of the device's 1st channel in the aggregated ring buffer		
		static UINT32		* pNewChannelOffset;			// Array of indices of the 1st channel location in the aggregated
															// ring buffer for the next future inserted AudioBuffer instance
		UINT32				nInstance;						// Device's "GUID"
		static UINT32		nNewInstance;					// "GUID" of the next AudioBuffer

#ifdef DEBUG
		UINT32				durationCounter					{ 0 };
#endif // DEBUG
};