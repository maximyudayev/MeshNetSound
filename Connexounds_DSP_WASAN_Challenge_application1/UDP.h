#pragma once
#include <winsock2.h>

/// <summary>
/// <para>Creates a UDP socket.</para>
/// </summary>
SOCKET* CreateSocketUDP();

/// <summary>
/// <para>Release previously created UDP socket.</para>
/// </summary>
/// <param name="pUDPSocket">- location to retreive from socket to be destroyed.</param>
void CloseSocketUDP(SOCKET* pUDPSocket);