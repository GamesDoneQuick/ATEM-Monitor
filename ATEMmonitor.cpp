#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include<stdio.h>
#include<winsock2.h>
#include<random>
#include<ctime>
#include "SerialClass.h"

#pragma comment(lib,"ws2_32.lib") //Winsock Library

#define BUFLEN 4028  //Max length of buffer
#define DEBUG 0

#define ATEM_headerCmd_AckRequest 0x08	// Please acknowledge reception of this package...
#define ATEM_headerCmd_HelloPacket 0x10	
#define ATEM_headerCmd_Resend 0x20			// This is a resent information
#define ATEM_headerCmd_RequestNextAfter 0x40	// I'm requesting you to resend something to me.
#define ATEM_headerCmd_Ack 0x80		// This package is an acknowledge to package id (byte 4-5) ATEM_headerCmd_AckRequest

#define ATEM_maxInitPackageCount 40		// The maximum number of initialization packages. By observation on a 2M/E 4K can be up to (not fixed!) 32. We allocate a f more then...
#define ATEM_packetBufferLength 96		// Size of packet buffer
#define TALLY_COUNT 8

SOCKET s;
struct sockaddr_in server, si_other;
int slen, recv_len;
char buf[BUFLEN];
int bufoffset = 0;
WSADATA wsa;
int outport;
const int inport = 9910;

char sendPacket[ATEM_packetBufferLength];
uint16_t sessionID = 0x53AB;
uint16_t localPacketID = 0;
uint8_t missedInitializationPackages[(ATEM_maxInitPackageCount + 7) / 8];	// Used to track which initialization packages have been missed

uint16_t atemProgramInputVideoSource[2];
uint16_t atemPreviewInputVideoSource[2];
uint16_t atemTallyByIndexSources;
uint8_t atemTallyByIndexTallyFlags[21];

bool isConnected = false;
bool waitingForIncoming = false;
bool initPayloadSent = false;
bool hasInitialized = false;
uint8_t initPayloadSentAtPacketId;
Serial* SerialCOM;

uint8_t highByte(int num) {
	return (num & 0xFF00) >> 8;
}
uint8_t lowByte(int num) {
	return (num & 0x00FF);
}
uint16_t word(uint8_t high, uint8_t low) {
	return (high << 8) + low;
}
int randomRange(int min, int max) {
	return (rand() % (max - min + 1)) + min;
}


void createPacketBuffer(const uint8_t headerCmd, const uint16_t lengthOfData, const uint16_t remotePacketID = 0) {
	memset(sendPacket, 0, ATEM_packetBufferLength);

	sendPacket[0] = headerCmd | (highByte(lengthOfData) & 0x07);  // Command bits + length MSB
	sendPacket[1] = lowByte(lengthOfData);  // length LSB
	
	sendPacket[2] = highByte(sessionID);  // Session ID
	sendPacket[3] = lowByte(sessionID);  // Session ID

	sendPacket[4] = highByte(remotePacketID);  // Remote Packet ID, MSB
	sendPacket[5] = lowByte(remotePacketID);  // Remote Packet ID, LSB

	if (!(headerCmd & (ATEM_headerCmd_HelloPacket | ATEM_headerCmd_Ack | ATEM_headerCmd_RequestNextAfter))) {
		localPacketID++;

		sendPacket[10] = highByte(localPacketID);  // Local Packet ID, MSB
		sendPacket[11] = lowByte(localPacketID);  // Local Packet ID, LSB
	}
}


void sendPacketBuffer(const uint16_t lengthOfData) {
#if DEBUG
	printf("--> ");
	for (int i = 0; i < lengthOfData; i++) {
		printf("%02X ", (uint8_t)sendPacket[i]);
	}
	printf("\n");
#endif

	if (sendto(s, sendPacket, lengthOfData, 0, (struct sockaddr*) &si_other, slen) == SOCKET_ERROR)
	{
		printf("sendto() failed with error code : %d", WSAGetLastError());
		closesocket(s);
		WSACleanup();
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}
}


void beginSocket() {
	//Initialise winsock
	printf("\nInitialising Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		printf("Failed. Error Code : %d", WSAGetLastError());
		WSACleanup();
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}
	printf("Initialised.\n");

	//Create a socket
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		printf("Could not create socket : %d", WSAGetLastError());
		WSACleanup();
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}
	printf("Socket created.\n");

	//Prepare the sockaddr_in structure
	outport = randomRange(50100, 65300);
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(outport);

	//Bind
	if (bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
	{
		printf("Bind failed with error code : %d", WSAGetLastError());
		closesocket(s);
		WSACleanup();
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}
	printf("Bind created on port: %d\n", outport);
}


void processPayload(char* payload, char* cmdStr, uint16_t payloadlen) {
	uint8_t mE, keyer, aUXChannel;
	uint16_t sources;
	long temp;
	uint8_t readBytesForTlSr;

	if (!strcmp(cmdStr, PSTR("PrgI"))) {
		mE = payload[0];
		if (mE <= 1) {
#if DEBUG
			temp = atemProgramInputVideoSource[mE];
#endif
			atemProgramInputVideoSource[mE] = word(payload[2], payload[3]);
#if DEBUG
			if (atemProgramInputVideoSource[mE] != temp) {
				printf("atemProgramInputVideoSource[mE=%d] = %d\n", mE, atemProgramInputVideoSource[mE]);
			}
#endif
		}
	}

	else if (!strcmp(cmdStr, PSTR("PrvI"))) {
		mE = payload[0];
		if (mE <= 1) {
#if DEBUG
			temp = atemPreviewInputVideoSource[mE];
#endif
			atemPreviewInputVideoSource[mE] = word(payload[2], payload[3]);
#if DEBUG
			if (atemPreviewInputVideoSource[mE] != temp) {
				printf("atemPreviewInputVideoSource[mE=%d] = %d\n", mE, atemPreviewInputVideoSource[mE]);
			}
#endif
		}
	}

	else if (!strcmp(cmdStr, PSTR("TlIn"))) {
		sources = word(payload[0], payload[1]);
		if (sources <= 20) {
#if DEBUG
			temp = atemTallyByIndexSources;
#endif
			atemTallyByIndexSources = word(payload[0], payload[1]);
#if DEBUG
			if (atemTallyByIndexSources != temp) {
				printf("atemTallyByIndexSources = %d\n", atemTallyByIndexSources);
			}
#endif

			for (uint8_t a = 0; a<sources; a++) {
#if DEBUG
				temp = atemTallyByIndexTallyFlags[a];
#endif
				atemTallyByIndexTallyFlags[a] = payload[2 + a];
#if DEBUG
				if (atemTallyByIndexTallyFlags[a] != temp) {
					printf("atemTallyByIndexTallyFlags[a=%d] = %d\n", a, atemTallyByIndexTallyFlags[a]);
				}
#endif
			}

			for (uint8_t a = 0; a < TALLY_COUNT; a++) {
				SerialCOM->WriteData('0' + atemTallyByIndexTallyFlags[a]);
			}
		}
	}
}


void parsePayload(char* inpacket, uint16_t packetlen) {
	if (packetlen < 8) {
		return;
	}

	// Payload header
	uint16_t payloadlen = word(inpacket[0], (uint8_t)inpacket[1]);
	char cmdstr[] = { inpacket[4], inpacket[5], inpacket[6], inpacket[7], '\0' };

#if DEBUG
	printf("  %s ", cmdstr);
	printf("%04X ", payloadlen);
	printf("%02X%02X", (uint8_t)inpacket[2], (uint8_t)inpacket[3]);

	/*
	printf(" - ");
	for (int i = 8; i < payloadlen; i++) {
		printf("%02X", (uint8_t)inpacket[i]);
	}
	printf("\n");
	*/
#endif

	processPayload(inpacket + 8, cmdstr, payloadlen);

	parsePayload(inpacket + payloadlen, packetlen - payloadlen);
}


void processPacket(char* inpacket, uint16_t packetlen) {
	sessionID = word(inpacket[2], inpacket[3]);
	uint8_t headerBitmask = inpacket[0];
	uint8_t remotePacketID = word(inpacket[10], inpacket[11]);

#if DEBUG
	for (int i = 0; i < 12; i += 2) {
		printf("%02X%02X ", (uint8_t)inpacket[i], (uint8_t)inpacket[i + 1]);
	}

	if ((packetlen > 12) && (headerBitmask & ATEM_headerCmd_HelloPacket)) {
		printf(" - ");
		for (int i = 12; i < packetlen; i++) {
			printf("%02X", (uint8_t)inpacket[i]);
		}
	}

	printf("\n");
#endif

	if (remotePacketID < ATEM_maxInitPackageCount) {
		missedInitializationPackages[remotePacketID >> 3] &= ~(0x01 << (remotePacketID & 0x07));
	}

	waitingForIncoming = false;

	if (headerBitmask & ATEM_headerCmd_HelloPacket) {	// Respond to "Hello" packages:
		printf("Hello Recieved.\n");
		isConnected = true;

		createPacketBuffer(ATEM_headerCmd_Ack, 12);
		sendPacket[9] = 0x03;	// This seems to be what the client should send upon first request. 
		sendPacketBuffer(12);
	}

	if (!initPayloadSent && packetlen == 12 && remotePacketID > 1) {
		printf("Handshake Finished.\n");

		initPayloadSent = true;
		initPayloadSentAtPacketId = remotePacketID;
	}

	if (initPayloadSent && ((headerBitmask & ATEM_headerCmd_AckRequest) || (headerBitmask & ATEM_headerCmd_Resend))) { 	// Respond to request for acknowledge	(and to resends also, whatever...  
		createPacketBuffer(ATEM_headerCmd_Ack, 12, remotePacketID);
		sendPacketBuffer(12);
	}
	else if (initPayloadSent && (headerBitmask & ATEM_headerCmd_RequestNextAfter) && hasInitialized) {	// ATEM is requesting a previously sent package which must have dropped out of the order. We return an empty one so the ATEM doesnt' crash (which some models will, if it doesn't get an answer before another 63 commands gets sent from the controller.)
		uint8_t b1 = inpacket[6];
		uint8_t b2 = inpacket[7];

		createPacketBuffer(ATEM_headerCmd_Ack, 12, 0);
		sendPacket[0] = ATEM_headerCmd_AckRequest;	// Overruling this. A small trick because createCommandHeader shouldn't increment local package ID counter
		sendPacket[10] = b1;
		sendPacket[11] = b2;
		sendPacketBuffer(12);
	}

	
	if ((packetlen > 12) && !(headerBitmask & ATEM_headerCmd_HelloPacket)) {
		parsePayload(inpacket + 12, packetlen - 12);
	}

	/*
	// After initialization, we check which packages were missed and ask for them:
	if (!hasInitialized && initPayloadSent && !waitingForIncoming) {
		for (uint8_t i = 1; i < initPayloadSentAtPacketId; i++) {
			if (i <= ATEM_maxInitPackageCount) {
				if (missedInitializationPackages[i >> 3] & (0x01 << (i & 0x07))) {

					clearPacketBuffer();
					createPacketBuffer(ATEM_headerCmd_RequestNextAfter, 12);
					sendPacket[6] = highByte(i - 1);  // Resend Packet ID, MSB
					sendPacket[7] = lowByte(i - 1);  // Resend Packet ID, LSB
					sendPacket[8] = 0x01;
					sendPacketBuffer(12);

					waitingForIncoming = true;
					break;
				}
			}
			else {
				break;
			}
		}
		if (!waitingForIncoming) {
			hasInitialized = true;
		}
	}
	*/
}


void initATEM() {
	sessionID = 0x53AB;
	localPacketID = 0;
	isConnected = false;
	waitingForIncoming = false;
	initPayloadSent = false;
	hasInitialized = false;
	bufoffset = 0;

	beginSocket();

	// Send Hello
	printf("Sending Hello\n");
	createPacketBuffer(ATEM_headerCmd_HelloPacket, 12 + 8);
	sendPacket[12] = 0x01;	// This seems to be what the client should send upon first request. 
	sendPacket[9] = 0x3a;	// This seems to be what the client should send upon first request. 
	sendPacketBuffer(20);

	//clear the buffer by filling null, it might have previously received data
	memset(buf, '\0', BUFLEN);

	//keep listening for data
	printf("Waiting for data...\n");
}


bool initArduino() {
	wchar_t comname[32];
	for (int i = 1; i < 256; i++) {
		swprintf(comname, 32, L"\\\\.\\COM%d", i);

		SerialCOM = new Serial(comname);    // adjust as needed

		if (SerialCOM->IsConnected()) {
			wprintf(L"Connected to %s\n", comname);
			SerialCOM->WriteData("ATEMmonitor v1.0\n");
			Sleep(100);

			clock_t now, lastping;
			lastping = clock();

			char buf[256];
			uint16_t len = SerialCOM->ReadData(buf, 256);
			buf[len] = 0;

			if (strcmp(buf, "Tally Controller v1.0\n")) {
				wprintf(L"Device did not handshake.\n", comname);
				continue;
			}

			SerialCOM->WriteData("Handshake OK\n");
			printf("Handshake OK\n");
			Sleep(100);
			return true;
		}
		else {
			wprintf(L"Failed to connect to %s\n", comname);
			delete SerialCOM;
		}
	}
	return false;
}


int main()
{
	clock_t now, lastping;

	srand(time(NULL));
	
	FILE *fp;
	char ipbuff[255];
	errno_t ferr = fopen_s(&fp, "ATEM_IP.txt", "r");
	if (fp == NULL) {
		printf("Could not open ATEM_IP.txt: %d\n", ferr);
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}
	fgets(ipbuff, 255, (FILE*)fp);
	fclose(fp);

	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(9910);
	si_other.sin_addr.s_addr = inet_addr(ipbuff);
	slen = sizeof(si_other);

	if (!initArduino()) {
		printf("Failed to connect to Arduino.\n");
		printf("Press ENTER to close...");
		getchar();

		exit(EXIT_FAILURE);
	}


	initATEM();
	now = clock();
	lastping = now;

	while (1)
	{
		now = clock();
		float timediff = ((float)(abs(now - lastping)) / CLOCKS_PER_SEC) * 1000;
		if (timediff >= 1000 && timediff < 60000) {
			printf("Timeout connection [%dms]. Attempting to reconnect...\n", (int)timediff);
			closesocket(s);
			WSACleanup();

			initATEM();
			now = clock();
			lastping = now;
		}

		// Get data if it is available
		u_long availableData;
		ioctlsocket(s, FIONREAD, &availableData);
		if (availableData > 0) {
			//try to receive some data (some should exist)
			if ((recv_len = recv(s, buf + bufoffset, BUFLEN - bufoffset, 0)) < 0)
			{
				printf("recvfrom() failed with error code : %d", WSAGetLastError());
				closesocket(s);
				WSACleanup();
				printf("Press ENTER to close...");
				getchar();

				exit(EXIT_FAILURE);
			}

			uint16_t buflength = recv_len + bufoffset;
			uint16_t bufindex = 0;

			// Process all full packets found
			uint16_t packetlen = word(buf[0 + bufindex] & 0x07, buf[1 + bufindex]);
			while (bufindex + packetlen <= buflength) {
				// Process Packet
				char *packet = buf + bufindex;

				processPacket(packet, packetlen);
				lastping = now;

				// Clear packet data
				memset(packet, '\0', packetlen);

				// Next packet length
				bufindex += packetlen;

				if (bufindex == buflength) {
					break;
				}

				packetlen = word(buf[0 + bufindex] & 0x07, buf[1 + bufindex]);
			}

			// Move the buffer data of any partial packets to the start
			char tempbuff[BUFLEN];
			memcpy(buf + bufindex, tempbuff, buflength - bufindex);
			memcpy(tempbuff, buf, buflength - bufindex);
			bufoffset = buflength - bufindex;
		}
	}

	closesocket(s);
	WSACleanup();

	return 0;
}