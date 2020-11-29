/*
    TODO: 
        I. support dynamic updates to the aggregator like:
            1. Addition/removal of capture devices.
            2. Sample-rate conversion.
            3. Output channel mask changes.
            4. Circular buffer size.
        II. multithread the aggregator so that each thread is responsible for each set of functionality:
            a. Control lane for communication with JUCE/DSP.
            b. Capture and pre-processing of data.
        III. convert class into a thread-safe Singleton.
        IV. facilitate modularity for cross-platform portability.
        V. provide better device friendly names (i.e Max's Airpods, etc.)
*/

#include "Aggregator.h"

//---------- Windows macro definitions ----------------------------------------------//
static const CLSID     CLSID_MMDeviceEnumerator     = __uuidof(MMDeviceEnumerator);
static const IID       IID_IMMDeviceEnumerator      = __uuidof(IMMDeviceEnumerator);
static const IID       IID_IAudioClient             = __uuidof(IAudioClient);
static const IID       IID_IAudioCaptureClient      = __uuidof(IAudioCaptureClient);

//#pragma comment(lib, "uuid.lib")

/// <summary>
/// <para>Aggregator constructor.</para>
/// <para>High level, clean interface to perform all the cumbersome setup
/// and negotiation with WASAPI, user parameters, etc.</para>
/// <para>Note: not thread-safe, must be instantiated only once.</para>
/// <para>TODO: convert class into a thread-safe Singleton.</para>
/// </summary>
Aggregator::Aggregator()
{
    
}

/// <summary>
/// <para>Aggregator destructor.</para>
/// <para>Frees all alloc'ed memory and cleans up underlying classes.</para>
/// </summary>
Aggregator::~Aggregator()
{
    //-------- Release memory alloc'ed by Windows for capture device interfaces
    // Release each capture device interface
    if (pCaptureDeviceAll != NULL)
        for (UINT32 i = 0; i < nAllCaptureDevices; i++) 
            SAFE_RELEASE(pCaptureDeviceAll[i])
    
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        CoTaskMemFree(pwfx[i]);
        SAFE_RELEASE(pAudioClient[i])
        SAFE_RELEASE(pCaptureClient[i])
    }
    
    CoTaskMemFree(pCollection);
    SAFE_RELEASE(pEnumerator)

    //-------- Release memory alloc'ed by Aggregator
    // Destruct AudioBuffer objects and gracefully wrap up their work
    for (UINT32 i = 0; i < nCaptureDevices; i++)
        pAudioBuffer[i]->~AudioBuffer();

    // Release memory alloc'ed for ring buffer
    if (pCircularBuffer != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels; i++)
            if (pCircularBuffer[i] != NULL)
                free(pCircularBuffer[i]);

        free(pCircularBuffer);
    }
    // Set number of aggregated channels back to 0
    nAggregatedChannels = 0;

    // Free dynamic arrays holding reference to all these variables as last step
    if (pCaptureDeviceAll != NULL)      free(pCaptureDeviceAll);
    if (pCaptureDevice != NULL)         free(pCaptureDevice);
    if (pAudioClient != NULL)           free(pAudioClient);
    if (pCaptureClient != NULL)         free(pCaptureClient);
    if (pAudioBuffer != NULL)           free(pAudioBuffer);
    if (pData != NULL)                  free(pData);
    if (pwfx != NULL)                   free(pwfx);
    if (nGCD != NULL)                   free(nGCD);
    if (nGCDDiv != NULL)                free(nGCDDiv);
    if (nGCDTFreqDiv != NULL)           free(nGCDTFreqDiv);
    if (nUpsample != NULL)              free(nUpsample);
    if (nDownsample != NULL)            free(nDownsample);
    if (nEndpointBufferSize != NULL)    free(nEndpointBufferSize);
    if (nEndpointPackets != NULL)       free(nEndpointPackets);
    if (flags != NULL)                  free(flags);
}

/// <summary>
/// <para>Alloc's memory and instantiates WASAPI interface
/// to stream capture device data.</para>
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
    
    //-------- Try to list all available capture devices AGGREGATOR_OP_ATTEMPTS times
    do
    {
        hr = ListCaptureDevices();
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)

    //-------- Try to get user choice of capture devices AGGREGATOR_OP_ATTEMPTS times
    attempt = 0; // don't forget to reset the number of attempts
    do
    {
        hr = GetUserCaptureDevices();
    } while (hr != S_OK && ++attempt < AGGREGATOR_OP_ATTEMPTS);
        EXIT_ON_ERROR(hr)
    
    //-------- Use information obtained from user inputs to dynamically create the system
    pAudioClient        = (IAudioClient**)malloc(nCaptureDevices * sizeof(IAudioClient*));
    pwfx                = (WAVEFORMATEX**)malloc(nCaptureDevices * sizeof(WAVEFORMATEX*));
    pAudioBuffer        = (AudioBuffer**)malloc(nCaptureDevices * sizeof(AudioBuffer*));
    pCaptureClient      = (IAudioCaptureClient**)malloc(nCaptureDevices * sizeof(IAudioCaptureClient*));
    pData               = (BYTE**)malloc(nCaptureDevices * sizeof(BYTE*));
    nGCD                = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    nGCDDiv             = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    nGCDTFreqDiv        = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    nUpsample           = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    nDownsample         = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    flags               = (DWORD*)malloc(nCaptureDevices * sizeof(DWORD));
    nEndpointBufferSize = (UINT32*)malloc(nCaptureDevices * sizeof(UINT32));
    nEndpointPackets    = (UINT32*)malloc(nCaptureDevices * sizeof(UINT32));

    //-------- Check if allocation of any of the crucial variables failed, clean up and return with ENOMEM otherwise
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
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Activate capture devices as COM objects
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pCaptureDevice[i]->Activate(IID_IAudioClient, CLSCTX_ALL,
                                    NULL, (void**)&pAudioClient[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get format settings for both devices and initialize their client objects
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioClient[i]->GetMixFormat(&pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < nCaptureDevices; i++)
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
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioClient[i]->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        0, 0,
                                        0, pwfx[i], NULL);
            EXIT_ON_ERROR(hr)
    }

    std::cout << "<--------Capture Device Details-------->" << std::endl << std::endl;

    //-------- Get the size of the actual allocated buffers and obtain capturing interfaces
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioClient[i]->GetBufferSize(&nEndpointBufferSize[i]);
            EXIT_ON_ERROR(hr)
        
        hr = pAudioClient[i]->GetService(IID_IAudioCaptureClient,
                                        (void**)&pCaptureClient[i]);
            EXIT_ON_ERROR(hr)
        printf("[%d]:\nThe %d-th buffer size is: %d\n", i, i, nEndpointBufferSize[i]);
    }

    //-------- Instantiate AudioBuffer object for each user-chosen capture device
    for (UINT32 i = 0; i < nCaptureDevices; i++)
        pAudioBuffer[i] = new AudioBuffer("Device " + std::to_string(i) + " ");

    //-------- Notify the audio sink which format to use
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioBuffer[i]->SetFormat(pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < nCaptureDevices; i++) nAggregatedChannels += pwfx[i]->nChannels;
    
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
            hr = ENOMEM;
            goto Exit;
        }
#ifdef DEBUG
        memset(pCircularBuffer[i], 1, nCircularBufferSize * sizeof(FLOAT));
#endif
    }

    //-------- Initialize AudioBuffer objects' buffers using the obtained information
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioBuffer[i]->InitBuffer(&nEndpointBufferSize[i], pCircularBuffer,
                                    &nCircularBufferSize,
                                    nUpsample[i], nDownsample[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioBuffer[i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

    //-------- If initialization succeeded, return with S_OK
    return hr;

    //-------- If initialization failed at any of above steps, clean up memory prior to next attempt
    Exit:
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        CoTaskMemFree(pwfx[i]);
        SAFE_RELEASE(pAudioClient[i])
        SAFE_RELEASE(pCaptureClient[i])

        pAudioBuffer[i]->~AudioBuffer();
    }

    // Free circular buffer
    if (pCircularBuffer != NULL)
    {
        for (UINT32 i = 0; i < nAggregatedChannels; i++)
            if (pCircularBuffer[i] != NULL)
                free(pCircularBuffer[i]);

        free(pCircularBuffer);
    }
    // Set number of aggregated channels back to 0
    nAggregatedChannels = 0;

    // Free dynamic arrays holding reference to 
    if (pAudioClient != NULL)           free(pAudioClient);
    if (pCaptureClient != NULL)         free(pCaptureClient);
    if (pAudioBuffer != NULL)           free(pAudioBuffer);
    if (pData != NULL)                  free(pData);
    if (pwfx != NULL)                   free(pwfx);
    if (nGCD != NULL)                   free(nGCD);
    if (nGCDDiv != NULL)                free(nGCDDiv);
    if (nGCDTFreqDiv != NULL)           free(nGCDTFreqDiv);
    if (nUpsample != NULL)              free(nUpsample);
    if (nDownsample != NULL)            free(nDownsample);
    if (nEndpointBufferSize != NULL)    free(nEndpointBufferSize);
    if (nEndpointPackets != NULL)       free(nEndpointPackets);
    if (flags != NULL)                  free(flags);

    SAFE_RELEASE(pEnumerator)

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

    //-------- Reset and start capturing on all selected devices
    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioClient[i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[i]->Start();
            EXIT_ON_ERROR(hr)
    }
    
    std::cout << MSG "Starting audio capture." << std::endl;

    //-------- Capture endpoint buffer data in a poll-based fashion
    while (!bDone)
    {
        // Captures data from all devices
        for (UINT32 i = 0; i < nCaptureDevices; i++)
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
/// <para>Flags to each AudioBuffer to not request new frames from WASAPI
/// and stops each WASAPI stream.</para>
/// <para>Note: not thread-safe. Must add mutex on bDone.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::Stop()
{
    HRESULT hr = S_OK;
    bDone = TRUE;

    for (UINT32 i = 0; i < nCaptureDevices; i++)
    {
        hr = pAudioClient[i]->Stop();  // Stop recording.
            EXIT_ON_ERROR(hr)
    }
    std::cout << MSG "Stopped audio capture." << std::endl;
    
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
HRESULT Aggregator::ListCaptureDevices()
{  
    HRESULT hr = S_OK;
    LPWSTR pwszID = NULL;
    IPropertyStore* pProps = NULL;
    PROPVARIANT varName;

    //-------- Enumerate capture endpoints
    hr = pEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pCollection);
        EXIT_ON_ERROR(hr)
    
    //-------- Count the number of available devices
    hr = pCollection->GetCount(&nAllCaptureDevices);
        EXIT_ON_ERROR(hr)
    
    if (nAllCaptureDevices == 0)
    {
        printf(MSG "No capture endpoints were detected\n");
        hr = RPC_S_NO_ENDPOINT_FOUND;
        goto Exit;
    }
    
    //-------- Alloc memory for all the capture devices
    pCaptureDeviceAll = (IMMDevice**)malloc(nAllCaptureDevices * sizeof(IMMDevice*));
    if (pCaptureDeviceAll == NULL)
    {
        hr = ENOMEM;
        goto Exit;
    }

    //-------- Go through the capture device list, output to console and save reference to each device
    for (UINT32 i = 0; i < nAllCaptureDevices; i++)
    {
        // Get next device from the list
        hr = pCollection->Item(i, &pCaptureDeviceAll[i]);
            EXIT_ON_ERROR(hr)
        
        // Get the device's ID
        hr = pCaptureDeviceAll[i]->GetId(&pwszID);
            EXIT_ON_ERROR(hr)

        // Get the device's properties in read-only mode
        hr = pCaptureDeviceAll[i]->OpenPropertyStore(STGM_READ, &pProps);
            EXIT_ON_ERROR(hr)
        
        // Initialize container for property value
        PropVariantInit(&varName);
        
        // Get the endpoint's friendly-name property
        hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
            EXIT_ON_ERROR(hr)

        // Print endpoint friendly name and endpoint ID
        printf("Capture Device #%d: \"%S\" (%S)\n", i, varName.pwszVal, pwszID);

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
    if (pCaptureDeviceAll != NULL)
    {
        for (UINT32 i = 0; i < nAllCaptureDevices; i++)
            SAFE_RELEASE(pCaptureDeviceAll[i])

        free(pCaptureDeviceAll);
    }

    CoTaskMemFree(pwszID);
    PropVariantClear(&varName);
    CoTaskMemFree(pCollection);
    SAFE_RELEASE(pProps)

    return hr;
}

/// <summary>
/// <para>Prompts user to choose from devices available to the system.</para>
/// <para>Must be called after Aggregator::ListCaptureDevices.</para>
/// <para>TODO: incorporate more user input verification.</para>
/// </summary>
/// <returns></returns>
HRESULT Aggregator::GetUserCaptureDevices()
{   
    HRESULT hr = S_OK;
    UINT32 nUserChoice;
    BOOL bInSet, bUserDone = bInSet = FALSE;
    CHAR sInput[11]; // Char array for UINT32 (10 digit number) + \n
    
    //-------- Allocate memory for the first user-chosen capture device
    pCaptureDevice = (IMMDevice**)malloc(sizeof(IMMDevice*));
    if (pCaptureDevice == NULL)
    {
        hr = ENOMEM;
        return hr;
    }

    std::cout << MSG "Select all desired out of available " << nAllCaptureDevices << " input devices." << std::endl;
    
    //-------- Prompt user to select an input device until they indicate they are done or until no more devices are left
    while (!bUserDone && nCaptureDevices < nAllCaptureDevices)
    {
        std::cout   << MSG "Choose next input device (currently selected " 
                    << nCaptureDevices << " devices) or press [ENTER] to proceed." 
                    << std::endl;

        std::cin.get(sInput, 11);
        std::string str(sInput);

        // Skip cin to next line to accept another input on next loop iteration
        std::cin.clear();
        std::cin.ignore(11, '\n');

        // If user chose at least 1 device and pressed ENTER
        if (str.length() == 0 && nCaptureDevices > 0) break;
        // If user attempts to proceed without choosing single device
        else if (str.length() == 0 && nCaptureDevices == 0)
        {
            std::cout << WARN "You must choose at least 1 capture device." << std::endl;
            continue;
        }
        // If user entered a number
        // TODO: check if the input is actually a number (atoi is not safe if input is not an integer)
        else
        {
            nUserChoice = std::atoi(sInput);
            bInSet = FALSE;
            
            // Check if user input a number within the range of available device indices
            if (nUserChoice < 0 || nUserChoice > nAllCaptureDevices - 1)
            {
                std::cout << WARN "You must pick one of existing devices, a number between 0 and " << nAllCaptureDevices - 1 << std::endl;
                continue;
            }

            // Check if user already chose this device
            for (UINT32 i = 0; i < nCaptureDevices; i++)
            {
                bInSet = pCaptureDevice[i] == pCaptureDeviceAll[nUserChoice];
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
                IMMDevice** dummy = (IMMDevice**)realloc(pCaptureDevice, ++nCaptureDevices * sizeof(IMMDevice*));
                if (dummy == NULL)
                {
                    free(pCaptureDevice);
                    hr = ENOMEM;
                    goto Exit;
                }
                else
                {
                    pCaptureDevice = dummy;
                    pCaptureDevice[nCaptureDevices - 1] = pCaptureDeviceAll[nUserChoice];
                }
            }
        }
    }
    std::cout << MSG << nCaptureDevices << " devices selected." << std::endl << std::endl;

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