#include "lux/communication/interprocess/Publisher.hpp"
#include "lux/communication/interprocess/Subscriber.hpp"
#include "lux/communication/UdpMultiCast.hpp"
#include <zmq.hpp>
#include <cstring>

namespace lux::communication::interprocess {

zmq::context_t& globalContext()
{
    static zmq::context_t ctx{1};
    return ctx;
}

std::string defaultEndpoint(const std::string& topic)
{
#ifdef _WIN32
    size_t h = std::hash<std::string>{}(topic);
    return "tcp://127.0.0.1:" + std::to_string(20000 + (h % 10000));
#else
    return "ipc:///tmp/" + topic + ".ipc";
#endif
}

void sendDiscovery(const std::string& topic, const std::string& endpoint)
{
    try {
        UdpMultiCast mc{"239.255.0.1", 30000};
        std::string msg = topic + ':' + endpoint;
        mc.send(msg);
    } catch (...) {
    }
}

std::optional<std::string> waitDiscovery(const std::string& /*topic*/,
    std::chrono::milliseconds /*timeout*/)
{
    return std::nullopt;
}

class PublisherImpl : public PublisherImplBase
{
public:
    PublisherImpl(const std::string& topic, const std::string& endpoint)
        : topic_(topic), endpoint_(endpoint),
          socket_(globalContext(), zmq::socket_type::pub)
    {
        socket_.bind(endpoint_);
        sendDiscovery(topic_, endpoint_);
    }

    void publish(const void* data, size_t size) override
    {
        zmq::message_t m(size);
        std::memcpy(m.data(), data, size);
        socket_.send(m, zmq::send_flags::none);
    }

private:
    std::string topic_;
    std::string endpoint_;
    zmq::socket_t socket_;
};

class ZmqSubscriberSocket : public SubscriberSocket
{
public:
    ZmqSubscriberSocket()
        : socket_(globalContext(), zmq::socket_type::sub)
    {
        socket_.set(zmq::sockopt::subscribe, "");
        socket_.set(zmq::sockopt::rcvtimeo, 100);
        socket_.set(zmq::sockopt::linger, 0);
    }

    void connect(const std::string& endpoint) override
    {
        socket_.connect(endpoint);
    }

    bool receive(void* data, size_t size) override
    {
        zmq::message_t msg;
        auto result = socket_.recv(msg, zmq::recv_flags::none);
        if (!result) {
            return false;
        }
        std::memcpy(data, msg.data(), std::min<size_t>(size, msg.size()));
        return true;
    }

private:
    zmq::socket_t socket_;
};

std::unique_ptr<PublisherImplBase> makePublisherImpl(
    const std::string& topic, const std::string& endpoint)
{
    std::string ep = endpoint.empty() ? defaultEndpoint(topic) : endpoint;
    return std::make_unique<PublisherImpl>(topic, std::move(ep));
}

std::unique_ptr<SubscriberSocket> createSubscriberSocket()
{
    return std::make_unique<ZmqSubscriberSocket>();
}

} // namespace lux::communication::interprocess
