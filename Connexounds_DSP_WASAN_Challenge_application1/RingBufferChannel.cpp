#include "RingBufferChannel.h"

UINT32 RingBufferChannel::nNewInstance{ 0 };

RingBufferChannel::RingBufferChannel()
{
	// Initialize read/write locks for ring buffer read and write offset
	InitializeSRWLock(&this->srwWriteOffset);
	InitializeSRWLock(&this->srwReadOffset);
	InitializeSRWLock(&this->srwWriteAheadReadByLap);
    InitializeSRWLock(&this->srwFramesAvailable);
}

RingBufferChannel::~RingBufferChannel()
{

}

BOOL RingBufferChannel::BindAudioEffect(AudioEffect* pAudioEffect)
{
	// Return FALSE if the audio effect is already bound to the ring buffer channel
	if (this->nAudioEffect > 0 && pAudioEffect != NULL)
		for (UINT32 i = 0; i < this->nAudioEffect; i++)
			if (this->pAudioEffectMap[i].pAudioEffect == pAudioEffect) return FALSE;
	
	// Else bind it
	AUDIOEFFECTMAPEL* dummy;

	// If the list of bound effects is empty
	if (this->pAudioEffectMap == NULL)
		dummy = (AUDIOEFFECTMAPEL*)malloc(sizeof(AUDIOEFFECTMAPEL));
	// If the list of bound effects is non-empty
	else
		dummy = (AUDIOEFFECTMAPEL*)realloc(this->pAudioEffectMap, (this->nAudioEffect + 1) * sizeof(AUDIOEFFECTMAPEL));

	// If allocation succeeded, update pointer
	if (dummy != NULL)
		this->pAudioEffectMap = dummy;
	// Else return FALSE
	else
		return FALSE;

	// Record new audio effect's pointer and set its consumed status to 0
	this->pAudioEffectMap[this->nAudioEffect].pAudioEffect = pAudioEffect;
	this->pAudioEffectMap[this->nAudioEffect].bConsumed = 0;
	// Increment number of audio effects consuming this ring buffer channel
	this->nAudioEffect++;

	return TRUE;
}

BOOL RingBufferChannel::UnbindAudioEffect(AudioEffect* pAudioEffect)
{
	if (this->nAudioEffect == 0) return TRUE;

	// Return FALSE if the audio effect was not found bound to the ring buffer channel
	BOOL bFound = FALSE;
	if (this->nAudioEffect > 0 && pAudioEffect != NULL)
		for (UINT32 i = 0; i < this->nAudioEffect; i++)
			if (this->pAudioEffectMap[i].pAudioEffect == pAudioEffect) bFound = TRUE;

	if (!bFound) return FALSE;

	// If no more audio effects associated remain, free array and return with TRUE
	if ((this->nAudioEffect - 1) == 0)
	{
		free(this->pAudioEffectMap);
		this->pAudioEffectMap = NULL;
		this->nAudioEffect = 0;
		return TRUE;
	}

	// If some nodes still remain, create a placeholder for new array
	AUDIOEFFECTMAPEL* dummy = (AUDIOEFFECTMAPEL*)malloc((this->nAudioEffect - 1) * sizeof(AUDIOEFFECTMAPEL));

	// If allocation succeeded, update pointer
	if (dummy != NULL)
	{
		// Copy all but the requested audio effect associations into the new array
		for (UINT32 i = 0, j = 0; i < this->nAudioEffect; i++, j++)
			dummy[i] = (this->pAudioEffectMap[j].pAudioEffect == pAudioEffect) ? 
				this->pAudioEffectMap[++j] : 
				this->pAudioEffectMap[j];

		// Free old memory and update pointer
		free(this->pAudioEffectMap);
		this->pAudioEffectMap = dummy;
	}
	// Else return FALSE
	else
		return FALSE;

	// Only when everything succeeded, update number of associated audio effects
	this->nAudioEffect--;

	return TRUE;
}

UINT32 RingBufferChannel::GetFramesAvailable()
{
    return (this->nWriteOffset > this->nReadOffset) ?
        (this->nWriteOffset - this->nReadOffset) :                      // data to read is linear
        (this->nBufferSize - this->nReadOffset + this->nWriteOffset);   // data to read is circular
}

BOOL RingBufferChannel::SetConsumedFlag(AudioEffect* pEffect)
{
    for (UINT32 i = 0; i < this->nAudioEffect; i++)
        if (this->pAudioEffectMap[i].pAudioEffect == pEffect)
        {
            this->pAudioEffectMap[i].bConsumed = TRUE;
            return TRUE;
        }

    return FALSE;
}

BOOL RingBufferChannel::IsReadByAll()
{
    for (UINT32 i = 0; i < this->nAudioEffect; i++)
        if (this->pAudioEffectMap[i].bConsumed == FALSE) return FALSE;
    
    return TRUE;
}

BOOL RingBufferChannel::ReadNextPacket(AudioEffect* pEffect)
{
    //AcquireSRWLockShared(&this->srwFramesAvailable);

    // Feed data to the calling audio effect thread into the provided callback
    DSPPACKET iDSPPacket = {
        this,
        this->nFramesAvailable
    };
    pEffect->Process(&iDSPPacket);

    this->SetConsumedFlag(pEffect);

    if (this->IsReadByAll())
    {
        //////////////////////////////////////////////////////////////////////////////
        //////////////// TODO: Remove chunk lock, update read offset, ////////////////
        ////////////////       clear condition variable if no more    //////////////// 
        ////////////////       data available yet                     ////////////////
        //////////////////////////////////////////////////////////////////////////////
        // Update read pointer respecting the circular buffer traversal
        this->nReadOffset = (this->nReadOffset + this->nFramesAvailable) % this->nBufferSize;

        this->bWriteAheadReadByLap = (this->nReadOffset > this->nWriteOffset);
    }

    //ReleaseSRWLockShared(&this->srwFramesAvailable);

    return TRUE;
}

HRESULT RingBufferChannel::WriteNextPacket(AudioEffect* pEffect)
{
    // Output DSP'ed data into the output ring buffer

    // Get pointer to the processed data buffer
    FLOAT* pData = pEffect->GetResult(this);
    UINT32 nSamplesWritten = pEffect->GetNumSamples(this);

    if ((this->nWriteOffset + nSamplesWritten) < this->nBufferSize)
    {
        // If moving data does not result in circular traversal of ring buffer, copy data directly in chunk
        memcpy(this->pBuffer + this->nWriteOffset,
            pData,
            sizeof(FLOAT) * nSamplesWritten);
    }
    else
    {
        // If moving data will result in circular traversal of ring buffer,
        // first copy only the data up till the end of ring buffer
        memcpy(this->pBuffer + this->nWriteOffset,
            pData,
            sizeof(FLOAT) * (nSamplesWritten - ((this->nWriteOffset + nSamplesWritten) % this->nBufferSize)));

        // Then copy the rest into the beginning of the ring buffer, 
        // don't forget to offset into the source buffer by the number of samples written previously
        memcpy(this->pBuffer,
            pData + (nSamplesWritten - ((this->nWriteOffset + nSamplesWritten) % this->nBufferSize)),
            sizeof(FLOAT) * ((this->nWriteOffset + nSamplesWritten) % this->nBufferSize));
    }

    //-------------------- End --------------------//
    UINT32 nWriteOffsetOld = this->nWriteOffset, nReadOffsetOld = this->nReadOffset;
    UINT32 nWriteOffsetNew = (nWriteOffsetOld + nSamplesWritten) % this->nBufferSize;
    BOOL bWriteAheadReadByLap = this->bWriteAheadReadByLap;

    // Update the offset to position after the last frame of the current chunk
    this->nWriteOffset = nWriteOffsetNew;

    // If write offset exceeded read offset by a whole lap, update read offset to the position of write offset,
    // hence drop frames that were delayed and try to catch up from the new position
    if (((nWriteOffsetOld > nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
        nWriteOffsetNew > nReadOffsetOld &&
        nWriteOffsetNew < nWriteOffsetOld) ||
        ((nWriteOffsetOld < nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
            nWriteOffsetNew < nReadOffsetOld &&
            nWriteOffsetNew < nWriteOffsetOld) ||
        ((nWriteOffsetOld < nReadOffsetOld || (bWriteAheadReadByLap && nWriteOffsetOld == nReadOffsetOld)) &&
            nWriteOffsetNew > nReadOffsetOld))
    {
        // Set read offset from the same point as the last non-overwritten sample
        this->nReadOffset = nWriteOffsetNew;
    }

    // Write offset catching up on read offset from the left (lower array indices)
    // indicating that if both match and write offset increased, then read offset is 
    // about to read overwritten newest data - not good, jump to the oldest valid instead
    this->bWriteAheadReadByLap = (
        (nWriteOffsetNew <= nReadOffsetOld && nWriteOffsetOld > nReadOffsetOld) ||
        (nWriteOffsetNew <= nReadOffsetOld && nWriteOffsetOld < nWriteOffsetNew)
    );

    return ERROR_SUCCESS;
}

BOOL RingBufferChannel::PrepareToPullDataIn()
{
    

    return TRUE;
}

BOOL RingBufferChannel::FinishToPullDataIn()
{

	return TRUE;
}

BOOL RingBufferChannel::PrepareToPushDataOut()
{

	return TRUE;
}

BOOL RingBufferChannel::FinishToPushDataOut()
{

	return TRUE;
}

UINT32 RingBufferChannel::GetBufferSize()
{
	return this->nBufferSize;
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
