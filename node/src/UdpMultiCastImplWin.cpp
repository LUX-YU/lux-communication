#include "lux/communication/UdpMultiCast.hpp"
#include <cassert>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace lux::communication
{
	class UdpMultiCastImpl
	{
	public:
		UdpMultiCastImpl(std::string_view addr, int prt)
		: addr(addr), port(prt)
		{
			int rst = WSAStartup(MAKEWORD(2, 2), &wsaData);
			assert((rst == 0, "WSAStartup failed with error"));
			
                        sock = socket(AF_INET, SOCK_DGRAM, 0);
                        assert(sock != INVALID_SOCKET);
                        u_long mode = 1;
                        ioctlsocket(sock, FIONBIO, &mode); // non-blocking

			BOOL reuse = TRUE;
			rst = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
			assert(rst != SOCKET_ERROR);

			memset(&multicastAddr, 0, sizeof(multicastAddr));
			multicastAddr.sin_family		= AF_INET;
			multicastAddr.sin_addr.s_addr	= inet_addr(addr.data());
			multicastAddr.sin_port			= htons(port);
		}

		~UdpMultiCastImpl()
		{
			closesocket(sock);
			WSACleanup();
		}

		bool close()
		{
			return closesocket(sock) == 0;
		}

		int send(std::string_view msg)
		{
			// if no error and msg has not been sent completely, keep sending
			const char* data = msg.data();
			int len			 = msg.size();
			int sent_len     = 0;
			while(sent_len < len)
			{
				int rst = sendto(sock, data + sent_len, len - sent_len, 0, (sockaddr*)&multicastAddr, sizeof(multicastAddr));
				if(rst == SOCKET_ERROR)
				{
					return rst;
				}
				sent_len += rst;
			}
			return sent_len;
		}

		int send(void* data, size_t len)
		{
			int sent_len = 0;
			while (sent_len < len)
			{
				int rst = sendto(sock, (char*)data + sent_len, len - sent_len, 0, (sockaddr*)&multicastAddr, sizeof(multicastAddr));
				if (rst == SOCKET_ERROR)
				{
					return rst;
				}
				sent_len += rst;
			}
			return sent_len;
		}

		int bind()
		{
			sockaddr_in localAddr;
			memset(&localAddr, 0, sizeof(localAddr));
			localAddr.sin_family		= AF_INET;
			localAddr.sin_addr.s_addr	= INADDR_ANY;
			localAddr.sin_port			= htons(port);

			return ::bind(sock, (sockaddr*)&localAddr, sizeof(localAddr));
		}

		int bind(std::string_view addr)
		{
			sockaddr_in localAddr;
			memset(&localAddr, 0, sizeof(localAddr));
			localAddr.sin_family		= AF_INET;
			localAddr.sin_addr.s_addr	= inet_addr(addr.data());
			localAddr.sin_port			= htons(port);
			
			return ::bind(sock, (sockaddr*)&localAddr, sizeof(localAddr));
		}

		int recvFrom(char* buffer, int len, SockAddr& from_addr)
		{
			sockaddr_in from;
			memset(&from, 0, sizeof(from));
			int fromLen = sizeof(from);
			int rst = recvfrom(sock, buffer, len, 0, (sockaddr*)&from, &fromLen);
			from_addr.addr = from.sin_addr.s_addr;
			from_addr.port = ntohs(from.sin_port);
			return rst;
		}

	private:
		SOCKET		sock;
		std::string addr;
		int			port;
		WSADATA		wsaData;
		sockaddr_in multicastAddr;
	};

	UdpMultiCast::UdpMultiCast(std::string_view group, int port)
	{
		_impl = std::make_unique<UdpMultiCastImpl>(group, port);
	}

	UdpMultiCast::~UdpMultiCast() = default;

	bool UdpMultiCast::close()
	{
		return _impl->close();
	}

	int UdpMultiCast::bind()
	{
		return _impl->bind();
	}

	/*
	 * @param addr: bind address
	 */
	int UdpMultiCast::bind(std::string_view addr)
	{
		return _impl->bind(addr);
	}

	void UdpMultiCast::send(std::string_view msg)
	{
		_impl->send(msg);
	}

	void UdpMultiCast::send(void* data, size_t len)
	{
		_impl->send(data, len);
	}

	int UdpMultiCast::recvFrom(char* buffer, int len, SockAddr& from_addr)
	{
		return _impl->recvFrom(buffer, len, from_addr);
	}
}