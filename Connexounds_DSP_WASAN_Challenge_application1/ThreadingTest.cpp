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

int main(int argc, char* argv[])
{
    DWORD MicCaptureThreadID, BTCaptureThreadID;

    AudioBuffer* micBuffer = new AudioBuffer("Mic capture");
    AudioBuffer* BTBuffer = new AudioBuffer("BT capture");

    IMMDeviceCollection* pCollection = listAudioEndpoints();

    LPWSTR pwszIDMic = GetIDpEndpoint(pCollection, "internal microphone");
    LPWSTR pwszIDBT = GetIDpEndpoint(pCollection, "Bluetooth headset");

    std::cout << "Starting capture thread" << endl;
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDurationMic = REFTIMES_PER_SEC, hnsRequestedDurationBT = REFTIMES_PER_SEC;
    REFERENCE_TIME hnsActualDurationMic, hnsActualDurationBT;
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
    UINT64 DeviceCapturepositionMic = 0, LastDeviceCapturepositionMic = 0;

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

    //--------- Get format settings for both devices and initialize their client objects
    hr = pAudioClientMic->GetMixFormat(&pwfxMic);
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->GetMixFormat(&pwfxBT);
        EXIT_ON_ERROR(hr)

    hr = pAudioClientMic->Initialize(AUDCLNT_SHAREMODE_SHARED,
            0, hnsRequestedDurationMic,
            0, pwfxMic, NULL);
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->Initialize(AUDCLNT_SHAREMODE_SHARED,
            0, hnsRequestedDurationBT,
            0, pwfxBT, NULL);
        EXIT_ON_ERROR(hr)

    //-------- Get the size of the allocated buffers and obtain capturing interfaces.
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

        //-------- Notify the audio sink which format to use.
    hr = micBuffer->SetFormat(pwfxMic);
        EXIT_ON_ERROR(hr)

    hr = BTBuffer->SetFormat(pwfxBT);
        EXIT_ON_ERROR(hr)

    micBuffer->SetBufferSize(&bufferFrameCountMic);
    BTBuffer->SetBufferSize(&bufferFrameCountBT);

    micBuffer->WriteWAV();
    BTBuffer->WriteWAV();

    hr = pAudioClientMic->Reset();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->Reset();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientMic->Start();
        EXIT_ON_ERROR(hr)

    hr = pAudioClientBT->Start();
        EXIT_ON_ERROR(hr)

    while (!bDone)
    {
        hr = pCaptureClientMic->GetNextPacketSize(&packetLengthMic);
            EXIT_ON_ERROR(hr)
        hr = pCaptureClientBT->GetNextPacketSize(&packetLengthBT);
            EXIT_ON_ERROR(hr)

        LastDeviceCapturepositionMic = DeviceCapturepositionMic;

        if (packetLengthBT > 0)
        {
            hr = pCaptureClientBT->GetBuffer(&pDataBT,
                &numFramesAvailableBT,
                &flagsBT, NULL, NULL);
                EXIT_ON_ERROR(hr)

            if (flagsBT & AUDCLNT_BUFFERFLAGS_SILENT) pDataBT = NULL;  // Tell CopyData to write silence.

            hr = BTBuffer->CopyData(pDataBT, numFramesAvailableBT, &bDone);
                EXIT_ON_ERROR(hr)

            hr = pCaptureClientBT->ReleaseBuffer(numFramesAvailableBT);
                EXIT_ON_ERROR(hr)
        }

        if (packetLengthMic > 0)
        {
            hr = pCaptureClientMic->GetBuffer(&pDataMic,
                &numFramesAvailableMic,
                &flagsMic, NULL, NULL);
                EXIT_ON_ERROR(hr)

            if (flagsMic & AUDCLNT_BUFFERFLAGS_SILENT) pDataMic = NULL;  // Tell CopyData to write silence.

            hr = micBuffer->CopyData(pDataMic, numFramesAvailableMic, &bDone);
                EXIT_ON_ERROR(hr)

            hr = pCaptureClientMic->ReleaseBuffer(numFramesAvailableMic);
                EXIT_ON_ERROR(hr)
            
            // Get the available data in the shared buffer.
            /*hr = pCaptureClientMic->GetBuffer(&pDataMic,
                &numFramesAvailableMic,
                &flagsMic, &DeviceCapturepositionMic, NULL);
            EXIT_ON_ERROR(hr)*/

            // Copy the available capture data to the audio sink.
                /*  hr = micBuffer->CopyData(pDataMic, numFramesAvailableMic, &bDone);
            EXIT_ON_ERROR(hr)

                hr = pCaptureClientMic->ReleaseBuffer(numFramesAvailableMic);
            EXIT_ON_ERROR(hr)*/
        }
    }

    hr = pAudioClientMic->Stop();  // Stop recording.
        EXIT_ON_ERROR(hr)
    hr = pAudioClientBT->Stop();  // Stop recording.
        EXIT_ON_ERROR(hr)

    micBuffer->~AudioBuffer();
    BTBuffer->~AudioBuffer();

    Exit:
    printf("%d\n", hr);
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