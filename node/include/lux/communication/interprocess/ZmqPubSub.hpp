#pragma once

#include <string>
#include <thread>
#include <functional>
#include <atomic>
#include <optional>
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

inline std::optional<std::string> waitDiscovery(const std::string& topic, std::chrono::milliseconds timeout = std::chrono::milliseconds{200})
{
    try {
        UdpMultiCast mc{"239.255.0.1", 30000};
        mc.bind();
        char buf[256];
        SockAddr from{};
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            int ret = mc.recvFrom(buf, sizeof(buf)-1, from);
            if (ret > 0) {
                buf[ret] = '\0';
                std::string s(buf);
                auto pos = s.find(':');
                if (pos != std::string::npos) {
                    std::string t = s.substr(0, pos);
                    if (t == topic) {
                        return s.substr(pos+1);
                    }
                }
            }
        }
    } catch (...) {
    }
    return std::nullopt;
}

template<typename T>
class Publisher
{
public:
    explicit Publisher(const std::string& topic, std::string endpoint = defaultEndpoint(topic))
        : topic_(topic), endpoint_(std::move(endpoint)), socket_(globalContext(), zmq::socket_type::pub)
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

template<typename T, typename Callback>
class Subscriber
{
public:
    Subscriber(const std::string& topic, Callback cb)
        : topic_(topic), callback_(std::move(cb)), socket_(globalContext(), zmq::socket_type::sub)
    {
        auto ep = waitDiscovery(topic_);
        if (!ep) {
            endpoint_ = defaultEndpoint(topic_);
        } else {
            endpoint_ = *ep;
        }
        socket_.set(zmq::sockopt::subscribe, "");
        socket_.connect(endpoint_);
        running_ = true;
        thread_ = std::thread([this]{ recvLoop(); });
    }

    ~Subscriber()
    {
        stop();
    }

    void stop()
    {
        if (running_) {
            running_ = false;
            if (thread_.joinable())
                thread_.join();
        }
    }

private:
    void recvLoop()
    {
        while (running_) {
            zmq::message_t msg;
            if (socket_.recv(msg, zmq::recv_flags::none)) {
                T value;
                std::memcpy(&value, msg.data(), sizeof(T));
                callback_(value);
            }
        }
    }

    std::string topic_;
    std::string endpoint_;
    zmq::socket_t socket_;
    Callback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace lux::communication::interprocess

