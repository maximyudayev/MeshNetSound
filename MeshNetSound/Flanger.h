#pragma once
#define _USE_MATH_DEFINES
#define TRANSPOSITION 480           // specifies the transposition range in samples (used for allocation of delay buffer)

#include "AudioEffect.h"
#include "AudioBuffer.h"
#include "RingBufferChannel.h"
#include <cmath>

typedef struct FlangerContext {
	float	sinePhase						{ 0.0 },
			sinefrequency					{ 0.0 },
			flangerDepth					{ 0.0 },
			feedbackLevel					{ 0.0 },		// should ALWAYS be lower than 1 !!!
			* delayBuffer,
			* feedbackBuffer;

	int		delayBufferWritePosition		{ 0 },
			feedbackBufferWritePosition		{ 0 };
} FLANGERCONTEXT;

/// <summary>
/// Class implementing a flanger algorithm using an FIR comb filter with optional feedback.
/// </summary>
class Flanger : public AudioEffect
{
	public:
		Flanger(int sampleRate, int nrOfChannels, RingBufferChannel** pRingBufferChannel) : AudioEffect(sampleRate, nrOfChannels, (void**)pRingBufferChannel)
		{
			delayBufferSize = TRANSPOSITION * 2;

			for (int i = 0; i < nrOfChannels; i++)
			{
				pRingBufferChannelMap[i].pRingBufferChannelEffectContext = new FLANGERCONTEXT();
				// allocate heap structure for delay buffer
				((FLANGERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->delayBuffer = (float*)malloc(sizeof(float) * delayBufferSize);
				// allocate heap structure for feedback buffer (same size as delaybuffer)
				((FLANGERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->feedbackBuffer = (float*)malloc(sizeof(float) * delayBufferSize);
			}
		}

		~Flanger()
		{
			// free heap structure for delay buffer
			for (int i = 0; i < nrOfChannels; i++)
			{
				free(((FLANGERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->delayBuffer);
				free(((FLANGERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->feedbackBuffer);
				delete (FLANGERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext);
			}
		}

		/// <summary>
		/// <para>Actual DSP callback, applies flanger to a single channel.</para>
		/// <para>Note: when using multi-channel flanger, must be called for each channel in a loop./para>
		/// <para>Uses one single delay line that is recombined with the current signal to create 
		/// the comb-filter effect.</para>
		/// <para>Performs linear interpolation on the delay time.</para>
		/// </summary>
        /// <param name="pDSPPacket"></param>
        void Process(DSPPacket* pDSPPacket)	override					// pass input buffer by reference, get maxDelayInSamples from UI component
        {
			RINGCHANNELMAPEL* pContext = this->GetRingChannelMapEl(pDSPPacket->pRingBufferChannel);
			pContext->nSamples = pDSPPacket->nSamples;
			
			FillDelaybuffer(pContext, 1.0);

			float* processBuffer = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferPointer();
			int bufferSize = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferSize();
			int offset = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetReadOffset();
			float* outputBuffer = pContext->fOutputBuffer;

			float* delayBuffer = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBuffer;
			float* feedbackBuffer = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackBuffer;
			int delayBufferWritePosition = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition;
			int feedbackBufferWritePosition = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackBufferWritePosition;
			float flangerDepth = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->flangerDepth;
			float feedbackLevel = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackLevel;

			for (int sample = 0; sample < pContext->nSamples; sample++)
			{
				const float* delay = delayBuffer;
				const float* feedback = feedbackBuffer;

				float delayTime = LFOSinewave(TRANSPOSITION, pContext);
				int delayTimeInSamples = static_cast<int>(delayTime);
				float fractionalDelay = delayTime - delayTimeInSamples;

				int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples) % delayBufferSize;		          // perform linear interpolation for now
				int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples - 1) % delayBufferSize;

				float output = processBuffer[(offset + sample) % bufferSize] 
					+ flangerDepth * ((1.0 - fractionalDelay) * delay[(readPosition1 + sample) % delayBufferSize] + fractionalDelay * delay[(readPosition2 + sample) % delayBufferSize])
					+ feedbackLevel * ((1.0 - fractionalDelay) * feedback[(readPosition1 + sample) % delayBufferSize] + fractionalDelay * feedback[(readPosition2 + sample) % delayBufferSize]);

				feedbackBuffer[feedbackBufferWritePosition + sample] = output;

				outputBuffer[sample] = output;
			}
        }
		
		/// <summary>
		/// <para>Copies each packet received at the callback into the circular delay buffer.</para>
		/// <para>Allows the algorithm to use an 'arbitrarily' delayed sample within the transposition range.
		/// Lets LFO modulator specifiy the position in the buffer at any time instance for the delay line.</para>
		/// </summary>
		/// <param name="pContext"></param>
		/// <param name="gain"></param>
		void FillDelaybuffer(RINGCHANNELMAPEL* pContext, const float gain)
		{
			float* buffer = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferPointer();
			int bufferSize = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferSize();
			int offset = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetReadOffset();
			float* delayBuffer = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBuffer;
			int delayBufferWritePosition = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition;

			// Copy data from main input ring buffer chunk into delay buffer
			for (int sample = 0; sample < bufferSize; sample++)
				delayBuffer[(delayBufferWritePosition + sample) % delayBufferSize] = gain * buffer[(offset + sample) % bufferSize];
		}

		/// <summary>
		/// <para>The LFO modulator.</para>
		/// <para>Outputs at each given time instance the amount of delay that needs to be implemented 
		/// in the delay line.</para>
		/// </summary>
		/// <param name="maxDelayInSamples"></param>
		/// <param name="pContext"></param>
		/// <returns></returns>
		float LFOSinewave(int maxDelayInSamples, RINGCHANNELMAPEL* pContext)
		{
			float* sinefrequency = &((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sinefrequency;
			float* sinePhase = &((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sinePhase;
			
			*sinePhase = *sinePhase + *sinefrequency / sampleRate;
			if (*sinePhase >= 1) *sinePhase -= 1;
			return (maxDelayInSamples / 2) * (sin(2 * M_PI * *sinePhase) + 1);
		}

		/// <summary>
		/// <para>Updates the write index of the delay buffer after storing a packet in the callback.</para>
		/// <para>Doesn't do this in the fillDelayBuffer; can only be adjusted after each channel was copied.
		/// Must be called by the owning class after the channel loop completed.</para>
		/// </summary>
		/// <param name="numsamplesInBuffer"></param>
		/// <param name="pContext"></param>
		void AdjustDelayBufferWritePosition(int numsamplesInBuffer, RINGCHANNELMAPEL* pContext)
		{
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition += numsamplesInBuffer;
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition %= delayBufferSize;
		}
		
		/// <summary>
		/// <para>Updates the write index of the feedback buffer after storing a packet in the callback.</para>
		/// </summary>
		/// <param name="numsamplesInBuffer"></param>
		void AdjustFeedBackBufferWritePosition(int numsamplesInBuffer, RINGCHANNELMAPEL* pContext)
		{
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackBufferWritePosition += numsamplesInBuffer;
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackBufferWritePosition %= delayBufferSize;
		}

		/// <summary>
		/// <para>Setter member function for GUI controlled owner of the flanger object.</para>
		/// </summary>
		/// <param name="depth"></param>
		void SetDepth(float depth, RINGCHANNELMAPEL* pContext)
		{
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->flangerDepth = depth;
		}

		/// <summary>
		/// <para></para>
		/// </summary>
		/// <param name="feedback"></param>
		void SetFeedback(float feedback, RINGCHANNELMAPEL* pContext)
		{
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->feedbackLevel = feedback;
		}

		/// <summary>
		/// <para></para>
		/// </summary>
		/// <param name="rate"></param>
		void SetLFO(float rate, RINGCHANNELMAPEL* pContext)
		{
			((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sinefrequency = rate;
		}

	private:
		int			delayBufferSize;
};
