#include <windows.h>
#include <iostream>
#include <fstream>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <stdio.h>
#include <memory>
#include <Audioclient.h>

#include <system_error>
#include <Functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <initguid.h>  // Put this in to get rid of linker errors.
#include <devpkey.h>  // Property keys defined here are now defined inline.
#include <propsys.h>

#include "AudioBuffer.h"
#include "config.h"
using namespace std;

//--------------------- function prototypes---------------------------------//

IMMDeviceCollection* listAudioEndpoints();
LPWSTR GetIDpEndpoint(IMMDeviceCollection* pCollection, string device);
DWORD gcd(DWORD a, DWORD b);

struct threadInfo
{
    threadInfo(AudioBuffer* buffer, LPWSTR id, INT32 length)
    {
        audiobuffer = buffer;
        pwszID = id;
        packetLengthCompare = length;
    }

    AudioBuffer* audiobuffer;
    LPWSTR pwszID;
    INT32 packetLengthCompare;

    ~threadInfo() {
        delete(audiobuffer);
    }
};

//---------- Windows macro definitions ----------------------------------------------//

const CLSID     CLSID_MMDeviceEnumerator    = __uuidof(MMDeviceEnumerator);
const IID       IID_IMMDeviceEnumerator     = __uuidof(IMMDeviceEnumerator);
const IID       IID_IAudioClient            = __uuidof(IAudioClient);
const IID       IID_IAudioCaptureClient     = __uuidof(IAudioCaptureClient);

#pragma comment(lib, "uuid.lib")

//----------------------------------------------------------------------------------//
/*

DWORD WINAPI MicCaptureThread(LPVOID lpParameter)
{


}

DWORD WINAPI BTCaptureThread(LPVOID lpParameter)
{

}
*/

//--------MAIN THREAD--------------------------------------------------------------------//

/// <summary>
/// <para>Entry point of the program.</para>
/// <para>TODO: facilitate arbitrary device number connection.</para>
/// <para>TODO: create AudioBuffer objects based on users CLI device choice
/// instead of hardcoding.</para>
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int main(int argc, char* argv[])
{
    std::cout << "Starting capture thread" << endl;

    AudioBuffer*            audioBuffer[]                       = { new AudioBuffer("Mic capture"), 
                                                                    new AudioBuffer("BT capture") };

    FLOAT**                 pCircularBuffer                     = NULL;

    IMMDeviceCollection*    pCollection                         = listAudioEndpoints();

    LPWSTR                  pwszID[]                            = { GetIDpEndpoint(pCollection, "internal microphone"),
                                                                    GetIDpEndpoint(pCollection, "Bluetooth headset") };
    
    HRESULT                 hr;
    HANDLE                  hEvent[NUM_ENDPOINTS];

    IMMDeviceEnumerator*    pEnumerator                         = NULL;
    IMMDevice*              pDevice[NUM_ENDPOINTS]              = { NULL };
    IAudioClient*           pAudioClient[NUM_ENDPOINTS]         = { NULL };
    IAudioCaptureClient*    pCaptureClient[NUM_ENDPOINTS]       = { NULL };
    WAVEFORMATEX*           pwfx[NUM_ENDPOINTS]                 = { NULL };
    
    BOOL                    bDone                               = FALSE;
    
    BYTE*                   pData[NUM_ENDPOINTS]                = { NULL };

    UINT32                  nEndpointBufferSize[NUM_ENDPOINTS]  = { 0 },
                            nEndpointPackets[NUM_ENDPOINTS]     = { 0 },
                            nAggregatedChannels                 = 0,
                            nCircularBufferSize                 = AGGREGATOR_CIRCULAR_BUFFER_SIZE;

    DWORD                   flags[NUM_ENDPOINTS],
                            nUpsample[NUM_ENDPOINTS], 
                            nDownsample[NUM_ENDPOINTS],
                            nGCD[NUM_ENDPOINTS], 
                            nGCDDiv[NUM_ENDPOINTS],
                            nGCDTFreqDiv[NUM_ENDPOINTS],
                            iCaptureThread[NUM_ENDPOINTS];


    hr = CoInitialize(0);

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
        EXIT_ON_ERROR(hr)

    //-------- Obtain IMM device from its id + activate both devices as COM objects
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pEnumerator->GetDevice(pwszID[i], &pDevice[i]);
            EXIT_ON_ERROR(hr)

        hr = pDevice[i]->Activate(IID_IAudioClient, CLSCTX_ALL,
                                    NULL, (void**)&pAudioClient[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Get format settings for both devices and initialize their client objects
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pAudioClient[i]->GetMixFormat(&pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
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
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pAudioClient[i]->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        0, 0,
                                        0, pwfx[i], NULL);
            EXIT_ON_ERROR(hr)
    }

    //-------- Create event handles and register for buffer-event notifications
    //for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    //{
    //    hEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

    //    // Terminate the program if handle creation failed
    //    if (hEvent[i] == NULL)
    //    {
    //        hr = E_FAIL;
    //        goto Exit;
    //    }
    //}

    ////-------- Set buffer filled event handle
    //for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    //{
    //    hr = pAudioClient[i]->SetEventHandle(hEvent[i]);
    //        EXIT_ON_ERROR(hr)
    //}

    //-------- Get the size of the actual allocated buffers and obtain capturing interfaces
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pAudioClient[i]->GetBufferSize(&nEndpointBufferSize[i]);
            EXIT_ON_ERROR(hr)
        
        hr = pAudioClient[i]->GetService(IID_IAudioCaptureClient,
                                        (void**)&pCaptureClient[i]);
            EXIT_ON_ERROR(hr)
        printf("The %d-th buffer size is: %d\n", i, nEndpointBufferSize[i]);
    }

    //-------- Notify the audio sink which format to use
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = audioBuffer[i]->SetFormat(pwfx[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Set data structure size for channelwise audio storage
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++) nAggregatedChannels += pwfx[i]->nChannels;
    
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

    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = audioBuffer[i]->InitBuffer(&nEndpointBufferSize[i], pCircularBuffer,
                                    &nCircularBufferSize,
                                    nUpsample[i], nDownsample[i]);
            EXIT_ON_ERROR(hr)
    }

    //-------- Write captured data into a WAV file for debugging
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = audioBuffer[i]->InitWAV();
            EXIT_ON_ERROR(hr)
    }

    //-------- Reset and start capturing
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pAudioClient[i]->Reset();
            EXIT_ON_ERROR(hr)

        hr = pAudioClient[i]->Start();
            EXIT_ON_ERROR(hr)
    }
    
    cout << "starting capture" << endl;

    // Captures endpoint buffer data in an event-based fashion
    // In next revisions, abstract from the number of endpoints to avoid duplication of logic below
    while (!bDone)
    {
        // Wait for the buffer fill event to trigger for both endpoints or until timeout
        /*DWORD retval = WaitForMultipleObjects(NUM_ENDPOINTS, hEvent, TRUE, ENDPOINT_TIMEOUT_MILLISEC);*/

        // Capture data from all devices
        // TODO: split between multiple threads/cores
        for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
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

                hr = audioBuffer[i]->CopyData(pData[i], &bDone);
                    EXIT_ON_ERROR(hr)

                hr = pCaptureClient[i]->ReleaseBuffer(nEndpointBufferSize[i]);
                    EXIT_ON_ERROR(hr)
            }
        }
    }
    
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hr = pAudioClient[i]->Stop();  // Stop recording.
            EXIT_ON_ERROR(hr)
    }
    
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
        audioBuffer[i]->~AudioBuffer();

    Exit:
    printf("%d\n", hr);
    for (UINT32 i = 0; i < NUM_ENDPOINTS; i++)
    {
        /*if (hEvent[i] != NULL)
            CloseHandle(hEvent[i]);*/

        CoTaskMemFree(pwfx[i]);
        SAFE_RELEASE(pDevice[i])
        SAFE_RELEASE(pAudioClient[i])
        SAFE_RELEASE(pCaptureClient[i])
    }
    
    SAFE_RELEASE(pEnumerator)
    SAFE_RELEASE(pCollection)
    
    for (UINT32 i = 0; i < nAggregatedChannels; i++)
        if (pCircularBuffer[i] != NULL) 
            free(pCircularBuffer[i]);

    if (pCircularBuffer != NULL) free(pCircularBuffer);

    return hr;
}

IMMDeviceCollection* listAudioEndpoints()
{
    HRESULT hr = S_OK;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDeviceCollection* pCollection = NULL;
    IMMDevice* pEndpoint = NULL;
    IPropertyStore* pProps = NULL;
    LPWSTR pwszID = NULL;
    DWORD* pdwState = NULL;

    hr = CoInitialize(0);

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
        EXIT_ON_ERROR(hr);

    hr = pEnumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE /*| DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED*/, &pCollection);

    UINT count;
    hr = pCollection->GetCount(&count);

    if (count == 0) {
        printf("no endpoints were detected");
    }
    //otherwise go through the list
    for (ULONG nr = 0; nr < count; nr++)
    {
        hr = pCollection->Item(nr, &pEndpoint);

        hr = pEndpoint->GetId(&pwszID);
            EXIT_ON_ERROR(hr)

        hr = pEndpoint->OpenPropertyStore(
            STGM_READ, &pProps);

            EXIT_ON_ERROR(hr)

        static PROPERTYKEY key;

        GUID IDevice_FriendlyName = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } };
        key.pid = 14;
        key.fmtid = IDevice_FriendlyName;
        PROPVARIANT varName;
        
        // Initialize container for property value.
        PropVariantInit(&varName);

        // Get the endpoint's friendly-name property.
        hr = pProps->GetValue(
            key, &varName);
            EXIT_ON_ERROR(hr)

        // Print endpoint friendly name and endpoint ID.
        printf("Endpoint %d: \"%S\" (%S)\n",
            nr, varName.pwszVal, pwszID);

        CoTaskMemFree(pwszID);
        pwszID = NULL;
        PropVariantClear(&varName);
            SAFE_RELEASE(pProps)
            SAFE_RELEASE(pEndpoint)
    }

        SAFE_RELEASE(pEnumerator)

    Exit:
    CoTaskMemFree(pwszID);
        SAFE_RELEASE(pEnumerator)
        SAFE_RELEASE(pProps)

    return pCollection;
}

LPWSTR GetIDpEndpoint(IMMDeviceCollection* pCollection, string device)
{
    IMMDevice* pEndpoint = NULL;
    LPWSTR pwszID = NULL;
    HRESULT hr;
    ULONG number;

    std::cout << "Select " << device << " as input device " << endl;
    std::cin >> number;

    hr = pCollection->Item(number, &pEndpoint);
    hr = pEndpoint->GetId(&pwszID);

        SAFE_RELEASE(pEndpoint)

    return pwszID;
}

/// <summary>
/// <para>Finds Greatest Common Divisor of two integers.</para>
/// </summary>
/// <param name="a">- first integer</param>
/// <param name="b">- second integer</param>
/// <returns>The greatest common devisor of "a" and "b"</returns>
DWORD gcd(DWORD a, DWORD b)
{
    if (b == 0) return a;
    return gcd(b, a % b);
}