#include "config.h"
#include <iostream>
#include "UDP.h"

SOCKET* CreateSocketUDP()
{
	SOCKET* pUDPSocket = (SOCKET*)malloc(sizeof(SOCKET));
	if (pUDPSocket == NULL) return NULL;

	if ((*pUDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET)
	{
		std::cout << ERR << "Failed creating a UDP socket. Error Code: "
			<< WSAGetLastError()
			<< std::endl;
		
		return NULL;
	}
	std::cout << ERR << "Successfully created a UDP socket." << std::endl;

	return pUDPSocket;
}

void CloseSocketUDP(SOCKET* pUDPSocket)
{
	if (pUDPSocket != NULL)
	{
		closesocket(*pUDPSocket);
		free(pUDPSocket);
		pUDPSocket = NULL;
	}
}