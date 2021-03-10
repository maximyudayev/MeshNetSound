#pragma once

typedef struct DSPPacket {
	void* pRingBufferChannel; // context - which RingBufferChannel produced data for audio effect to consume
	UINT32 nSamples;
} DSPPACKET;

typedef struct RingChannelMapEl {
	void* pRingBufferChannel;
	int nSamples { 0 };
	float fOutputBuffer[AUDIOEFFECT_OUTPUT_BUFFER_SIZE];
	void* pRingBufferChannelEffectContext;
} RINGCHANNELMAPEL;

class AudioEffect
{
	public:
		AudioEffect(int sampleRate, int nrOfChannels, void** pRingBufferChannel)
		{
			this->nrOfChannels = nrOfChannels;
			this->sampleRate = sampleRate;

			pRingBufferChannelMap = (RINGCHANNELMAPEL*)malloc(sizeof(RINGCHANNELMAPEL) * nrOfChannels);
			for (int i = 0; i < nrOfChannels; i++)
			{
				pRingBufferChannelMap[i] = RINGCHANNELMAPEL();
				pRingBufferChannelMap[i].pRingBufferChannel = pRingBufferChannel[i];
			}
		}

		virtual ~AudioEffect()
		{
			free(this->pRingBufferChannelMap);
		}

		virtual void Process(DSPPacket* pDSPPacket) = 0;
		
		/// <summary>
		/// <para>Returns number of samples of the specified channel of the processed buffer.</para>
		/// </summary>
		/// <returns></returns>
		int GetNumSamples(void* pRingBufferChannel)
		{
			RINGCHANNELMAPEL* pRingChannelMapEl = GetRingChannelMapEl(pRingBufferChannel);
			return (pRingChannelMapEl != NULL) ? pRingChannelMapEl->nSamples : -1;
		}
		
		/// <summary>
		/// <para>Returns the data of the specified channel of the processed buffer.</para>
		/// </summary>
		/// <param name="pRingBufferChannel"></param>
		/// <returns></returns>
		float* GetResult(void* pRingBufferChannel)
		{
			RINGCHANNELMAPEL* pRingChannelMapEl = GetRingChannelMapEl(pRingBufferChannel);
			return (pRingChannelMapEl != NULL) ? pRingChannelMapEl->fOutputBuffer : NULL;
		}

		RINGCHANNELMAPEL* GetRingChannelMapEl(void* pRingBufferChannel)
		{
			for (int i = 0; i < this->nrOfChannels; i++)
				if (pRingBufferChannelMap[i].pRingBufferChannel == pRingBufferChannel) return &pRingBufferChannelMap[i];

			return NULL;
		}

	protected:
		RINGCHANNELMAPEL	* pRingBufferChannelMap;
		int					sampleRate					{ AGGREGATOR_SAMPLE_FREQ }, 
							nrOfChannels				{ 0 };
};
