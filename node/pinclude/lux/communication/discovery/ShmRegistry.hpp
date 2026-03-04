#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <lux/communication/visibility.h>
#include "ShmRegistryDefs.hpp"

namespace lux::communication::platform { class SharedMemorySegment; }

namespace lux::communication::discovery {

// ──────── Query / result types ────────

struct LookupFilter {
    std::string  topic_name;
    uint64_t     type_hash = 0;        // 0 = any type
    EndpointRole role = EndpointRole::Publisher;
};

struct LookupResult {
    uint32_t     pid;
    uint64_t     domain_id;
    std::string  topic_name;
    std::string  type_name;
    uint64_t     type_hash;
    EndpointRole role;
    std::string  shm_segment_name;
    std::string  net_endpoint;
    std::string  hostname;
};

// ──────── ShmRegistry ────────

/// Cross-process discovery registry backed by a POSIX/Win32 shared memory segment.
/// Multiple processes sharing the same domain_id see the same registry.
class LUX_COMMUNICATION_PUBLIC ShmRegistry {
public:
    /// Create or open the registry for the given domain.
    /// @param domain_id   Domain number (maps to a unique SHM name)
    /// @param max_entries Maximum slots (only effective on first creation)
    explicit ShmRegistry(size_t domain_id, size_t max_entries = 1024);
    ~ShmRegistry();

    ShmRegistry(const ShmRegistry&) = delete;
    ShmRegistry& operator=(const ShmRegistry&) = delete;

    // ──── Write ────

    /// Register an endpoint. Returns the slot index (>= 0), or -1 if full.
    int32_t announce(const TopicEndpointInfo& info);

    /// Unregister a previously announced slot.
    void withdraw(int32_t slot_index);

    /// Unregister every entry belonging to the given PID.
    void withdrawAll(uint32_t pid);

    /// Refresh the heartbeat timestamp for all entries owned by pid.
    void heartbeat(uint32_t pid);

    // ──── Read ────

    /// Find matching entries.
    std::vector<LookupResult> lookup(const LookupFilter& filter) const;

    /// Current generation counter (incremented on each mutation).
    uint64_t generation() const;

    /// Block until generation changes or timeout. Returns true on change.
    bool waitForChange(uint64_t known_generation,
                       std::chrono::milliseconds timeout) const;

    // ──── Maintenance ────

    /// Mark entries with stale heartbeats as Free. Returns count cleaned.
    size_t gcStaleEntries(std::chrono::seconds stale_timeout = std::chrono::seconds{5});

private:
    void lock();
    void unlock();

    ShmRegistryHeader*          header();
    const ShmRegistryHeader*    header() const;

    TopicEndpointEntry*         entryAt(size_t index);
    const TopicEndpointEntry*   entryAt(size_t index) const;

    int32_t findFreeSlot() const;

    platform::SharedMemorySegment* shm_ = nullptr;
    size_t domain_id_;
};

} // namespace lux::communication::discovery
