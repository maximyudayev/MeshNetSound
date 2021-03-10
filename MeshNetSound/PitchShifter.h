#pragma once
#define _USE_MATH_DEFINES
#define TRANSPOSITION 480           // specifies the transposition range in samples (used for allocation of delay buffer)

#include "AudioEffect.h"
#include "AudioBuffer.h"
#include "RingBufferChannel.h"
#include <cmath>

typedef struct PitchShifterContext {
    float   sawtoothPhase1              { 0.0 },
            sawtoothPhase2              { 0.5 },  // sawtooth functions shifted by pi/2 with respect to each other
            sawtoothFrequency           { 0.0 },
            * delayBuffer;

    int     delayBufferWritePosition    { 0 };

    bool    pitchUporDown               { FALSE };
} PITCHSHIFTERCONTEXT;

/// <summary>
/// Class implementing a Doppler effect based pitch shifting algorithm.
/// </summary>
class PitchShifter : public AudioEffect
{
    public:
        PitchShifter(int sampleRate, int nrOfChannels, RingBufferChannel** pRingBufferChannel) : AudioEffect(sampleRate, nrOfChannels, (void**)pRingBufferChannel)
        {
            delayBufferSize = TRANSPOSITION * 2;

            for (int i = 0; i < nrOfChannels; i++)
            {
                pRingBufferChannelMap[i].pRingBufferChannelEffectContext = new PITCHSHIFTERCONTEXT();
                // allocate heap structure for delay buffer
                ((PITCHSHIFTERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->delayBuffer = (float*)malloc(sizeof(float) * delayBufferSize);
            }
        }

        ~PitchShifter()
        {
            // free heap structure for delay buffer
            for (int i = 0; i < nrOfChannels; i++)
            {
                free(((PITCHSHIFTERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext))->delayBuffer);
                delete (PITCHSHIFTERCONTEXT*)(pRingBufferChannelMap[i].pRingBufferChannelEffectContext);
            }
        }
        
        /// <summary>
        /// <para>Actual DSP callback, applyies the pitch shift to a single channel.</para>
        /// <para>Note: when using multi-channel (polyphonic) pitch shift, must be called 
        /// for each channel in a loop.</para>
        /// <para>Uses two different 'delay lines' within the same delay buffer, by sawtooth 
        /// modulation of the delay time.</para>
        /// <para>Each delay line has its separate sawtooth modulator and they are 180° out of phase.</para>
        /// <para>To eliminate glitches, uses sine envelopes for each delay line. Since they are 180° out of phase,
        ///  output power is constant.</para>
        /// </summary>
        /// <param name="pDSPPacket"></param>
        void Process(DSPPacket* pDSPPacket) override
        {
            RINGCHANNELMAPEL* pContext = this->GetRingChannelMapEl(pDSPPacket->pRingBufferChannel);
            pContext->nSamples = pDSPPacket->nSamples;

            FillDelaybuffer(pContext, 1.0);

            float* processBuffer = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferPointer();
            int bufferSize = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetBufferSize();
            int offset = ((RingBufferChannel*)pContext->pRingBufferChannel)->GetReadOffset();
            float* outputBuffer = pContext->fOutputBuffer;

            int delayBufferWritePosition = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition;
            float* delayBuffer = ((FLANGERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBuffer;

            for (auto sample = 0; sample < pContext->nSamples; sample++)
            {
                const float* delay = delayBuffer;

                float delaySamples1 = sawtooth1(TRANSPOSITION, pContext);
                float delaySamples2 = sawtooth2(TRANSPOSITION, pContext);
                int delayTime1 = static_cast<int>(delaySamples1);
                int delayTime2 = static_cast<int>(delaySamples2);

                int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTime1) % delayBufferSize;
                int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTime2) % delayBufferSize;

                float gain1 = sin(M_PI * delaySamples1 / TRANSPOSITION);
                float gain2 = sin(M_PI * delaySamples2 / TRANSPOSITION);

                outputBuffer[sample] = gain1 * (delay[(readPosition1 + sample) % delayBufferSize]) + gain2 * (delay[(readPosition2 + sample) % delayBufferSize]);
            }
        }

        /// <summary>
        /// <para>Copies each packet received at the callback into the circular delay buffer.</para>
        /// <para>Allows the algorithm to use an 'arbitrarily' delayed sample within the
        /// transposition range.</para>
        /// <para>Lets sawtooth modulators can specifiy the position in the buffer at any 
        /// time instance for their respective delay line.</para>
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
        /// <para>The sawtooth modulators that output at each given time instance the 
        /// amount of delay that needs to be implemented in the delay lines.</para>
        /// <para>Note: When assessing the audio qualitatively, interpolation seemed 
        /// not necessary in this implementation.</para>
        /// </summary>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="pContext"></param>
        /// <returns>A float at this point. Leaves room for possible (linear) interpolation of the delay time.</returns>
        float sawtooth1(int maxDelayInSamples, RINGCHANNELMAPEL* pContext)
        {
            float* sawtoothFrequency = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sawtoothFrequency;
            float* sawtoothPhase1 = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sawtoothPhase1;
            bool* pitchUporDown = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->pitchUporDown;
            float samplespercycle = sampleRate / *sawtoothFrequency;
            *sawtoothPhase1 += (1 / samplespercycle);   																
            if (*sawtoothPhase1 >= 1) *sawtoothPhase1 -= 1;
            if (*pitchUporDown == false) return maxDelayInSamples * *sawtoothPhase1;
            if (*pitchUporDown == true) return maxDelayInSamples * (1 - *sawtoothPhase1);
        }

        /// <summary>
        /// 
        /// </summary>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="pContext"></param>
        /// <returns></returns>
        float sawtooth2(int maxDelayInSamples, RINGCHANNELMAPEL* pContext)
        {
            float* sawtoothFrequency = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sawtoothFrequency;
            float* sawtoothPhase2 = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sawtoothPhase1;
            bool* pitchUporDown = &((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->pitchUporDown;
            float samplespercycle = sampleRate / *sawtoothFrequency;
            *sawtoothPhase2 += (1 / samplespercycle);
            if (*sawtoothPhase2 >= 1) *sawtoothPhase2 -= 1;
            if (*pitchUporDown == false) return maxDelayInSamples * *sawtoothPhase2;
            if (*pitchUporDown == true) return maxDelayInSamples * (1 - *sawtoothPhase2);
        }

        /// <summary>
        /// <para>Updates the write index of the circular buffer after storing a packet in the callback.</para>
        /// <para>Doesn't do this in the fillDelayBuffer; can only be adjusted after each channel was copied.</para>
        /// <para>Must called by the owning class after the channel loop has completed.</para>
        /// </summary>
        /// <param name="numsamplesInBuffer"></param>
        /// <param name="pContext"></param>
        void AdjustDelayBufferWritePosition(int numsamplesInBuffer, RINGCHANNELMAPEL* pContext)
        {
            ((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition += numsamplesInBuffer;
            ((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->delayBufferWritePosition %= delayBufferSize;
        }

        /// <summary>
        /// <para>Setter member functions for GUI controlled owner of the pitch shifting object.</para>
        /// </summary>
        void setUp(RINGCHANNELMAPEL* pContext)
        {
            ((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->pitchUporDown = true;
        }

        /// <summary>
        /// 
        /// </summary>
        void setDown(RINGCHANNELMAPEL* pContext)
        {
            ((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->pitchUporDown = false;
        }

        /// <summary>
        /// 
        /// </summary>
        /// <param name="rate"></param>
        void setLevel(float rate, RINGCHANNELMAPEL* pContext)
        {
            ((PITCHSHIFTERCONTEXT*)pContext->pRingBufferChannelEffectContext)->sawtoothFrequency = rate;
        }

    private:
        int             delayBufferSize;
};
