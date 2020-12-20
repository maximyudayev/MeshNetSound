/*
	TODO:
		I.------include packet sequence number in each packet to synchronize sender and receiver.
*/

#include "UDPAudioBuffer.h"

#pragma comment(lib, "Ws2_32.lib")

void UDPAudioBuffer::ReceiveDataUDP(SOCKET* pUDPSocket, CHAR* sUDPServerIP, UDPAudioBuffer** pUDPAudioBuffer, UINT32 nWASANNodes, BOOL* bDone)
{
	SOCKADDR_IN UDPServer, UDPClient;
	INT32 nClientLength = sizeof(UDPClient), nBytesIn;
	CHAR buf[TEMP_UDP_BUFFER_SIZE];
	UDPAudioBuffer* pUDPCaptureClient;
	
	// Fill sockaddr struct with IP and port number on which to listen to UDP traffic
	UDPServer.sin_family = AF_INET;
	UDPServer.sin_addr.s_addr = inet_addr(sUDPServerIP);
	UDPServer.sin_port = htons(UDP_RCV_PORT);

	// Bind server socket
	if (bind(*pUDPSocket, (SOCKADDR*)&UDPServer, sizeof(UDPServer)) == SOCKET_ERROR)
	{
		std::cout << ERR << "Socket bind failed. Error Code: "
			<< WSAGetLastError()
			<< std::endl;

		WSACleanup();
		exit(EXIT_FAILURE);
	}
	std::cout << MSG << "Server UDP socket bind succeeded." << std::endl;

	while (!*bDone)
	{
		// Clear buffer from previous junk
		memset(buf, 0, TEMP_UDP_BUFFER_SIZE);
		// Blocking receive call
		if ((nBytesIn = recvfrom(*pUDPSocket, buf, TEMP_UDP_BUFFER_SIZE, 0, (SOCKADDR*)&UDPClient, &nClientLength)) != SOCKET_ERROR)
		{
			// Get sender's IP address
			CHAR* UDPClientIP = inet_ntoa(UDPClient.sin_addr);

			// Get the UDPAudioBuffer instance corresponding to this IP or drop the data otherwise
			if ((pUDPCaptureClient = GetBufferByIP(pUDPAudioBuffer, nWASANNodes, UDPClientIP)) != NULL)
			{
				// Update the endpoint size with the actual UDP packet length
				pUDPCaptureClient->SetEndpointBufferSize(nBytesIn);

				// Get data and pull it into the corresponding ring buffer location
				pUDPCaptureClient->PullData((BYTE*)buf);
			}
		}
	}
}

void UDPAudioBuffer::SendDataUDP(UINT32 nFrames)
{
	SOCKADDR_IN UDPServer;
	CHAR buf[TEMP_UDP_BUFFER_SIZE];
	INT32 nServerLength = sizeof(UDPServer);

	// Specify receiver details
	UDPServer.sin_family = AF_INET;
	UDPServer.sin_addr.s_addr = inet_addr(this->pWASANNodeIP);
	UDPServer.sin_port = htons(UDP_RCV_PORT);

	// Push data into the UDP sending buffer
	this->PushData((BYTE*)buf, nFrames);
	
	// Send UDP packet to WASAN render node
	if (sendto(*this->pUDPSocket, buf, nFrames + 1, 0, (SOCKADDR*)&UDPServer, nServerLength) == SOCKET_ERROR)
	{
		std::cout	<< ERR << "UDP packet send failed. Error Code: "
					<< WSAGetLastError()
					<< std::endl;
	}
}

void UDPAudioBuffer::SetSocketUDP(SOCKET* pSocket)
{
	this->pUDPSocket = pSocket;
}

SOCKET* UDPAudioBuffer::GetSocketUDP()
{
	return this->pUDPSocket;
}

UDPAudioBuffer* UDPAudioBuffer::GetBufferByIP(UDPAudioBuffer** pUDPAudioBuffer, UINT32 nUDPAudioBuffer, CHAR* sIP)
{
	for (UINT32 i = 0; i < nUDPAudioBuffer; i++)
	{
		if (strcmp(pUDPAudioBuffer[i]->pWASANNodeIP, sIP) == 0) return pUDPAudioBuffer[i];
	}
	return NULL;
}
