#pragma once

#include <string>
#include <cstring>

#include <zmq.hpp>

#include <lux/communication/visibility.h>
#include "lux/communication/UdpMultiCast.hpp"

namespace lux::communication::interprocess {

zmq::context_t& globalContext();

std::string defaultEndpoint(const std::string& topic);

void sendDiscovery(const std::string& topic, const std::string& endpoint);

template<typename T>
class Publisher
{
public:
    explicit Publisher(const std::string& topic, std::string endpoint = "")
        : topic_(topic),
          endpoint_(endpoint.empty() ? defaultEndpoint(topic) : std::move(endpoint)),
          socket_(globalContext(), zmq::socket_type::pub)
    {
        socket_.bind(endpoint_);
        sendDiscovery(topic_, endpoint_);
    }

    void publish(const T& msg)
    {
        zmq::message_t m(sizeof(T));
        std::memcpy(m.data(), &msg, sizeof(T));
        socket_.send(m, zmq::send_flags::none);
    }

private:
    std::string topic_;
    std::string endpoint_;
    zmq::socket_t socket_;
};

} // namespace lux::communication::interprocess

