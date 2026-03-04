#include "lux/communication/discovery/MulticastAnnouncer.hpp"
#include "lux/communication/discovery/ShmRegistryDefs.hpp"   // fnv1a_64
#include "lux/communication/platform/PlatformDefs.hpp"
#include "lux/communication/UdpMultiCast.hpp"

#include <cstring>
#include <chrono>
#include <thread>

namespace lux::communication::discovery {

MulticastAnnouncer::MulticastAnnouncer() = default;

MulticastAnnouncer::~MulticastAnnouncer() {
    stopListening();
}

// ──────── Send helpers ────────

void MulticastAnnouncer::announce(
    const DiscoveryPacket& pkt, int repeat_count, int interval_ms)
{
    lux::communication::UdpMultiCast mc{kDiscoveryGroup, kDiscoveryPort};

    for (int i = 0; i < repeat_count; ++i) {
        // UdpMultiCast::send takes void* (non-const); the data is not mutated.
        mc.send(const_cast<void*>(static_cast<const void*>(&pkt)),
                sizeof(DiscoveryPacket));
        if (i + 1 < repeat_count) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }
}

void MulticastAnnouncer::withdraw(const DiscoveryPacket& pkt) {
    lux::communication::UdpMultiCast mc{kDiscoveryGroup, kDiscoveryPort};
    mc.send(const_cast<void*>(static_cast<const void*>(&pkt)),
            sizeof(DiscoveryPacket));
}

void MulticastAnnouncer::query(
    const std::string& topic_name,
    uint64_t type_hash,
    uint64_t domain_id,
    uint8_t  role)
{
    DiscoveryPacket pkt{};
    pkt.type            = static_cast<uint8_t>(PacketType::Query);
    pkt.role            = role;
    pkt.domain_id       = domain_id;
    pkt.topic_name_hash = fnv1a_64(topic_name);
    pkt.type_hash       = type_hash;
    pkt.pid             = platform::currentPid();
    pkt.timestamp_ns    = platform::steadyNowNs();

    std::strncpy(pkt.topic_name, topic_name.c_str(), sizeof(pkt.topic_name) - 1);
    auto hn = platform::currentHostname();
    std::strncpy(pkt.hostname, hn.c_str(), sizeof(pkt.hostname) - 1);

    announce(pkt, 3, 50);
}

// ──────── Receive ────────

void MulticastAnnouncer::startListening(PacketCallback callback) {
    if (listening_.exchange(true)) return;  // already running

    listen_thread_ = std::thread([this, cb = std::move(callback)]() {
        listenLoop(cb);
    });
}

void MulticastAnnouncer::stopListening() {
    if (!listening_.exchange(false)) return;
    if (listen_thread_.joinable()) listen_thread_.join();
}

void MulticastAnnouncer::listenLoop(PacketCallback callback) {
    lux::communication::UdpMultiCast mc{kDiscoveryGroup, kDiscoveryPort};
    mc.bind();
    mc.joinGroup();
    mc.setRecvTimeout(200);   // 200 ms – lets us re-check listening_ flag

    char buffer[1024]{};
    lux::communication::SockAddr from{};

    while (listening_.load(std::memory_order_relaxed)) {
        int received = mc.recvFrom(buffer, sizeof(buffer), from);
        if (received < static_cast<int>(sizeof(DiscoveryPacket))) {
            continue;   // timeout or runt packet
        }

        const auto& pkt = *reinterpret_cast<const DiscoveryPacket*>(buffer);
        if (!isValidPacket(pkt)) continue;

        callback(pkt,
                 static_cast<uint32_t>(from.addr),
                 static_cast<uint16_t>(from.port));
    }
}

} // namespace lux::communication::discovery
