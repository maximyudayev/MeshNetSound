/*
    TODO:
        I.------enforce conventions across entire project.
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
	CHAR sInput[2]; // buffer to receive quit command
	BOOL bQuit = FALSE;
	
	hr = pAggregator.Initialize();
	
	hr = pAggregator.Start();

	// "Interactive" CLI
	// continues capturing until user clicks [q] : quit
	while (!bQuit)
	{
		std::cin.get(sInput, 2);
		std::string str(sInput);

		// Skip cin to next line to accept another input on next loop iteration
		std::cin.clear();
		std::cin.ignore(2, '\n');

		// If user pressed q, hence terminates the program
		if (strcmp(str.c_str(), "q") == 0) break;
		// If user pressed any other button
		else
			std::cout << MSG "Press [q] to exit." << std::endl;
	}

	hr = pAggregator.Stop();

	return hr;
}