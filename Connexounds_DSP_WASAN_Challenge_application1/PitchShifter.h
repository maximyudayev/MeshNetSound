#pragma once
#define _USE_MATH_DEFINES
#define TRANSPOSITION 480           // specifies the transposition range in samples (used for allocation of delay buffer)

#include "AudioEffect.h"
#include "AudioBuffer.h"
#include <cmath>

/// <summary>
/// Class implementing a Doppler effect based pitch shifting algorithm.
/// </summary>
class PitchShifter : public AudioEffect
{
    public:
        PitchShifter(int sampleRate, int nrOfChannels)
        {
            delayBufferSize = TRANSPOSITION * 2;

            // allocate heap structure for delay buffer
            delayBuffer = (float**)malloc(sizeof(float*) * nrOfChannels);
            for (auto i = 0; i < nrOfChannels; i++)
            {
                delayBuffer[i] = (float*)malloc(sizeof(float) * delayBufferSize);
            }

            this->sampleRate = sampleRate;
            this->nrOfChannels = nrOfChannels;
        }

        ~PitchShifter()
        {
            // free heap structure for delay buffer
            for (int i = 0; i < nrOfChannels; i++)
            {
                float* currentIntPtr = delayBuffer[i];
                free(currentIntPtr);
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
        /// <param name="inbuffer"></param>
        /// <param name="startSample"></param>
        /// <param name="numSamples"></param>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="channel"></param>
        /// <param name="deviceGain"></param>
        void process(DSPPacket* inbuffer) override
        {
            FLOAT** processBuffer = inbuffer->pData;
            nrOfChannels = inbuffer->nChannels;
            nrOfSamples = inbuffer->nSamples;

            for (auto channel = 0; channel < nrOfChannels; ++channel)
            {
                fillDelaybuffer(nrOfSamples, channel, processBuffer[channel], 1.0);

                for (auto sample = 0; sample < inbuffer->nSamples; ++sample)
                {
                    const float* delay = delayBuffer[channel];

                    float delaySamples1 = sawtooth1(TRANSPOSITION, channel);
                    float delaySamples2 = sawtooth2(TRANSPOSITION, channel);
                    int delayTime1 = static_cast<int>(delaySamples1);
                    int delayTime2 = static_cast<int>(delaySamples2);

                    int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTime1) % delayBufferSize;
                    int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTime2) % delayBufferSize;

                    float gain1 = sin(M_PI * delaySamples1 / TRANSPOSITION);
                    float gain2 = sin(M_PI * delaySamples2 / TRANSPOSITION);

                    processBuffer[channel][sample] = gain1 * (delay[(readPosition1 + sample) % delayBufferSize]) + gain2 * (delay[(readPosition2 + sample) % delayBufferSize]);

                    outputBuffer = processBuffer;


                }
            }
        }

        /// <summary>
        /// <para>Returns the data of the specified channel of the processed buffer.</para>
        /// </summary>
        /// <param name="bufferLength"></param>

        float* getChannelData(int nChannel) override
        {
            return outputBuffer[nChannel];
        }


        /// <summary>
        /// <para>Returns number of samples of the specified channel of the processed buffer.</para>
        /// </summary>
        /// <param name="bufferLength"></param>

        int getNumSamples() override
        {
            return nrOfSamples;
        }

        /// <summary>
        /// <para>Copies each packet received at the callback into the circular delay buffer.</para>
        /// <para>Allows the algorithm to use an 'arbitrarily' delayed sample within the
        /// transposition range.</para>
        /// <para>Lets sawtooth modulators can specifiy the position in the buffer at any 
        /// time instance for their respective delay line.</para>
        /// </summary>
        /// <param name="bufferLength"></param>
        /// <param name="channel"></param>
        /// <param name="delayBufferLength"></param>
        /// <param name="bufferData"></param>
        /// <param name="gain"></param>
        void fillDelaybuffer(const int bufferLength, int channel, float* bufferData, const float gain)
        {
            if (delayBufferSize > bufferLength + delayBufferWritePosition)
            {
                // Copy data from main input ring buffer chunk into delay buffer
                for (auto sample = 0; sample < bufferLength; sample++)
                {
                    delayBuffer[channel][delayBufferWritePosition + sample] = gain * bufferData[sample];
                }
            }
            else
            {
                const int delayBufferRemaining = delayBufferSize - delayBufferWritePosition;
                
                // Copy data from main input ring buffer chunk that still fits into delay buffer
                for (auto sample = 0; sample < delayBufferRemaining; sample++)
                {
                    delayBuffer[channel][delayBufferWritePosition + sample] = gain * bufferData[sample];
                }

                // Copy the rest of the data from main input ring buffer chunk into delay buffer
                for (auto sample = 0; sample < delayBufferRemaining; sample++)
                {
                    delayBuffer[channel][sample] = gain * bufferData[delayBufferRemaining + sample];
                }
            }
        }

        /// <summary>
        /// <para>The sawtooth modulators that output at each given time instance the 
        /// amount of delay that needs to be implemented in the delay lines.</para>
        /// <para>Note: When assessing the audio qualitatively, interpolation seemed 
        /// not necessary in this implementation.</para>
        /// </summary>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="channel"></param>
        /// <returns>A float at this point. Leaves room for possible (linear) interpolation of the delay time.</returns>
        float sawtooth1(int maxDelayInSamples, int channel)
        {
            float samplespercycle = sampleRate / sawtoothFrequency;
            sawtoothPhase1[channel] += (1 / samplespercycle);   																
            if (sawtoothPhase1[channel] >= 1) sawtoothPhase1[channel] -= 1;
            if (pitchUporDown == false) return maxDelayInSamples * sawtoothPhase1[channel];
            if (pitchUporDown == true) return maxDelayInSamples * (1 - sawtoothPhase1[channel]);
        }

        /// <summary>
        /// 
        /// </summary>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="channel"></param>
        /// <returns></returns>
        float sawtooth2(int maxDelayInSamples, int channel) 
        {
            float samplespercycle = sampleRate / sawtoothFrequency;                                                             
            sawtoothPhase2[channel] += (1 / samplespercycle);   																
            if (sawtoothPhase2[channel] >= 1) sawtoothPhase2[channel] -= 1;
            if (pitchUporDown == false) return maxDelayInSamples * sawtoothPhase2[channel];
            if (pitchUporDown == true) return maxDelayInSamples * (1 - sawtoothPhase2[channel]);
        }

        /// <summary>
        /// <para>Updates the write index of the circular buffer after storing a packet in the callback.</para>
        /// <para>Doesn't do this in the fillDelayBuffer; can only be adjusted after each channel was copied.</para>
        /// <para>Must called by the owning class after the channel loop has completed.</para>
        /// </summary>
        /// <param name="numsamplesInBuffer"></param>
        void adjustDelayBufferWritePosition(int numsamplesInBuffer)                                                             
        {
            delayBufferWritePosition += numsamplesInBuffer;
            delayBufferWritePosition %= delayBufferSize;
        }

        /// <summary>
        /// <para>Setter member functions for GUI controlled owner of the pitch shifting object.</para>
        /// </summary>
        void setUp()
        {
            pitchUporDown = true;
        }

        /// <summary>
        /// 
        /// </summary>
        void setDown()
        {
            pitchUporDown = false;
        }

        /// <summary>
        /// 
        /// </summary>
        /// <param name="rate"></param>
        void setLevel(float rate)
        {
            sawtoothFrequency = rate;
        }

    private:
        float           sawtoothPhase1[2]           { 0.0, 0.0 }, 
                        sawtoothPhase2[2]           { 0.5, 0.5 },  // sawtooth functions shifted by pi/2 with respect to each other
                        sawtoothFrequency           { 0.0 },
                        sampleRate                  { AGGREGATOR_SAMPLE_FREQ },
                        ** delayBuffer;
        
        int                         delayBufferWritePosition    { 0 },
                                    delayBufferSize,
                                    nrOfChannels,
                                    nrOfSamples;
        
        bool                        pitchUporDown               { FALSE };

        float**                     delayBuffer;
        float**                     outputBuffer;

};
