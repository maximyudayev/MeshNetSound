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
		AudioBuffer(std::string filename);
		~AudioBuffer();
		HRESULT SetFormat(WAVEFORMATEX* pwfx);
		HRESULT CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone);

	private :
		std::ofstream outputFile;
		int duration_counter{0};

};

