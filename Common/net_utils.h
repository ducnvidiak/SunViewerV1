#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>

namespace common {

	bool initWinsock();
	void cleanupWinsock();

	bool sendAll(SOCKET s, const void* data, size_t size);
	bool recvAll(SOCKET s, void* data, size_t size);

	SOCKET createServer(uint16_t port);
	SOCKET acceptClient(SOCKET server);

	SOCKET connectToServer(const std::string& ip, uint16_t port);

	void closeSocket(SOCKET s);

}