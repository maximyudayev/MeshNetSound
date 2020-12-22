
typedef struct DSPPacket {
	UINT32 nSamples;
	UINT32 nChannels;
	FLOAT** pData;
} DSPPACKET;


class AudioEffect
{
	public :
		virtual void process(DSPPacket* inbuffer) = 0;
		virtual int getNumSamples() = 0;
		virtual float* getChannelData(int nChannel) = 0;
		virtual ~AudioEffect() = 0;

		
};
