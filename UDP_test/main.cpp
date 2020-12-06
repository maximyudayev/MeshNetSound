#include "UDP.h"


int main(void)
{
	//startSocketUDPNListen();
	auto [s_ptr, sockaddr_ptr] = startSocketUDPSend();
	const char* test = "dit is een test";
	while(1){
	sendDataUDP_debug(*s_ptr, sockaddr_ptr, test);
	}
}