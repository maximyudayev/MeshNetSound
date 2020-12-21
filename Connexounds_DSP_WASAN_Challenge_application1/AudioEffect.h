
class AudioEffect
{
	
	public :
		virtual void process(DSPPacket* inbuffer) = 0;
		virtual ~AudioEffect() = 0;
};


typedef struct DSPPacket {
	UINT32 nSamples;
	UINT32 nChannels;
	FLOAT** pData;
} DSPPACKET;