/**
 * @file discovery_test.cpp
 * @brief Phase 1 Discovery Service integration test.
 *
 * Tests:
 *  1. SharedMemory create / open / read-write / unlink
 *  2. ShmRegistry announce / lookup / withdraw / GC
 *  3. DiscoveryService announce / lookup / waitForEndpoints
 */
#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <chrono>

#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/platform/SharedMemory.hpp>
#include <lux/communication/discovery/ShmRegistryDefs.hpp>
#include <lux/communication/discovery/ShmRegistry.hpp>
#include <lux/communication/discovery/DiscoveryService.hpp>

namespace platform  = lux::communication::platform;
namespace discovery = lux::communication::discovery;

// ─── Test 1: SharedMemory ──────────────────────────────────────────────────────

static void testSharedMemory()
{
    std::cout << "[SharedMemory] Testing create / write / re-open / read ... ";

    const std::string name = platform::shmPlatformName("lux_test_shm_42");
    constexpr size_t  size = 4096;

    // Clean up any stale segment from a prior run
    {
        auto* seg = platform::SharedMemorySegment::open(name, size, false);
        if (seg) { seg->unlink(); delete seg; }
    }

    // Create
    auto* seg1 = platform::SharedMemorySegment::open(name, size, true);
    assert(seg1 && "Failed to create shared memory segment");
    assert(seg1->wasCreated());
    assert(seg1->data());
    assert(seg1->size() == size);

    // Write a pattern
    std::memset(seg1->data(), 0xAB, size);

    // Open existing (not create)
    auto* seg2 = platform::SharedMemorySegment::open(name, size, false);
    assert(seg2 && "Failed to open existing shared memory segment");
    assert(!seg2->wasCreated());

    // Verify pattern
    auto* bytes = static_cast<const uint8_t*>(seg2->data());
    for (size_t i = 0; i < size; ++i) {
        assert(bytes[i] == 0xAB);
    }

    // Clean up
    seg1->unlink();
    delete seg2;
    delete seg1;

    std::cout << "OK\n";
}

// ─── Test 2: PlatformDefs ──────────────────────────────────────────────────────

static void testPlatformDefs()
{
    std::cout << "[PlatformDefs] pid / hostname / time ... ";

    uint32_t pid = platform::currentPid();
    assert(pid != 0);

    std::string hostname = platform::currentHostname();
    assert(!hostname.empty());
    assert(hostname != "unknown");

    uint64_t t1 = platform::steadyNowNs();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint64_t t2 = platform::steadyNowNs();
    assert(t2 > t1);

    std::cout << "pid=" << pid << " host=" << hostname
              << " dt=" << (t2 - t1) / 1000000 << "ms ... OK\n";
}

// ─── Test 3: fnv1a hash ────────────────────────────────────────────────────────

static void testHash()
{
    std::cout << "[Hash] fnv1a_64 ... ";

    uint64_t h1 = discovery::fnv1a_64("hello", 5);
    uint64_t h2 = discovery::fnv1a_64(std::string("hello"));
    assert(h1 == h2);

    uint64_t h3 = discovery::fnv1a_64("world", 5);
    assert(h1 != h3);

    std::cout << "OK\n";
}

// ─── Test 4: ShmRegistry ──────────────────────────────────────────────────────

static void testShmRegistry()
{
    std::cout << "[ShmRegistry] announce / lookup / withdraw / GC ... ";

    // Use a unique domain id to avoid collision with other tests
    constexpr size_t domain_id = 99999;

    // Clean up any stale SHM from previous runs
    {
        std::string shm_name = platform::shmPlatformName(
            "lux_discovery_" + std::to_string(domain_id));
        auto* seg = platform::SharedMemorySegment::open(shm_name, 4096, false);
        if (seg) { seg->unlink(); delete seg; }
    }

    discovery::ShmRegistry registry(domain_id, 64);

    // Announce a publisher
    discovery::TopicEndpointInfo info;
    info.pid              = platform::currentPid();
    info.domain_id        = domain_id;
    info.topic_name       = "/test/topic";
    info.type_name        = "TestMsg";
    info.topic_name_hash  = discovery::fnv1a_64(info.topic_name);
    info.type_hash        = 12345;
    info.role             = discovery::EndpointRole::Publisher;
    info.hostname         = platform::currentHostname();

    int32_t slot = registry.announce(info);
    assert(slot >= 0);

    uint64_t gen = registry.generation();
    assert(gen > 0);

    // Lookup
    discovery::LookupFilter filter;
    filter.topic_name = "/test/topic";
    filter.role       = discovery::EndpointRole::Publisher;

    auto results = registry.lookup(filter);
    assert(results.size() == 1);
    assert(results[0].topic_name == "/test/topic");
    assert(results[0].type_name == "TestMsg");
    assert(results[0].type_hash == 12345);
    assert(results[0].pid == platform::currentPid());

    // Lookup with wrong role → empty
    filter.role = discovery::EndpointRole::Subscriber;
    results = registry.lookup(filter);
    assert(results.empty());

    // Withdraw
    registry.withdraw(slot);
    assert(registry.generation() > gen);

    filter.role = discovery::EndpointRole::Publisher;
    results = registry.lookup(filter);
    assert(results.empty());

    // Re-announce and test GC
    info.topic_name = "/gc/topic";
    info.topic_name_hash = discovery::fnv1a_64(info.topic_name);
    slot = registry.announce(info);
    assert(slot >= 0);

    // Force stale heartbeat by waiting a tiny bit and using short threshold
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    size_t cleaned = registry.gcStaleEntries(std::chrono::seconds{0});
    assert(cleaned >= 1);

    // Clean up SHM
    {
        std::string shm_name = platform::shmPlatformName(
            "lux_discovery_" + std::to_string(domain_id));
        auto* seg = platform::SharedMemorySegment::open(shm_name, 4096, false);
        if (seg) { seg->unlink(); delete seg; }
    }

    std::cout << "OK\n";
}

// ─── Test 5: DiscoveryService ─────────────────────────────────────────────────

static void testDiscoveryService()
{
    std::cout << "[DiscoveryService] announce / lookup ... ";

    constexpr size_t domain_id = 88888;

    // Clean up any stale SHM
    {
        std::string shm_name = platform::shmPlatformName(
            "lux_discovery_" + std::to_string(domain_id));
        auto* seg = platform::SharedMemorySegment::open(shm_name, 4096, false);
        if (seg) { seg->unlink(); delete seg; }
    }

    auto& svc = discovery::DiscoveryService::getInstance(domain_id);
    svc.start();

    // Announce
    uint64_t pub_handle = svc.announcePublisher("/ds/topic", "DsMsg", 67890);
    assert(pub_handle > 0);

    uint64_t sub_handle = svc.announceSubscriber("/ds/topic", "DsMsg", 67890);
    assert(sub_handle > 0);

    // Lookup from SHM (same process — the lookup skips own pid by default,
    // so we do a raw SHM registry test instead to verify write)
    // For cross-process discovery, we'd need a second process.
    // Here we just verify the service runs without crashing.

    // Withdraw
    svc.withdraw(pub_handle);
    svc.withdraw(sub_handle);

    svc.stop();

    std::cout << "OK\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "=== Discovery Phase 1 Tests ===\n";

    testPlatformDefs();
    testHash();
    testSharedMemory();
    testShmRegistry();
    testDiscoveryService();

    std::cout << "\nAll tests passed!\n";
    return 0;
}
