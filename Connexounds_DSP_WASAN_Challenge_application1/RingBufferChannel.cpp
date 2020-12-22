#include "RingBufferChannel.h"

UINT32 RingBufferChannel::nNewInstance{ 0 };

RingBufferChannel::RingBufferChannel()
{
	// Initialize read/write locks for ring buffer read and write offset
	InitializeSRWLock(&this->srwWriteOffset);
	InitializeSRWLock(&this->srwReadOffset);
	InitializeSRWLock(&this->srwWriteAheadReadByLap);
}

RingBufferChannel::~RingBufferChannel()
{

}

FLOAT* RingBufferChannel::GetBufferPointer()
{
	return this->pBuffer;
}

UINT32 RingBufferChannel::GetWriteOffset()
{
	return this->nWriteOffset;
}

void RingBufferChannel::SetWriteOffset(UINT32 nOffset)
{
	this->nWriteOffset;
}

UINT32 RingBufferChannel::GetReadOffset()
{
	return this->nReadOffset;
}

void RingBufferChannel::SetReadOffset(UINT32 nOffset)
{
	this->nReadOffset = nOffset;
}

BOOL RingBufferChannel::GetWriteAheadReadByLap()
{
	return this->bWriteAheadReadByLap;
}

void RingBufferChannel::SetWriteAheadReadByLap(BOOL bAhead)
{
	this->bWriteAheadReadByLap = bAhead;
}

SRWLOCK RingBufferChannel::GetReadOffsetSRWLock()
{
	return this->srwReadOffset;
}

SRWLOCK RingBufferChannel::GetWriteOffsetSRWLock()
{
	return this->srwWriteOffset;
}

SRWLOCK RingBufferChannel::GetWriteAheadReadByLapSRWLock()
{
	return this->srwWriteAheadReadByLap;
}
