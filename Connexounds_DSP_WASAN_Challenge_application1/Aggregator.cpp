/*
    TODO: 
        I. support dynamic updates to the aggregator like:
            1. Addition of capture devices
            2. Sample-rate conversion
            3. 
        II. verify all memory is released and free'd properly after refactoring and encapsulation
        III. multithread the aggregator so that each thread is responsible for eacg set of functionality
            a. Control lane for communication with JUCE/DSP
            b. Capture and pre-processing of data
*/

#include "Aggregator.h"

//---------- Windows macro definitions ----------------------------------------------//
static const CLSID     CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID       IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID       IID_IAudioClient = __uuidof(IAudioClient);
static const IID       IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

//#pragma comment(lib, "uuid.lib")

/// <summary>
/// <para>Aggregator constructor.</para>
/// <para>High level, clean interface to perform all the cumbersome setup
/// and negotiation with WASAPI, user parameters, etc.</para>
/// </summary>
Aggregator::Aggregator()
{
    HRESULT hr = S_OK;

    hr = CoInitialize(0);

    hr = CoCreateInstance(
            CLSID_MMDeviceEnumerator, NULL,
            CLSCTX_ALL, IID_IMMDeviceEnumerator,
            (void**)&pEnumerator);
        EXIT_ON_ERROR(hr);

    hr = GetUserCaptureDevices();
    
    Exit:
    
}

/// <summary>
/// <para>Aggregator destructor.</para>
/// <para>Frees all alloc'ed memory and cleans up underlying classes.</para>
/// </summary>
Aggregator::~Aggregator()
{
    // Release memory alloc'ed by Windows for capture device interfaces
    for (UINT32 i = 0; i < nAllCaptureEndpoints; i++)
        SAFE_RELEASE(pCaptureDeviceAll[i])

    // Release memory alloc'ed by Aggregator for capture device arrays
    free(pCaptureDeviceAll);
    free(pCaptureDevice);

//Exit:
//    printf("%d\n", hr);
//    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
//    {
//        CoTaskMemFree(pwfx[i]);
//        SAFE_RELEASE(pCaptureDevice[i])
//            SAFE_RELEASE(pAudioClient[i])
//            SAFE_RELEASE(pCaptureClient[i])
//    }
//
//    SAFE_RELEASE(pEnumerator)
//
//        for (UINT32 i = 0; i < nAggregatedChannels; i++)
//            if (pCircularBuffer[i] != NULL)
//                free(pCircularBuffer[i]);
//
//    if (pCircularBuffer != NULL) free(pCircularBuffer);
}

/// <summary>
/// <para></para>
/// <para>TODO:</para>
/// <para>TODO:</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::Initialize()
{
    HRESULT hr = S_OK;

    pAudioClient        = (IAudioClient**)malloc(nCaptureEndpoints * sizeof(IAudioClient*));
    pwfx                = (WAVEFORMATEX**)malloc(nCaptureEndpoints * sizeof(WAVEFORMATEX*));
    pAudioBuffer        = (AudioBuffer**)malloc(nCaptureEndpoints * sizeof(AudioBuffer*));
    pCaptureClient      = (IAudioCaptureClient**)malloc(nCaptureEndpoints * sizeof(IAudioCaptureClient*));
    pData               = (BYTE**)malloc(nCaptureEndpoints * sizeof(BYTE*));
    nGCD                = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    nGCDDiv             = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    nGCDTFreqDiv        = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    nUpsample           = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    nDownsample         = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    flags               = (DWORD*)malloc(nCaptureEndpoints * sizeof(DWORD));
    nEndpointBufferSize = (UINT32*)malloc(nCaptureEndpoints * sizeof(UINT32));
    nEndpointPackets    = (UINT32*)malloc(nCaptureEndpoints * sizeof(UINT32));

    if (pAudioClient == NULL ||
        pCaptureClient == NULL ||
        pAudioBuffer == NULL ||
        pData == NULL ||
        pwfx == NULL ||
        nGCD == NULL ||
        nGCDDiv == NULL ||
        nGCDTFreqDiv == NULL ||
        nUpsample == NULL ||
        nDownsample == NULL ||
        nEndpointBufferSize == NULL ||
        nEndpointPackets == NULL ||
        flags == NULL)
    {
        if (pAudioClient != NULL) free(pAudioClient);
        if (pCaptureClient != NULL) free(pCaptureClient);
        if (pAudioBuffer != NULL) free(pAudioBuffer);
        if (pData != NULL) free(pData);
        if (pwfx != NULL) free(pwfx);
        if (nGCD != NULL) free(nGCD);
        if (nGCDDiv != NULL) free(nGCDDiv);
        if (nGCDTFreqDiv != NULL) free(nGCDTFreqDiv);
        if (nUpsample != NULL) free(nUpsample);
        if (nDownsample != NULL) free(nDownsample);
        if (nEndpointBufferSize != NULL) free(nEndpointBufferSize);
        if (nEndpointPackets != NULL) free(nEndpointPackets);
        if (flags != NULL) free(flags);
        
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Activate capture devices as COM objects
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        /*hr = pEnumerator->GetDevice(pwszID[i], &pCaptureDevice[i]);
            EXIT_ON_ERROR(hr)*/

        hr = pCaptureDevice[i]->Activate(IID_IAudioClient, CLSCTX_ALL,
                                    NULL, (void**)&pAudioClient[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get format settings for both devices and initialize their client objects
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioClient[i]->GetMixFormat(&pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        nGCD[i] = gcd(pwfx[i]->nSamplesPerSec, AGGREGATOR_SAMPLE_FREQ);

        nGCDDiv[i] = pwfx[i]->nSamplesPerSec / nGCD[i];
        nGCDTFreqDiv[i] = AGGREGATOR_SAMPLE_FREQ / nGCD[i];

        if (pwfx[i]->nSamplesPerSec < AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[i] = max(nGCDDiv[i], nGCDTFreqDiv[i]);
            nDownsample[i] = min(nGCDDiv[i], nGCDTFreqDiv[i]);
        }
        else if (pwfx[i]->nSamplesPerSec > AGGREGATOR_SAMPLE_FREQ)
        {
            nUpsample[i] = min(nGCDDiv[i], nGCDTFreqDiv[i]);
            nDownsample[i] = max(nGCDDiv[i], nGCDTFreqDiv[i]);
        }
        else
        {
            nUpsample[i] = 1;
            nDownsample[i] = 1;
        }
    }

    //-------- Initialize streams to operate in callback mode
    // Allow WASAPI to choose endpoint buffer size, glitches otherwise
    // for both, event-driven and polling methods, outputs 448 frames for mic
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioClient[i]->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        0, 0,
                                        0, pwfx[i], NULL);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get the size of the actual allocated buffers and obtain capturing interfaces
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioClient[i]->GetBufferSize(&nEndpointBufferSize[i]);
            EXIT_ON_ERROR(hr)
        
        hr = pAudioClient[i]->GetService(IID_IAudioCaptureClient,
                                        (void**)&pCaptureClient[i]);
            EXIT_ON_ERROR(hr)
        printf("The %d-th buffer size is: %d\n", i, nEndpointBufferSize[i]);
    }

    //-------- Instantiate AudioBuffer object for each user-chosen capture device
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
        pAudioBuffer[i] = new AudioBuffer("Device " + i);

    //-------- Notify the audio sink which format to use
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioBuffer[i]->SetFormat(pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nCaptureEndpoints; i++) nAggregatedChannels += pwfx[i]->nChannels;
    
    pCircularBuffer = (FLOAT**)malloc(nAggregatedChannels * sizeof(FLOAT*));
    if (pCircularBuffer == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }
#ifdef DEBUG
    memset(pCircularBuffer, 1, nAggregatedChannels * sizeof(FLOAT*));
#endif

    for (UINT32 i = 0; i < nAggregatedChannels; i++)
    {
        pCircularBuffer[i] = (FLOAT*)malloc(nCircularBufferSize * sizeof(FLOAT));
        if (pCircularBuffer[i] == NULL)
        {
            for (UINT32 i = 0; i < nAggregatedChannels; i++)
                if (pCircularBuffer[i] != NULL) free(pCircularBuffer[i]);

            free(pCircularBuffer);

            hr = ENOMEM;
            goto Exit;
        }
#ifdef DEBUG
        memset(pCircularBuffer[i], 1, nCircularBufferSize * sizeof(FLOAT));
#endif
    }

    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioBuffer[i]->InitBuffer(&nEndpointBufferSize[i], pCircularBuffer,
                                    &nCircularBufferSize,
                                    nUpsample[i], nDownsample[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioBuffer[i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

    Exit:
    return hr;
}

/// <summary>
/// <para>Starts capturing audio from user-selected devices on a poll basis.</para>
/// <para>TODO: turn into a separate thread to provide additional functionality
/// without waiting for CPU or delaying processing of incoming data.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::Start()
{
    HRESULT hr = S_OK;

    //-------- Reset and start capturing
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioClient[i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[i]->Start();
            EXIT_ON_ERROR(hr)
    }
    
    std::cout << "starting capture" << std::endl;

    //-------- Capture endpoint buffer data in a poll-based fashion
    while (!bDone)
    {
        // Captures data from all devices
        for (UINT32 i = 0; i < nCaptureEndpoints; i++)
        {
            hr = pCaptureClient[i]->GetNextPacketSize(&nEndpointPackets[i]);
                EXIT_ON_ERROR(hr)

            if (nEndpointPackets[i] > 0)
            {
                hr = pCaptureClient[i]->GetBuffer(&pData[i],
                                                &nEndpointBufferSize[i],
                                                &flags[i], NULL, NULL);
                    EXIT_ON_ERROR(hr)

                if (flags[i] & AUDCLNT_BUFFERFLAGS_SILENT) 
                    pData[i] = NULL;  // Tell CopyData to write silence.

                hr = pAudioBuffer[i]->CopyData(pData[i], &bDone);
                    EXIT_ON_ERROR(hr)

                hr = pCaptureClient[i]->ReleaseBuffer(nEndpointBufferSize[i]);
                    EXIT_ON_ERROR(hr)
            }
        }
    }
    
    Exit:
    return hr;
}

/// <summary>
/// 
/// </summary>
/// <returns></returns>
HRESULT Aggregator::Stop()
{
    HRESULT hr = S_OK;
    bDone = TRUE;

    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
    {
        hr = pAudioClient[i]->Stop();  // Stop recording.
            EXIT_ON_ERROR(hr)
    }
    
    for (UINT32 i = 0; i < nCaptureEndpoints; i++)
        pAudioBuffer[i]->~AudioBuffer();
    
    Exit:
    return hr;
}

/// <summary>
/// <para>Pipes all active capture devices into console.</para> 
/// <para>Records each device into pCaptureDevice data structure to 
/// later retreive the desired ones based on user input.</para>
/// </summary>
/// <returns>
/// <para>S_OK if successful.</para>
/// <para>ENOMEM if memory (re)allocation for IMMDevice array fails.</para>
/// <para>IMMDevice/IMMDeviceCollection/IMMDeviceEnumerator/IPropertyStore
/// specific error otherwise.</para>
/// </returns>
HRESULT Aggregator::ListCaptureEndpoints()
{  
    HRESULT hr = S_OK;
    UINT32 nCount;
    LPWSTR pwszID;
    IPropertyStore* pProps;
    PROPVARIANT varName;
    PROPERTYKEY key;
    GUID IDevice_FriendlyName = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } };
    key.pid = 14;
    key.fmtid = IDevice_FriendlyName;

    //-------- Enumerate capture endpoints
    hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
        EXIT_ON_ERROR(hr)
    
    //-------- Count the number of available devices
    hr = pCollection->GetCount(&nCount);
        EXIT_ON_ERROR(hr)
    
    if (nCount == 0)
    {
        printf("No capture endpoints were detected\n");
        hr = RPC_S_NO_ENDPOINT_FOUND;
        goto Exit;
    }
    
    //-------- Alloc memory for the first capture device pointer
    nAllCaptureEndpoints = 1;
    pCaptureDeviceAll = (IMMDevice**)malloc(sizeof(IMMDevice*));
    if (pCaptureDeviceAll == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Go through the capture device list
    for (UINT32 i = 0; i < nCount; i++)
    {
        // Get next device from the list
        hr = pCollection->Item(i, &pCaptureDeviceAll[i]);
            EXIT_ON_ERROR(hr)
        
        // Get the device's ID
        hr = pCaptureDeviceAll[i]->GetId(&pwszID);
            EXIT_ON_ERROR(hr)

        // Get the device's properties
        hr = pCaptureDeviceAll[i]->OpenPropertyStore(STGM_READ, &pProps);
            EXIT_ON_ERROR(hr)
        
        // Initialize container for property value
        PropVariantInit(&varName);

        // Get the endpoint's friendly-name property
        hr = pProps->GetValue(key, &varName);
            EXIT_ON_ERROR(hr)

        // Print endpoint friendly name and endpoint ID
        printf("Endpoint %d: \"%S\" (%S)\n", i, varName.pwszVal, pwszID);

        // If there is another capture device after this one, alloc more memory
        if (i < nCount - 1)
        {
            IMMDevice** dummy = (IMMDevice**)realloc(pCaptureDeviceAll, ++nAllCaptureEndpoints * sizeof(IMMDevice*));
            if (dummy == NULL)
            {
                free(pCaptureDeviceAll);
                PropVariantClear(&varName);

                hr = ENOMEM;
                goto Exit;
            }
            else
                pCaptureDeviceAll = dummy;
        }

        // Release memory alloc'ed by Windows before next loop iteration
        CoTaskMemFree(pwszID);
        PropVariantClear(&varName);
            SAFE_RELEASE(pProps)
    }

    return hr;

    Exit:
    CoTaskMemFree(pwszID);
    CoTaskMemFree(pCollection);
        SAFE_RELEASE(pProps)

    return hr;
}

/// <summary>
/// <para>Prompts user to choose from devices available to the system.</para>
/// <para>Must be called after Aggregator::ListCaptureDevices.</para>
/// <para>TODO: incorporate more user input sanitization.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::GetUserCaptureDevices()
{   
    HRESULT hr = S_OK;
    UINT32 nUserChoice;
    BOOL bInSet, bUserDone = bInSet = FALSE;
    CHAR sInput[11]; // Char array for UINT32 (10 digit number) + \n
    
    IMMDevice** pCaptureDevice = (IMMDevice**)malloc(sizeof(IMMDevice*));
    if (pCaptureDevice == NULL)
    {
        hr = ENOMEM;
        return hr;
    }

    std::cout << "Select all desired out of available " << nAllCaptureEndpoints << " input devices." << std::endl;
    
    //-------- Prompt user to select an input device until they indicate they are done or until no more devices are left
    while (!bUserDone || nCaptureEndpoints < nAllCaptureEndpoints)
    {
        std::cout << "Choose next input device (#" << nCaptureEndpoints << ") or press [ENTER] to proceed." << std::endl;
        std::cin.get(sInput, 11);
        
        // If user chose at least 1 device and pressed ENTER
        if (sInput == "\n" && nCaptureEndpoints > 0) break;
        // If user attempts to proceed without choosing single device
        else if (sInput == "\n" && nCaptureEndpoints == 0)
        {
            std::cout << "You must choose at least 1 capture device." << std::endl;
            continue;
        }
        // If user entered a number
        // TODO: check if the input is actually a number (atoi is not safe if input is not an integer)
        else
        {
            nUserChoice = std::atoi(sInput);
            bInSet = FALSE;
            
            // Check if user input a number within the range of available device indices
            if (nUserChoice < 0 || nUserChoice > nAllCaptureEndpoints - 1)
            {
                std::cout << "You must pick one of existing devices, a number between 0 and " << nAllCaptureEndpoints - 1 << std::endl;
                continue;
            }

            // Check if user already chose this device
            for (UINT32 i = 0; i < nCaptureEndpoints; i++)
            {
                bInSet = pCaptureDevice[i] == pCaptureDeviceAll[nUserChoice];
                if (bInSet) break;
            }
            
            // Check if this device is already chosen
            if (bInSet)
            {
                std::cout << "You cannot choose the same device more than once." << std::endl;
                continue;
            }
            // If all is good, add the device into the list of devices to use for aggregator
            else
            {
                IMMDevice** dummy = (IMMDevice**)realloc(pCaptureDevice, ++nCaptureEndpoints * sizeof(IMMDevice*));
                if (dummy == NULL)
                {
                    free(pCaptureDevice);
                    hr = ENOMEM;
                    goto Exit;
                }
                else
                {
                    pCaptureDevice = dummy;
                    pCaptureDevice[nCaptureEndpoints] = pCaptureDeviceAll[nUserChoice];
                }
            }
        }
    }

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