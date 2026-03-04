#pragma once
#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <lux/communication/visibility.h>
#include "DiscoveryPacket.hpp"

namespace lux::communication::discovery {

/// Sends and receives UDP multicast discovery packets on the LAN.
/// Uses the existing UdpMultiCast helper for the underlying socket.
class LUX_COMMUNICATION_PUBLIC MulticastAnnouncer {
public:
    MulticastAnnouncer();
    ~MulticastAnnouncer();

    MulticastAnnouncer(const MulticastAnnouncer&) = delete;
    MulticastAnnouncer& operator=(const MulticastAnnouncer&) = delete;

    // ──── Send ────

    /// Broadcast an ANNOUNCE / REPLY packet (repeated for reliability).
    void announce(const DiscoveryPacket& pkt,
                  int repeat_count = 3,
                  int interval_ms  = 50);

    /// Send a single WITHDRAW packet.
    void withdraw(const DiscoveryPacket& pkt);

    /// Build and send a QUERY packet for the given topic.
    void query(const std::string& topic_name,
               uint64_t type_hash,
               uint64_t domain_id,
               uint8_t  role);

    // ──── Receive ────

    using PacketCallback = std::function<void(const DiscoveryPacket& pkt,
                                              uint32_t from_addr,
                                              uint16_t from_port)>;

    /// Start a background thread that listens for multicast packets.
    void startListening(PacketCallback callback);

    /// Stop the background listener.
    void stopListening();

    bool isListening() const { return listening_.load(std::memory_order_relaxed); }

private:
    void listenLoop(PacketCallback callback);

    std::thread       listen_thread_;
    std::atomic<bool> listening_{false};
};

} // namespace lux::communication::discovery
