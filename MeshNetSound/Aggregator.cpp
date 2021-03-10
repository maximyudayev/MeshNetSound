/*
    TODO: 
        I.------support dynamic updates to the aggregator like:
                1. Addition/removal of capture devices.
                2. Sample-rate conversion.
                3. Output channel mask changes.
                4. Circular buffer size.
        II.-----facilitate modularity for cross-platform portability.
        III.----perform setup of WiFi-direct and virtual audio devices before listing WASAPI devices;
                display all appropriate capture/render devices together.
        IV.-----ensure graceful Exit strategy in all methods, currently not all scenarios are considered
                and not all memory is freed.
        V.------provide safety when an audio device is removed, hot plugged while program is running.
        VI.-----do CLI user input validation.
        VII.----negotiate WASAN node metadata across participating nodes to establish stream format dynamically.
        IIX.----create an optimally sized for target PC thread pool to process in parallel audio streams.
        IX.-----consider adapting wireless code into an inheriting WASAPI audio client class.
*/

#include "Aggregator.h"
#include "Flanger.h"
#include "PitchShifter.h"

//---------- Windows macro definitions ----------------------------------------------//
static const CLSID  CLSID_MMDeviceEnumerator    = __uuidof(MMDeviceEnumerator);
static const IID    IID_IMMDeviceEnumerator     = __uuidof(IMMDeviceEnumerator);
static const IID    IID_IAudioClient            = __uuidof(IAudioClient);
static const IID    IID_IAudioCaptureClient     = __uuidof(IAudioCaptureClient);
static const IID    IID_IAudioRenderClient      = __uuidof(IAudioRenderClient);

Aggregator::Aggregator(){}

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
        if (pRingBuffer[j] != NULL)
        {
            for (UINT32 i = 0; i < nAggregatedChannels[j]; i++)
                delete pRingBuffer[j][i];

            free(pRingBuffer[j]);
        }
        // Set number of aggregated channels back to 0
        nAggregatedChannels[j] = 0;

        // Free dynamic arrays holding reference to all these variables as last step
        if (pDeviceAll[j] != NULL)              free(pDeviceAll[j]);
        if (pDevice[j] != NULL)                 free(pDevice[j]);
        if (pWASANNodeIP[j] != NULL)            free(pWASANNodeIP[j]);
        if (pAudioClient[j] != NULL)            free(pAudioClient[j]);
        if (pwfx[j] != NULL)                    free(pwfx[j]);
        if (pAudioBuffer[j] != NULL)            free(pAudioBuffer[j]);
        if (pData[j] != NULL)                   free(pData[j]);
        if (nGCD[j] != NULL)                    free(nGCD[j]);
        if (nGCDDiv[j] != NULL)                 free(nGCDDiv[j]);
        if (nGCDTFreqDiv[j] != NULL)            free(nGCDTFreqDiv[j]);
        if (nUpsample[j] != NULL)               free(nUpsample[j]);
        if (nDownsample[j] != NULL)             free(nDownsample[j]);
        if (nEndpointBufferSize[j] != NULL)     free(nEndpointBufferSize[j]);
        if (nEndpointPackets[j] != NULL)        free(nEndpointPackets[j]);
        if (flags[j] != NULL)                   free(flags[j]);
    }

    //-------- Free LP Filter memory of the Resampler class
    Resampler::FreeLPFilter();

    if (pCaptureClient != NULL)                     free(pCaptureClient);
    if (pRenderClient != NULL)                      free(pRenderClient);
    if (pAudioBufferGroupId != NULL)                free(pAudioBufferGroupId);

    SAFE_RELEASE(pEnumerator)

    //-------- Release Winsock DLL memory
    WSACleanup();
}

HRESULT Aggregator::Initialize()
{
    HRESULT hr = ERROR_SUCCESS;
    UINT32 attempt = 0;

    std::cout << "<-------- Starting Aggregator -------->" << std::endl << std::endl;

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
    //-------- Try to get user choice of WASAN capture nodes AGGREGATOR_OP_ATTEMPTS times
    do
    {
        hr = GetWASANNodes(AGGREGATOR_CAPTURE);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to list all available capture devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = ListAvailableDevices(AGGREGATOR_CAPTURE);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of capture devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetWASAPIDevices(AGGREGATOR_CAPTURE);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of WASAN render nodes AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetWASANNodes(AGGREGATOR_RENDER);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)
    
    //-------- Try to list all available render devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = ListAvailableDevices(AGGREGATOR_RENDER);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of render devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetWASAPIDevices(AGGREGATOR_RENDER);
    } while (hr != ERROR_SUCCESS && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    std::cout   << MSG << nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]
                << " inputs and "
                << nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]
                << " outputs selected." END
                << std::endl << std::endl;

    //---------------- Initialization ----------------//

    //-------- Initialize winsock if user chose to connect to other WASAN nodes
    if (nWASANNodes[AGGREGATOR_CAPTURE] != 0 || nWASANNodes[AGGREGATOR_RENDER] != 0)
    {
        WSADATA wsa;
        std::cout << MSG "Initialising Winsock DLL..." END << std::endl;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            std::cout   << ERR "Winsock initialization failed. Error Code: " 
                        << WSAGetLastError() << END
                        << std::endl;
            exit(EXIT_FAILURE);
        }
        std::cout << SUC "Success. Winsock initialized." END << std::endl;
    }

    //-------- Allocate space for indices of 1 input ring buffer and 1 output ring buffer 
    pAudioBufferGroupId = (UINT32*)malloc(2 * sizeof(UINT32));

    //-------- Initialize LP Filter of the Resampler class
    Resampler::InitLPFilter(FALSE, RESAMPLER_ROLLOFF_FREQ, RESAMPLER_BETA, RESAMPLER_L_TWOS_EXP);

    hr = InitializeCapture();
        EXIT_ON_ERROR(hr);

    hr = InitializeRender();
        EXIT_ON_ERROR(hr)

    //-------- If initialization succeeded, return with S_OK
    return hr;

    //-------- If initialization failed at any of above steps, clean up memory prior to next attempt
Exit:
    SAFE_RELEASE(pEnumerator)

    if (pAudioBufferGroupId != NULL)        free(pAudioBufferGroupId);

    return hr;
}

HRESULT Aggregator::InitializeCapture()
{
    HRESULT hr = ERROR_SUCCESS;

    //-------- Use information obtained from user inputs to dynamically create the system
    pAudioClient[AGGREGATOR_CAPTURE]            = (IAudioClient**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(IAudioClient*));
    pwfx[AGGREGATOR_CAPTURE]                    = (WAVEFORMATEX**)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(WAVEFORMATEX*));
    pCaptureClient                              = (IAudioCaptureClient**)malloc(nDevices[AGGREGATOR_CAPTURE] * sizeof(IAudioCaptureClient*));
    pAudioBuffer[AGGREGATOR_CAPTURE]            = (AudioBuffer**)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(AudioBuffer*));
    pData[AGGREGATOR_CAPTURE]                   = (BYTE**)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(BYTE*));
    nGCD[AGGREGATOR_CAPTURE]                    = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    nGCDDiv[AGGREGATOR_CAPTURE]                 = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    nGCDTFreqDiv[AGGREGATOR_CAPTURE]            = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    nUpsample[AGGREGATOR_CAPTURE]               = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    nDownsample[AGGREGATOR_CAPTURE]             = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    flags[AGGREGATOR_CAPTURE]                   = (DWORD*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(DWORD));
    nEndpointBufferSize[AGGREGATOR_CAPTURE]     = (UINT32*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(UINT32));
    nEndpointPackets[AGGREGATOR_CAPTURE]        = (UINT32*)malloc((nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]) * sizeof(UINT32));
    
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

    //////////////////////////////////////////////////////////////////////////////////////////////////
    //-------- TODO: negotiate wireless node metadata and format (can be hardcoded for now) --------//
    //////////////////////////////////////////////////////////////////////////////////////////////////
    for (UINT32 i = 0; i < nWASANNodes[AGGREGATOR_CAPTURE]; i++)
    {
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i] = (WAVEFORMATEX*)malloc(sizeof(WAVEFORMATEXTENSIBLE));
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->nSamplesPerSec = TEMP_AGGREGATOR_SAMPLE_PER_SEC;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->nChannels = TEMP_AGGREGATOR_CHANNELS;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->nBlockAlign = TEMP_AGGREGATOR_BLOCK_ALIGN;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->wBitsPerSample = TEMP_AGGREGATOR_BIT_PER_SAMPLE;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->cbSize = TEMP_AGGREGATOR_CB_SIZE;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->wFormatTag = TEMP_AGGREGATOR_FORMAT_TAG;
        pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i]->nAvgBytesPerSec = TEMP_AGGREGATOR_AVG_BYTE_PER_SEC;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i])->SubFormat = TEMP_AGGREGATOR_SUBFORMAT;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i])->dwChannelMask = TEMP_AGGREGATOR_CHANNEL_MASK;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i])->Samples.wValidBitsPerSample = TEMP_AGGREGATOR_VALID_BIT_PER_SAMPLE;
    
        nEndpointBufferSize[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i] = TEMP_AGGREGATOR_ENDPOINT_BUF_SIZE;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////
    //----------------------------------------------------------------------------------------------//
    //////////////////////////////////////////////////////////////////////////////////////////////////

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < (nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]); i++)
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

    //-------- Initialize streams to operate in poll mode
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
    }

    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
        printf("[%d]:\nThe %d-th buffer size is: %d\n", i, i, nEndpointBufferSize[AGGREGATOR_CAPTURE][i]);
    
    //-------- Create a new AudioBuffer object group - corresponds to 1 input ring buffer
    hr = AudioBuffer::CreateBufferGroup(&pAudioBufferGroupId[0]);
        EXIT_ON_ERROR(hr)
    
    //-------- Instantiate AudioBuffer for each user-chosen capture device
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
        pAudioBuffer[AGGREGATOR_CAPTURE][i] = 
            new AudioBuffer(
                "Hardware Capture Device " + std::to_string(i) + " ", 
                pAudioBufferGroupId[0]);

    //-------- Instantiate AudioBuffer for each user-chosen WASAN capture node
    for (UINT32 i = 0; i < nWASANNodes[AGGREGATOR_CAPTURE]; i++)
        pAudioBuffer[AGGREGATOR_CAPTURE][nDevices[AGGREGATOR_CAPTURE] + i] = 
            new UDPAudioBuffer(
                "WASAN Capture Node " + std::to_string(i) + " ", 
                pAudioBufferGroupId[0], 
                pWASANNodeIP[AGGREGATOR_CAPTURE] + i * AGGREGATOR_CIN_IP_LEN);

    //-------- Notify the audio sink which format to use
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->SetFormat(pwfx[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
        nAggregatedChannels[AGGREGATOR_CAPTURE] += pwfx[AGGREGATOR_CAPTURE][i]->nChannels;
    
    //-------- Create input ring buffer
    pRingBuffer[AGGREGATOR_CAPTURE] = (RingBufferChannel**)malloc(nAggregatedChannels[AGGREGATOR_CAPTURE] * sizeof(RingBufferChannel*));
    for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_CAPTURE]; i++)
        pRingBuffer[AGGREGATOR_CAPTURE][i] = new RingBufferChannel();
    
    //-------- Initialize AudioBuffer objects' buffers using the obtained information
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
    {
        UINT32 nChannels = pAudioBuffer[AGGREGATOR_CAPTURE][i]->GetChannelNumber();
        
        RingBufferChannel** pBuffer = (RingBufferChannel**)malloc(nChannels * sizeof(RingBufferChannel*));

        for (UINT32 j = 0; j < nChannels; j++)
            pBuffer[j] = pRingBuffer[AGGREGATOR_CAPTURE][i];

        hr = pAudioBuffer[AGGREGATOR_CAPTURE][i]->InitBuffer(&nEndpointBufferSize[AGGREGATOR_CAPTURE][i],
                                                    pBuffer,
                                                    &nCircularBufferSize[AGGREGATOR_CAPTURE],
                                                    nUpsample[AGGREGATOR_CAPTURE][i], 
                                                    nDownsample[AGGREGATOR_CAPTURE][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
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
    if (pRingBuffer[AGGREGATOR_CAPTURE] != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_CAPTURE]; i++)
            delete pRingBuffer[AGGREGATOR_CAPTURE][i];

        free(pRingBuffer[AGGREGATOR_CAPTURE]);
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

HRESULT Aggregator::InitializeRender()
{
    HRESULT hr = ERROR_SUCCESS;

    //-------- Use information obtained from user inputs to dynamically create the system
    pAudioClient[AGGREGATOR_RENDER]         = (IAudioClient**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(IAudioClient*));
    pwfx[AGGREGATOR_RENDER]                 = (WAVEFORMATEX**)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(WAVEFORMATEX*));
    pRenderClient                           = (IAudioRenderClient**)malloc(nDevices[AGGREGATOR_RENDER] * sizeof(IAudioRenderClient*));
    pAudioBuffer[AGGREGATOR_RENDER]         = (AudioBuffer**)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(AudioBuffer*));
    pData[AGGREGATOR_RENDER]                = (BYTE**)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(BYTE*));
    nGCD[AGGREGATOR_RENDER]                 = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    nGCDDiv[AGGREGATOR_RENDER]              = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    nGCDTFreqDiv[AGGREGATOR_RENDER]         = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    nUpsample[AGGREGATOR_RENDER]            = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    nDownsample[AGGREGATOR_RENDER]          = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    flags[AGGREGATOR_RENDER]                = (DWORD*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(DWORD));
    nEndpointBufferSize[AGGREGATOR_RENDER]  = (UINT32*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(UINT32));
    nEndpointPackets[AGGREGATOR_RENDER]     = (UINT32*)malloc((nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]) * sizeof(UINT32));

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

    //////////////////////////////////////////////////////////////////////////////////////////////////
    //-------- TODO: negotiate wireless node metadata and format (can be hardcoded for now) --------//
    //////////////////////////////////////////////////////////////////////////////////////////////////
    for (UINT32 i = 0; i < nWASANNodes[AGGREGATOR_CAPTURE]; i++)
    {
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i] = (WAVEFORMATEX*)malloc(sizeof(WAVEFORMATEXTENSIBLE));
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->nSamplesPerSec = TEMP_AGGREGATOR_SAMPLE_PER_SEC;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->nChannels = TEMP_AGGREGATOR_CHANNELS;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->nBlockAlign = TEMP_AGGREGATOR_BLOCK_ALIGN;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->wBitsPerSample = TEMP_AGGREGATOR_BIT_PER_SAMPLE;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->cbSize = TEMP_AGGREGATOR_CB_SIZE;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->wFormatTag = TEMP_AGGREGATOR_FORMAT_TAG;
        pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i]->nAvgBytesPerSec = TEMP_AGGREGATOR_AVG_BYTE_PER_SEC;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i])->SubFormat = TEMP_AGGREGATOR_SUBFORMAT;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i])->dwChannelMask = TEMP_AGGREGATOR_CHANNEL_MASK;
        reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i])->Samples.wValidBitsPerSample = TEMP_AGGREGATOR_VALID_BIT_PER_SAMPLE;

        nEndpointBufferSize[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i] = TEMP_AGGREGATOR_ENDPOINT_BUF_SIZE;
    }
    //////////////////////////////////////////////////////////////////////////////////////////////////
    //----------------------------------------------------------------------------------------------//
    //////////////////////////////////////////////////////////////////////////////////////////////////

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
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_CAPTURE]; i++)
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
    }
    
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]; i++)
        printf("[%d]:\nThe %d-th buffer size is: %d\n", i, i, nEndpointBufferSize[AGGREGATOR_RENDER][i]);

    //-------- Create a new AudioBuffer object group - corresponds to 1 output ring buffer
    hr = AudioBuffer::CreateBufferGroup(&pAudioBufferGroupId[1]);
        EXIT_ON_ERROR(hr)

    //-------- Instantiate AudioBuffer for each user-chosen render device
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
        pAudioBuffer[AGGREGATOR_RENDER][i] = 
            new AudioBuffer(
                "Hardware Render Device " + std::to_string(i) + " ", 
                pAudioBufferGroupId[1]);

    //-------- Instantiate AudioBuffer for each user-chosen WASAN render node
    for (UINT32 i = 0; i < nWASANNodes[AGGREGATOR_RENDER]; i++)
        pAudioBuffer[AGGREGATOR_RENDER][nDevices[AGGREGATOR_RENDER] + i] = 
            new UDPAudioBuffer(
                "WASAN Render Node " + std::to_string(i) + " ", 
                pAudioBufferGroupId[1], 
                pWASANNodeIP[AGGREGATOR_RENDER] + i * AGGREGATOR_CIN_IP_LEN);

    //-------- Notify the audio source which format to use
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->SetFormat(pwfx[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]; i++)
        nAggregatedChannels[AGGREGATOR_RENDER] += pwfx[AGGREGATOR_RENDER][i]->nChannels;

    //-------- Create input ring buffer
    pRingBuffer[AGGREGATOR_RENDER] = (RingBufferChannel**)malloc(nAggregatedChannels[AGGREGATOR_RENDER] * sizeof(RingBufferChannel*));
    for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_RENDER]; i++)
        pRingBuffer[AGGREGATOR_RENDER][i] = new RingBufferChannel();

    //-------- Initialize AudioBuffer objects' buffers using the obtained information
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]; i++)
    {
        UINT32 nChannels = pAudioBuffer[AGGREGATOR_RENDER][i]->GetChannelNumber();

        RingBufferChannel** pBuffer = (RingBufferChannel**)malloc(nChannels * sizeof(RingBufferChannel*));

        for (UINT32 j = 0; j < nChannels; j++)
            pBuffer[j] = pRingBuffer[AGGREGATOR_RENDER][i];

        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->InitBuffer(&nEndpointBufferSize[AGGREGATOR_RENDER][i],
                                                            pBuffer,
                                                            &nCircularBufferSize[AGGREGATOR_RENDER],
                                                            nUpsample[AGGREGATOR_RENDER][i],
                                                            nDownsample[AGGREGATOR_RENDER][i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER] + nWASANNodes[AGGREGATOR_RENDER]; i++)
    {
        hr = pAudioBuffer[AGGREGATOR_RENDER][i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

    //-------- If initialization succeeded, return with S_OK
    return hr;

    //-------- If initialization failed at any of above steps, clean up memory prior to next attempt
Exit:
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        CoTaskMemFree(pwfx[AGGREGATOR_RENDER][i]);
        SAFE_RELEASE(pAudioClient[AGGREGATOR_RENDER][i])
        SAFE_RELEASE(pRenderClient[i])

        delete pAudioBuffer[AGGREGATOR_RENDER][i];
    }

    // Free circular buffer
    if (pRingBuffer[AGGREGATOR_RENDER] != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels[AGGREGATOR_RENDER]; i++)
            delete pRingBuffer[AGGREGATOR_RENDER][i];

        free(pRingBuffer[AGGREGATOR_RENDER]);
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

HRESULT Aggregator::Start()
{
    HRESULT hr = ERROR_SUCCESS;

    bDone[AGGREGATOR_CAPTURE] = FALSE;
    bDone[AGGREGATOR_RENDER] = FALSE;

    hr = StartCapture();
        EXIT_ON_ERROR(hr)

    hr = StartRender();
        EXIT_ON_ERROR(hr)

    return hr;
Exit:
    return hr;
}

HRESULT Aggregator::Stop()
{
    HRESULT hr = ERROR_SUCCESS;

    bDone[AGGREGATOR_CAPTURE] = TRUE;
    bDone[AGGREGATOR_RENDER] = TRUE;

    hr = StopCapture();
        EXIT_ON_ERROR(hr)

    hr = StopRender();
        EXIT_ON_ERROR(hr)

    return hr;
Exit:
    return hr;
}

HRESULT Aggregator::StartCapture()
{
    HRESULT hr = ERROR_SUCCESS;
    UINT32 nThreadId = 0;

    //-------- Reset and start capturing on all selected devices
    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Start();
            EXIT_ON_ERROR(hr)
    }

    std::cout << MSG "Starting audio capture." END << std::endl;

    if (nWASANNodes[AGGREGATOR_CAPTURE] > 0 && nDevices[AGGREGATOR_CAPTURE] > 0)
    {
        hCaptureThread = (HANDLE*)malloc(2 * sizeof(HANDLE));
        dwCaptureThreadId = (DWORD*)malloc(2 * sizeof(DWORD));
        nCaptureThread = 2;
    }
    else if (nWASANNodes[AGGREGATOR_CAPTURE] > 0 || nDevices[AGGREGATOR_CAPTURE] > 0)
    {
        hCaptureThread = (HANDLE*)malloc(sizeof(HANDLE));
        dwCaptureThreadId = (DWORD*)malloc(sizeof(DWORD));
        nCaptureThread = 1;
    } // else it remains 0

    //-------- Start capturing on all chosen WASAN node sockets
    if (nWASANNodes[AGGREGATOR_CAPTURE] > 0)
    {
        // Create a struct for the UDP capture thread to access all necessary data elements
        UDPCAPTURETHREADPARAM pCaptureThreadParam = {
            UDPServerIP,
            (UDPAudioBuffer**)(pAudioBuffer[AGGREGATOR_CAPTURE] + nDevices[AGGREGATOR_CAPTURE]),
            nWASANNodes[AGGREGATOR_CAPTURE],
            &bDone[AGGREGATOR_CAPTURE]
        };

        // Create a server listener thread
        hCaptureThread[nThreadId] = CreateThread(NULL, 0, UDPCaptureThread, (LPVOID)&pCaptureThreadParam, 0, &dwCaptureThreadId[nThreadId]);

        if (hCaptureThread[nThreadId] == NULL)
        {
            std::cout << ERR "Failed to create UDP Server thread." END << std::endl;

            hr = ERROR_SERVICE_NO_THREAD;
                EXIT_ON_ERROR(hr)
        }

        std::cout << SUC "Succesfully created UDP Server thread." END << std::endl;
        nThreadId++; // increment the helper variable
    }

    //-------- Start capturing on all chosen WASAPI devices
    if (nDevices[AGGREGATOR_CAPTURE] > 0)
    {
        // Create a struct for the UDP capture thread to access all necessary data elements
        WASAPICAPTURETHREADPARAM pCaptureThreadParam = {
            pAudioBuffer[AGGREGATOR_CAPTURE],
            nDevices[AGGREGATOR_CAPTURE],
            &bDone[AGGREGATOR_CAPTURE],
            &flags[AGGREGATOR_CAPTURE],
            pData[AGGREGATOR_CAPTURE],
            &nEndpointBufferSize[AGGREGATOR_CAPTURE],
            &nEndpointPackets[AGGREGATOR_CAPTURE],
            pCaptureClient
        };

        // Create a server listener thread
        hCaptureThread[nThreadId] = CreateThread(NULL, 0, WASAPICaptureThread, (LPVOID)&pCaptureThreadParam, 0, &dwCaptureThreadId[nThreadId]);

        if (hCaptureThread[nThreadId] == NULL)
        {
            std::cout << ERR "Failed to create WASAPI capture thread." END << std::endl;

            hr = ERROR_SERVICE_NO_THREAD;
                EXIT_ON_ERROR(hr)
        }

        std::cout << SUC "Succesfully created WASAPI capture thread." END << std::endl;
    }

    return hr;

Exit:
    return hr;
}

HRESULT Aggregator::StopCapture()
{
    HRESULT hr = ERROR_SUCCESS;

    for (UINT32 i = 0; i < nDevices[AGGREGATOR_CAPTURE]; i++)
    {
        // Stop recording
        hr = pAudioClient[AGGREGATOR_CAPTURE][i]->Stop();  
            EXIT_ON_ERROR(hr)
    }
    std::cout << MSG "Stopped audio capture." END << std::endl;

    // Wait for capture threads to terminate
    WaitForMultipleObjects(nCaptureThread, hCaptureThread, TRUE, INFINITE);
    for (UINT32 i = 0; i < nCaptureThread; i++)
    {
        CloseHandle(hCaptureThread[i]);
        hCaptureThread[i] = NULL;
        dwCaptureThreadId[i] = NULL;
    }

    if (hCaptureThread != NULL) free(hCaptureThread);
    if (dwCaptureThreadId != NULL) free(dwCaptureThreadId);

    return hr;

Exit:
    return hr;
}

HRESULT Aggregator::StartRender()
{
    HRESULT hr = ERROR_SUCCESS;
    UINT32 nThreadId = 0;

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

    std::cout << MSG "Starting audio render." END << std::endl;

    if (nWASANNodes[AGGREGATOR_RENDER] > 0 || nDevices[AGGREGATOR_RENDER] > 0)
    {
        hRenderThread = (HANDLE*)malloc(sizeof(HANDLE));
        dwRenderThreadId = (DWORD*)malloc(sizeof(DWORD));
        nRenderThread = 1;

        // Create a struct for the UDP capture thread to access all necessary data elements
        RENDERTHREADPARAM pCaptureThreadParam = {
            &bDone[AGGREGATOR_RENDER],
            nDevices[AGGREGATOR_RENDER],
            pAudioBuffer[AGGREGATOR_RENDER],
            &nEndpointBufferSize[AGGREGATOR_RENDER],
            pRenderClient,
            pData[AGGREGATOR_RENDER],
            &flags[AGGREGATOR_RENDER],
            (UDPAudioBuffer**)(pAudioBuffer[AGGREGATOR_RENDER] + nDevices[AGGREGATOR_RENDER]),
            nWASANNodes[AGGREGATOR_RENDER]
        };

        // Create a server listener thread
        hRenderThread[nThreadId] = CreateThread(NULL, 0, RenderThread, (LPVOID)&pCaptureThreadParam, 0, &dwRenderThreadId[nThreadId]);

        if (hRenderThread[nThreadId] == NULL)
        {
            std::cout << ERR "Failed to create audio render Aggregator thread." END << std::endl;

            hr = ERROR_SERVICE_NO_THREAD;
                EXIT_ON_ERROR(hr)
        }

        std::cout << SUC "Succesfully created audio render Aggregator thread." END << std::endl;
    } // else it remains 0

    return hr;

Exit:
    return hr;
}

HRESULT Aggregator::StopRender()
{
    HRESULT hr = ERROR_SUCCESS;

    for (UINT32 i = 0; i < nDevices[AGGREGATOR_RENDER]; i++)
    {
        // Stop rendering
        hr = pAudioClient[AGGREGATOR_RENDER][i]->Stop();  
            EXIT_ON_ERROR(hr)
    }
    std::cout << MSG "Stopped audio render." END << std::endl;

    // Wait for render threads to terminate
    WaitForMultipleObjects(nRenderThread, hRenderThread, TRUE, INFINITE);
    for (UINT32 i = 0; i < nRenderThread; i++)
    {
        CloseHandle(hRenderThread[i]);
        hRenderThread[i] = NULL;
        dwRenderThreadId[i] = NULL;
    }

    if (hRenderThread != NULL) free(hRenderThread);
    if (dwRenderThreadId != NULL) free(dwRenderThreadId);

    return hr;

Exit:
    return hr;
}

HRESULT Aggregator::ListAvailableDevices(UINT8 nDeviceType)
{  
    HRESULT hr = ERROR_SUCCESS;
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
        std::cout   << MSG "No " << ((nDeviceType == AGGREGATOR_CAPTURE)? "capture" : "render") 
                    << " endpoints were detected" END 
                    << std::endl;
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

HRESULT Aggregator::GetWASANNodes(UINT8 nDeviceType)
{
    HRESULT hr = ERROR_SUCCESS;
    BOOL bUserDone = FALSE;
    CHAR sInput[AGGREGATOR_CIN_IP_LEN]; // Char array for UINT32 (10 digit number or 15 character IP address) + \n
    INT32 nStringCompare = 0;

    //-------- Allocate memory for the first user-chosen endpoint device
    pWASANNodeIP[nDeviceType] = (CHAR*)malloc(AGGREGATOR_CIN_IP_LEN * sizeof(CHAR));
    if (pWASANNodeIP[nDeviceType] == NULL)
    {
        hr = ENOMEM;
        return hr;
    }

    //-------- Prompt user to connect WASAN nodes
    std::cout << MSG "Enter the IP addresses of "
        << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
        << " WASAN nodes. I.e: 192.168.137.1" END << std::endl;

    while (!bUserDone)
    {
        std::cout << MSG "Choose next "
            << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
            << " WASAN node (currently selected "
            << nWASANNodes[nDeviceType] << " nodes) or press [ENTER] to proceed." END
            << std::endl;

        std::cin.get(sInput, AGGREGATOR_CIN_IP_LEN);
        std::string str(sInput);

        // Skip cin to next line to accept another input on next loop iteration
        std::cin.clear();
        std::cin.ignore(AGGREGATOR_CIN_IP_LEN, '\n');

        // If user pressed ENTER, hence does not desire to connect WASAN nodes
        if (str.length() == 0) break;
        // If user provided input
        else
        {
            nStringCompare = 1;

            // Check if user already chose this node
            for (UINT32 i = 0; i < nWASANNodes[nDeviceType]; i++)
            {
                nStringCompare = strcmp(pWASANNodeIP[nDeviceType] + AGGREGATOR_CIN_IP_LEN * i, sInput);
                if (nStringCompare == 0) break;
            }

            // Check if this node is already chosen
            if (nStringCompare == 0)
            {
                std::cout << WRN "You cannot choose the same node more than once." END << std::endl;
                continue;
            }
            // If all is good, add the device into the list of nodes to use for aggregator
            else
            {
                CHAR* dummy = (CHAR*)realloc(pWASANNodeIP[nDeviceType], ++nWASANNodes[nDeviceType] * AGGREGATOR_CIN_IP_LEN * sizeof(CHAR));
                if (dummy == NULL)
                {
                    free(pWASANNodeIP[nDeviceType]);
                    hr = ENOMEM;
                    goto Exit;
                }
                else
                {
                    pWASANNodeIP[nDeviceType] = dummy;
                    memcpy(pWASANNodeIP[nDeviceType] + (nWASANNodes[nDeviceType] - 1) * AGGREGATOR_CIN_IP_LEN, sInput, AGGREGATOR_CIN_IP_LEN);
                }
            }
        }
    }

    std::cout << MSG << nWASANNodes[nDeviceType]
        << ((nDeviceType == AGGREGATOR_CAPTURE) ? " capture" : " render")
        << " WASAN nodes selected." END
        << std::endl << std::endl;

    //-------- Prompt user for current device's IP address if any WASAN capture nodes are to be connected
    if (nWASANNodes > 0 && nDeviceType == AGGREGATOR_CAPTURE)
    {
        std::cout   << MSG "Enter the IP address of this device on which to listen to WASAN capture nodes as a UDP server." << std::endl
                    << TAB "In current implementation, enter the IP address from the mobile hotspot tab. I.e: 192.168.137.1" << std::endl
                    << TAB "Update firewall rules to allow UDP traffic on that IP, on port " << UDP_RCV_PORT << " , on public networks." END
                    << std::endl << std::endl;

        bUserDone = FALSE;
        while (!bUserDone)
        {
            std::cout << MSG "Enter this device's (UDP server) IP address." END << std::endl;

            std::cin.get(sInput, AGGREGATOR_CIN_IP_LEN);
            std::string str(sInput);

            // Skip cin to next line to accept another input on next loop iteration
            std::cin.clear();
            std::cin.ignore(AGGREGATOR_CIN_IP_LEN, '\n');

            // If user attempts to proceed without enterint own IP address
            if (str.length() == 0)
            {
                std::cout << WRN "You must enter this device's IP address to act as UDP server to receive audio streams from other WASAN capture nodes." END << std::endl;
                continue;
            }
            // If user entered a number, copy IP to Aggregator and exit loop
            else
            {
                strcpy(UDPServerIP, str.c_str());
                break;
            }
        }
    }

    return hr;

Exit:
    return hr;
}

HRESULT Aggregator::GetWASAPIDevices(UINT8 nDeviceType)
{
    HRESULT hr = ERROR_SUCCESS;
    BOOL bInSet, bUserDone = bInSet = FALSE;
    CHAR sInput[AGGREGATOR_CIN_DEVICEID_LEN]; // Char array for UINT32 (10 digit number or 15 character IP address) + \n
    UINT32 nUserChoice;

    //-------- Allocate memory for the first user-chosen endpoint device
    pDevice[nDeviceType] = (IMMDevice**)malloc(sizeof(IMMDevice*));
    if (pDevice[nDeviceType] == NULL)
    {
        hr = ENOMEM;
        return hr;
    }

    std::cout << MSG "Select all desired out of available "
        << nAllDevices[nDeviceType]
        << " hardware "
        << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
        << " devices." END
        << std::endl;

    //-------- Prompt user to select an input device until they indicate they are done or until no more devices are left
    while (!bUserDone && nDevices[nDeviceType] < nAllDevices[nDeviceType])
    {
        std::cout << MSG "Choose next "
            << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
            << " device (currently selected "
            << nDevices[nDeviceType] << " devices) or press [ENTER] to proceed." END
            << std::endl;

        std::cin.get(sInput, AGGREGATOR_CIN_DEVICEID_LEN);
        std::string str(sInput);

        // Skip cin to next line to accept another input on next loop iteration
        std::cin.clear();
        std::cin.ignore(AGGREGATOR_CIN_DEVICEID_LEN, '\n');

        // If user chose at least 1 device and pressed ENTER
        if (str.length() == 0 && nDevices[nDeviceType] > 0) break;
        // If user attempts to proceed without choosing a single device or WASAN node
        else if (str.length() == 0 && nDevices[nDeviceType] == 0 && nWASANNodes[nDeviceType] == 0)
        {
            std::cout << WRN "You must choose at least 1 "
                << ((nDeviceType == AGGREGATOR_CAPTURE) ? "capture" : "render")
                << " device." END
                << std::endl;
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
                std::cout << WRN "You must pick one of existing devices, a number between 0 and "
                    << nAllDevices[nDeviceType] - 1 << END
                    << std::endl;
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
                std::cout << WRN "You cannot choose the same device more than once." END << std::endl;
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
    std::cout << MSG << nDevices[nDeviceType]
        << ((nDeviceType == AGGREGATOR_CAPTURE) ? " capture" : " render")
        << " devices selected." END
        << std::endl << std::endl;

    return hr;

Exit:
    return hr;
}

DWORD Aggregator::gcd(DWORD a, DWORD b)
{
    if (b == 0) return a;
    return gcd(b, a % b);
}

DWORD WINAPI UDPCaptureThread(LPVOID lpParam)
{
    // Cast void pointer into familiar struct
    UDPCAPTURETHREADPARAM* pCaptureThreadParam = (UDPCAPTURETHREADPARAM*)lpParam;
    
    // Create UDP server socket
    SOCKET* server = CreateSocketUDP();

    if (server != NULL)
    {
        // Put thread into listen loop
        UDPAudioBuffer::ReceiveDataUDP(server,
            pCaptureThreadParam->sUDPServerIP,
            pCaptureThreadParam->pUDPAudioBuffer,
            pCaptureThreadParam->nWASANNodes,
            pCaptureThreadParam->bDone);

        // Destroy socket after server returns from the receive routine
        CloseSocketUDP(server);
    }
    std::cout << MSG << "UDP capture thread exited." END << std::endl;

    return 0;
}

DWORD WINAPI WASAPICaptureThread(LPVOID lpParam)
{
    HRESULT hr = ERROR_SUCCESS;
    // Cast void pointer into familiar struct
    WASAPICAPTURETHREADPARAM* pCaptureThreadParam = (WASAPICAPTURETHREADPARAM*)lpParam;

    // Capture endpoint buffer data in a poll-based fashion
    while (!*pCaptureThreadParam->bDone)
    {
        // Captures data from all devices
        for (UINT32 i = 0; i < pCaptureThreadParam->nDevices; i++)
        {
            hr = pCaptureThreadParam->pCaptureClient[i]->GetNextPacketSize(pCaptureThreadParam->nEndpointPackets[i]);
                EXIT_ON_ERROR(hr)

            if (*pCaptureThreadParam->nEndpointPackets[i] > 0)
            {
                hr = pCaptureThreadParam->pCaptureClient[i]->GetBuffer(&pCaptureThreadParam->pData[i],
                                                pCaptureThreadParam->nEndpointBufferSize[i],
                                                pCaptureThreadParam->flags[i], NULL, NULL);
                    EXIT_ON_ERROR(hr)

                if (*pCaptureThreadParam->flags[i] & AUDCLNT_BUFFERFLAGS_SILENT)
                    pCaptureThreadParam->pData[i] = NULL;  // Tell PullData to write silence.

                hr = pCaptureThreadParam->pAudioBuffer[i]->PushData(pCaptureThreadParam->pData[i]);
                    EXIT_ON_ERROR(hr)

                hr = pCaptureThreadParam->pCaptureClient[i]->ReleaseBuffer(*pCaptureThreadParam->nEndpointBufferSize[i]);
                    EXIT_ON_ERROR(hr)
            }
        }
    }

    std::cout << MSG << "WASAPI capture thread exited." END << std::endl;

    return hr;

Exit:
    return hr;
}

DWORD WINAPI RenderThread(LPVOID lpParam)
{
    HRESULT hr = ERROR_SUCCESS;
    // Cast void pointer into familiar struct
    RENDERTHREADPARAM* pRenderThreadParam = (RENDERTHREADPARAM*)lpParam;

    // Creates UDP sockets for each UDPAudioBuffer
    for (UINT32 i = 0; i < pRenderThreadParam->nWASANNodes; i++)
        pRenderThreadParam->pUDPAudioBuffer[i]->SetSocketUDP(CreateSocketUDP());

    //-------- Render buffer data in a poll-based fashion
    while (!*pRenderThreadParam->bDone)
    {
        // Pushes data from ring buffer into corresponding devices
        for (UINT32 i = 0; i < pRenderThreadParam->nDevices; i++)
        {
            // Wait for the ring buffer corresponding to this channel-masked device to exceed the number of samples
            // needed to ensure SRC does not come short on original samples when filling output device's buffer
            if (pRenderThreadParam->pAudioBuffer[i]->FramesAvailable() >= pRenderThreadParam->pAudioBuffer[i]->GetMinFramesOut())
            {
                // Get the pointer from WASAPI where to write data to
                hr = pRenderThreadParam->pRenderClient[i]->GetBuffer(*pRenderThreadParam->nEndpointBufferSize[i], &pRenderThreadParam->pData[i]);
                    EXIT_ON_ERROR(hr)

                // Load data from AudioBuffer's ring buffer into the WASAPI buffer for this device
                hr = pRenderThreadParam->pAudioBuffer[i]->PullData(pRenderThreadParam->pData[i], *pRenderThreadParam->nEndpointBufferSize[i]);
                    EXIT_ON_ERROR(hr)

                // Release buffer before next packet
                hr = pRenderThreadParam->pRenderClient[i]->ReleaseBuffer(*pRenderThreadParam->nEndpointBufferSize[i], *pRenderThreadParam->flags[i]);
                    EXIT_ON_ERROR(hr)
            }
        }

        // Pushes data from ring buffer into corresponding WASAN render nodes over UDP
        for (UINT32 i = 0; i < pRenderThreadParam->nWASANNodes; i++)
        {
            UINT32 nFrames = pRenderThreadParam->pUDPAudioBuffer[i]->FramesAvailable();
            if (nFrames > 0)
            {
                // Get the lesser of the number of frames to write to the device
                nFrames = min(nFrames, *pRenderThreadParam->nEndpointBufferSize[pRenderThreadParam->nDevices + i]);

                // Load data from UDPAudioBuffer's ring buffer into the buffer for this device
                pRenderThreadParam->pUDPAudioBuffer[i]->SendDataUDP(nFrames);
            }
        }
    }

   
    // Destroy sockets after UDP clients are done pushing data to server
    for (UINT32 i = 0; i < pRenderThreadParam->nWASANNodes; i++)
        CloseSocketUDP(pRenderThreadParam->pUDPAudioBuffer[i]->GetSocketUDP());

    return hr;

Exit:
    return hr;
}

// Move AudioEffect thread creation back into the init function
DWORD WINAPI DSPThread(LPVOID lpParam)
{
    HRESULT hr = ERROR_SUCCESS;
    DSPTHREADPARAM* pDSPThreadParam = (DSPTHREADPARAM*)lpParam;

    std::cout << MSG "Starting interactive CLI and Audio Effect threads." END << std::endl;

    // Create an a separate thread for each AudioEffect
    HANDLE* hAudioEffectThread = (HANDLE*)malloc(pDSPThreadParam->nDevices * sizeof(HANDLE));
    DWORD* dwAudioEffectThreadId = (DWORD*)malloc(pDSPThreadParam->nDevices * sizeof(DWORD));
    AUDIOEFFECTTHREADPARAM* pAudioEffectThreadParam = (AUDIOEFFECTTHREADPARAM*)malloc(pDSPThreadParam->nDevices * sizeof(AUDIOEFFECTTHREADPARAM));
    
    if (hAudioEffectThread == NULL || dwAudioEffectThreadId == NULL || pAudioEffectThreadParam == NULL)
    {
        std::cout << ERR "Failed to allocate heap for Audio Effect threads." END << std::endl;

        hr = ENOMEM;
            EXIT_ON_ERROR(hr)
    }
    
    for (UINT32 i = 0; i < pDSPThreadParam->nDevices; i++)
    {
        pAudioEffectThreadParam[i].pAudioBuffer[AGGREGATOR_CAPTURE]->
        pAudioEffectThreadParam[i].pEffect = new Flanger(AGGREGATOR_SAMPLE_FREQ, )


        hAudioEffectThread[i] = CreateThread(NULL, 0, AudioEffectThread, (LPVOID)&pAudioEffectThreadParam[i], 0, &dwAudioEffectThreadId[i]);
        
        if (hAudioEffectThread[i] == NULL)
        {
            std::cout << ERR "Failed to create an Audio Effect thread." END << std::endl;

            hr = ERROR_SERVICE_NO_THREAD;
                EXIT_ON_ERROR(hr)
        }
    }

    std::cout << SUC "Succesfully created all Audio Effect threads." END << std::endl;

    

    std::cout << MSG "Waiting on Audio Effect threads." END << std::endl;

    WaitForMultipleObjects(pDSPThreadParam->nDevices, hAudioEffectThread, TRUE, INFINITE);
    for (UINT32 i = 0; i < pDSPThreadParam->nDevices; i++)
    {
        CloseHandle(hAudioEffectThread[i]);
        hAudioEffectThread[i] = NULL;
        dwAudioEffectThreadId[i] = NULL;
    }
    if (hAudioEffectThread != NULL) 
    { 
        free(hAudioEffectThread);
        hAudioEffectThread = NULL;
    }
    if (dwAudioEffectThreadId != NULL) 
    {
        free(dwAudioEffectThreadId);
        dwAudioEffectThreadId = NULL;
    }
    if (pAudioEffectThreadParam != NULL) 
    {
        free(pAudioEffectThreadParam);
        pAudioEffectThreadParam = NULL;
    }

    std::cout << SUC "Succesfully closed all Audio Effect threads." END << std::endl;

    return hr;

Exit:
    return hr;
}

DWORD WINAPI AudioEffectThread(LPVOID lpParam)
{
    HRESULT hr = ERROR_SUCCESS;
    AUDIOEFFECTTHREADPARAM* pAudioEffectThreadParam = (AUDIOEFFECTTHREADPARAM*)lpParam;
    UINT32 nFramesAvailable = 0;

    while (!*pAudioEffectThreadParam->bDone)
    {
        if ((nFramesAvailable = pAudioEffectThreadParam->pAudioBuffer[AGGREGATOR_CAPTURE]->FramesAvailable()) > 0)
        {
            // Read data from the input ring buffer into the audio effect
            pAudioEffectThreadParam->pAudioBuffer[AGGREGATOR_CAPTURE]->ReadNextPacket(pAudioEffectThreadParam->pEffect);
            // Write data from audio effect into the output ring buffer
            pAudioEffectThreadParam->pAudioBuffer[AGGREGATOR_RENDER]->WriteNextPacket(pAudioEffectThreadParam->pEffect);
        }
    }

    return hr;

Exit:
    return hr;
}
