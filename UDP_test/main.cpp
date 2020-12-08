#include "UDP.h"


int main(void)
{
	auto [s, sockaddr_server,sockaddr_si_other] = startSocketUDPListen();

	/*
	auto [s, sockaddr] = startSocketUDPSend();
	const char* test = "dit is een test";
	while(1){
	sendDataUDP_debug(s,sockaddr, test);
	}*/
}