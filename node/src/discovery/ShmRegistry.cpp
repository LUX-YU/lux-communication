#include "lux/communication/discovery/ShmRegistry.hpp"
#include "lux/communication/platform/SharedMemory.hpp"
#include "lux/communication/platform/PlatformDefs.hpp"

#include <cstring>
#include <thread>
#include <chrono>
#include <stdexcept>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define LUX_CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define LUX_CPU_PAUSE() __asm__ volatile("yield")
#else
#define LUX_CPU_PAUSE() ((void)0)
#endif

namespace lux::communication::discovery
{
    // ──────── Construction / destruction ────────

    ShmRegistry::ShmRegistry(size_t domain_id, size_t max_entries)
        : domain_id_(domain_id)
    {
        std::string shm_name = platform::shmPlatformName(
            "lux_discovery_" + std::to_string(domain_id));

        size_t total_size = sizeof(ShmRegistryHeader) + max_entries * sizeof(TopicEndpointEntry);

        shm_ = platform::SharedMemorySegment::open(shm_name, total_size, /*create=*/true);
        if (!shm_)
        {
            throw std::runtime_error("ShmRegistry: failed to open SHM segment: " + shm_name);
        }

        auto *hdr = header();

        if (shm_->wasCreated())
        {
            // First process – initialise the header.
            hdr->version = kShmRegistryVersion;
            hdr->max_entries = static_cast<uint32_t>(max_entries);
            hdr->reserved = 0;
            hdr->padding = 0;
            hdr->generation.store(0, std::memory_order_relaxed);
            hdr->spinlock.store(0, std::memory_order_relaxed);
            // Magic written last with release so all prior writes are visible.
            hdr->magic.store(kShmRegistryMagic, std::memory_order_release);
        }
        else
        {
            // Opened an existing segment – wait for initialisation to finish.
            int spins = 0;
            while (hdr->magic.load(std::memory_order_acquire) != kShmRegistryMagic)
            {
                if (++spins > 100000)
                {
                    throw std::runtime_error("ShmRegistry: timeout waiting for initialisation");
                }
                std::this_thread::yield();
            }
        }
    }

    ShmRegistry::~ShmRegistry()
    {
        delete shm_;
        shm_ = nullptr;
    }

    // ──────── Spinlock ────────

    void ShmRegistry::lock()
    {
        auto &lk = header()->spinlock;
        auto start = std::chrono::steady_clock::now();

        for (;;)
        {
            // Fast CAS attempt.
            uint32_t expected = 0;
            if (lk.compare_exchange_weak(expected, 1,
                                         std::memory_order_acquire, std::memory_order_relaxed))
            {
                return;
            }

            // Spin with PAUSE / YIELD instructions.
            for (int i = 0; i < 64; ++i)
            {
                LUX_CPU_PAUSE();
                expected = 0;
                if (lk.compare_exchange_weak(expected, 1,
                                             std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return;
                }
            }

            std::this_thread::yield();

            // Guard against a crashed lock holder: force-acquire after 2 s.
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(2))
            {
                lk.store(1, std::memory_order_acquire);
                return;
            }
        }
    }

    void ShmRegistry::unlock()
    {
        header()->spinlock.store(0, std::memory_order_release);
    }

    // ──────── Accessors ────────

    ShmRegistryHeader *ShmRegistry::header()
    {
        return reinterpret_cast<ShmRegistryHeader *>(shm_->data());
    }
    const ShmRegistryHeader *ShmRegistry::header() const
    {
        return reinterpret_cast<const ShmRegistryHeader *>(shm_->data());
    }

    TopicEndpointEntry *ShmRegistry::entryAt(size_t index)
    {
        auto *base = reinterpret_cast<char *>(shm_->data()) + sizeof(ShmRegistryHeader);
        return reinterpret_cast<TopicEndpointEntry *>(base + index * sizeof(TopicEndpointEntry));
    }
    const TopicEndpointEntry *ShmRegistry::entryAt(size_t index) const
    {
        auto *base = reinterpret_cast<const char *>(shm_->data()) + sizeof(ShmRegistryHeader);
        return reinterpret_cast<const TopicEndpointEntry *>(
            base + index * sizeof(TopicEndpointEntry));
    }

    int32_t ShmRegistry::findFreeSlot() const
    {
        uint32_t max = header()->max_entries;
        for (uint32_t i = 0; i < max; ++i)
        {
            if (entryAt(i)->status.load(std::memory_order_relaxed) == static_cast<uint32_t>(EntryStatus::Free))
            {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    // ──────── Write operations ────────

    int32_t ShmRegistry::announce(const TopicEndpointInfo &info)
    {
        lock();

        int32_t slot = findFreeSlot();
        if (slot < 0)
        {
            unlock();
            return -1; // registry full
        }

        auto *e = entryAt(static_cast<size_t>(slot));

        e->pid = info.pid;
        e->domain_id = info.domain_id;
        e->topic_name_hash = info.topic_name_hash;
        e->type_hash = info.type_hash;
        e->role = static_cast<uint8_t>(info.role);
        e->reserved1[0] = e->reserved1[1] = e->reserved1[2] = 0;
        e->reserved2 = 0;

        std::memset(e->topic_name, 0, kMaxTopicNameLen);
        std::strncpy(e->topic_name, info.topic_name.c_str(), kMaxTopicNameLen - 1);

        std::memset(e->type_name, 0, kMaxTypeNameLen);
        std::strncpy(e->type_name, info.type_name.c_str(), kMaxTypeNameLen - 1);

        std::memset(e->shm_segment_name, 0, kMaxEndpointLen);
        std::strncpy(e->shm_segment_name, info.shm_segment_name.c_str(), kMaxEndpointLen - 1);

        std::memset(e->net_endpoint, 0, kMaxEndpointLen);
        std::strncpy(e->net_endpoint, info.net_endpoint.c_str(), kMaxEndpointLen - 1);

        std::memset(e->hostname, 0, kMaxHostnameLen);
        std::strncpy(e->hostname, info.hostname.c_str(), kMaxHostnameLen - 1);

        e->heartbeat_ns.store(platform::steadyNowNs(), std::memory_order_relaxed);

        // Mark active last – makes the entry visible to readers.
        e->status.store(static_cast<uint32_t>(EntryStatus::Active),
                        std::memory_order_release);

        header()->generation.fetch_add(1, std::memory_order_release);
        unlock();
        return slot;
    }

    void ShmRegistry::withdraw(int32_t slot_index)
    {
        if (slot_index < 0 || static_cast<uint32_t>(slot_index) >= header()->max_entries)
            return;

        lock();
        entryAt(static_cast<size_t>(slot_index))
            ->status.store(static_cast<uint32_t>(EntryStatus::Free),
                           std::memory_order_release);
        header()->generation.fetch_add(1, std::memory_order_release);
        unlock();
    }

    void ShmRegistry::withdrawAll(uint32_t pid)
    {
        lock();

        uint32_t max = header()->max_entries;
        bool changed = false;

        for (uint32_t i = 0; i < max; ++i)
        {
            auto *e = entryAt(i);
            if (e->status.load(std::memory_order_relaxed) == static_cast<uint32_t>(EntryStatus::Active) && e->pid == pid)
            {
                e->status.store(static_cast<uint32_t>(EntryStatus::Free),
                                std::memory_order_relaxed);
                changed = true;
            }
        }

        if (changed)
        {
            header()->generation.fetch_add(1, std::memory_order_release);
        }
        unlock();
    }

    void ShmRegistry::heartbeat(uint32_t pid)
    {
        // No lock needed – only our own entries' heartbeat field is written.
        uint64_t now = platform::steadyNowNs();
        uint32_t max = header()->max_entries;

        for (uint32_t i = 0; i < max; ++i)
        {
            auto *e = entryAt(i);
            if (e->status.load(std::memory_order_relaxed) == static_cast<uint32_t>(EntryStatus::Active) && e->pid == pid)
            {
                e->heartbeat_ns.store(now, std::memory_order_relaxed);
            }
        }
    }

    // ──────── Read operations ────────

    std::vector<LookupResult> ShmRegistry::lookup(const LookupFilter &filter) const
    {
        std::vector<LookupResult> results;
        uint64_t name_hash = fnv1a_64(filter.topic_name);
        uint32_t max = header()->max_entries;

        for (uint32_t i = 0; i < max; ++i)
        {
            auto *e = entryAt(i);

            if (e->status.load(std::memory_order_acquire) != static_cast<uint32_t>(EntryStatus::Active))
                continue;

            if (e->topic_name_hash != name_hash)
                continue;
            if (e->role != static_cast<uint8_t>(filter.role))
                continue;
            if (filter.type_hash != 0 && e->type_hash != filter.type_hash)
                continue;

            // Confirm topic name (hash-collision guard).
            if (std::strncmp(e->topic_name,
                             filter.topic_name.c_str(), kMaxTopicNameLen) != 0)
                continue;

            LookupResult r;
            r.pid = e->pid;
            r.domain_id = e->domain_id;
            r.topic_name = e->topic_name;
            r.type_name = e->type_name;
            r.type_hash = e->type_hash;
            r.role = static_cast<EndpointRole>(e->role);
            r.shm_segment_name = e->shm_segment_name;
            r.net_endpoint = e->net_endpoint;
            r.hostname = e->hostname;
            results.push_back(std::move(r));
        }

        return results;
    }

    uint64_t ShmRegistry::generation() const
    {
        return header()->generation.load(std::memory_order_acquire);
    }

    bool ShmRegistry::waitForChange(
        uint64_t known_generation,
        std::chrono::milliseconds timeout) const
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        // Spin phase.
        for (int i = 0; i < 100; ++i)
        {
            if (header()->generation.load(std::memory_order_acquire) != known_generation)
                return true;
            LUX_CPU_PAUSE();
        }

        // Yield / sleep phase.
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (header()->generation.load(std::memory_order_acquire) != known_generation)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return header()->generation.load(std::memory_order_acquire) != known_generation;
    }

    // ──────── Maintenance ────────

    size_t ShmRegistry::gcStaleEntries(std::chrono::seconds stale_timeout)
    {
        uint64_t now = platform::steadyNowNs();
        uint64_t threshold_ns = static_cast<uint64_t>(stale_timeout.count()) * 1000000000ULL;

        lock();

        uint32_t max = header()->max_entries;
        size_t cleaned = 0;

        for (uint32_t i = 0; i < max; ++i)
        {
            auto *e = entryAt(i);
            if (e->status.load(std::memory_order_relaxed) != static_cast<uint32_t>(EntryStatus::Active))
                continue;

            uint64_t hb = e->heartbeat_ns.load(std::memory_order_relaxed);
            if (now > hb && (now - hb) > threshold_ns)
            {
                e->status.store(static_cast<uint32_t>(EntryStatus::Free),
                                std::memory_order_relaxed);
                ++cleaned;
            }
        }

        if (cleaned > 0)
        {
            header()->generation.fetch_add(1, std::memory_order_release);
        }

        unlock();
        return cleaned;
    }

} // namespace lux::communication::discovery
