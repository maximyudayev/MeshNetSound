#pragma once
#include <JuceHeader.h>
#define LATENCY_TIME 0.010

/// <summary>
/// Class implementing a flanger algorithm using an FIR comb filter with optional feedback.
/// </summary>
class Flanger 
{
	public:
		Flanger(){}

		/// <summary>
		/// <para>Initialization of the delay buffer, hardcoded for 2 channels atm for demonstration purposes.</para>
		/// <para>Regarding scalibiltiy, nr. of channels and buffer size should be provided as parameters under GUI control.</para>
		/// </summary>
		/// <param name="SamplesPerBlockExpected"></param>
		/// <param name="SampleRate"></param>
		void initialize(int SamplesPerBlockExpected, double SampleRate)
		{
			transposition_range = LATENCY_TIME * SampleRate;
			delayBufferSize = SamplesPerBlockExpected + transposition_range;
			delayBuffer.setSize(2, delayBufferSize);
			delayBuffer.clear();

			feedbackBuffer.setSize(2, delayBufferSize);
			feedbackBuffer.clear();
		}

        /// <summary>
        /// <para>Actual DSP callback, applies flanger to a single channel.</para>
		/// <para>Note: when using multi-channel flanger, must be called for each channel in a loop./para>
		/// <para>Uses one single delay line that is recombined with the current signal to create 
		/// the comb-filter effect.</para>
		/// <para>Performs linear interpolation on the delay time.</para>
        /// </summary>
        /// <param name="inbuffer"></param>
        /// <param name="startSample"></param>
        /// <param name="numSamples"></param>
        /// <param name="maxDelayInSamples"></param>
        /// <param name="channel"></param>
        /// <param name="DeviceGain"></param>
        void process(AudioBuffer<float>* inbuffer, int startSample, int numSamples, int maxDelayInSamples, int channel, float DeviceGain)						// pass input buffer by reference, get maxDelayInSamples from UI component
        {
			float* writeBuffer = inbuffer->getWritePointer(channel, startSample);
			const float* readBuffer =  inbuffer->getReadPointer(channel, startSample);
			

			const int delayBufferSize = delayBuffer.getNumSamples();
			fillDelaybuffer(inbuffer->getNumSamples(), channel, delayBufferSize, readBuffer, 1.0);

			for (auto sample = 0; sample < numSamples; ++sample)

			{
				const float* delay = delayBuffer.getReadPointer(channel, startSample);
				const float* feedback = feedbackBuffer.getReadPointer(channel, startSample);

				float delayTime = lfo_sinewave(maxDelayInSamples, channel);
				int delayTimeInSamples = static_cast<int>(delayTime);
				float fractionalDelay = delayTime - delayTimeInSamples;

				int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples) % delayBufferSize;		          // perform linear interpolation for now
				int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples - 1) % delayBufferSize;

				float output = writeBuffer[sample] + flangerDepth*((1.0-fractionalDelay)*delay[(readPosition1 + sample) % delayBufferSize] + fractionalDelay *delay[(readPosition2 + sample) % delayBufferSize])
					+ feedbackLevel*((1.0-fractionalDelay)* feedback[(readPosition1 + sample) % delayBufferSize] + fractionalDelay * feedback[(readPosition2 + sample) % delayBufferSize]);
				
				feedbackBuffer.setSample(channel, feedbackBufferWritePosition + sample, output);
				writeBuffer[sample] = DeviceGain*output;

			}
        }

		/// <summary>
		/// <para>Copies each packet received at the callback into the circular delay buffer.</para>
		/// <para>Allows the algorithm to use an 'arbitrarily' delayed sample within the transposition range.
		/// Lets LFO modulator specifiy the position in the buffer at any time instance for the delay line.</para>
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
		/// <para>The LFO modulator.</para>
		/// <para>Outputs at each given time instance the amount of delay that needs to be implemented 
		/// in the delay line.</para>
		/// </summary>
		/// <param name="maxDelayInSamples"></param>
		/// <param name="channel"></param>
		/// <returns></returns>
		float lfo_sinewave(int maxDelayInSamples, int channel)
		{
			sinePhase[channel] = sinePhase[channel] + sinefrequency/sampleRate;
			if (sinePhase[channel] >= 1) sinePhase[channel] -= 1;
			return (maxDelayInSamples/2)*(sin(2 * double_Pi * sinePhase[channel])+1);
		}

		/// <summary>
		/// <para>Updates the write index of the delay buffer after storing a packet in the callback.</para>
		/// <para>Doesn't do this in the fillDelayBuffer; can only be adjusted after each channel was copied.
		/// Must be called by the owning class after the channel loop completed.</para>
		/// </summary>
		/// <param name="numsamplesInBuffer"></param>
		void adjustDelayBufferWritePosition(int numsamplesInBuffer)                                                             
		{
			delayBufferWritePosition += numsamplesInBuffer;
			delayBufferWritePosition %= delayBufferSize;
		}
		
		/// <summary>
		/// <para>Updates the write index of the feedback buffer after storing a packet in the callback.</para>
		/// </summary>
		/// <param name="numsamplesInBuffer"></param>
		void adjustFeedBackBufferWritePosition(int numsamplesInBuffer)                                                             
		{
			feedbackBufferWritePosition += numsamplesInBuffer;
			feedbackBufferWritePosition %= delayBufferSize;
		}

		/// <summary>
		/// <para>Setter member function for GUI controlled owner of the flanger object.</para>
		/// </summary>
		/// <param name="depth"></param>
		void setDepth(float depth)
		{
			flangerDepth = depth;
		}

		/// <summary>
		/// <para></para>
		/// </summary>
		/// <param name="feedback"></param>
		void setFeedback(float feedback)
		{
			flangerDepth = feedback;
		}

		/// <summary>
		/// <para></para>
		/// </summary>
		/// <param name="rate"></param>
		void setLFO(float rate)
		{
			sinefrequency = rate;
		}

	private:
		float						sinePhase[2]				{ 0.0, 0.0 },
									sinefrequency				{ 0.0 },
									flangerDepth				{ 0.0 },
									feedbackLevel				{ 0.0 },		// should ALWAYS be lower than 1 !!
									sampleRate					{ 44100 };
		
		int							delayBufferWritePosition	{ 0 }, 
									feedbackBufferWritePosition { 0 }, 
									transposition_range, 
									delayBufferSize;

		juce::AudioBuffer<float>	delayBuffer, 
									feedbackBuffer;
};
