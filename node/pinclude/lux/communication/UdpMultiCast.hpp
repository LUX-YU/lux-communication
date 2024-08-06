#pragma once
#include <memory>
#include <string_view>

namespace lux::communication
{
	struct SockAddr
	{
		int32_t addr;
		int		port;
	};

	class UdpMultiCastImpl;
	class UdpMultiCast
	{
	public:
		/*
		 * @param group: multicast group address from 224.0.0.0 to 239.255.255.255
		 * @param port:  port number
		 */
		UdpMultiCast(std::string_view group, int port);
		UdpMultiCast(const UdpMultiCast&) = delete;
		UdpMultiCast& operator=(const UdpMultiCast&) = delete;

		UdpMultiCast(UdpMultiCast&&) = default;
		UdpMultiCast& operator=(UdpMultiCast&&) = default;

		~UdpMultiCast();

		bool close();

		int bind();

		/*
		 * @param addr: bind address
		 */
		int bind(std::string_view addr);

		void send(std::string_view msg);
		void send(void* data, size_t len);
		int recvFrom(char* buffer, int len, SockAddr& from_addr);

	private:
		std::unique_ptr<UdpMultiCastImpl> _impl;
	};
}
