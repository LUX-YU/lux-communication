/// Phase 3 — Loopback Optimization unit tests.
///
/// Tests:
///   1. FrameHeader new flags (kFlagLoaned, kFlagPooled)
///   2. ShmRingWriter::cancelSlot — cancel + re-acquire
///   3. LoanedMessage construct + cancel
///   4. LoanedMessage construct + commit
///   5. LoanedMessage move semantics
///   6. LoanedMessage ring-full → invalid
///   7. ShmDataPool layout sizes
///   8. ShmDataPool allocate + release basic
///   9. ShmDataPool ref-count → auto-free
///  10. ShmDataPool pool-full → nullptr
///  11. ShmDataPool concurrent release
///  12. ShmDataPool split on alloc
///  13. HugePages TryHuge fallback
///  14. PoolDescriptor round-trip through ring

#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/ShmRingBuffer.hpp>
#include <lux/communication/transport/ShmRingWriter.hpp>
#include <lux/communication/transport/ShmRingReader.hpp>
#include <lux/communication/transport/LoanedMessage.hpp>
#include <lux/communication/transport/ShmDataPool.hpp>
#include <lux/communication/serialization/Serializer.hpp>
#include <lux/communication/platform/SharedMemory.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <string>

using namespace lux::communication;

// ─── Helpers ───────────────────────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "  FAIL: " #expr " (line " << __LINE__ << ")\n"; \
            ++tests_failed; \
        } else { \
            ++tests_passed; \
        } \
    } while (0)

// POD test message types.
struct TestPod {
    int32_t  x;
    float    y;
    double   z;
};
static_assert(serialization::TriviallyCopyableMsg<TestPod>);

struct LargePod {
    char data[128 * 1024]; // 128 KB — above kPoolThreshold
};
static_assert(serialization::TriviallyCopyableMsg<LargePod>);

// ─── Test 1: FrameHeader new flags ─────────────────────────────────────────────

void test_frame_header_new_flags() {
    std::cout << "[1] FrameHeader new flags ... ";

    transport::FrameHeader h;
    CHECK(!transport::isLoaned(h));
    CHECK(!transport::isPooled(h));

    transport::setLoaned(h);
    CHECK(transport::isLoaned(h));
    CHECK(!transport::isPooled(h));
    CHECK(transport::isValidFrame(h));

    transport::setPooled(h);
    CHECK(transport::isLoaned(h));
    CHECK(transport::isPooled(h));

    // Existing flags unaffected.
    transport::setFormat(h, transport::SerializationFormat::RawMemcpy);
    CHECK(transport::getFormat(h) == transport::SerializationFormat::RawMemcpy);
    CHECK(transport::isLoaned(h));
    CHECK(transport::isPooled(h));

    // Bit positions don't overlap.
    CHECK((transport::kFlagLoaned & 0x1F) == 0x10);   // bit 4
    CHECK((transport::kFlagPooled & 0x3F) == 0x20);   // bit 5

    std::cout << "OK\n";
}

// ─── Test 2: cancelSlot ────────────────────────────────────────────────────────

void test_cancel_slot() {
    std::cout << "[2] cancelSlot ... ";

    transport::ShmRingWriter writer("test_cancel_slot_ring", 4, 4096);

    // Acquire a slot.
    void* slot1 = writer.acquireSlot();
    CHECK(slot1 != nullptr);

    // Cancel it.
    writer.cancelSlot();

    // Re-acquire should return same slot (write_seq wasn't advanced).
    void* slot2 = writer.acquireSlot();
    CHECK(slot2 != nullptr);
    CHECK(slot2 == slot1);

    // Clean up: commit and release.
    writer.commitSlot(0);

    std::cout << "OK\n";
}

// ─── Test 3: LoanedMessage construct + cancel ──────────────────────────────────

void test_loaned_message_cancel() {
    std::cout << "[3] LoanedMessage cancel ... ";

    transport::ShmRingWriter writer("test_loan_cancel_ring", 4, 4096);

    transport::FrameHeader hdr;
    hdr.topic_hash = 42;
    hdr.seq_num = 1;
    hdr.payload_size = sizeof(TestPod);
    transport::setFormat(hdr, transport::SerializationFormat::RawMemcpy);
    transport::setLoaned(hdr);

    {
        transport::LoanedMessage<TestPod> loan(&writer, hdr);
        CHECK(loan.valid());
        CHECK(loan.get() != nullptr);

        // Fill data.
        loan->x = 100;
        loan->y = 2.5f;
        loan->z = 3.14;

        // Don't publish — destructor should cancel.
    }

    // Slot should be free, so acquiring should still succeed (same index).
    void* slot = writer.acquireSlot();
    CHECK(slot != nullptr);
    writer.cancelSlot();

    std::cout << "OK\n";
}

// ─── Test 4: LoanedMessage construct + commit ──────────────────────────────────

void test_loaned_message_commit() {
    std::cout << "[4] LoanedMessage commit ... ";

    transport::ShmRingWriter writer("test_loan_commit_ring", 4, 4096);
    transport::ShmRingReader reader("test_loan_commit_ring");

    transport::FrameHeader hdr;
    hdr.topic_hash = 42;
    hdr.seq_num = 1;
    hdr.payload_size = sizeof(TestPod);
    transport::setFormat(hdr, transport::SerializationFormat::RawMemcpy);
    transport::setLoaned(hdr);

    {
        transport::LoanedMessage<TestPod> loan(&writer, hdr);
        CHECK(loan.valid());
        loan->x = 42;
        loan->y = 1.5f;
        loan->z = 9.99;

        // Commit the loan (mark slot READY + advance write_seq).
        loan.commit(static_cast<uint32_t>(sizeof(transport::FrameHeader) + sizeof(TestPod)));
    }

    // Reader should see the data.
    auto view = reader.acquireReadView(std::chrono::milliseconds{100});
    CHECK(view.data != nullptr);
    CHECK(view.size >= sizeof(transport::FrameHeader) + sizeof(TestPod));

    auto* read_hdr = static_cast<const transport::FrameHeader*>(view.data);
    CHECK(transport::isLoaned(*read_hdr));
    CHECK(read_hdr->topic_hash == 42);

    const TestPod* pod = reinterpret_cast<const TestPod*>(
        static_cast<const char*>(view.data) + sizeof(transport::FrameHeader));
    CHECK(pod->x == 42);
    CHECK(pod->y == 1.5f);
    CHECK(pod->z == 9.99);

    reader.releaseReadView();

    std::cout << "OK\n";
}

// ─── Test 5: LoanedMessage move semantics ──────────────────────────────────────

void test_loaned_message_move() {
    std::cout << "[5] LoanedMessage move ... ";

    transport::ShmRingWriter writer("test_loan_move_ring", 4, 4096);

    transport::FrameHeader hdr;
    hdr.topic_hash = 99;
    hdr.seq_num = 5;
    hdr.payload_size = sizeof(TestPod);
    transport::setFormat(hdr, transport::SerializationFormat::RawMemcpy);

    transport::LoanedMessage<TestPod> loan1(&writer, hdr);
    CHECK(loan1.valid());
    loan1->x = 7;

    // Move construct.
    transport::LoanedMessage<TestPod> loan2(std::move(loan1));
    CHECK(!loan1.valid());
    CHECK(loan2.valid());
    CHECK(loan2->x == 7);

    // Move assign.
    transport::LoanedMessage<TestPod> loan3;
    CHECK(!loan3.valid());
    loan3 = std::move(loan2);
    CHECK(!loan2.valid());
    CHECK(loan3.valid());
    CHECK(loan3->x == 7);

    // Let loan3 destruct — should cancel safely.

    std::cout << "OK\n";
}

// ─── Test 6: LoanedMessage ring full → invalid ────────────────────────────────

void test_loaned_message_ring_full() {
    std::cout << "[6] LoanedMessage ring-full ... ";

    // Ring of 2 slots.
    transport::ShmRingWriter writer("test_loan_full_ring", 2, 4096);

    transport::FrameHeader hdr;
    hdr.payload_size = sizeof(TestPod);

    // Fill both slots.
    transport::LoanedMessage<TestPod> loan1(&writer, hdr);
    CHECK(loan1.valid());
    loan1.commit(static_cast<uint32_t>(sizeof(transport::FrameHeader) + sizeof(TestPod)));

    transport::LoanedMessage<TestPod> loan2(&writer, hdr);
    CHECK(loan2.valid());
    loan2.commit(static_cast<uint32_t>(sizeof(transport::FrameHeader) + sizeof(TestPod)));

    // Third loan should fail.
    transport::LoanedMessage<TestPod> loan3(&writer, hdr);
    CHECK(!loan3.valid());

    std::cout << "OK\n";
}

// ─── Test 7: ShmDataPool layout sizes ──────────────────────────────────────────

void test_pool_layout() {
    std::cout << "[7] ShmDataPool layout ... ";

    CHECK(sizeof(transport::PoolHeader)     == 64);
    CHECK(sizeof(transport::BlockHeader)    == 32);
    CHECK(sizeof(transport::PoolDescriptor) == 16);
    CHECK(alignof(transport::PoolHeader)    == 64);
    CHECK(alignof(transport::BlockHeader)   == 16);

    std::cout << "OK\n";
}

// ─── Test 8: ShmDataPool allocate + release ────────────────────────────────────

void test_pool_alloc_release() {
    std::cout << "[8] ShmDataPool alloc/release ... ";

    // 1 MB pool, min block 256B.
    transport::ShmDataPool pool("test_pool_alloc", 1024 * 1024, 256);

    CHECK(pool.capacity() == 1024 * 1024);
    CHECK(pool.allocatedBytes() == 0);

    // Allocate 1024 bytes with ref_count=1.
    auto result = pool.allocate(1024, 1);
    CHECK(result.payload != nullptr);
    CHECK(result.pool_offset > 0);
    CHECK(result.ref_count_offset > 0);
    CHECK(pool.allocatedBytes() > 0);

    // Write some data.
    std::memset(result.payload, 0xAB, 1024);

    // Read it back.
    const void* read_ptr = pool.read(result.pool_offset);
    CHECK(read_ptr != nullptr);
    CHECK(read_ptr == result.payload);

    // Release (ref_count goes from 1 to 0 → block freed).
    uint64_t alloc_before = pool.allocatedBytes();
    pool.release(result.ref_count_offset);
    CHECK(pool.allocatedBytes() < alloc_before);

    std::cout << "OK\n";
}

// ─── Test 9: ShmDataPool refcount auto-free ────────────────────────────────────

void test_pool_refcount() {
    std::cout << "[9] ShmDataPool refcount ... ";

    transport::ShmDataPool pool("test_pool_refcount", 1024 * 1024, 256);

    // ref_count = 3.
    auto result = pool.allocate(512, 3);
    CHECK(result.payload != nullptr);
    uint64_t alloc_after = pool.allocatedBytes();
    CHECK(alloc_after > 0);

    // Release 1st — still allocated.
    pool.release(result.ref_count_offset);
    CHECK(pool.allocatedBytes() == alloc_after);

    // Release 2nd — still allocated.
    pool.release(result.ref_count_offset);
    CHECK(pool.allocatedBytes() == alloc_after);

    // Release 3rd — freed.
    pool.release(result.ref_count_offset);
    CHECK(pool.allocatedBytes() < alloc_after);

    std::cout << "OK\n";
}

// ─── Test 10: ShmDataPool pool-full ────────────────────────────────────────────

void test_pool_full() {
    std::cout << "[10] ShmDataPool full ... ";

    // Small pool: 512 bytes (very tight).
    transport::ShmDataPool pool("test_pool_full", 512, 64);

    // Try to allocate more than the pool can hold.
    auto r1 = pool.allocate(400, 1);
    CHECK(r1.payload != nullptr);

    // Second alloc should fail — not enough space.
    auto r2 = pool.allocate(400, 1);
    CHECK(r2.payload == nullptr);

    // Release first, then second should succeed.
    pool.release(r1.ref_count_offset);
    auto r3 = pool.allocate(400, 1);
    CHECK(r3.payload != nullptr);

    pool.release(r3.ref_count_offset);

    std::cout << "OK\n";
}

// ─── Test 11: ShmDataPool concurrent release ───────────────────────────────────

void test_pool_concurrent_release() {
    std::cout << "[11] ShmDataPool concurrent release ... ";

    transport::ShmDataPool pool("test_pool_concurrent", 1024 * 1024, 256);

    constexpr int N = 4;
    auto result = pool.allocate(1024, N);
    CHECK(result.payload != nullptr);
    uint64_t alloc_bytes = pool.allocatedBytes();

    // N threads each release once.
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&pool, rc_offset = result.ref_count_offset]() {
            pool.release(rc_offset);
        });
    }
    for (auto& t : threads) t.join();

    // After all releases, block should be freed.
    CHECK(pool.allocatedBytes() < alloc_bytes);

    std::cout << "OK\n";
}

// ─── Test 12: ShmDataPool split ────────────────────────────────────────────────

void test_pool_split() {
    std::cout << "[12] ShmDataPool split ... ";

    // 64KB pool.
    transport::ShmDataPool pool("test_pool_split", 64 * 1024, 256);

    // Allocate a small block.
    auto r1 = pool.allocate(1024, 1);
    CHECK(r1.payload != nullptr);

    // Allocate another — should come from split remainder.
    auto r2 = pool.allocate(1024, 1);
    CHECK(r2.payload != nullptr);
    CHECK(r2.pool_offset != r1.pool_offset);

    // Both should be readable.
    std::memset(r1.payload, 0x11, 1024);
    std::memset(r2.payload, 0x22, 1024);
    CHECK(static_cast<const uint8_t*>(pool.read(r1.pool_offset))[0] == 0x11);
    CHECK(static_cast<const uint8_t*>(pool.read(r2.pool_offset))[0] == 0x22);

    pool.release(r1.ref_count_offset);
    pool.release(r2.ref_count_offset);

    std::cout << "OK\n";
}

// ─── Test 13: HugePages TryHuge fallback ───────────────────────────────────────

void test_huge_pages_fallback() {
    std::cout << "[13] HugePages TryHuge fallback ... ";

    const std::string name = platform::shmPlatformName("test_huge_try");

    // TryHuge should succeed even without huge page privileges (graceful fallback).
    auto* seg = platform::SharedMemorySegment::open(
        name, 4 * 1024 * 1024, true, platform::HugePageOption::TryHuge);
    CHECK(seg != nullptr);
    CHECK(seg->data() != nullptr);
    CHECK(seg->size() >= 4 * 1024 * 1024);

    // isHugePage() may be true or false depending on system config.
    // We just verify it doesn't crash.
    [[maybe_unused]] bool huge = seg->isHugePage();

    seg->unlink();
    delete seg;

    // None mode should also work (existing behaviour).
    auto* seg2 = platform::SharedMemorySegment::open(
        platform::shmPlatformName("test_huge_none"), 4096, true,
        platform::HugePageOption::None);
    CHECK(seg2 != nullptr);
    CHECK(!seg2->isHugePage());
    seg2->unlink();
    delete seg2;

    std::cout << "OK\n";
}

// ─── Test 14: PoolDescriptor round-trip through ring ───────────────────────────

void test_pool_descriptor_ring_roundtrip() {
    std::cout << "[14] PoolDescriptor ring roundtrip ... ";

    transport::ShmRingWriter writer("test_pool_desc_ring", 4, 4096);
    transport::ShmRingReader reader("test_pool_desc_ring");

    // Build frame header with kFlagPooled set.
    transport::FrameHeader hdr;
    hdr.topic_hash   = 12345;
    hdr.seq_num      = 1;
    hdr.payload_size = sizeof(transport::PoolDescriptor);
    transport::setFormat(hdr, transport::SerializationFormat::RawMemcpy);
    transport::setPooled(hdr);

    // Build descriptor.
    transport::PoolDescriptor desc;
    desc.pool_offset      = 0x1000;
    desc.data_size        = 65536;
    desc.ref_count_offset = 0x0044;

    // Write to ring.
    void* slot = writer.acquireSlot();
    CHECK(slot != nullptr);
    std::memcpy(slot, &hdr, sizeof(hdr));
    std::memcpy(static_cast<char*>(slot) + sizeof(hdr), &desc, sizeof(desc));
    writer.commitSlot(static_cast<uint32_t>(sizeof(hdr) + sizeof(desc)));

    // Read from ring.
    auto view = reader.acquireReadView(std::chrono::milliseconds{100});
    CHECK(view.data != nullptr);

    auto* read_hdr = static_cast<const transport::FrameHeader*>(view.data);
    CHECK(transport::isPooled(*read_hdr));
    CHECK(!transport::isLoaned(*read_hdr));
    CHECK(read_hdr->topic_hash == 12345);
    CHECK(read_hdr->payload_size == sizeof(transport::PoolDescriptor));

    const auto* read_desc = reinterpret_cast<const transport::PoolDescriptor*>(
        static_cast<const char*>(view.data) + sizeof(transport::FrameHeader));
    CHECK(read_desc->pool_offset == 0x1000);
    CHECK(read_desc->data_size == 65536);
    CHECK(read_desc->ref_count_offset == 0x0044);

    reader.releaseReadView();

    std::cout << "OK\n";
}

// ─── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Phase 3 Loopback Optimization Tests ===\n\n";

    test_frame_header_new_flags();
    test_cancel_slot();
    test_loaned_message_cancel();
    test_loaned_message_commit();
    test_loaned_message_move();
    test_loaned_message_ring_full();
    test_pool_layout();
    test_pool_alloc_release();
    test_pool_refcount();
    test_pool_full();
    test_pool_concurrent_release();
    test_pool_split();
    test_huge_pages_fallback();
    test_pool_descriptor_ring_roundtrip();

    std::cout << "\n=== Results: " << tests_passed << " passed, "
              << tests_failed << " failed ===\n";
    return tests_failed == 0 ? 0 : 1;
}
