#include <stdio.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <tuple>

std::tuple<int, sockaddr_in, sockaddr_in> startSocketUDPListen();
void receiveDataUDP(SOCKET s, sockaddr_in si_other, FLOAT** pBuffer, UINT32 nBufferOffset);
std::tuple<int, sockaddr_in> startSocketUDPSend();
void sendDataUDP(SOCKET s, sockaddr_in si_other, FLOAT** pBuffer, UINT32 nBufferOffset);
void closeSocket(SOCKET s);