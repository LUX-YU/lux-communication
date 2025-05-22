#include "lux/communication/UdpMultiCast.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>

#include "lux/communication/visibility.h"

namespace lux::communication
{
	class LUX_COMMUNICATION_PUBLIC UdpMultiCastImpl
	{
	public:
		UdpMultiCastImpl(std::string_view addr, int prt)
		: addr(addr), port(prt)
		{	
                        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                        assert(sock != -1);

                        int flags = fcntl(sock, F_GETFL, 0);
                        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

			int on = 1;
			auto rst = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
			assert(rst != -1);

			std::memset(&multicastAddr, 0, sizeof(multicastAddr));
			multicastAddr.sin_family		= AF_INET;
			multicastAddr.sin_addr.s_addr	= inet_addr(addr.data());
			multicastAddr.sin_port			= htons(port);
		}

		~UdpMultiCastImpl()
		{
			::close(sock);
		}

		bool close()
		{
			return ::close(sock) == 0;
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
				if(rst == -1)
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
				if (rst == -1)
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
			socklen_t si_len = sizeof(struct sockaddr_in);
			int rst = recvfrom(sock, buffer, len, 0, (struct sockaddr*)&from, &si_len);
			from_addr.addr = from.sin_addr.s_addr;
			from_addr.port = ntohs(from.sin_port);
			return rst;
		}

	private:
		int		    sock;
		std::string addr;
		int			port;
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