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
#include "AudioEffect.h"



/// <summary>
/// Class representing a distinct physical or virtual device with associated ring buffer space,
/// sample rate conversion details, and auxillary data required for getting data into the 
/// DSP processor thread.
/// </summary>
class AudioBuffer
{
	public:
		/// <summary>
		/// <para>AudioBuffer constructor.</para>
		/// <para>Calling thread must call AudioBuffer::SetFormat() and AudioBuffer::InitBuffer()
		/// before using any other functionality of the class.</para>
		/// <para>Note: The AudioBuffer does not limit, but assumes the maximum number
		/// of channels per arbitrary device to not exceed 2^32. That includes
		/// "virtual" devices (i.e. aggregated WASAN devices connected to another,
		/// each having aggregated own devices). In case larger number of aggregated
		/// channels is required for a truly enormous WASAN (i.e. up to 2^64 aggregated channels
		/// in the network, composed of N multichanneled connected devices, PCs, BT sets, etc.)
		/// only the data type of all variables (i.e. in loops) operating with nChannels
		/// variable, along with the variable itself, must be updated to the desired bit width
		/// (i.e. UINT8, UINT16, UINT32, UINT64).</para>
		/// </summary>
		/// <param name="filename">- stores filename used for writing WAV files.</param>
		/// <param name="nMember">- stores AudioBuffer group membership ID to cluster by ring buffer membership.</param>
		AudioBuffer(std::string filename, UINT32 nMember);

		/// <summary>
		/// <para>AudioBuffer destructor.</para>
		/// <para>Closes WAV files, if used, and frees alloc'ed memory of FILE arrays and file lengths.</para>
		/// </summary>
		virtual ~AudioBuffer();

		/// <summary>
		/// <para>Creates an alias for a group of AudioBuffer's to separate each by
		/// membership to a particular ring buffer.</para>
		/// </summary>
		/// <param name="pGroup">- pointer to the location to store group membership ID.</param>
		/// <returns>Status.</returns>
		static HRESULT CreateBufferGroup(UINT32* pGroup);

		/// <summary>
		/// <para>Removes AudioBuffer group's index from array of existing ones,
		/// decrements the number of groups remaining.</para>
		/// </summary>
		/// <param name="nGroup">- the group "GUID" to remove.</param>
		/// <returns>Status.</returns>
		static HRESULT RemoveBufferGroup(UINT32 nGroup);

		/// <summary>
		/// <para>Retreives static array index corresponding to nGroup "GUID"
		/// to index into corresponding static channel offset.</para>
		/// </summary>
		/// <param name="nGroup">- the group "GUID" to look up array index for.</param>
		/// <returns>Array index of the AudioBuffer group.</returns>
		static UINT32 GetBufferGroupIndex(UINT32 nGroup);

		/// <summary>
		/// <para>Copies WAVEFORMATEX data received from WASAPI for all audio related operations.</para>
		/// <para>WAVEFORMATEX uses 16bit value for channels, hence maximum intrinsic number of 
		/// virtual channels is 65'536.</para>
		/// </summary>
		/// <param name="pwfx">- WAVEFORMATEX pointer obtained from calling thread after 
		/// WASAPI inits AudioClient.</param>
		/// <returns>ERROR_SUCCESS or ENOMEM.</returns>
		HRESULT SetFormat(WAVEFORMATEX* pwfx);

		/// <summary>
		/// <para>Initializes stream resample properties and endpoint buffer size.</para>
		/// <para>Note: currently not thread-safe since AudioBuffer::nNextChannelOffset
		/// does not yet have a mutex. Each AudioBuffer should call InitBuffer sequentially
		/// in order to properly setup circular buffer channel offsets for each channel.</para>
		/// </summary>
		/// <param name="nEndpointBufferSize">- length of endpoint buffer per channel</param>
		/// <param name="pCircularBuffer">- pointer to memory of the first channel of the corresponding 
		/// device in the circular buffer of the aggregator</param>
		/// <param name="nCircularBufferSize">- size of circular buffer of the aggregator</param>
		/// <param name="nUpsample">- upsampling factor</param>
		/// <param name="nDownsample">- downsampling factor</param>
		/// <returns>
		/// ERROR_SUCCESS if buffer set up succeeded.
		/// </returns>
		HRESULT InitBuffer(UINT32* nEndpointBufferSize, FLOAT** pCircularBuffer, 
							UINT32* nCircularBufferSize, DWORD nUpsample, DWORD nDownsample);
		
		/// <summary>
		/// <para>Initializes .WAV file headers for each channel of a device.
		/// If original stream is resampled, also initializes .WAV file headers 
		/// for the resampled version of the dat.</para>
		/// </summary>
		/// <returns>
		/// <para>ERROR_TOO_MANY_OPEN_FILES if fopen fails for FILE_OPEN_ATTEMPTS times.</para>
		/// <para>ERROR_SUCCESS if WAV file for each channel is properly initialized.</para>
		/// </returns>		
		HRESULT InitWAV();
		
		/// <summary>
		/// <para>The main audio data decimating method.</para>
		/// <para>Splits original WASAPI stream channelwise and stores decimated resampled
		/// data in the aggregator's circular buffer previously passed by the calling thread
		/// in AudioBuffer::InitBuffer(). Resamples in-place in time domain the original signal
		/// as it is being pushed onto the buffer to the calling thread's desired frequency.</para>
		/// <para>Goes over each audio packet only once. Decimates blocks by offsetting and 
		/// casting as FLOAT pointers.</para>
		/// <para>Current implementation promotes and employs in-place buffer processing to reduce latency and
		/// reduce technically unnecessary latency as the result of writing several intermediate buffers.</para>
		/// <para>Writes data to a WAV file if calling thread invoked AudioBuffer::InitWAV() prior.</para>
		/// <para>Calling thread must ensure integrity of pBuffer and allocate enough
		/// memory in first dimension to fit all channels for all devices.</para>
		/// <para>Each AudioBuffer object writes resampled channelwise data decimated from
		/// captured endpoint data to consecutive row vectors of RESAMPLEFMT_T.pBuffer.</para>
		/// </summary>
		/// <param name="pData">- pointer to the first byte into the endpoint's newly captured packet.</param>
		/// <param name="bDone">- address of the variable responsible for terminating the program.</param>
		/// <returns></returns>
		HRESULT PullData(BYTE* pData);
		
		/// <summary>
		/// <para>Pushes data of the AudioBuffer into a corresponding render endpoint device.</para>
		/// <para>Note: data in the buffer must already be resampled, in PullData step, to adjust sample
		/// rate specifically to the device to which AudioBuffer will be outputting.</para>
		/// <para>Note: does not yet enforce matching dimensions between AudioBuffer and destination
		/// render endpoint device.</para>
		/// </summary>
		/// <param name="pData">- pointer to the first byte into the buffer to place audio packet for render.</param>
		/// <param name="nFrames">- number of frames available in the ring buffer to be pushed for render.</param>
		/// <returns></returns>		
		HRESULT PushData(BYTE* pData, UINT32 nFrames);

		/// <summary>
		/// <para>Returns a reference to a section of the input ringbuffer, extended with metadata inside a DSPpacket struct</para>
		/// <para>Must be called by the DSPthread to provide the processing DSP objects to operate on buffer data
		/// </para>
		/// </summary>
		/// <returns></returns>

		HRESULT ReadNextPacket(AudioEffect* pEffect);

		HRESULT WriteNextPacket(AudioEffect* pEffect);

		/// <summary>
		/// <para>Gets the number of frames in the buffer available for reading.</para>
		/// </summary>
		/// <returns></returns>		
		UINT32 FramesAvailable();

		/// <summary>
		/// <para>Update the nMinFramesOut variable for the device associated with this AudioBuffer.</para>
		/// <para>Must be called for render responsible AudioBuffer after changing resampling factor 
		/// ENDPOINTFMT.nBufferSize.</para>
		/// </summary>
		/// <returns></returns>
		HRESULT UpdateMinFramesOut();

		/// <summary>
		/// <para>Get nMinFramesOut varible for the device associated with this AudioBuffer.</para>
		/// </summary>
		/// <returns>Least number of frames needed for safe SRC for this device.</returns>
		UINT32 GetMinFramesOut();
		
	protected:
		/// <summary>
		/// <para>Function for derived classes to simulate the effect of WASAPI updating endpoint
		/// buffer size on each returned packet.</para>
		/// </summary>
		/// <param name="nFrames">- number of frames available in the device's buffer.</param>
		/// <returns></returns>		
		HRESULT SetEndpointBufferSize(UINT32 nFrames);

	private:
		// WAV file output related variables
		FILE				** fOriginalOutputFiles			{ NULL },
							** fResampledOutputFiles		{ NULL };
		DWORD				* nOriginalFileLength			{ NULL },
							* nResampledFileLength			{ NULL };
		std::string			sFilename;
		BOOL				bOutputWAV						{ FALSE };
	
		// Circular buffer related variables
		Resampler			* pResampler;
		RESAMPLEFMT			tResampleFmt;
		UINT32				nChannelOffset,					// Index of the device's 1st channel in the aggregated ring buffer		
							nTimeAlignOffset				{ 0 },
							nMinFramesOut					{ 0 };		// Indicator for output ring buffer when safe to SRC for output
																		// to avoid coming short on samples
		BOOL				bWriteAheadReadByLap			{ FALSE };
		SRWLOCK				srwWriteOffset,					// R/W Locks protect buffer offset variables from being overwritten by 
							srwReadOffset,					// a producer thread while a consumer thread uses them.
							srwWriteAheadReadByLap;			// Ensures that ring buffer samples of a corresponding device can be
															// overwritten (in case consumer is slower than producer, i.e DSP vs. capture)
															// only when no consumer thread is in the process of reading data 
															// from the ring buffer.
		// Endpoint buffer related variables
		ENDPOINTFMT			tEndpointFmt;

		// Auxillary variables for grouping by memership
		// to a particular ring buffer and global identification
		UINT32				nMemberId;						// AudioBuffer group membership alias of the device
		static UINT32		nGroups;						// Counter of the number of AudioBuffer groups
		static UINT32		nNewGroupId;					// Auto-incremented "GUID" of the AudioBuffer group
		static UINT32		* pGroupId;						// Array of AudioBuffer group indices
		
		static UINT32		* pNewChannelOffset;			// Array of indices of the 1st channel location in the aggregated
															// ring buffer for the next future inserted AudioBuffer instance
		UINT32				nInstance;						// Device's "GUID"
		static UINT32		nNewInstance;					// "GUID" of the next AudioBuffer
};
