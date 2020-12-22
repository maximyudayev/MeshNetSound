#pragma once
#include <windows.h>
#include "config.h"

class RingBufferChannel
{
	public:
		RingBufferChannel();

		~RingBufferChannel();

		FLOAT* GetBufferPointer();

		UINT32 GetWriteOffset();

		void SetWriteOffset(UINT32 nOffset);

		UINT32 GetReadOffset();

		void SetReadOffset(UINT32 nOffset);

		BOOL GetWriteAheadReadByLap();

		void SetWriteAheadReadByLap(BOOL bAhead);

		SRWLOCK GetReadOffsetSRWLock();

		SRWLOCK GetWriteOffsetSRWLock();

		SRWLOCK GetWriteAheadReadByLapSRWLock();

	private:
		FLOAT				pBuffer[AGGREGATOR_CIRCULAR_BUFFER_SIZE]	{ 0 };

		UINT32				nWriteOffset					{ 0 },
							nReadOffset						{ 0 };

		BOOL				bWriteAheadReadByLap			{ FALSE };
		SRWLOCK				srwWriteOffset,					// R/W Locks protect buffer offset variables from being overwritten by 
							srwReadOffset,					// a producer thread while a consumer thread uses them.
							srwWriteAheadReadByLap;			// Ensures that ring buffer samples of a corresponding device can be
															// overwritten (in case consumer is slower than producer, i.e DSP vs. capture)
															// only when no consumer thread is in the process of reading data 
															// from the ring buffer.

		UINT32				nInstance;						
		static UINT32		nNewInstance;					
};
