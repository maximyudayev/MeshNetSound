#include <windows.h>
#include <iostream>
#include <fstream>
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <stdio.h>
#include <memory>

#include <system_error>
#include <Functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <initguid.h>  // Put this in to get rid of linker errors.
#include <devpkey.h>  // Property keys defined here are now defined inline.
#include <propsys.h>

#include "AudioBuffer.h"
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

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000
#define ENDPOINT_TIMEOUT_MILLISEC 2000
#define NUM_ENDPOINTS 2

// In the future must be ideally made into dynamic controls or a CLI arguments
#define DSP_BUFFER_LEN 2048
#define DSP_SAMPLE_FREQ 44100

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

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
/// <para>Entrypoint of the program.</para>
/// <para>TODO: remove duplicate code, facilitate arbitrary device number connection.</para>
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int main(int argc, char* argv[])
{
    DWORD MicCaptureThreadID, BTCaptureThreadID;
    HANDLE hEvent[NUM_ENDPOINTS];

    AudioBuffer* micBuffer = new AudioBuffer("Mic capture");
    AudioBuffer* BTBuffer = new AudioBuffer("BT capture");

    IMMDeviceCollection* pCollection = listAudioEndpoints();

    LPWSTR pwszIDMic = GetIDpEndpoint(pCollection, "internal microphone");
    LPWSTR pwszIDBT = GetIDpEndpoint(pCollection, "Bluetooth headset");

    std::cout << "Starting capture thread" << endl;
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDurationMic = REFTIMES_PER_SEC, hnsRequestedDurationBT = REFTIMES_PER_SEC;
    UINT32 bufferFrameCountMic, bufferFrameCountBT;
    UINT32 numFramesAvailableMic, numFramesAvailableBT;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDeviceMic = NULL, * pDeviceBT = NULL;
    IAudioClient* pAudioClientMic = NULL, * pAudioClientBT = NULL;
    IAudioCaptureClient* pCaptureClientMic = NULL, * pCaptureClientBT = NULL;
    WAVEFORMATEX* pwfxMic = NULL, * pwfxBT = NULL;
    UINT32 packetLengthMic = 0, packetLengthBT = 0;
    BOOL bDone = FALSE;
    BYTE* pDataMic, * pDataBT;
    DWORD flagsMic, flagsBT;
    DWORD nUpsampleMic, nDownsampleMic, nUpsampleBT, nDownsampleBT;
    DWORD gcdMic, gcdMicDiv, gcdMicTFreqDiv, gcdBT, gcdBTDiv, gcdBTTFreqDiv;

    hr = CoInitialize(0);

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
        EXIT_ON_ERROR(hr)

    //-------- Obtain IMM device from its id + activate both devices as COM objects
    hr = pEnumerator->GetDevice(pwszIDMic, &pDeviceMic);
        EXIT_ON_ERROR(hr)

    hr = pEnumerator->GetDevice(pwszIDBT, &pDeviceBT);
        EXIT_ON_ERROR(hr)

    hr = pDeviceMic->Activate(IID_IAudioClient, CLSCTX_ALL,
            NULL, (void**)&pAudioClientMic);
        EXIT_ON_ERROR(hr)

    hr = pDeviceBT->Activate(IID_IAudioClient, CLSCTX_ALL,
            NULL, (void**)&pAudioClientBT);
        EXIT_ON_ERROR(hr)

    //-------- Get format settings for both devices and initialize their client objects
    hr = pAudioClientMic->GetMixFormat(&pwfxMic);
        EXIT_ON_ERROR(hr)
    
    hr = pAudioClientBT->GetMixFormat(&pwfxBT);
        EXIT_ON_ERROR(hr)

    //-------- Calculate the period of each AudioClient buffer based on user's desired DSP buffer length
    gcdMic = gcd(pwfxMic->nSamplesPerSec, DSP_SAMPLE_FREQ);
    gcdBT = gcd(pwfxBT->nSamplesPerSec, DSP_SAMPLE_FREQ);

    gcdMicDiv = pwfxMic->nSamplesPerSec / gcdMic;
    gcdMicTFreqDiv = DSP_SAMPLE_FREQ / gcdMic;

    gcdBTDiv = pwfxBT->nSamplesPerSec / gcdBT;
    gcdBTTFreqDiv = DSP_SAMPLE_FREQ / gcdBT;
    
    if (pwfxMic->nSamplesPerSec < DSP_SAMPLE_FREQ)
    {
        nUpsampleMic = max(gcdMicDiv, gcdMicTFreqDiv);
        nDownsampleMic = min(gcdMicDiv, gcdMicTFreqDiv);
    }
    else if (pwfxMic->nSamplesPerSec > DSP_SAMPLE_FREQ)
    {
        nUpsampleMic = min(gcdMicDiv, gcdMicTFreqDiv);
        nDownsampleMic = max(gcdMicDiv, gcdMicTFreqDiv);
    }
    else
    {
        nUpsampleMic = 1;
        nDownsampleMic = 1;
    }

    if (pwfxBT->nSamplesPerSec < DSP_SAMPLE_FREQ)
    {
        nUpsampleBT = max(gcdBTDiv, gcdBTTFreqDiv);
        nDownsampleBT = min(gcdBTDiv, gcdBTTFreqDiv);
    }
    else if (pwfxBT->nSamplesPerSec > DSP_SAMPLE_FREQ)
    {
        nUpsampleBT = min(gcdBTDiv, gcdBTTFreqDiv);
        nDownsampleBT = max(gcdBTDiv, gcdBTTFreqDiv);
    }
    else 
    {
        nUpsampleBT = 1;
        nDownsampleBT = 1;
    }

    // Optional logic if user wants to have DSP buffer of fixed length
    //hnsRequestedDurationMic = DSP_BUFFER_LEN * nDownsampleMic / nUpsampleMic / pwfxMic->nSamplesPerSec * REFTIMES_PER_SEC;
    //hnsRequestedDurationBT = DSP_BUFFER_LEN * nDownsampleBT / nUpsampleBT / pwfxBT->nSamplesPerSec * REFTIMES_PER_SEC;

    //-------- Initialize streams to operate in callback mode
    hr = pAudioClientMic->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hnsRequestedDurationMic,
            0, pwfxMic, NULL);
        EXIT_ON_ERROR(hr)
    
    hr = pAudioClientBT->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hnsRequestedDurationBT,
            0, pwfxBT, NULL);
        EXIT_ON_ERROR(hr)

    //-------- Create event handles and register for buffer-event notifications
    for (UINT8 i = 0; i < NUM_ENDPOINTS; i++)
    {
        hEvent[i] = CreateEvent(NULL, FALSE, FALSE, NULL);

        // Terminate the program if handle creation failed
        if (hEvent[i] == NULL)
        {
            hr = E_FAIL;
            goto Exit;
        }
    }
    
    hr = pAudioClientMic->SetEventHandle(hEvent[0]);
        EXIT_ON_ERROR(hr)
    
    hr = pAudioClientBT->SetEventHandle(hEvent[1]);
        EXIT_ON_ERROR(hr)

    //-------- Get the size of the actual allocated buffers and obtain capturing interfaces
    hr = pAudioClientMic->GetBufferSize(&bufferFrameCountMic);
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->GetBufferSize(&bufferFrameCountBT);
        EXIT_ON_ERROR(hr)

    hr = pAudioClientMic->GetService(IID_IAudioCaptureClient, 
                                    (void**)&pCaptureClientMic);
        EXIT_ON_ERROR(hr)
    printf("The mic buffer size is: %d\n", bufferFrameCountMic);

    hr = pAudioClientBT->GetService(IID_IAudioCaptureClient, 
                                    (void**)&pCaptureClientBT);
        EXIT_ON_ERROR(hr)
    printf("The BT buffer size is: %d\n", bufferFrameCountBT);

    //-------- Notify the audio sink which format to use
    hr = micBuffer->SetFormat(pwfxMic);
        EXIT_ON_ERROR(hr)

    hr = BTBuffer->SetFormat(pwfxBT);
        EXIT_ON_ERROR(hr)

    //-------- Set data structure size for channelwise audio storage
    micBuffer->SetBufferSize(&bufferFrameCountMic);
    BTBuffer->SetBufferSize(&bufferFrameCountBT);

    //-------- Write captured data into a WAV file for debugging
    micBuffer->WriteWAV();
    BTBuffer->WriteWAV();

    //-------- Reset and start capturing
    hr = pAudioClientMic->Reset();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->Reset();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientMic->Start();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->Start();
        EXIT_ON_ERROR(hr)
    
    // Captures endpoint buffer data in an event-based fashion
    // In next revisions, abstract from the number of endpoints to avoid duplication of logic below
    while (!bDone)
    {
        // Wait for the buffer fill event to trigger for both endpoints or until timeout
        DWORD retval = WaitForMultipleObjects(NUM_ENDPOINTS, hEvent, TRUE, ENDPOINT_TIMEOUT_MILLISEC);


        // Capture of mic data
        hr = pCaptureClientMic->GetBuffer(&pDataMic,
                                        &bufferFrameCountMic,
                                        &flagsMic, NULL, NULL);
            EXIT_ON_ERROR(hr)

        if (flagsMic & AUDCLNT_BUFFERFLAGS_SILENT) pDataMic = NULL;  // Tell CopyData to write silence.

        hr = micBuffer->CopyData(pDataMic, bufferFrameCountMic, &bDone);
            EXIT_ON_ERROR(hr)

        hr = pCaptureClientMic->ReleaseBuffer(bufferFrameCountMic);
            EXIT_ON_ERROR(hr)


        // Capture of BT data
        hr = pCaptureClientBT->GetBuffer(&pDataBT,
                                        &bufferFrameCountBT,
                                        &flagsBT, NULL, NULL);
            EXIT_ON_ERROR(hr)

        if (flagsBT & AUDCLNT_BUFFERFLAGS_SILENT) pDataBT = NULL;  // Tell CopyData to write silence.

        hr = BTBuffer->CopyData(pDataBT, bufferFrameCountBT, &bDone);
            EXIT_ON_ERROR(hr)

        hr = pCaptureClientBT->ReleaseBuffer(bufferFrameCountBT);
            EXIT_ON_ERROR(hr)
    }

    hr = pAudioClientMic->Stop();  // Stop recording.
        EXIT_ON_ERROR(hr)
    hr = pAudioClientBT->Stop();  // Stop recording.
        EXIT_ON_ERROR(hr)

    micBuffer->~AudioBuffer();
    BTBuffer->~AudioBuffer();

    Exit:
    printf("%d\n", hr);
    for (UINT8 i = 0; i < NUM_ENDPOINTS; i++)
        if (hEvent[i] != NULL) CloseHandle(hEvent[i]);
    CoTaskMemFree(pwfxMic);
    CoTaskMemFree(pwfxBT);
        SAFE_RELEASE(pEnumerator)
        SAFE_RELEASE(pDeviceMic)
        SAFE_RELEASE(pAudioClientMic)
        SAFE_RELEASE(pCaptureClientMic)
        SAFE_RELEASE(pDeviceBT)
        SAFE_RELEASE(pAudioClientBT)
        SAFE_RELEASE(pCaptureClientBT)

    return hr;

    //---------------------------------------------------------------------------------

    SAFE_RELEASE(pCollection)

    return 0;
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