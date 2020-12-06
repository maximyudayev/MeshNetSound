#include "UDP.h"


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

void startSocketUDPNListen() {
	SOCKET s;
	struct sockaddr_in server, si_other;
	int slen, recv_len;
	char buf[255];
	WSADATA wsa;

	slen = sizeof(si_other);

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

	while (1)
	{
		printf("Waiting for data...");
		fflush(stdout);

		//clear the buffer by filling null, it might have previously received data
		memset(buf, '\0', 255);

		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(s, buf, 255, 0, (struct sockaddr*)&si_other, &slen)) == SOCKET_ERROR)
		{
			printf("recvfrom() failed with error code : %d", WSAGetLastError());
			exit(EXIT_FAILURE);
		}
		
		//print details of the client/peer and the data received
		//printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		printf("Data: %s\n", buf);

	}
	closesocket(s);
	WSACleanup();
}

std::tuple<int*,sockaddr_in*> startSocketUDPSend() {
	struct sockaddr_in si_other;
	int s, slen = sizeof(si_other);
	char buf[255];
	char message[255] = "dit is een test";
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
	inet_pton(AF_INET, "127.0.0.1", &si_other.sin_addr);
	return { &s,&si_other };
}

void sendDataUDP(SOCKET s, sockaddr_in* si_other, FLOAT** pBuffer, UINT32 nBufferOffset) {

	if (sendto(s, message, strlen(message), 0, (struct sockaddr*) &si_other, sizeof(si_other)) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
}

void sendDataUDP_debug(SOCKET s, sockaddr_in* si_other, const char* test) {

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