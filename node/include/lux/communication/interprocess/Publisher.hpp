#pragma once

#include <string>
#include <cstring>

#include <zmq.hpp>

#include <lux/communication/visibility.h>
#include "lux/communication/UdpMultiCast.hpp"

namespace lux::communication::interprocess {

inline zmq::context_t& globalContext()
{
    static zmq::context_t ctx{1};
    return ctx;
}

inline std::string defaultEndpoint(const std::string& topic)
{
#ifdef _WIN32
    size_t h = std::hash<std::string>{}(topic);
    return "tcp://127.0.0.1:" + std::to_string(20000 + (h % 10000));
#else
    return "ipc:///tmp/" + topic + ".ipc";
#endif
}

inline void sendDiscovery(const std::string& topic, const std::string& endpoint)
{
    try {
        UdpMultiCast mc{"239.255.0.1", 30000};
        std::string msg = topic + ':' + endpoint;
        mc.send(msg);
    } catch (...) {
    }
}

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

