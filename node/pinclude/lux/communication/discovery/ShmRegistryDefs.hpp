#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace lux::communication::discovery {

// ──────── Constants ────────

static constexpr uint32_t kShmRegistryMagic   = 0x4C555852; // "LUXR"
static constexpr uint32_t kShmRegistryVersion = 1;

static constexpr size_t kMaxTopicNameLen = 240;
static constexpr size_t kMaxTypeNameLen  = 128;
static constexpr size_t kMaxEndpointLen  = 128;
static constexpr size_t kMaxHostnameLen  = 64;

// ──────── Enums ────────

enum class EntryStatus : uint32_t {
    Free     = 0,
    Active   = 1,
    Removing = 2,
};

enum class EndpointRole : uint8_t {
    Publisher  = 1,
    Subscriber = 2,
};

// ──────── SHM layout structs ────────

/// Lock-free atomic types must be truly lock-free for cross-process safety.
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "atomic<uint32_t> must be lock-free for shared memory");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "atomic<uint64_t> must be lock-free for shared memory");

/// A single entry in the SHM discovery registry.
/// Layout is fixed-size and cache-line-aligned for array indexing.
struct alignas(64) TopicEndpointEntry {
    // ── Identity ──
    std::atomic<uint32_t> status;           // EntryStatus
    uint32_t              pid;              // Process ID
    uint64_t              domain_id;        // Domain ID
    uint64_t              topic_name_hash;  // FNV-1a hash for fast filtering
    uint64_t              type_hash;        // lux::cxx::basic_type_info::hash()

    // ── Names (null-terminated) ──
    char topic_name[kMaxTopicNameLen];
    char type_name[kMaxTypeNameLen];

    // ── Endpoint ──
    uint8_t  role;                          // EndpointRole
    uint8_t  reserved1[3];
    uint32_t reserved2;
    char     shm_segment_name[kMaxEndpointLen];
    char     net_endpoint[kMaxEndpointLen];
    char     hostname[kMaxHostnameLen];

    // ── Liveness ──
    std::atomic<uint64_t> heartbeat_ns;     // steady_clock timestamp
};

static_assert(sizeof(TopicEndpointEntry) % 64 == 0,
              "TopicEndpointEntry size must be a multiple of 64");

/// Header of the SHM registry region (first 32 bytes).
struct ShmRegistryHeader {
    std::atomic<uint32_t> magic;            // kShmRegistryMagic once initialized
    uint32_t              version;
    uint32_t              max_entries;
    uint32_t              reserved;
    std::atomic<uint64_t> generation;       // Incremented on every write
    std::atomic<uint32_t> spinlock;         // 0 = unlocked, 1 = locked
    uint32_t              padding;
};

static_assert(sizeof(ShmRegistryHeader) == 32, "ShmRegistryHeader must be 32 bytes");

// ──────── Non-SHM helper struct for announce() input ────────

/// Plain C++ struct used to pass data into ShmRegistry::announce().
struct TopicEndpointInfo {
    uint32_t     pid              = 0;
    uint64_t     domain_id        = 0;
    uint64_t     topic_name_hash  = 0;
    uint64_t     type_hash        = 0;
    EndpointRole role             = EndpointRole::Publisher;
    std::string  topic_name;
    std::string  type_name;
    std::string  shm_segment_name;
    std::string  net_endpoint;
    std::string  hostname;
};

} // namespace lux::communication::discovery

// ──────── Hash utility ────────
// Include OUTSIDE the namespace to avoid nesting.
#include <lux/communication/Hash.hpp>

namespace lux::communication::discovery {

/// FNV-1a 64-bit hash.
/// Kept in discovery namespace for backward compatibility.
using lux::communication::fnv1a_64;

} // namespace lux::communication::discovery
