#include "UDP.h"

//WifiDirectServices code
/*
void startAdvertiser() {
	//create a new Service Advertiser, servicename should be string

	WiFiDirectServiceAdvertiser advertiser = WiFiDirectServiceAdvertiser(L"test");

	//register for session requests from Seeker, TODO: handle event ??
	//tbh dont give a fuck about accepting those bad bois, auto accept all the way babyyyyy
	//Advertiser.SessionRequested += OnSessionrequested;
	

	//auto accept all session requests from seekers
	advertiser.AutoAcceptSession(true);

	//__hook(&WiFiDirectServiceAdvertiser::AutoAcceptSessionConnected, advertiser, &OnConnection);
	//start the advertiser
	advertiser.Start();
	printf("ADVERTISER STARTED");
}

void OnConnection(WiFiDirectServiceAdvertiser sender, WiFiDirectServiceSessionRequestedEventArgs args) {
	//get session Request object
	WiFiDirectServiceSessionRequest sessionRequest = args.GetSessionRequest();

	//retrieve DeviceInformation from the Session Request
	DeviceInformation deviceInfo = sessionRequest.DeviceInformation();

	// Accept the Session request from the Service Seeker
	WiFiDirectServiceSession Session = sender.ConnectAsync(deviceInfo).GetResults();

	//socket
	printf("CONNECTION MADE");
}

void startServiceSeeker() {
	printf("ADVERTISER STARTED");
	 winrt::hstring serviceSelector = WiFiDirectService::GetSelector(L"test");
	 
	 printf("ADVERTISER STARTED");
	 DeviceInformationCollection devInfoCollection = DeviceInformation::FindAllAsync(serviceSelector).GetResults();
	 printf("ADVERTISER STARTED");
	 WiFiDirectService Service = WiFiDirectService::FromIdAsync(devInfoCollection.GetAt(0).Id()).GetResults();
	 printf("ADVERTISER STARTED");
	 WiFiDirectServiceSession Session = Service.ConnectAsync().GetResults();

	 auto EndpointPairs = Session.GetConnectionEndpointPairs();

	 //input socket
	 printf("ADVERTISER STARTED");
}
*/

std::tuple<int, sockaddr_in,sockaddr_in> startSocketUDPListen() {
	SOCKET s;
	struct sockaddr_in server, si_other;
	WSADATA wsa;

	//Initialise winsock
	printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//Create a socket
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		printf("Could not create socket : %d", WSAGetLastError());
	}
	printf("Socket created.\n");

	//Prepare the sockaddr_in structure, listens to all incoming UDP packets
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(8888);

	//Bind
	if (bind(s, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	puts("Bind done");
	
	return { s,server,si_other };
}

void receiveDataUDP(SOCKET s, sockaddr_in si_other, FLOAT** pBuffer, UINT32 nBufferOffset) {
	int recv_len,slen;
	char* buf;

	slen = sizeof(si_other);
	while (1)
	{
		printf("Waiting for data...");
		fflush(stdout);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', 255); //255 hangt af van packet length 

		for (UINT32 i = 0; i < 2; i++) {//2 = amount of channels
			//try to receive some data, this is a blocking call
			if ((recv_len = recvfrom(s, buf, 255, 0, (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR)
			{
				*(pBuffer[i] + (nBufferOffset) % 16) = *(((FLOAT*)buf) + i);
			}
		}
	}
}

void receiveDataUDP_debug(SOCKET s, sockaddr_in si_other) {
	int recv_len, slen;
	char* buf;

	slen = sizeof(si_other);
	while (1)
	{
		printf("Waiting for data...");
		fflush(stdout);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', 255); //255 hangt af van packet length 

		if ((recv_len = recvfrom(s, buf, 255, 0, (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR)
		{
			printf("Data: %s\n", buf);
		}
	}
}

std::tuple<int,sockaddr_in> startSocketUDPSend() {
	struct sockaddr_in si_other;
	int s, slen = sizeof(si_other);
	WSADATA wsa;

	//Initialise winsock
	printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//create socket
	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
	{
		printf("socket() failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}

	//setup address structure
	memset((char*)&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(8888);
	inet_pton(AF_INET, "192.168.137.1", &si_other.sin_addr);
	return { s,si_other };
}

void sendDataUDP(SOCKET s, sockaddr_in si_other, FLOAT** pBuffer, UINT32 nBufferOffset) {
	//get the data, i assume i know how many channels there are in the aggregator otherwise max fix plz
	char* buf; //do i need initialization ?

	for (UINT32 i = 0; i < 2; i++){//2 = amount of channels
		memcpy((pBuffer[i] + (nBufferOffset) % 16), buf, 16);  //assume 16 bytes !!
	}

	if (sendto(s, buf, sizeof(buf), 0, (struct sockaddr*) &si_other, sizeof(si_other)) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
}

void sendDataUDP_debug(SOCKET s, sockaddr_in si_other, const char* test) {

	if (sendto(s, test, strlen(test), 0, (struct sockaddr*)&si_other, sizeof(si_other)) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
}

void closeSocket(SOCKET s) {
	closesocket(s);
	WSACleanup();
}