#include "lux/communication/interprocess/Publisher.hpp"
#include "lux/communication/interprocess/Subscriber.hpp"
#include "lux/communication/UdpMultiCast.hpp"

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

} // namespace lux::communication::interprocess
