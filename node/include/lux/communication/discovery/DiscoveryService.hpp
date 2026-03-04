#pragma once
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <cstdint>
#include <memory>
#include <lux/communication/visibility.h>

namespace lux::communication::discovery {

// ──────── Public data types ────────

/// Describes one endpoint (publisher or subscriber) of a topic.
struct LUX_COMMUNICATION_PUBLIC TopicEndpoint {
    std::string topic_name;
    std::string type_name;
    uint64_t    type_hash  = 0;
    uint64_t    domain_id  = 0;
    uint32_t    pid        = 0;
    std::string hostname;

    enum class Role : uint8_t { Publisher = 1, Subscriber = 2 };
    Role role = Role::Publisher;

    /// Transport hints (populated by Phase 2+).
    std::string shm_segment_name;
    std::string net_endpoint;
};

enum class DiscoveryEventType {
    EndpointDiscovered,
    EndpointLost,
};

struct DiscoveryEvent {
    DiscoveryEventType type;
    TopicEndpoint      endpoint;
};

using DiscoveryCallback = std::function<void(const DiscoveryEvent&)>;

// ──────── Discovery service ────────

/// Unified discovery service combining same-machine SHM registry and LAN multicast.
/// One instance per domain_id (singleton).
class LUX_COMMUNICATION_PUBLIC DiscoveryService {
public:
    /// Get (or create) the singleton for the given domain.
    static DiscoveryService& getInstance(size_t domain_id = 0);

    ~DiscoveryService();

    // ──── Announce / Withdraw ────

    /// Register a publisher endpoint.  Returns a handle for later withdraw().
    uint64_t announcePublisher(const std::string& topic_name,
                               const std::string& type_name,
                               uint64_t type_hash,
                               const std::string& shm_name      = "",
                               const std::string& net_endpoint   = "");

    /// Register a subscriber endpoint.
    uint64_t announceSubscriber(const std::string& topic_name,
                                const std::string& type_name,
                                uint64_t type_hash,
                                const std::string& shm_name      = "",
                                const std::string& net_endpoint   = "");

    /// Withdraw a previously announced endpoint.
    void withdraw(uint64_t handle);

    // ──── Lookup ────

    /// Synchronous lookup of matching remote endpoints.
    std::vector<TopicEndpoint> lookup(
        const std::string& topic_name,
        TopicEndpoint::Role role,
        uint64_t type_hash = 0) const;

    /// Block until at least one matching endpoint appears, or timeout.
    std::vector<TopicEndpoint> waitForEndpoints(
        const std::string& topic_name,
        TopicEndpoint::Role role,
        uint64_t type_hash = 0,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{500}) const;

    // ──── Event listeners ────

    /// Subscribe to discovery events for a given topic. Returns a listener ID.
    uint64_t addListener(const std::string& topic_name, DiscoveryCallback callback);

    /// Remove a previously registered listener.
    void removeListener(uint64_t listener_id);

    // ──── Lifecycle ────

    /// Start the multicast listener and heartbeat threads.
    void start();

    /// Stop everything and withdraw all local endpoints.
    void stop();

private:
    DiscoveryService(size_t domain_id);
    DiscoveryService(const DiscoveryService&) = delete;
    DiscoveryService& operator=(const DiscoveryService&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lux::communication::discovery
