#pragma once
#include <stdio.h>
#include <winsock2.h>
#include "config.h"
#include "AudioBuffer.h"
#include "UDP.h"

class UDPAudioBuffer : public AudioBuffer
{
	public:
		UDPAudioBuffer(std::string filename, UINT32 nMember, CHAR* ip) : AudioBuffer(filename, nMember)
		{
			// Point to the beginning of the corresponding IP address string
			pWASANNodeIP = ip;
		};

		/// <summary>
		/// <para>Static UDP server listener functionality.</para>
		/// <para>Binds to the socket at the IP address provided by the user
		/// corresponding to this device. Listens to port for incoming UDP traffic
		/// and drops any packets if arrived from clients not selected
		/// as AudioBuffer capture nodes.</para>
		/// <para>Note: Provide an overload if multiple threads, each dedicated to 
		/// an individual socket is desired.</para>
		/// </summary>
		/// <param name="pUDPSocket">- server socket to bind to.</param>
		/// <param name="sUDPServerIP">- server IP address on which to listen to traffic to.</param>
		/// <param name="pUDPAudioBuffer">- array of pointers to UDPAudioBuffer objects
		/// to match traffic with WASAN capture node objects.</param>
		/// <param name="nWASANNodes">- number of WASAN capture nodes in the array.</param>
		/// <param name="bDone">- indicator when user terminated the program.</param>
		static void ReceiveDataUDP(SOCKET* pUDPSocket, CHAR* sUDPServerIP, UDPAudioBuffer** pUDPAudioBuffer, UINT32 nWASANNodes, BOOL* bDone);
		
		/// <summary>
		/// <para>UDP client sender functionality to push data to WASAN render nodes.</para>
		/// <para>Note: if socket error occurs, data does not get resent.</para>
		/// </summary>
		/// <param name="nFrames">- number of frames from output ring buffer to push over UDP
		/// for the associated socket.</param>
		void SendDataUDP(UINT32 nFrames);

		/// <summary>
		/// Sets socket pointer on the UDPAudioBuffer object.
		/// </summary>
		/// <param name="pSocket">- pointer to the created socket.</param>
		void SetSocketUDP(SOCKET* pSocket);

		/// <summary>
		/// <para>Gets socket pointer of the UDPAudioBuffer object.</para>
		/// <para>Used mainly to retreive socket associated with an UDPAudioBuffer instance
		/// to close and free the memory.</para>
		/// </summary>
		/// <returns>Pointer to a UDP socket of this UDPAudioBuffer.</returns>
		SOCKET* GetSocketUDP();

	private:
		/// <summary>
		/// <para>Iterates over the array of UDPAudioBuffer pointers and 
		/// seeks one with matching IP address.</para>
		/// </summary>
		/// <param name="pUDPAudioBuffer">- array of UDPAudioBuffer pointers.</param>
		/// <param name="nUDPAudioBuffer">- number of UDPAudioBuffer objects in the array.</param>
		/// <param name="sIP">- IP address to use to look up the matching UDPAudioBuffer.</param>
		/// <returns>Pointer to UDPAudioBuffer object having this IPv4 address.</returns>
		static UDPAudioBuffer* GetBufferByIP(UDPAudioBuffer** pUDPAudioBuffer, UINT32 nUDPAudioBuffer, CHAR* sIP);

		CHAR* pWASANNodeIP;
		SOCKET* pUDPSocket;
};