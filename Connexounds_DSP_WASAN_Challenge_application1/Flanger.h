#pragma once
#define _USE_MATH_DEFINES
#define TRANSPOSITION 480           // specifies the transposition range in samples (used for allocation of delay buffer)

#include "AudioEffect.h"
#include "AudioBuffer.h"
#include <cmath>

/// <summary>
/// Class implementing a flanger algorithm using an FIR comb filter with optional feedback.
/// </summary>
class Flanger : public AudioEffect
{
	public:
		Flanger(int sampleRate, int nrOfChannels)
		{
			delayBufferSize = TRANSPOSITION * 2;

			// allocate heap structure for delay buffer
			delayBuffer = (float**)malloc(sizeof(float*) * nrOfChannels);
			for (auto i = 0; i < nrOfChannels; i++)
			{
				delayBuffer[i] = (float*)malloc(sizeof(float) * delayBufferSize);
			}
			
			// allocate heap structure for feedback buffer (same size as delaybuffer)
			feedbackBuffer = (float**)malloc(sizeof(float*) * nrOfChannels);
			for (auto i = 0; i < nrOfChannels; i++)
			{
				feedbackBuffer[i] = (float*)malloc(sizeof(float) * delayBufferSize);
			}

			this->sampleRate = sampleRate;
			this->nrOfChannels = nrOfChannels;
		}

		~Flanger()
		{
			// free heap structure for delay buffer
			for (int i = 0; i < nrOfChannels; i++)
			{
				float* currentIntPtr = delayBuffer[i];
				free(currentIntPtr);
			}

			for (int i = 0; i < nrOfChannels; i++)
			{
				float* currentIntPtr = feedbackBuffer[i];
				free(currentIntPtr);
			}
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
        void process(DSPPacket* inbuffer)	override					// pass input buffer by reference, get maxDelayInSamples from UI component
        {
			FLOAT** processBuffer = inbuffer->pData;
			nrOfChannels = inbuffer->nChannels;
			nrOfSamples = inbuffer->nSamples;
			
			for (auto channel = 0; channel < nrOfChannels; ++channel)
			{
				fillDelaybuffer(nrOfSamples, channel, processBuffer[channel], 1.0);

				for (auto sample = 0; sample < nrOfSamples; ++sample)
				{
					const float* delay = delayBuffer[channel];
					const float* feedback = feedbackBuffer[channel];

					float delayTime = lfo_sinewave(TRANSPOSITION, channel);
					int delayTimeInSamples = static_cast<int>(delayTime);
					float fractionalDelay = delayTime - delayTimeInSamples;

					int readPosition1 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples) % delayBufferSize;		          // perform linear interpolation for now
					int readPosition2 = (delayBufferSize + delayBufferWritePosition - delayTimeInSamples - 1) % delayBufferSize;

					float output = processBuffer[channel][sample] + flangerDepth * ((1.0 - fractionalDelay) * delay[(readPosition1 + sample) % delayBufferSize] + fractionalDelay * delay[(readPosition2 + sample) % delayBufferSize])
						+ feedbackLevel * ((1.0 - fractionalDelay) * feedback[(readPosition1 + sample) % delayBufferSize] + fractionalDelay * feedback[(readPosition2 + sample) % delayBufferSize]);

					feedbackBuffer[channel][feedbackBufferWritePosition + sample] = output;
						
					processBuffer[channel][sample] = output;
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
		/// <para>Allows the algorithm to use an 'arbitrarily' delayed sample within the transposition range.
		/// Lets LFO modulator specifiy the position in the buffer at any time instance for the delay line.</para>
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
			return (maxDelayInSamples/2)*(sin(2 * M_PI * sinePhase[channel])+1);
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
		float		sinePhase[2]				{ 0.0, 0.0 },
					sinefrequency				{ 0.0 },
					flangerDepth				{ 0.0 },
					feedbackLevel				{ 0.0 },		// should ALWAYS be lower than 1 !!
					sampleRate					{ AGGREGATOR_SAMPLE_FREQ },
					** delayBuffer,
					** feedbackBuffer;
		

		int							delayBufferWritePosition	{ 0 }, 
									feedbackBufferWritePosition { 0 }, 
									nrOfChannels,
									nrOfSamples,
									delayBufferSize;

		float**						delayBuffer;
		float**						feedbackBuffer;
		float**						outputBuffer;

};
