/*
    TODO: 
        I.------support dynamic updates to the aggregator like:
                1. Addition/removal of capture devices.
                2. Sample-rate conversion.
                3. Output channel mask changes.
                4. Circular buffer size.
        II.-----multithread the aggregator so that each thread is responsible for each set of functionality:
                a. Control lane for communication with JUCE/DSP.
                b. Capture and pre-processing of data.
        III.----convert class into a thread-safe Singleton.
        IV.-----facilitate modularity for cross-platform portability.
        V.------provide better device friendly names (i.e Max's Airpods, etc.)
        VI.-----convert class into a thread-safe Singleton.
        VII.----turn into a separate thread to provide additional functionality without waiting 
                for CPU or delaying processing of incoming data.
        IIX.----incorporate more user input verification.
        IX.-----check if the input is actually a number (atoi is not safe if input is not an integer)
        X.------perform setup of WiFi-direct and virtual audio devices before listing WASAPI devices;
                display all appropriate capture/render devices together.
        XI.-----ensure graceful Exit strategy in all methods, currently not all scenarios are considered
                and not all memory is freed.
        XII.----provide safety when an audio device is removed, hot plugged while program is running.
*/

#include "Aggregator.h"

//---------- Windows macro definitions ----------------------------------------------//
static const CLSID  CLSID_MMDeviceEnumerator    = __uuidof(MMDeviceEnumerator);
static const IID    IID_IMMDeviceEnumerator     = __uuidof(IMMDeviceEnumerator);
static const IID    IID_IAudioClient            = __uuidof(IAudioClient);
static const IID    IID_IAudioCaptureClient     = __uuidof(IAudioCaptureClient);
static const IID    IID_IAudioRenderClient      = __uuidof(IAudioRenderClient);

/// <summary>
/// <para>Aggregator constructor.</para>
/// <para>High level, clean interface to perform all the cumbersome setup
/// and negotiation with WASAPI, user parameters, etc.</para>
/// <para>Note: not thread-safe, must be instantiated only once.</para>
/// </summary>
Aggregator::Aggregator(){}

/// <summary>
/// <para>Aggregator destructor.</para>
/// <para>Frees all alloc'ed memory and cleans up underlying classes.</para>
/// </summary>
Aggregator::~Aggregator()
{
    for (UINT32 j = 0; j < 2; j++)
    {
        //-------- Release memory alloc'ed by Windows for capture device interfaces
        // Release each capture device interface
        if (pDeviceAll[j] != NULL)
            for (UINT32 i = 0; i < nAllDevices[j]; i++)
                SAFE_RELEASE(pDeviceAll[j][i])

        for (UINT32 i = 0; i < nDevices[j]; i++)
        {
            CoTaskMemFree(pwfx[j][i]);
            SAFE_RELEASE(pAudioClient[j][i])
            if (j == 1) SAFE_RELEASE(pCaptureClient[i])
            else if (j == 0) SAFE_RELEASE(pRenderClient[i])
        }

        CoTaskMemFree(pCollection[j]);

        //-------- Release memory alloc'ed by Aggregator
        // Destruct AudioBuffer objects and gracefully wrap up their work
        for (UINT32 i = 0; i < nDevices[j]; i++)
            delete pAudioBuffer[j][i];

        //-------- Free AudioBuffer object group - corresponds to 1 input and 1 output ring buffer
        AudioBuffer::RemoveBufferGroup(pAudioBufferGroupId[j]);

        // Release memory alloc'ed for ring buffer
        if (pCircularBuffer[j] != NULL)
        {
            for (UINT32 i = 0; i < nAggregatedChannels[j]; i++)
                if (pCircularBuffer[j][i] != NULL)
                    free(pCircularBuffer[j][i]);

            free(pCircularBuffer[j]);
        }
        // Set number of aggregated channels back to 0
        nAggregatedChannels[j] = 0;

        // Free dynamic arrays holding reference to all these variables as last step
        if (pDeviceAll[j] != NULL)              free(pDeviceAll[j]);
        if (pDevice[j] != NULL)                 free(pDevice[j]);
        if (pAudioClient[j] != NULL)            free(pAudioClient[j]);
        if (pwfx[j] != NULL)                    free(pwfx[j]);
        if (pAudioBuffer[j] != NULL)            free(pAudioBuffer[j]);
        if (pData[j] != NULL)                   free(pData[j]);
        if (nGCD[j] != NULL)                    free(nGCD);
        if (nGCDDiv[j] != NULL)                 free(nGCDDiv);
        if (nGCDTFreqDiv[j] != NULL)            free(nGCDTFreqDiv);
        if (nUpsample[j] != NULL)               free(nUpsample);
        if (nDownsample[j] != NULL)             free(nDownsample);
        if (nEndpointBufferSize[j] != NULL)     free(nEndpointBufferSize);
        if (nEndpointPackets[j] != NULL)        free(nEndpointPackets);
        if (flags[j] != NULL)                   free(flags);
    }

    //-------- Free LP Filter memory of the Resampler class
    Resampler::FreeLPFilter();

    if (pCaptureClient != NULL)                     free(pCaptureClient);
    if (pRenderClient != NULL)                      free(pRenderClient);
    if (pAudioBufferGroupId != NULL)                free(pAudioBufferGroupId);

    SAFE_RELEASE(pEnumerator)
}

/// <summary>
/// <para>Alloc's memory and instantiates WASAPI interface
/// to stream capture device data.</para>
/// <para>Note: if at some point crashes, likely it is because of
/// double deletion of pAudioBuffer or pResampler. Check if EXIT_ON_ERROR
/// is triggered before either is instantiated using "new" and if so, provide
/// extra safety checks.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::Initialize()
{
    HRESULT hr = S_OK;
    UINT32 attempt = 0;

    //-------- Initialize Aggregator basics
    hr = CoInitialize(0);
        EXIT_ON_ERROR(hr)
    
    //-------- Instantiate IMMDevice Enumerator
    hr = CoCreateInstance(
            CLSID_MMDeviceEnumerator, NULL,
            CLSCTX_ALL, IID_IMMDeviceEnumerator,
            (void**)&pEnumerator);
        EXIT_ON_ERROR(hr)
    
    //---------------- Listing and Recording of User Sink/Source Device Choices ----------------//

    //-------- Try to list all available capture devices AGGREGATOR_OP_ATTEMPTS times
    do
    {
        hr = ListAvailableDevices(AGGREGATOR_CAPTURE);
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of capture devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetUserChoiceDevices(AGGREGATOR_CAPTURE);
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to list all available render devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = ListAvailableDevices(AGGREGATOR_RENDER);
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of render devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetUserChoiceDevices(AGGREGATOR_RENDER);
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //---------------- Initialization ----------------//

    // allocate space for indices of 1 input ring buffer and 1 output ring buffer 
    pAudioBufferGroupId = (UINT32*)malloc(2 * sizeof(UINT32));  

    //-------- Initialize LP Filter of the Resampler class
    Resampler::InitLPFilter(FALSE, RESAMPLER_ROLLOFF_FREQ, RESAMPLER_BETA, RESAMPLER_L_TWOS_EXP);

    hr = InitializeCapture();
        EXIT_ON_ERROR(hr);

    hr = InitializeRender();
        EXIT_ON_ERROR(hr)

Exit:
    SAFE_RELEASE(pEnumerator)

    if (pAudioBufferGroupId != NULL)        free(pAudioBufferGroupId);

    return hr;
}

/// <summary>
/// <para>Wrapper for all initialization steps on the capturing side.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::InitializeCapture()
{
    HRESULT hr = ERROR_SUCCESS;

    //-------- Use information obtained from user inputs to dynamically create the system

    pAudioClient[AGGREGATOR_CAPTURE]            = (IAudioClient**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(IAudioClient*));
    pwfx[AGGREGATOR_CAPTURE]                    = (WAVEFORMATEX**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(WAVEFORMATEX*));
    pCaptureClient                              = (IAudioCaptureClient**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(IAudioCaptureClient*));
    pAudioBuffer[AGGREGATOR_CAPTURE]            = (AudioBuffer**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(AudioBuffer*));
    pData[AGGREGATOR_CAPTURE]                   = (BYTE**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(BYTE*));
    nGCD[AGGREGATOR_CAPTURE]                    = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    nGCDDiv[AGGREGATOR_CAPTURE]                 = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    nGCDTFreqDiv[AGGREGATOR_CAPTURE]            = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    nUpsample[AGGREGATOR_CAPTURE]               = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    nDownsample[AGGREGATOR_CAPTURE]             = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    flags[AGGREGATOR_CAPTURE]                   = (DWORD*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(DWORD));
    nEndpointBufferSize[AGGREGATOR_CAPTURE]     = (UINT32*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(UINT32));
    nEndpointPackets[AGGREGATOR_CAPTURE]        = (UINT32*)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(UINT32));
    
    //-------- Check if allocation of any of the crucial variables failed, clean up and return with ENOMEM otherwise
    if (pAudioClient[AGGREGATOR_CAPTURE] == NULL ||
        pCaptureClient == NULL ||
        pAudioBuffer[AGGREGATOR_CAPTURE] == NULL ||
        pData[AGGREGATOR_CAPTURE] == NULL ||
        pwfx[AGGREGATOR_CAPTURE] == NULL ||
        nGCD[AGGREGATOR_CAPTURE] == NULL ||
        nGCDDiv[AGGREGATOR_CAPTURE] == NULL ||
        nGCDTFreqDiv[AGGREGATOR_CAPTURE] == NULL ||
        nUpsample[AGGREGATOR_CAPTURE] == NULL ||
        nDownsample[AGGREGATOR_CAPTURE] == NULL ||
        nEndpointBufferSize[AGGREGATOR_CAPTURE] == NULL ||
        nEndpointPackets[AGGREGATOR_CAPTURE] == NULL ||
        flags[AGGREGATOR_CAPTURE] == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Activate capture devices as COM objects
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pDevice[AGGREGATOR_CAPTURE][i]->Activate(IID_IAudioClient, CLSCTX_ALL,
                                    NULL, (void**)&pAudioClient[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get format settings for both devices and initialize their client objects
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->GetMixFormat(&pwfx[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        nGCD[AGGREGATOR_CAPTURE][i] = gcd(pwfx[AGGREGATOR_CAPTURE][i]->nSamplesPerSec, AGGREGATOR_SAMPLE_FREQ);

        nGCDDiv[AGGREGATOR_CAPTURE][i] = pwfx[AGGREGATOR_CAPTURE][i]->nSamplesPerSec / nGCD[AGGREGATOR_CAPTURE][i];
        nGCDTFreqDiv[AGGREGATOR_CAPTURE][i] = AGGREGATOR_SAMPLE_FREQ / nGCD[AGGREGATOR_CAPTURE][i];

        if (pwfx[AGGREGATOR_CAPTURE][i]->nSamplesPerSec < AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[AGGREGATOR_CAPTURE][i] = max(nGCDDiv[AGGREGATOR_CAPTURE][i], nGCDTFreqDiv[AGGREGATOR_CAPTURE][i]);
            nDownsample[AGGREGATOR_CAPTURE][i] = min(nGCDDiv[AGGREGATOR_CAPTURE][i], nGCDTFreqDiv[AGGREGATOR_CAPTURE][i]);
        }
        else if (pwfx[AGGREGATOR_CAPTURE][i]->nSamplesPerSec > AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[AGGREGATOR_CAPTURE][i] = min(nGCDDiv[AGGREGATOR_CAPTURE][i], nGCDTFreqDiv[AGGREGATOR_CAPTURE][i]);
            nDownsample[AGGREGATOR_CAPTURE][i] = max(nGCDDiv[AGGREGATOR_CAPTURE][i], nGCDTFreqDiv[AGGREGATOR_CAPTURE][i]);
        }
        else
        {
            nUpsample[AGGREGATOR_CAPTURE][i] = 1;
            nDownsample[AGGREGATOR_CAPTURE][i] = 1;
        }
    }

    //-------- Initialize streams to operate in callback mode
    // Allow WASAPI to choose endpoint buffer size, glitches otherwise
    // for both, event-driven and polling methods, outputs 448 frames for mic
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        0, 0,
                                        0, pwfx[AGGREGATOR_CAPTURE][i], NULL);
            EXIT_ON_ERROR(hr)
    }

    std::cout << "<-------- Capture Device Details -------->" << std::endl << std::endl;

    //-------- Get the size of the actual allocated buffers and obtain capturing interfaces
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->GetBufferSize(&nEndpointBufferSize[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
        
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->GetService(IID_IAudioCaptureClient,
                                        (void**)&pCaptureClient[i]);
            EXIT_ON_ERROR(hr)
        printf("[%d]:\nThe %d-th buffer size is: %d\n", i, i, nEndpointBufferSize[AGGREGATOR_CAPTURE][i]);
    }
    
    //-------- Create a new AudioBuffer object group - corresponds to 1 input ring buffer
    hr = AudioBuffer::CreateBufferGroup(&pAudioBufferGroupId[0]);
        EXIT_ON_ERROR(hr)
    
    //-------- Instantiate AudioBuffer for each user-chosen capture device
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
        pAudioBuffer[AGGREGATOR_CAPTURE][i] = new AudioBuffer("Capture Device " + std::to_string(i) + " ", pAudioBufferGroupId[0]);

    //-------- Notify the audio sink which format to use
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->SetFormat(pwfx[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++) 
        nAggregatedChannels[AGGREGATOR_CAPTURE] += pwfx[AGGREGATOR_CAPTURE][i]->nChannels;
    
    pCircularBuffer[AGGREGATOR_CAPTURE] = (FLOAT**)malloc(nAggregatedChannels[AGGREGATOR_CAPTURE] * sizeof(FLOAT*));
    if (pCircularBuffer[AGGREGATOR_CAPTURE] == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }
#ifdef DEBUG
    memset(pCircularBuffer[AGGREGATOR_CAPTURE], 1, nAggregatedChannels[AGGREGATOR_CAPTURE] * sizeof(FLOAT*));
#endif

    for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_CAPTURE]; i++)
    {
        pCircularBuffer[AGGREGATOR_CAPTURE][i] = (FLOAT*)malloc(nCircularBufferSize[AGGREGATOR_CAPTURE] * sizeof(FLOAT));
        if (pCircularBuffer[AGGREGATOR_CAPTURE][i] == NULL)
        {
            hr = ENOMEM;
            goto Exit;
        }
#ifdef DEBUG
        memset(pCircularBuffer[AGGREGATOR_CAPTURE][i], 1, nCircularBufferSize[AGGREGATOR_CAPTURE] * sizeof(FLOAT));
#endif
    }

    //-------- Initialize AudioBuffer objects' buffers using the obtained information
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->InitBuffer(&nEndpointBufferSize[AGGREGATOR_CAPTURE][i],
                                                    pCircularBuffer[AGGREGATOR_CAPTURE],
                                                    &nCircularBufferSize[AGGREGATOR_CAPTURE],
                                                    nUpsample[AGGREGATOR_CAPTURE][i], 
                                                    nDownsample[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

    //-------- If initialization succeeded, return with S_OK
    return hr;

    //-------- If initialization failed at any of above steps, clean up memory prior to next attempt
Exit:
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        CoTaskMemFree(pwfx[AGGREGATOR_CAPTURE][i]);
        SAFE_RELEASE(pAudioClient[AGGREGATOR_CAPTURE][i])
        SAFE_RELEASE(pCaptureClient[i])

        delete pAudioBuffer[AGGREGATOR_CAPTURE][i];
    }

    // Free circular buffer
    if (pCircularBuffer[AGGREGATOR_CAPTURE] != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_CAPTURE]; i++)
            if (pCircularBuffer[AGGREGATOR_CAPTURE][i] != NULL)
                free(pCircularBuffer[AGGREGATOR_CAPTURE][i]);

        free(pCircularBuffer[AGGREGATOR_CAPTURE]);
    }
    // Set number of aggregated channels back to 0
    nAggregatedChannels[AGGREGATOR_CAPTURE] = 0;

    // Free dynamic arrays holding reference to 
    if (pAudioClient[AGGREGATOR_CAPTURE] != NULL)           free(pAudioClient[AGGREGATOR_CAPTURE]);
    if (pCaptureClient != NULL)                             free(pCaptureClient);
    if (pAudioBuffer[AGGREGATOR_CAPTURE] != NULL)           free(pAudioBuffer[AGGREGATOR_CAPTURE]);
    if (pData[AGGREGATOR_CAPTURE] != NULL)                  free(pData[AGGREGATOR_CAPTURE]);
    if (pwfx[AGGREGATOR_CAPTURE] != NULL)                   free(pwfx[AGGREGATOR_CAPTURE]);
    if (nGCD[AGGREGATOR_CAPTURE] != NULL)                   free(nGCD[AGGREGATOR_CAPTURE]);
    if (nGCDDiv[AGGREGATOR_CAPTURE] != NULL)                free(nGCDDiv[AGGREGATOR_CAPTURE]);
    if (nGCDTFreqDiv[AGGREGATOR_CAPTURE] != NULL)           free(nGCDTFreqDiv[AGGREGATOR_CAPTURE]);
    if (nUpsample[AGGREGATOR_CAPTURE] != NULL)              free(nUpsample[AGGREGATOR_CAPTURE]);
    if (nDownsample[AGGREGATOR_CAPTURE] != NULL)            free(nDownsample[AGGREGATOR_CAPTURE]);
    if (nEndpointBufferSize[AGGREGATOR_CAPTURE] != NULL)    free(nEndpointBufferSize[AGGREGATOR_CAPTURE]);
    if (nEndpointPackets[AGGREGATOR_CAPTURE] != NULL)       free(nEndpointPackets[AGGREGATOR_CAPTURE]);
    if (flags[AGGREGATOR_CAPTURE] != NULL)                  free(flags[AGGREGATOR_CAPTURE]);

    return hr;
}

/// <summary>
/// <para>Wrapper for all initialization steps on the rendering side.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::InitializeRender()
{
    HRESULT hr = ERROR_SUCCESS;

    //-------- Use information obtained from user inputs to dynamically create the system

    pAudioClient[AGGREGATOR_RENDER]         = (IAudioClient**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(IAudioClient*));
    pwfx[AGGREGATOR_RENDER]                 = (WAVEFORMATEX**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(WAVEFORMATEX*));
    pRenderClient                           = (IAudioRenderClient**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(IAudioRenderClient*));
    pAudioBuffer[AGGREGATOR_RENDER]         = (AudioBuffer**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(AudioBuffer*));
    pData[AGGREGATOR_RENDER]                = (BYTE**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(BYTE*));
    nGCD[AGGREGATOR_RENDER]                 = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    nGCDDiv[AGGREGATOR_RENDER]              = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    nGCDTFreqDiv[AGGREGATOR_RENDER]         = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    nUpsample[AGGREGATOR_RENDER]            = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    nDownsample[AGGREGATOR_RENDER]          = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    flags[AGGREGATOR_RENDER]                = (DWORD*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(DWORD));
    nEndpointBufferSize[AGGREGATOR_RENDER]  = (UINT32*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(UINT32));
    nEndpointPackets[AGGREGATOR_RENDER]     = (UINT32*)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(UINT32));

    //-------- Check if allocation of any of the crucial variables failed, clean up and return with ENOMEM otherwise
    if (pAudioClient[AGGREGATOR_RENDER] == NULL ||
        pRenderClient == NULL ||
        pAudioBuffer[AGGREGATOR_RENDER] == NULL ||
        pData[AGGREGATOR_RENDER] == NULL ||
        pwfx[AGGREGATOR_RENDER] == NULL ||
        nGCD[AGGREGATOR_RENDER] == NULL ||
        nGCDDiv[AGGREGATOR_RENDER] == NULL ||
        nGCDTFreqDiv[AGGREGATOR_RENDER] == NULL ||
        nUpsample[AGGREGATOR_RENDER] == NULL ||
        nDownsample[AGGREGATOR_RENDER] == NULL ||
        nEndpointBufferSize[AGGREGATOR_RENDER] == NULL ||
        nEndpointPackets[AGGREGATOR_RENDER] == NULL ||
        flags[AGGREGATOR_RENDER] == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }
    //-------- Activate render devices as COM objects
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pDevice[AGGREGATOR_RENDER][i]->Activate(IID_IAudioClient, CLSCTX_ALL,
                                    NULL, (void**)&pAudioClient[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get format settings for both devices and initialize their client objects
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioClient[AGGREGATOR_RENDER][i]->GetMixFormat(&pwfx[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Initialize streams to operate in callback mode
    // Allow WASAPI to choose endpoint buffer size, glitches otherwise
    // for both, event-driven and polling methods, outputs 448 frames for mic
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioClient[AGGREGATOR_RENDER][i]->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                            0, 0,
                                                            0, pwfx[AGGREGATOR_RENDER][i], NULL);
            EXIT_ON_ERROR(hr)
    }

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        nGCD[AGGREGATOR_RENDER][i] = gcd(pwfx[AGGREGATOR_RENDER][i]->nSamplesPerSec, AGGREGATOR_SAMPLE_FREQ);

        nGCDDiv[AGGREGATOR_RENDER][i] = pwfx[AGGREGATOR_RENDER][i]->nSamplesPerSec / nGCD[AGGREGATOR_RENDER][i];
        nGCDTFreqDiv[AGGREGATOR_RENDER][i] = AGGREGATOR_SAMPLE_FREQ / nGCD[AGGREGATOR_RENDER][i];

        if (pwfx[AGGREGATOR_RENDER][i]->nSamplesPerSec < AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[AGGREGATOR_RENDER][i] = max(nGCDDiv[AGGREGATOR_RENDER][i], nGCDTFreqDiv[AGGREGATOR_RENDER][i]);
            nDownsample[AGGREGATOR_RENDER][i] = min(nGCDDiv[AGGREGATOR_RENDER][i], nGCDTFreqDiv[AGGREGATOR_RENDER][i]);
        }
        else if (pwfx[AGGREGATOR_RENDER][i]->nSamplesPerSec > AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[AGGREGATOR_RENDER][i] = min(nGCDDiv[AGGREGATOR_RENDER][i], nGCDTFreqDiv[AGGREGATOR_RENDER][i]);
            nDownsample[AGGREGATOR_RENDER][i] = max(nGCDDiv[AGGREGATOR_RENDER][i], nGCDTFreqDiv[AGGREGATOR_RENDER][i]);
        }
        else
        {
            nUpsample[AGGREGATOR_RENDER][i] = 1;
            nDownsample[AGGREGATOR_RENDER][i] = 1;
        }
    }

    std::cout << "<-------- Render Device Details -------->" << std::endl << std::endl;

    //-------- Get the size of the actual allocated buffers and obtain rendering interfaces
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioClient[AGGREGATOR_RENDER][i]->GetBufferSize(&nEndpointBufferSize[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[AGGREGATOR_RENDER][i]->GetService(IID_IAudioRenderClient,
                                            (void**)&pRenderClient[i]);
            EXIT_ON_ERROR(hr)
        printf("[%d]:\nThe %d-th buffer size is: %d\n", i, i, nEndpointBufferSize[AGGREGATOR_RENDER][i]);
    }

    //-------- Create a new AudioBuffer object group - corresponds to 1 output ring buffer
    hr = AudioBuffer::CreateBufferGroup(&pAudioBufferGroupId[1]);
        EXIT_ON_ERROR(hr)

    //-------- Instantiate AudioBuffer for each user-chosen render device
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
        pAudioBuffer[AGGREGATOR_RENDER][i] = new AudioBuffer("Render Device " + std::to_string(i) + " ", pAudioBufferGroupId[1]);

    //-------- Notify the audio source which format to use
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->SetFormat(pwfx[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
        nAggregatedChannels[AGGREGATOR_RENDER] += pwfx[AGGREGATOR_RENDER][i]->nChannels;

    pCircularBuffer[AGGREGATOR_RENDER] = (FLOAT**)malloc(nAggregatedChannels[AGGREGATOR_RENDER] * sizeof(FLOAT*));
    if (pCircularBuffer[AGGREGATOR_RENDER] == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }
#ifdef DEBUG
    memset(pCircularBuffer[AGGREGATOR_RENDER], 1, nAggregatedChannels[AGGREGATOR_RENDER] * sizeof(FLOAT*));
#endif

    for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_RENDER]; i++)
    {
        pCircularBuffer[AGGREGATOR_RENDER][i] = (FLOAT*)malloc(nCircularBufferSize[AGGREGATOR_RENDER] * sizeof(FLOAT));
        if (pCircularBuffer[AGGREGATOR_RENDER][i] == NULL)
        {
            hr = ENOMEM;
            goto Exit;
        }
#ifdef DEBUG
        memset(pCircularBuffer[AGGREGATOR_RENDER][i], 1, nCircularBufferSize[AGGREGATOR_RENDER] * sizeof(FLOAT));
#endif
    }

    //-------- Initialize AudioBuffer objects' buffers using the obtained information
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->InitBuffer(&nEndpointBufferSize[AGGREGATOR_RENDER][i],
                                                            pCircularBuffer[AGGREGATOR_RENDER],
                                                            &nCircularBufferSize[AGGREGATOR_RENDER],
                                                            nUpsample[AGGREGATOR_RENDER][i],
                                                            nDownsample[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

Exit:
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        CoTaskMemFree(pwfx[AGGREGATOR_RENDER][i]);
        SAFE_RELEASE(pAudioClient[AGGREGATOR_RENDER][i])
        SAFE_RELEASE(pRenderClient[i])

        delete pAudioBuffer[AGGREGATOR_RENDER][i];
    }

    // Free circular buffer
    if (pCircularBuffer[AGGREGATOR_RENDER] != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_RENDER]; i++)
            if (pCircularBuffer[AGGREGATOR_RENDER][i] != NULL)
                free(pCircularBuffer[AGGREGATOR_RENDER][i]);

        free(pCircularBuffer[AGGREGATOR_RENDER]);
    }
    // Set number of aggregated channels back to 0
    nAggregatedChannels[AGGREGATOR_RENDER] = 0;

    // Free dynamic arrays holding reference to 
    if (pAudioClient[AGGREGATOR_RENDER] != NULL)            free(pAudioClient[AGGREGATOR_RENDER]);
    if (pRenderClient != NULL)                              free(pRenderClient);
    if (pAudioBuffer[AGGREGATOR_RENDER] != NULL)            free(pAudioBuffer[AGGREGATOR_RENDER]);
    if (pData[AGGREGATOR_RENDER] != NULL)                   free(pData[AGGREGATOR_RENDER]);
    if (pwfx[AGGREGATOR_RENDER] != NULL)                    free(pwfx[AGGREGATOR_RENDER]);
    if (nGCD[AGGREGATOR_RENDER] != NULL)                    free(nGCD[AGGREGATOR_RENDER]);
    if (nGCDDiv[AGGREGATOR_RENDER] != NULL)                 free(nGCDDiv[AGGREGATOR_RENDER]);
    if (nGCDTFreqDiv[AGGREGATOR_RENDER] != NULL)            free(nGCDTFreqDiv[AGGREGATOR_RENDER]);
    if (nUpsample[AGGREGATOR_RENDER] != NULL)               free(nUpsample[AGGREGATOR_RENDER]);
    if (nDownsample[AGGREGATOR_RENDER] != NULL)             free(nDownsample[AGGREGATOR_RENDER]);
    if (nEndpointBufferSize[AGGREGATOR_RENDER] != NULL)     free(nEndpointBufferSize[AGGREGATOR_RENDER]);
    if (nEndpointPackets[AGGREGATOR_RENDER] != NULL)        free(nEndpointPackets[AGGREGATOR_RENDER]);
    if (flags[AGGREGATOR_RENDER] != NULL)                   free(flags[AGGREGATOR_RENDER]);

    return hr;
}

/// <summary>
/// <para>Starts capturing audio from user-selected devices on a poll basis.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::StartCapture()
{
    HRESULT hr = S_OK;

    //-------- Reset and start capturing on all selected devices
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Start();
            EXIT_ON_ERROR(hr)
    }
    
    std::cout << MSG "Starting audio capture." << std::endl;

    //-------- Capture endpoint buffer data in a poll-based fashion
    while (!bDone[AGGREGATOR_CAPTURE])
    {
        // Captures data from all devices
        for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
        {
            hr = pCaptureClient[i]->GetNextPacketSize(&nEndpointPackets[AGGREGATOR_CAPTURE][i]);
                EXIT_ON_ERROR(hr)

            if (nEndpointPackets[AGGREGATOR_CAPTURE][i] > 0)
            {
                hr = pCaptureClient[i]->GetBuffer(&pData[AGGREGATOR_CAPTURE][i],
                                                &nEndpointBufferSize[AGGREGATOR_CAPTURE][i],
                                                &flags[AGGREGATOR_CAPTURE][i], NULL, NULL);
                    EXIT_ON_ERROR(hr)

                if (flags[AGGREGATOR_CAPTURE][i] & AUDCLNT_BUFFERFLAGS_SILENT)
                    pData[AGGREGATOR_CAPTURE][i] = NULL;  // Tell PullData to write silence.

                hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->PullData(pData[AGGREGATOR_CAPTURE][i], &bDone[AGGREGATOR_CAPTURE]);
                    EXIT_ON_ERROR(hr)

                hr = pCaptureClient[i]->ReleaseBuffer(nEndpointBufferSize[AGGREGATOR_CAPTURE][i]);
                    EXIT_ON_ERROR(hr)
            }
        }
    }
    
Exit:
    return hr;
}

/// <summary>
/// <para>Flags to each AudioBuffer to not request new frames from WASAPI
/// and stops each WASAPI stream.</para>
/// <para>Note: not thread-safe. Must add mutex on bDone.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::StopCapture()
{
    HRESULT hr = S_OK;
    bDone[AGGREGATOR_CAPTURE] = TRUE;

    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        // Stop recording
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Stop();  
            EXIT_ON_ERROR(hr)
    }
    std::cout << MSG "Stopped audio capture." << std::endl;
    
Exit:
    return hr;
}

/// <summary>
/// <para>Starts rendering audio on user-selected devices on a poll basis.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::StartRender()
{
    HRESULT hr = S_OK;

    //-------- Reset and start rendering on all selected devices
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        // Clear audio client flags
        flags[AGGREGATOR_RENDER][i] = 0;

        hr = pAudioClient[AGGREGATOR_RENDER][i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[AGGREGATOR_RENDER][i]->Start();
            EXIT_ON_ERROR(hr)
    }

    std::cout << MSG "Starting audio render." << std::endl;

    //-------- Capture endpoint buffer data in a poll-based fashion
    while (!bDone[AGGREGATOR_RENDER])
    {
        // Pushes data from ring buffer into corresponding devices
        for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
        {
            UINT32 nFrames = pAudioBuffer[AGGREGATOR_RENDER][i]->FramesAvailable();
            if (nFrames > 0)
            {
                // Get the lesser of the number of frames to write to the device
                nFrames = min(nFrames, nEndpointBufferSize[AGGREGATOR_RENDER][i]);

                // Get the pointer from WASAPI where to write data to
                hr = pRenderClient[i]->GetBuffer(nFrames, &pData[AGGREGATOR_RENDER][i]);
                    EXIT_ON_ERROR(hr)
                
                // Load data from AudioBuffer's ring buffer into the WASAPI buffer for this device
                hr = pAudioBuffer[AGGREGATOR_RENDER][i]->PushData(pData[AGGREGATOR_RENDER][i], nFrames);
                    EXIT_ON_ERROR(hr)
                
                // Release buffer before next packet
                hr = pRenderClient[i]->ReleaseBuffer(nFrames, flags[AGGREGATOR_RENDER][i]);
                    EXIT_ON_ERROR(hr)
            }
        }
    }

Exit:
    return hr;
}

/// <summary>
/// <para>Flags to WASAPI to not expect new frames from AudioBuffer
/// and stops each WASAPI stream.</para>
/// <para>Note: not thread-safe. Must add mutex on bDone.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::StopRender()
{
    HRESULT hr = S_OK;
    bDone[AGGREGATOR_RENDER] = TRUE;

    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        // Stop rendering
        hr = pAudioClient[AGGREGATOR_RENDER][i]->Stop();  
            EXIT_ON_ERROR(hr)
    }
    std::cout << MSG "Stopped audio render." << std::endl;

Exit:
    return hr;
}

/// <summary>
/// <para>Pipes all active chosen type devices into console.</para> 
/// <para>Records each device into pDevice data structure to 
/// later retreive the desired ones based on user input.</para>
/// </summary>
/// <returns>
/// <para>S_OK if successful.</para>
/// <para>ENOMEM if memory (re)allocation for IMMDevice array fails.</para>
/// <para>IMMDevice/IMMDeviceCollection/IMMDeviceEnumerator/IPropertyStore
/// specific error otherwise.</para>
/// </returns>
HRESULT Aggregator::ListAvailableDevices(UINT8 nDeviceType)
{  
    HRESULT hr = S_OK;
    LPWSTR pwszID = NULL;
    IPropertyStore* pProps = NULL;
    PROPVARIANT varName;

    //-------- Enumerate endpoints
    hr = pEnumerator->EnumAudioEndpoints(((nDeviceType == AGGREGATOR_CAPTURE) ? eCapture : eRender), DEVICE_STATE_ACTIVE, &pCollection[nDeviceType]);
        EXIT_ON_ERROR(hr)
    
    //-------- Count the number of available devices
    hr = pCollection[nDeviceType]->GetCount(&nAllDevices[nDeviceType]);
        EXIT_ON_ERROR(hr)
    
    if (nAllDevices[nDeviceType] == 0)
    {
        std::cout << MSG "No " << ((nDeviceType == AGGREGATOR_CAPTURE)? "capture" : "render") << " endpoints were detected" << std::endl;
        hr = RPC_S_NO_ENDPOINT_FOUND;
        goto Exit;
    }
    
    //-------- Alloc memory for all the capture devices
    pDeviceAll[nDeviceType] = (IMMDevice**)malloc(nAllDevices[nDeviceType] * sizeof(IMMDevice*));
    if (pDeviceAll[nDeviceType] == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Go through the capture device list, output to console and save reference to each device
    for (UINT32 i = 0; i < nAllDevices[nDeviceType]; i++)
    {
        // Get next device from the list
        hr = pCollection[nDeviceType]->Item(i, &pDeviceAll[nDeviceType][i]);
            EXIT_ON_ERROR(hr)
        
        // Get the device's ID
        hr = pDeviceAll[nDeviceType][i]->GetId(&pwszID);
            EXIT_ON_ERROR(hr)

        // Get the device's properties in read-only mode
        hr = pDeviceAll[nDeviceType][i]->OpenPropertyStore(STGM_READ, &pProps);
            EXIT_ON_ERROR(hr)
        
        // Initialize container for property value
        PropVariantInit(&varName);
        
        // Get the endpoint's friendly-name property
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            EXIT_ON_ERROR(hr)

        // Print endpoint friendly name and endpoint ID
        printf("%s Device #%d: \"%S\" (%S)\n", ((nDeviceType == AGGREGATOR_CAPTURE) ? "Capture" : "Render"), i, varName.pwszVal, pwszID);

        // Release memory alloc'ed by Windows before next loop iteration
        CoTaskMemFree(pwszID);
        PropVariantClear(&varName);
        SAFE_RELEASE(pProps)
    }
    std::cout << std::endl;

    //-------- If listing devices succeeded, return with S_OK
    return hr;

    //-------- If listing devices failed at any step, clean up memory before next attempt
Exit:
    if (pDeviceAll[nDeviceType] != NULL)
    {
        for (UINT32 i = 0; i < nAllDevices[nDeviceType]; i++)
            SAFE_RELEASE(pDeviceAll[nDeviceType][i])

        free(pDeviceAll[nDeviceType]);
    }

    CoTaskMemFree(pwszID);
    PropVariantClear(&varName);
    CoTaskMemFree(pCollection[nDeviceType]);
    SAFE_RELEASE(pProps)

    return hr;
}

/// <summary>
/// <para>Prompts user to choose from devices available to the system.</para>
/// <para>Must be called after Aggregator::ListCaptureDevices.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::GetUserChoiceDevices(UINT8 nDeviceType)
{   
    HRESULT hr = S_OK;
    UINT32 nUserChoice;
    BOOL bInSet, bUserDone = bInSet = FALSE;
    CHAR sInput[11]; // Char array for UINT32 (10 digit number) + \n
    
    //-------- Allocate memory for the first user-chosen capture device
    pDevice[nDeviceType] = (IMMDevice**)malloc(sizeof(IMMDevice*));
    if (pDevice[nDeviceType] == NULL)
    {
        hr = ENOMEM;
        return hr;
    }

    std::cout << MSG "Select all desired out of available " << nAllDevices[nDeviceType] << " input devices." << std::endl;
    
    //-------- Prompt user to select an input device until they indicate they are done or until no more devices are left
    while (!bUserDone && nDevices[nDeviceType] < nAllDevices[nDeviceType])
    {
        std::cout   << MSG "Choose next "
                    << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
                    << " device (currently selected " 
                    << nDevices[nDeviceType] << " devices) or press [ENTER] to proceed."
                    << std::endl;

        std::cin.get(sInput, 11);
        std::string str(sInput);

        // Skip cin to next line to accept another input on next loop iteration
        std::cin.clear();
        std::cin.ignore(11, '\n');

        // If user chose at least 1 device and pressed ENTER
        if (str.length() == 0 && nDevices[nDeviceType] > 0) break;
        // If user attempts to proceed without choosing single device
        else if (str.length() == 0 && nDevices[nDeviceType] == 0)
        {
            std::cout   << WARN "You must choose at least 1 "
                        << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
                        << " device." << std::endl;
            continue;
        }
        // If user entered a number
        else
        {
            nUserChoice = std::atoi(sInput);
            bInSet = FALSE;
            
            // Check if user input a number within the range of available device indices
            if (nUserChoice < 0 || nUserChoice > nAllDevices[nDeviceType] - 1)
            {
                std::cout << WARN "You must pick one of existing devices, a number between 0 and " << nAllDevices[nDeviceType] - 1 << std::endl;
                continue;
            }

            // Check if user already chose this device
            for (UINT32 i = 0; i < nDevices[nDeviceType]; i++)
            {
                bInSet = (pDevice[nDeviceType][i] == pDeviceAll[nDeviceType][nUserChoice]);
                if (bInSet) break;
            }
            
            // Check if this device is already chosen
            if (bInSet)
            {
                std::cout << WARN "You cannot choose the same device more than once." << std::endl;
                continue;
            }
            // If all is good, add the device into the list of devices to use for aggregator
            else
            {
                IMMDevice** dummy = (IMMDevice**)realloc(pDevice[nDeviceType], ++nDevices[nDeviceType] * sizeof(IMMDevice*));
                if (dummy == NULL)
                {
                    free(pDevice[nDeviceType]);
                    hr = ENOMEM;
                    goto Exit;
                }
                else
                {
                    pDevice[nDeviceType] = dummy;
                    pDevice[nDeviceType][nDevices[nDeviceType] - 1] = pDeviceAll[nDeviceType][nUserChoice];
                }
            }
        }
    }
    std::cout   << MSG << nDevices[nDeviceType] 
                << ((nDeviceType == AGGREGATOR_CAPTURE) ? " capture" : " render")
                << " devices selected." << std::endl << std::endl;

Exit:
    return hr;
}

/// <summary>
/// <para>Finds Greatest Common Divisor of two integers.</para>
/// </summary>
/// <param name="a">- first integer</param>
/// <param name="b">- second integer</param>
/// <returns>The greatest common devisor of "a" and "b"</returns>
DWORD Aggregator::gcd(DWORD a, DWORD b)
{
    if (b == 0) return a;
    return gcd(b, a % b);
}