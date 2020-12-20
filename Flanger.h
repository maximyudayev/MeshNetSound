/***************************************************************************************
This class implements a flanger algorithm using a FIR comb filter with optional feedback
****************************************************************************************/

#pragma once
#include <JuceHeader.h>
#define LATENCY_TIME 0.010

class Flanger {

	public :

		Flanger()
		{

		}

		
		//************* Initialization of the delay buffer, hardcoded for 2 channels atm for demonstration purposes*******************//
		//************* Regarding scalibiltiy, nr. of channels and buffer size should be provided as parameters under GUI control ***//
   
		void initialize(int SamplesPerBlockExpected, double SampleRate)
		{

			transposition_range = LATENCY_TIME * SampleRate;
			delayBufferSize = SamplesPerBlockExpected + transposition_range;
			delayBuffer.setSize(2, delayBufferSize);
			delayBuffer.clear();

			feedbackBuffer.setSize(2, delayBufferSize);
			feedbackBuffer.clear();
		}

		//************ Actual DSP callback, applying the flanger to a single channel**********************************************//
		//************ Hence, when using multi-channel flanger, this function has to be called in a channel loop. ****************//
		//************ The implementation uses one single delay line that is recombined with the current signal to create the ****//
		//************ comb-filter effect. We perform linear interpolation on the delay time.*************************************//

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

		//******** This function copies each packet received at the callback into the circular delay buffer. This allows the algorithm***//
		//******** to use an 'arbitrarily' delayed sample within the transposition range. This way, the LFO modulator****** *************//
		//******** can specifiy the position in the buffer at any time instance for the delay line **************************************//

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


		//********* The LFO modulator, that output at each given time instance the amount of delay that needs to be implemented in *****//
		//********* the delay line.*****************************************************************************************************//
		
		float lfo_sinewave(int maxDelayInSamples, int channel)
		{
			sinePhase[channel] = sinePhase[channel] + sinefrequency/sampleRate;
			if (sinePhase[channel] >= 1) sinePhase[channel] -= 1;
			return (maxDelayInSamples/2)*(sin(2 * double_Pi * sinePhase[channel])+1);
		}

		
		

		//************ To update the write index of the delay buffer after storing a packet in the callback. We don't do this in the fillDelayBuffer ** //
		//************ as we can only adjust it after each channel has been copied. Hence, it has to be called by the owning class after the channel loop *//
		//************ has completed **********************************************************************************************************************//
		
		
		void adjustDelayBufferWritePosition(int numsamplesInBuffer)                                                             
		{
			delayBufferWritePosition += numsamplesInBuffer;
			delayBufferWritePosition %= delayBufferSize;
		}


		//************ To update the write index of the feedback buffer after storing a packet in the callback. ******************************************//
		

		void adjustFeedBackBufferWritePosition(int numsamplesInBuffer)                                                             
		{
			feedbackBufferWritePosition += numsamplesInBuffer;
			feedbackBufferWritePosition %= delayBufferSize;
		}




		//**********  Setter member functions for GUI controlled owner of the flanger object *************************************************************//


		void setDepth(float depth)
		{
			flangerDepth = depth;
		}

		void setFeedback(float feedback)
		{
			flangerDepth = feedback;
		}

		void setLFO(float rate)
		{
			sinefrequency = rate;
		}

		



	private :

		float sinePhase[2]{ 0.0,0.0 };
		float sinefrequency{ 0.0 };

		float flangerDepth{ 0.0 };
		float feedbackLevel{ 0.0 };		    // should ALWAYS be lower than 1 !!
		

		float sampleRate{ 44100 };
		int delayBufferWritePosition{ 0 }, feedbackBufferWritePosition {0};
		int transposition_range;
		int delayBufferSize;
		juce::AudioBuffer<float> delayBuffer, feedbackBuffer;


};