/*
    TODO:
        I.------enforce conventions across entire project.
		II.-----incorporate threading to parallelize work of audio capture, audio render,
				and resampling operations; don't forget synchronization mechanisms.
*/

#include "Aggregator.h"
#include <iostream>

/// <summary>
/// <para>Entry point of the program.</para>
/// </summary>
/// <param name="argc"></param>
/// <param name="argv"></param>
/// <returns></returns>
int main(int argc, char* argv[])
{
	HRESULT hr = ERROR_SUCCESS;

	// ASCII art
	std::cout << R"(                           __   __                     _     
                           \ \ / /                    | |    
  ___ ___  _ __  _ __   ___ \ V / ___  _   _ _ __   __| |___ 
 / __/ _ \| '_ \| '_ \ / _ \ > < / _ \| | | | '_ \ / _` / __|
| (_| (_) | | | | | | |  __// . \ (_) | |_| | | | | (_| \__ \
 \___\___/|_| |_|_| |_|\___/_/ \_\___/ \__,_|_| |_|\__,_|___/
                                                             )" << std::endl;

	// 
	std::cout << "<--------Starting Aggregator-------->" << std::endl << std::endl;

	Aggregator pAggregator;
	
	hr = pAggregator.Initialize();
	
	hr = pAggregator.StartCapture();

#ifndef DEBUG
	//hr = pAggregator.StopCapture();
#endif

	return hr;
}