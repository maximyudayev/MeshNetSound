#pragma once
#include <windows.h>
#include "config.h"
#include "AudioEffect.h"

typedef struct AudioEffectMapEl {
	AudioEffect* pAudioEffect;
	BOOL bConsumed;
} AUDIOEFFECTMAPEL;

class RingBufferChannel
{
	public:
		RingBufferChannel();

		~RingBufferChannel();

		BOOL BindAudioEffect(AudioEffect* pAudioEffect);

		BOOL UnbindAudioEffect(AudioEffect* pAudioEffect);

		UINT32 GetFramesAvailable();

		BOOL SetConsumedFlag(AudioEffect* pEffect);

		BOOL IsReadByAll();

		BOOL ReadNextPacket(AudioEffect* pEffect);

		HRESULT WriteNextPacket(AudioEffect* pEffect);



		BOOL PrepareToPullDataIn();

		BOOL FinishToPullDataIn();

		BOOL PrepareToPushDataOut();

		BOOL FinishToPushDataOut();

		
		UINT32 GetBufferSize();

		FLOAT* GetBufferPointer();

		UINT32 GetWriteOffset();

		void SetWriteOffset(UINT32 nOffset);

		UINT32 GetReadOffset();

		void SetReadOffset(UINT32 nOffset);

		BOOL GetWriteAheadReadByLap();

		void SetWriteAheadReadByLap(BOOL bAhead);

	private:
		FLOAT				pBuffer[AGGREGATOR_CIRCULAR_BUFFER_SIZE]	{ 0 };

		UINT32				nWriteOffset					{ 0 },
							nReadOffset						{ 0 },
							nAudioEffect					{ 0 },
							nBufferSize						{ AGGREGATOR_CIRCULAR_BUFFER_SIZE },
							nFramesAvailable				{ 0 };

		BOOL				bWriteAheadReadByLap			{ FALSE };

		AUDIOEFFECTMAPEL	*pAudioEffectMap				{ NULL };

		SRWLOCK				srwWriteOffset,					// R/W Locks protect buffer offset variables from being overwritten by 
							srwReadOffset,					// a producer thread while a consumer thread uses them.
							srwWriteAheadReadByLap,			// Ensures that ring buffer samples of a corresponding device can be
							srwFramesAvailable;				// overwritten (in case consumer is slower than producer, i.e DSP vs. capture)
															// only when no consumer thread is in the process of reading data 
															// from the ring buffer.

		UINT32				nInstance;						
		static UINT32		nNewInstance;					
};
