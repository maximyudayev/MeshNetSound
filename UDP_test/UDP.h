/*
#include <winrt/Windows.Devices.WifiDirect.Services.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <ppltasks.h>
#include <stdio.h>


using namespace winrt::Windows::Devices::WiFiDirect::Services;
using namespace winrt;
using namespace winrt::Windows::Devices::Enumeration;
using namespace concurrency;
//using namespace Windows::Devices::WiFiDirect::Services;
*/
#include<stdio.h>
#include<winsock2.h>
#include <Ws2tcpip.h>

#pragma comment(lib,"ws2_32.lib") //Winsock Library

//void startAdvertiser();

//void startServiceSeeker();

void startSocketUDPListen();
void startSocketUDPSend();