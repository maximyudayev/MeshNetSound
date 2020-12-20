/*
    TODO:
        I.------convert code to fixed-point arithmetic for performance gain and control.
        II.-----update Resampler::Resample() to start from smallest coefficients in both wings to
                increase precision of the floating point numbers.
        III.----optimize Resampler::Resample() to skip interpolation of samples for which original data
                already exists (i.e for which Ph == 0).
        IV.-----improve quality of audio signal by including NzFs/F's or Nz past and future samples in
                the filtering operation.
        V.------make a detailed drawing explaining the complex resampling logic.
        VI.-----include original license related to the excerpts of libresample used below with clear 
                detail of what is from the library author and what are own additions/changes, changes based on
                Julius O. Smith's research paper.
*/

#include "Resampler.h"
/*
 * reference: "Digital Filters, 2nd edition"
 *            R.W. Hamming, pp. 178-179
 *
 * Beta trades the rejection of the lowpass filter against the transition
 *    width from passband to stopband.  Larger Beta means a slower
 *    transition and greater stopband rejection.  See Rabiner and Gold
 *    (Theory and Application of DSP) under Kaiser windows for more about
 *    Beta.  The following table from Rabiner and Gold gives some feel
 *    for the effect of Beta:
 *
 * All ripples in dB, width of transition band = D*N where N = window length
 *
 *               BETA    D       PB RIP   SB RIP
 *               2.120   1.50  +-0.27      -30
 *               3.384   2.23    0.0864    -40
 *               4.538   2.93    0.0274    -50
 *               5.658   3.62    0.00868   -60
 *               6.764   4.32    0.00275   -70
 *               7.865   5.0     0.000868  -80
 *               8.960   5.7     0.000275  -90
 *               10.056  6.4     0.000087  -100
 */

RESAMPLERPARAMS	Resampler::tResamplerParams;

Resampler::Resampler(){}

Resampler::~Resampler(){}

DOUBLE Resampler::Izero(DOUBLE x)
{
    DOUBLE sum, u, halfx, temp;
    UINT32 n;

    sum = u = n = 1;
    halfx = x / 2.0;
    do
    {
        temp = halfx / (DOUBLE)n;
        n++;
        temp *= temp;
        u *= temp;
        sum += u;
    } while (u >= RESAMPLER_IZERO_EPSILON * sum);

    return(sum);
}

void Resampler::InitLPFilter(BOOL bHighQuality, DOUBLE fRollOff, DOUBLE fBeta, UINT32 nTwosExp)
{
    tResamplerParams.bHighQuality = bHighQuality;
    tResamplerParams.nNz = bHighQuality ? RESAMPLER_N_QUALITY_H : RESAMPLER_N_QUALITY_L;
    tResamplerParams.fRollOff = fRollOff;
    tResamplerParams.fFrq = 0.5 * fRollOff;
    tResamplerParams.fBeta = fBeta;
    tResamplerParams.nTwosExp = nTwosExp;
    tResamplerParams.nNl = 1 << nTwosExp;
    tResamplerParams.nNh = tResamplerParams.nNl * tResamplerParams.nNz;
    tResamplerParams.pImp = (FLOAT*)malloc(tResamplerParams.nNh * sizeof(FLOAT));
    tResamplerParams.pImpD = (FLOAT*)malloc(tResamplerParams.nNh * sizeof(FLOAT));

    DOUBLE IBeta, temp, temp1, inm1;

    // Calculate ideal lowpass filter impulse response coefficients
    tResamplerParams.pImp[0] = 2.0 * tResamplerParams.fFrq;
    for (UINT32 i = 1; i < tResamplerParams.nNh; i++)
    {
        temp = M_PI * (DOUBLE)i / (DOUBLE)tResamplerParams.nNl;
        tResamplerParams.pImp[i] = (FLOAT)(sin(2.0 * temp * tResamplerParams.fFrq) / temp); // Analog sinc function, cutoff = fFrq
    }
    
    // Calculate and Apply Kaiser window to ideal lowpass filter.
    // Note: last window value is IBeta which is NOT zero.
    // You're supposed to really truncate the window here, not ramp
    // it to zero. This helps reduce the first sidelobe.
    IBeta = 1.0 / Izero(tResamplerParams.fBeta);
    inm1 = 1.0 / (DOUBLE)(tResamplerParams.nNh - 1);
    for (UINT32 i = 1; i < tResamplerParams.nNh; i++)
    {
        temp = (DOUBLE)i * inm1;
        temp1 = 1.0 - temp * temp;
        temp1 = (temp1 < 0) ? 0 : temp1;
        tResamplerParams.pImp[i] *= (FLOAT)(Izero(tResamplerParams.fBeta * sqrt(temp1)) * IBeta);
    }

    // Storing deltas in ImpD makes linear interpolation
    // of the filter coefficients faster
    for (UINT32 i = 0; i < tResamplerParams.nNh - 1; i++)
        tResamplerParams.pImpD[i] = tResamplerParams.pImp[i + 1] - tResamplerParams.pImp[i];

    // Last coefficient not interpolated
    tResamplerParams.pImpD[tResamplerParams.nNh - 1] = (FLOAT)(tResamplerParams.pImp[tResamplerParams.nNh - 1]);
}

void Resampler::FreeLPFilter()
{
    free(tResamplerParams.pImp);
    free(tResamplerParams.pImpD);
}

void Resampler::SetLPScaling(FLOAT fFactor)
{
    // Account for increased filter gain when using factors less than 1
    if (fFactor < 1)
        this->fLPScale = 1.0 * fFactor;
    else
        this->fLPScale = 1.0;
}

UINT32 Resampler::Resample(RESAMPLEFMT& tResampleFmt, ENDPOINTFMT& tEndpointFmt, UINT32 nChannelOffset, BYTE** pDataSrc, BYTE** pDataDst, UINT32 nFramesLimit, BOOL bIn)
{
    DOUBLE dh = tResamplerParams.nNl;               // Step size through the filter table
    DOUBLE dt = 1.0 / tResampleFmt.fFactor;         // Output sampling period
    DOUBLE fEndTime = *tEndpointFmt.nBufferSize;
    DOUBLE fCurrentTime = 0;
    UINT32 nFramesWritten = 0;
    BOOLEAN bInterpolate = TRUE, bRight = FALSE;    // Flag indicating interpolation
    
    // If rational conversion factor is used, such that rho = L/M,
    // where L is number of filter samples between each zero-crossing
    // and M is an arbitrary integer, no interpolation is required
    if ((tResamplerParams.nNl & (tResampleFmt.nUpsample - 1)) == 0 ||                           // If L is a multiple of nUpsample
        ((tResampleFmt.nUpsample & (tResamplerParams.nNl - 1)) == 0 &&                          // If nUpsample is a multiple of L
        tResampleFmt.nUpsample % (tResampleFmt.nUpsample >> tResamplerParams.nTwosExp) == 0))   // and nDownsample is divisible by (nUpsample/L)
        bInterpolate = FALSE;
    
    // mu here remains fixed during computation of an output sample
    if (tResampleFmt.fFactor > 1.0)
    {
        // Specify the range of input samples to use for MAC, only 0th channel is enough
        FLOAT* XStart = bIn ? (FLOAT*)*pDataSrc - tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] - 1;
        FLOAT* XEnd = bIn ? (FLOAT*)*pDataSrc + *tEndpointFmt.nBufferSize * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + *tEndpointFmt.nBufferSize;

        // Perform linear convolution on the endpoint buffer
        // store result immediately into ring buffer
        while ((bIn && fCurrentTime < fEndTime) || (!bIn && nFramesWritten < nFramesLimit)) // mutually exclusive condition
        {
            FLOAT* Hp, * Hdp, * HEnd, v = 0, dummy;

            // Get the input sample using the integer part of the fCurrentTime float as index
            FLOAT* X = bIn ? (FLOAT*)*pDataSrc + ((UINT32)fCurrentTime) * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + ((UINT32)fCurrentTime);
            // Create relative index to sweep across samples for the case of bIn = FALSE -> SRC out of ring buffer
            INT32 nX = 0;

            // Make note of the next cell's offset to reduce computational cost
            UINT32 position = (tResampleFmt.nWriteOffset + nFramesWritten) % *tResampleFmt.nBufferSize;

            // Clear current ring buffer pointed cell for each channel
            for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
            {
                if (bIn)    // SRC of captured data into ring buffer
                    *(tResampleFmt.pBuffer[nChannelOffset + i] + position) = 0;
                else        // SRC of processed data out of ring buffer
                    *((FLOAT*)(*pDataDst + nFramesWritten * (UINT64)tEndpointFmt.nChannels + i)) = 0;
            }

            // Compute distance between current time and nearby input samples in relative units of input
            DOUBLE fLeftPhase = fCurrentTime - floor(fCurrentTime);
            DOUBLE fRightPhase = 1.0 - fLeftPhase;
            
            DOUBLE Ph = fLeftPhase;
            bRight = FALSE;
            // Do this block twice, once for left once for right wing
            for (UINT8 k = 0; k < 2; k++)
            {
                // Get the lowest-indexed filter coefficient closest to the input sample
                Ph *= tResamplerParams.nNl;
                DOUBLE mu = Ph - floor(Ph);                                     // Fractional part of Phase

                Hp = &tResamplerParams.pImp[(UINT32)Ph];
                Hdp = &tResamplerParams.pImpD[(UINT32)Ph];
                HEnd = &tResamplerParams.pImp[tResamplerParams.nNh];            // Length of table + 1 to compare using < inside loops

                // MAC as long as there are more filter coefficients and
                // as long as input samples used are not in the padded regions
                while (Hp < HEnd && ((bIn && X > XStart && X < XEnd) || (!bIn && (X + nX) > XStart && (X + nX) < XEnd))) // mutually exclusive condition
                {
                    if (bInterpolate)
                    {
                        dummy = *Hp + *Hdp * mu;            // Linearly interpolate filter coefficient
                        Hdp += (UINT32)dh;
                    }
                    else
                    {
                        dummy = *Hp;                        // Use just the filter coefficient
                    }
                    Hp += (UINT32)dh;

                    if (bIn)
                    {
                        // Encapsulating the smallest amount of MAC code into a loop to perform
                        // filtering on all device's channels ensures highest performance
                        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                        {
                            // Multiply (interpolated) filter coefficient with each of interleaved audio channel samples
                            FLOAT temp = dummy * *(X + i);

                            // Write results to each corresponding channel in the ring buffer
                            *(tResampleFmt.pBuffer[nChannelOffset + i] + position) += temp;
                        }
                        X += (bRight) ? tEndpointFmt.nChannels : -tEndpointFmt.nChannels;   // Decrements endpoint buffer pointer in left wing, 
                                                                                            // increments in right wing
                    }
                    else
                    {
                        // Encapsulating the smallest amount of MAC code into a loop to perform
                        // filtering on all device's channels ensures highest performance
                        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                        {
                            // Multiply (interpolated) filter coefficient with each of interleaved audio channel samples
                            FLOAT temp = dummy * *((FLOAT*)pDataSrc[nChannelOffset + i] + ((UINT32)fCurrentTime) + nX);

                            // Write results interleaved in the device's render linear buffer
                            *((FLOAT*)(*pDataDst + ((UINT32)fCurrentTime) + i)) += temp;
                        }
                        nX += (bRight) ? 1 : -1;    // Decrements endpoint buffer pointer in left wing, 
                                                    // increments in right wing
                    }
                }
                // Change wing and repeat
                bRight = TRUE; 
                Ph = fRightPhase;
                // Update to n+1 input sample prior to filtering the right wing
                X = bIn ? (FLOAT*)*pDataSrc + (((UINT64)fCurrentTime) + 1) * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + ((UINT32)fCurrentTime) + 1;
                nX = 1;
            }
            // Increment the number of resampled frames and the time register
            nFramesWritten++;
            fCurrentTime += dt;
        }
    }
    else // mu here changes throughout computation of an output sample
    {
        dh *= tResampleFmt.fFactor;
        
        // Specify the range of input samples to use for MAC, only 0th channel is enough
        FLOAT* XStart = bIn ? (FLOAT*)*pDataSrc - tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] - 1;
        FLOAT* XEnd = bIn ? (FLOAT*)*pDataSrc + *tEndpointFmt.nBufferSize * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + *tEndpointFmt.nBufferSize;

        // Perform linear convolution on the endpoint buffer
        // store result immediately into ring buffer
        while ((bIn && fCurrentTime < fEndTime) || (!bIn && nFramesWritten < nFramesLimit)) // mutually exclusive condition
        {
            FLOAT* Hp, * Hdp, * HEnd, v = 0, dummy;

            // Get the input sample using the integer part of the fCurrentTime float as index
            FLOAT* X = bIn ? (FLOAT*)*pDataSrc + ((UINT32)fCurrentTime) * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + ((UINT32)fCurrentTime);
            // Create relative index to sweep across samples for the case of bIn = FALSE -> SRC out of ring buffer
            INT32 nX = 0;

            // Make note of the next cell's offset to reduce computational cost
            UINT32 position = (tResampleFmt.nWriteOffset + nFramesWritten) % *tResampleFmt.nBufferSize;

            // Clear current ring buffer pointed cell for each channel
            for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
            {
                if (bIn)    // SRC of captured data into ring buffer
                    *(tResampleFmt.pBuffer[nChannelOffset + i] + position) = 0;
                else        // SRC of processed data out of ring buffer
                    *((FLOAT*)(*pDataDst + nFramesWritten * (UINT64)tEndpointFmt.nChannels + i)) = 0;
            }

            // Compute distance between current time and nearby input samples in relative units of input
            DOUBLE fLeftPhase = fCurrentTime - floor(fCurrentTime);
            DOUBLE fRightPhase = 1.0 - fLeftPhase;

            DOUBLE Ph = fLeftPhase;
            bRight = FALSE;
            // Do this block twice, once for left once for right wing
            for (UINT8 k = 0; k < 2; k++)
            {
                // Get the lowest-indexed filter coefficient closest to the input sample
                DOUBLE Ho = Ph * dh;
                HEnd = &tResamplerParams.pImp[tResamplerParams.nNh];            // Length of table + 1 to compare using < inside loops

                // MAC as long as there are more filter coefficients and
                // as long as input samples used are not in the padded regions
                while ((Hp = &tResamplerParams.pImp[(UINT32)Ho]) < HEnd && 
                        ((bIn && X > XStart && X < XEnd) || (!bIn && (X + nX) > XStart && (X + nX) < XEnd))) // mutually exclusive condition
                {
                    if (bInterpolate)
                    {
                        Hdp = &tResamplerParams.pImpD[(UINT32)Ho];
                        DOUBLE mu = Ho - floor(Ho);
                        dummy = *Hp + *Hdp * mu;
                    }
                    else
                    {
                        dummy = *Hp;
                    }

                    if (bIn)
                    {
                        // Encapsulating the smallest amount of MAC code into a loop to perform
                        // filtering on all device's channels ensures highest performance
                        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                        {
                            // Multiply (interpolated) filter coefficient with each of interleaved audio channel samples
                            FLOAT temp = dummy * *(X + i);

                            // Write results to each corresponding channel in the ring buffer
                            *(tResampleFmt.pBuffer[nChannelOffset + i] + position) += temp;

                            // Scale the output of the computd sample by 1/factor to account for unity gain
                            *(tResampleFmt.pBuffer[nChannelOffset + i] + position) *= this->fLPScale;
                        }
                        X += (bRight) ? tEndpointFmt.nChannels : -tEndpointFmt.nChannels;   // Decrements endpoint buffer pointer in left wing, 
                                                                                            // increments in right wing
                    }
                    else
                    {
                        // Encapsulating the smallest amount of MAC code into a loop to perform
                        // filtering on all device's channels ensures highest performance
                        for (UINT32 i = 0; i < tEndpointFmt.nChannels; i++)
                        {
                            // Multiply (interpolated) filter coefficient with each of interleaved audio channel samples
                            FLOAT temp = dummy * *((FLOAT*)pDataSrc[nChannelOffset + i] + ((UINT32)fCurrentTime) + nX);
                            
                            // Write results interleaved in the device's render linear buffer
                            *((FLOAT*)(*pDataDst + ((UINT32)fCurrentTime) + i)) += temp;

                            // Scale the output of the computd sample by 1/factor to account for unity gain
                            *((FLOAT*)(*pDataDst + ((UINT32)fCurrentTime) + i)) *= this->fLPScale;
                        }
                        nX += (bRight) ? 1 : -1;    // Decrements endpoint buffer pointer in left wing, 
                                                    // increments in right wing
                    }
                    Ho += dh;
                }
                // Change wing and repeat
                bRight = TRUE;
                Ph = fRightPhase;
                // Update to n+1 input sample prior to filtering the right wing
                X = bIn ? (FLOAT*)*pDataSrc + (((UINT64)fCurrentTime) + 1) * (UINT64)tEndpointFmt.nChannels : (FLOAT*)pDataSrc[nChannelOffset] + ((UINT32)fCurrentTime) + 1;
                nX = 1;
            }
            // Increment the number of resampled frames and the time register
            nFramesWritten++;
            fCurrentTime += dt;
        }
    }
    return nFramesWritten;
}
