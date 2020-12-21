#pragma once
#include <JuceHeader.h>
#define LATENCY_TIME 0.010           // specifies the transposition range in milliseconds (used for allocation of delay buffer)

/// <summary>
/// Class implementing a Doppler effect based pitch shifting algorithm.
/// </summary>
class PitchShifter 
{
    public:
        PitchShifter(){}

        /// <summary>
        /// <para>Initialization of the delay buffer, harcoded for 2 channels atm for demonstration purposes.</para>
        /// <para>Regarding scalibiltiy, nr. of channels and buffer size should be provided as parameters under GUI control.</para>
        /// </summary>
        /// <param name="SamplesPerBlockExpected"></param>
        /// <param name="SampleRate"></param>
        void initialize(int SamplesPerBlockExpected, double SampleRate) {

            transposition_range = LATENCY_TIME * SampleRate;
            delayBufferSize = SamplesPerBlockExpected + transposition_range;
            delayBuffer.setSize(2, delayBufferSize);
            delayBuffer.clear();
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
        void process(AudioBuffer<float>* inbuffer, int startSample, int numSamples, int maxDelayInSamples, int channel, float deviceGain)						
        {
            float* writeBuffer = inbuffer->getWritePointer(channel, startSample);

            const int delayBufferSize = delayBuffer.getNumSamples();
            fillDelaybuffer(inbuffer->getNumSamples(), channel, delayBufferSize, inbuffer->getReadPointer(channel, startSample), 1.0);

            for (auto sample = 0; sample < numSamples; ++sample)
            {
                const float* delay = delayBuffer.getReadPointer(channel, startSample);

                float delaySamples1 = sawtooth1(maxDelayInSamples, channel);
                float delaySamples2 = sawtooth2(maxDelayInSamples, channel);
                int delayTime1 = static_cast<int>(delaySamples1);
                int delayTime2 = static_cast<int>(delaySamples2);

                int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTime1) % delayBufferSize;
                int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTime2) % delayBufferSize;


                float gain1 = sin(double_Pi * delaySamples1 / maxDelayInSamples);
                float gain2 = sin(double_Pi * delaySamples2 / maxDelayInSamples);

                writeBuffer[sample] = deviceGain*(gain1 * (delay[(readPosition1 + sample) % delayBufferSize]) + gain2 * (delay[(readPosition2 + sample) % delayBufferSize]));
            }
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
        void fillDelaybuffer(const int bufferLength, int channel, const int delayBufferLength, const float* bufferData, const float gain)
        {

            if (delayBufferLength > bufferLength + delayBufferWritePosition)
            {
                delayBuffer.copyFromWithRamp(channel, delayBufferWritePosition, bufferData, bufferLength, gain, gain);
            }
            else
            {
                const int delayBufferRemaining = delayBufferLength - delayBufferWritePosition;
                delayBuffer.copyFromWithRamp(channel, delayBufferWritePosition, bufferData, delayBufferRemaining, gain, gain);
                delayBuffer.copyFromWithRamp(channel, 0, bufferData + delayBufferRemaining, bufferLength - delayBufferRemaining, gain, gain);

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
        float sawtooth1(int maxDelayInSamples, int channel) {																	

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
        float sawtooth2(int maxDelayInSamples, int channel) {
        
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
        float                       sawtoothPhase1[2]           { 0.0 }, 
                                    sawtoothPhase2[2]           { 0.5, 0.5 },  // sawtooth functions shifted by pi/2 with respect to each other
                                    sawtoothFrequency           { 0.0 },
                                    sampleRate                  { 44100 };
        
        int                         delayBufferWritePosition    { 0 },
                                    transposition_range,
                                    delayBufferSize;
        
        bool                        pitchUporDown               { FALSE };

        juce::AudioBuffer<float>    delayBuffer;
};
