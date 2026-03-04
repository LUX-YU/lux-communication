#include "lux/communication/discovery/DiscoveryService.hpp"

// Private headers (from pinclude/)
#include "lux/communication/discovery/ShmRegistry.hpp"
#include "lux/communication/discovery/MulticastAnnouncer.hpp"
#include "lux/communication/discovery/DiscoveryPacket.hpp"
#include "lux/communication/discovery/ShmRegistryDefs.hpp"
#include "lux/communication/platform/PlatformDefs.hpp"

#include <mutex>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace lux::communication::discovery {

// ════════════════════════════════════════════════════════════════════════════
//  Impl
// ════════════════════════════════════════════════════════════════════════════

struct DiscoveryService::Impl {
    size_t              domain_id;
    ShmRegistry         registry;
    MulticastAnnouncer  multicast;

    // ── heartbeat / GC thread ──
    std::thread         heartbeat_thread;
    std::atomic<bool>   running{false};

    // ── local endpoints registered by this process ──
    struct LocalEndpoint {
        uint64_t       handle;
        int32_t        shm_slot;
        TopicEndpoint  info;
    };
    std::mutex                  local_mutex;
    std::vector<LocalEndpoint>  local_endpoints;
    uint64_t                    next_handle = 1;

    // ── event listeners ──
    struct Listener {
        uint64_t          id;
        std::string       topic_filter;   // "" = match everything
        DiscoveryCallback callback;
    };
    std::mutex             listener_mutex;
    std::vector<Listener>  listeners;
    uint64_t               next_listener_id = 1;

    // ── known remote endpoints (from multicast) ──
    struct RemoteKey {
        std::string topic_name;
        uint32_t    pid;
        uint8_t     role;
        bool operator==(const RemoteKey& o) const {
            return topic_name == o.topic_name && pid == o.pid && role == o.role;
        }
    };
    struct RemoteKeyHash {
        size_t operator()(const RemoteKey& k) const {
            size_t h = std::hash<std::string>{}(k.topic_name);
            h ^= std::hash<uint32_t>{}(k.pid) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint8_t>{}(k.role) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::mutex remote_mutex;
    std::unordered_map<RemoteKey, TopicEndpoint, RemoteKeyHash> known_remotes;

    /// Tracks when each remote was last heard from (Announce / Reply / Heartbeat).
    std::unordered_map<RemoteKey, std::chrono::steady_clock::time_point, RemoteKeyHash>
        remote_last_seen;

    /// Discovery heartbeat configuration (matches NodeOptions defaults).
    uint32_t heartbeat_interval_ms = 2000;   ///< Send multicast heartbeat every N ms.
    uint32_t heartbeat_timeout_ms  = 6000;   ///< GC remote if not seen for N ms.

    // ── ctor ──
    explicit Impl(size_t domain_id_)
        : domain_id(domain_id_), registry(domain_id_) {}

    // ── heartbeat loop (runs in its own thread) ──
    void heartbeatLoop() {
        uint32_t my_pid = platform::currentPid();
        auto last_multicast_heartbeat = std::chrono::steady_clock::now();

        while (running.load(std::memory_order_relaxed)) {
            // ── 1. Local SHM heartbeat + GC ──
            registry.heartbeat(my_pid);
            registry.gcStaleEntries(std::chrono::seconds{5});

            // ── 2. Multicast heartbeat (send one Heartbeat per local endpoint) ──
            auto now = std::chrono::steady_clock::now();
            if (heartbeat_interval_ms > 0) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_multicast_heartbeat).count();
                if (elapsed_ms >= static_cast<int64_t>(heartbeat_interval_ms)) {
                    last_multicast_heartbeat = now;
                    sendMulticastHeartbeats(my_pid);
                }
            }

            // ── 3. GC stale remote endpoints ──
            if (heartbeat_timeout_ms > 0) {
                gcStaleRemotes(std::chrono::milliseconds{heartbeat_timeout_ms});
            }

            // Sleep ~1 s, checking the stop flag every 100 ms.
            for (int i = 0; i < 10 && running.load(std::memory_order_relaxed); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    // ── send one Heartbeat packet per local endpoint ──
    void sendMulticastHeartbeats(uint32_t my_pid) {
        std::lock_guard<std::mutex> lck(local_mutex);
        for (auto& le : local_endpoints) {
            uint8_t role_val = (le.info.role == TopicEndpoint::Role::Publisher) ? 1 : 2;
            DiscoveryPacket pkt{};
            pkt.type            = static_cast<uint8_t>(PacketType::Heartbeat);
            pkt.role            = role_val;
            pkt.domain_id       = domain_id;
            pkt.topic_name_hash = fnv1a_64(le.info.topic_name);
            pkt.type_hash       = le.info.type_hash;
            pkt.pid             = my_pid;
            pkt.timestamp_ns    = platform::steadyNowNs();
            std::strncpy(pkt.topic_name,   le.info.topic_name.c_str(),   sizeof(pkt.topic_name) - 1);
            std::strncpy(pkt.net_endpoint,  le.info.net_endpoint.c_str(), sizeof(pkt.net_endpoint) - 1);
            auto hn = platform::currentHostname();
            std::strncpy(pkt.hostname, hn.c_str(), sizeof(pkt.hostname) - 1);
            multicast.withdraw(pkt);   // single-send (same as withdraw)
        }
    }

    // ── remove remotes not heard from within timeout ──
    void gcStaleRemotes(std::chrono::milliseconds timeout) {
        auto now = std::chrono::steady_clock::now();
        std::vector<std::pair<RemoteKey, TopicEndpoint>> expired;

        {
            std::lock_guard<std::mutex> lck(remote_mutex);
            for (auto it = remote_last_seen.begin(); it != remote_last_seen.end(); ) {
                if ((now - it->second) > timeout) {
                    auto ep_it = known_remotes.find(it->first);
                    if (ep_it != known_remotes.end()) {
                        expired.emplace_back(it->first, ep_it->second);
                        known_remotes.erase(ep_it);
                    }
                    it = remote_last_seen.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // Fire EndpointLost events outside the lock.
        for (auto& [key, ep] : expired) {
            fireEvent(DiscoveryEventType::EndpointLost, ep);
        }
    }

    // ── multicast packet handler ──
    void onMulticastPacket(const DiscoveryPacket& pkt,
                           uint32_t /*from_addr*/, uint16_t /*from_port*/)
    {
        uint32_t my_pid = platform::currentPid();
        if (pkt.pid == my_pid)          return;   // ignore own packets
        if (pkt.domain_id != domain_id) return;   // wrong domain

        auto ptype = static_cast<PacketType>(pkt.type);

        if (ptype == PacketType::Announce || ptype == PacketType::Reply) {
            TopicEndpoint ep;
            ep.topic_name   = pkt.topic_name;
            ep.type_hash    = pkt.type_hash;
            ep.domain_id    = pkt.domain_id;
            ep.pid          = pkt.pid;
            ep.hostname     = pkt.hostname;
            ep.role         = (pkt.role == 1) ? TopicEndpoint::Role::Publisher
                                              : TopicEndpoint::Role::Subscriber;
            ep.net_endpoint = pkt.net_endpoint;

            RemoteKey key{ep.topic_name, ep.pid, pkt.role};

            bool is_new = false;
            {
                std::lock_guard<std::mutex> lck(remote_mutex);
                auto [it, inserted] = known_remotes.try_emplace(key, ep);
                is_new = inserted;
                if (!inserted) it->second = ep;   // update
                remote_last_seen[key] = std::chrono::steady_clock::now();
            }
            if (is_new) {
                fireEvent(DiscoveryEventType::EndpointDiscovered, ep);
            }
        }
        else if (ptype == PacketType::Heartbeat) {
            // Heartbeat: just refresh the last_seen timestamp for this remote.
            RemoteKey key{std::string(pkt.topic_name), pkt.pid, pkt.role};
            bool is_new_via_heartbeat = false;
            TopicEndpoint new_ep;

            {
                std::lock_guard<std::mutex> lck(remote_mutex);
                auto it = known_remotes.find(key);
                if (it != known_remotes.end()) {
                    remote_last_seen[key] = std::chrono::steady_clock::now();
                } else {
                    // First time seeing this remote via Heartbeat — treat as discovery.
                    new_ep.topic_name   = pkt.topic_name;
                    new_ep.type_hash    = pkt.type_hash;
                    new_ep.domain_id    = pkt.domain_id;
                    new_ep.pid          = pkt.pid;
                    new_ep.hostname     = pkt.hostname;
                    new_ep.role         = (pkt.role == 1) ? TopicEndpoint::Role::Publisher
                                                         : TopicEndpoint::Role::Subscriber;
                    new_ep.net_endpoint = pkt.net_endpoint;

                    known_remotes[key]    = new_ep;
                    remote_last_seen[key] = std::chrono::steady_clock::now();
                    is_new_via_heartbeat  = true;
                }
            }

            // Fire event outside the remote_mutex lock.
            if (is_new_via_heartbeat) {
                fireEvent(DiscoveryEventType::EndpointDiscovered, new_ep);
            }
        }
        else if (ptype == PacketType::Withdraw) {
            RemoteKey key{std::string(pkt.topic_name), pkt.pid, pkt.role};
            TopicEndpoint ep;
            bool removed = false;
            {
                std::lock_guard<std::mutex> lck(remote_mutex);
                auto it = known_remotes.find(key);
                if (it != known_remotes.end()) {
                    ep = it->second;
                    known_remotes.erase(it);
                    remote_last_seen.erase(key);
                    removed = true;
                }
            }
            if (removed) {
                fireEvent(DiscoveryEventType::EndpointLost, ep);
            }
        }
        else if (ptype == PacketType::Query) {
            // Reply with matching local endpoints.
            std::lock_guard<std::mutex> lck(local_mutex);
            for (auto& le : local_endpoints) {
                if (le.info.topic_name != pkt.topic_name) continue;
                uint8_t our_role_val = (le.info.role == TopicEndpoint::Role::Publisher) ? 1 : 2;
                if (pkt.role != 0 && our_role_val != pkt.role) continue;

                DiscoveryPacket reply{};
                reply.type            = static_cast<uint8_t>(PacketType::Reply);
                reply.role            = our_role_val;
                reply.domain_id       = domain_id;
                reply.topic_name_hash = fnv1a_64(le.info.topic_name);
                reply.type_hash       = le.info.type_hash;
                reply.pid             = my_pid;
                reply.timestamp_ns    = platform::steadyNowNs();
                std::strncpy(reply.topic_name,   le.info.topic_name.c_str(),   sizeof(reply.topic_name) - 1);
                std::strncpy(reply.net_endpoint,  le.info.net_endpoint.c_str(), sizeof(reply.net_endpoint) - 1);
                auto hn = platform::currentHostname();
                std::strncpy(reply.hostname, hn.c_str(), sizeof(reply.hostname) - 1);

                multicast.withdraw(reply);   // single-send (reuse withdraw helper)
            }
        }
    }

    void fireEvent(DiscoveryEventType type, const TopicEndpoint& ep) {
        std::lock_guard<std::mutex> lck(listener_mutex);
        DiscoveryEvent event{type, ep};
        for (auto& l : listeners) {
            if (l.topic_filter.empty() || l.topic_filter == ep.topic_name) {
                l.callback(event);
            }
        }
    }
};

// ════════════════════════════════════════════════════════════════════════════
//  Singleton
// ════════════════════════════════════════════════════════════════════════════

DiscoveryService& DiscoveryService::getInstance(size_t domain_id) {
    static std::mutex map_mutex;
    static std::unordered_map<size_t, std::unique_ptr<DiscoveryService>> instances;

    std::lock_guard<std::mutex> lck(map_mutex);
    auto it = instances.find(domain_id);
    if (it != instances.end()) return *it->second;

    auto* raw = new DiscoveryService(domain_id);
    instances[domain_id] = std::unique_ptr<DiscoveryService>(raw);
    return *instances[domain_id];
}

DiscoveryService::DiscoveryService(size_t domain_id)
    : impl_(std::make_unique<Impl>(domain_id)) {}

DiscoveryService::~DiscoveryService() {
    stop();
}

// ════════════════════════════════════════════════════════════════════════════
//  Announce / Withdraw
// ════════════════════════════════════════════════════════════════════════════

static DiscoveryPacket buildPacket(
    PacketType pt, uint8_t role, uint64_t domain_id,
    const std::string& topic_name, uint64_t type_hash,
    const std::string& net_endpoint)
{
    DiscoveryPacket pkt{};
    pkt.type            = static_cast<uint8_t>(pt);
    pkt.role            = role;
    pkt.domain_id       = domain_id;
    pkt.topic_name_hash = fnv1a_64(topic_name);
    pkt.type_hash       = type_hash;
    pkt.pid             = platform::currentPid();
    pkt.timestamp_ns    = platform::steadyNowNs();
    std::strncpy(pkt.topic_name,   topic_name.c_str(),   sizeof(pkt.topic_name) - 1);
    std::strncpy(pkt.net_endpoint, net_endpoint.c_str(), sizeof(pkt.net_endpoint) - 1);
    auto hn = platform::currentHostname();
    std::strncpy(pkt.hostname, hn.c_str(), sizeof(pkt.hostname) - 1);
    return pkt;
}

uint64_t DiscoveryService::announcePublisher(
    const std::string& topic_name,
    const std::string& type_name,
    uint64_t type_hash,
    const std::string& shm_name,
    const std::string& net_endpoint)
{
    uint32_t my_pid  = platform::currentPid();
    auto     hn      = platform::currentHostname();

    TopicEndpointInfo info;
    info.pid              = my_pid;
    info.domain_id        = impl_->domain_id;
    info.topic_name_hash  = fnv1a_64(topic_name);
    info.type_hash        = type_hash;
    info.role             = EndpointRole::Publisher;
    info.topic_name       = topic_name;
    info.type_name        = type_name;
    info.shm_segment_name = shm_name;
    info.net_endpoint     = net_endpoint;
    info.hostname         = hn;

    int32_t slot = impl_->registry.announce(info);

    uint64_t handle;
    {
        std::lock_guard<std::mutex> lck(impl_->local_mutex);
        handle = impl_->next_handle++;
        TopicEndpoint ep;
        ep.topic_name       = topic_name;
        ep.type_name        = type_name;
        ep.type_hash        = type_hash;
        ep.domain_id        = impl_->domain_id;
        ep.pid              = my_pid;
        ep.hostname         = hn;
        ep.role             = TopicEndpoint::Role::Publisher;
        ep.shm_segment_name = shm_name;
        ep.net_endpoint     = net_endpoint;
        impl_->local_endpoints.push_back({handle, slot, ep});
    }

    if (impl_->running.load(std::memory_order_relaxed)) {
        auto pkt = buildPacket(PacketType::Announce, 1,
                               impl_->domain_id, topic_name, type_hash, net_endpoint);
        impl_->multicast.announce(pkt);
    }

    return handle;
}

uint64_t DiscoveryService::announceSubscriber(
    const std::string& topic_name,
    const std::string& type_name,
    uint64_t type_hash,
    const std::string& shm_name,
    const std::string& net_endpoint)
{
    uint32_t my_pid  = platform::currentPid();
    auto     hn      = platform::currentHostname();

    TopicEndpointInfo info;
    info.pid              = my_pid;
    info.domain_id        = impl_->domain_id;
    info.topic_name_hash  = fnv1a_64(topic_name);
    info.type_hash        = type_hash;
    info.role             = EndpointRole::Subscriber;
    info.topic_name       = topic_name;
    info.type_name        = type_name;
    info.shm_segment_name = shm_name;
    info.net_endpoint     = net_endpoint;
    info.hostname         = hn;

    int32_t slot = impl_->registry.announce(info);

    uint64_t handle;
    {
        std::lock_guard<std::mutex> lck(impl_->local_mutex);
        handle = impl_->next_handle++;
        TopicEndpoint ep;
        ep.topic_name       = topic_name;
        ep.type_name        = type_name;
        ep.type_hash        = type_hash;
        ep.domain_id        = impl_->domain_id;
        ep.pid              = my_pid;
        ep.hostname         = hn;
        ep.role             = TopicEndpoint::Role::Subscriber;
        ep.shm_segment_name = shm_name;
        ep.net_endpoint     = net_endpoint;
        impl_->local_endpoints.push_back({handle, slot, ep});
    }

    if (impl_->running.load(std::memory_order_relaxed)) {
        auto pkt = buildPacket(PacketType::Announce, 2,
                               impl_->domain_id, topic_name, type_hash, net_endpoint);
        impl_->multicast.announce(pkt);
    }

    return handle;
}

void DiscoveryService::withdraw(uint64_t handle) {
    std::lock_guard<std::mutex> lck(impl_->local_mutex);

    for (auto it = impl_->local_endpoints.begin();
         it != impl_->local_endpoints.end(); ++it) {
        if (it->handle != handle) continue;

        impl_->registry.withdraw(it->shm_slot);

        if (impl_->running.load(std::memory_order_relaxed)) {
            uint8_t role_val = (it->info.role == TopicEndpoint::Role::Publisher) ? 1 : 2;
            auto pkt = buildPacket(PacketType::Withdraw, role_val,
                                   impl_->domain_id, it->info.topic_name,
                                   it->info.type_hash, it->info.net_endpoint);
            impl_->multicast.withdraw(pkt);
        }

        impl_->local_endpoints.erase(it);
        return;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Lookup
// ════════════════════════════════════════════════════════════════════════════

std::vector<TopicEndpoint> DiscoveryService::lookup(
    const std::string&  topic_name,
    TopicEndpoint::Role role,
    uint64_t            type_hash) const
{
    uint32_t my_pid = platform::currentPid();

    LookupFilter filter;
    filter.topic_name = topic_name;
    filter.type_hash  = type_hash;
    filter.role       = (role == TopicEndpoint::Role::Publisher)
                            ? EndpointRole::Publisher
                            : EndpointRole::Subscriber;

    auto shm_results = impl_->registry.lookup(filter);

    std::vector<TopicEndpoint> results;
    results.reserve(shm_results.size());

    for (auto& r : shm_results) {
        if (r.pid == my_pid) continue;   // skip self

        TopicEndpoint ep;
        ep.topic_name       = std::move(r.topic_name);
        ep.type_name        = std::move(r.type_name);
        ep.type_hash        = r.type_hash;
        ep.domain_id        = r.domain_id;
        ep.pid              = r.pid;
        ep.hostname         = std::move(r.hostname);
        ep.role             = role;
        ep.shm_segment_name = std::move(r.shm_segment_name);
        ep.net_endpoint     = std::move(r.net_endpoint);
        results.push_back(std::move(ep));
    }

    // Merge known remote endpoints (from multicast) without duplicates.
    {
        std::lock_guard<std::mutex> lck(impl_->remote_mutex);
        for (auto& [key, ep] : impl_->known_remotes) {
            if (ep.topic_name != topic_name || ep.role != role) continue;
            if (type_hash != 0 && ep.type_hash != type_hash)   continue;

            bool dup = false;
            for (auto& existing : results) {
                if (existing.pid == ep.pid) { dup = true; break; }
            }
            if (!dup) results.push_back(ep);
        }
    }

    return results;
}

std::vector<TopicEndpoint> DiscoveryService::waitForEndpoints(
    const std::string&  topic_name,
    TopicEndpoint::Role role,
    uint64_t            type_hash,
    std::chrono::milliseconds timeout) const
{
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        auto results = lookup(topic_name, role, type_hash);
        if (!results.empty()) return results;

        auto gen       = impl_->registry.generation();
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) break;

        impl_->registry.waitForChange(
            gen, std::min(remaining, std::chrono::milliseconds(50)));
    }

    return lookup(topic_name, role, type_hash);
}

// ════════════════════════════════════════════════════════════════════════════
//  Event listeners
// ════════════════════════════════════════════════════════════════════════════

uint64_t DiscoveryService::addListener(
    const std::string& topic_name, DiscoveryCallback callback)
{
    std::lock_guard<std::mutex> lck(impl_->listener_mutex);
    uint64_t id = impl_->next_listener_id++;
    impl_->listeners.push_back({id, topic_name, std::move(callback)});
    return id;
}

void DiscoveryService::removeListener(uint64_t listener_id) {
    std::lock_guard<std::mutex> lck(impl_->listener_mutex);
    auto& v = impl_->listeners;
    v.erase(
        std::remove_if(v.begin(), v.end(),
            [listener_id](const auto& l) { return l.id == listener_id; }),
        v.end());
}

// ════════════════════════════════════════════════════════════════════════════
//  Lifecycle
// ════════════════════════════════════════════════════════════════════════════

void DiscoveryService::start() {
    if (impl_->running.exchange(true)) return;   // already running

    impl_->multicast.startListening(
        [this](const DiscoveryPacket& pkt, uint32_t addr, uint16_t port) {
            impl_->onMulticastPacket(pkt, addr, port);
        });

    impl_->heartbeat_thread = std::thread([this] { impl_->heartbeatLoop(); });
}

void DiscoveryService::stop() {
    if (!impl_->running.exchange(false)) return;  // already stopped

    if (impl_->heartbeat_thread.joinable()) impl_->heartbeat_thread.join();
    impl_->multicast.stopListening();

    // Withdraw everything from SHM.
    impl_->registry.withdrawAll(platform::currentPid());

    // Send multicast WITHDRAW for each local endpoint.
    {
        std::lock_guard<std::mutex> lck(impl_->local_mutex);
        for (auto& le : impl_->local_endpoints) {
            uint8_t role_val = (le.info.role == TopicEndpoint::Role::Publisher) ? 1 : 2;
            auto pkt = buildPacket(PacketType::Withdraw, role_val,
                                   impl_->domain_id, le.info.topic_name,
                                   le.info.type_hash, le.info.net_endpoint);
            impl_->multicast.withdraw(pkt);
        }
    }
}

} // namespace lux::communication::discovery
