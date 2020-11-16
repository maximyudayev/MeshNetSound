#pragma once
#include <windows.h>
#include <iostream>
#include <fstream>
#include <mmsystem.h>
#include <MMDeviceAPI.h>
#include <AudioClient.h>
#include <AudioPolicy.h>
#include <mmreg.h>





class AudioBuffer
{
	public : 
		AudioBuffer();
		~AudioBuffer();
		HRESULT SetFormat(WAVEFORMATEX* pwfx);
		HRESULT CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone);

	private :
		std::ofstream outputFile;

};

