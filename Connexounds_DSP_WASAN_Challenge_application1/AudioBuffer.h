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
public:
	AudioBuffer(std::string filename);
	~AudioBuffer();
	HRESULT SetFormat(WAVEFORMATEX* pwfx);
	HRESULT CopyData(BYTE* pData, UINT32 numFramesAvailable, BOOL* bDone);
	HRESULT SetBufferSize(UINT32* pSize);

private:
	std::ofstream* output_files;
	std::string file_name;

	std::ofstream output_file1;
	std::ofstream output_file2;
	
	FLOAT** buffer = NULL;
	UINT32 buffer_samples_available{ 0 };
	UINT32 buffer_sz{ 0 };
	UINT32 duration_counter{ 0 };
	UINT8 block_sz{ 0 };
	UINT8 channels{ 0 };
	UINT8 sample_octet_num{ 0 };
};