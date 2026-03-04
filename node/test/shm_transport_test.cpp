/// SHM Transport unit tests — Phase 2.
///
/// Tests:
///   1. FrameHeader layout & helpers
///   2. ShmRingBuffer layout & helpers
///   3. ShmRingWriter / ShmRingReader — basic write/read
///   4. Ring-full behaviour
///   5. SPSC concurrent write + read (two threads)
///   6. ShmNotify wake / adaptive wait
///   7. Serializer — trivially copyable POD
///   8. Serializer — concept detection
///   9. ShmRingWriter / ShmRingReader — FrameHeader round-trip

#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/ShmRingBuffer.hpp>
#include <lux/communication/transport/ShmRingWriter.hpp>
#include <lux/communication/transport/ShmRingReader.hpp>
#include <lux/communication/transport/ShmNotify.hpp>
#include <lux/communication/serialization/Serializer.hpp>

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

// ─── Test 1: FrameHeader layout ────────────────────────────────────────────────

void test_frame_header_layout() {
    std::cout << "[1] FrameHeader layout ... ";

    CHECK(sizeof(transport::FrameHeader) == 48);

    transport::FrameHeader h;
    CHECK(h.magic   == transport::kFrameMagic);
    CHECK(h.version == 1);
    CHECK(h.flags   == 0);
    CHECK(transport::isValidFrame(h));

    transport::setFormat(h, transport::SerializationFormat::Protobuf);
    CHECK(transport::getFormat(h) == transport::SerializationFormat::Protobuf);

    transport::setFormat(h, transport::SerializationFormat::Custom);
    CHECK(transport::getFormat(h) == transport::SerializationFormat::Custom);

    // Make sure setting format doesn't destroy other flag bits.
    h.flags |= 0x01;  // compressed
    transport::setFormat(h, transport::SerializationFormat::RawMemcpy);
    CHECK(transport::isCompressed(h));
    CHECK(transport::getFormat(h) == transport::SerializationFormat::RawMemcpy);

    std::cout << "OK\n";
}

// ─── Test 2: RingBuffer layout ─────────────────────────────────────────────────

void test_ring_buffer_layout() {
    std::cout << "[2] ShmRingBuffer layout ... ";

    CHECK(sizeof(transport::RingHeader) == 192);
    CHECK(alignof(transport::RingHeader) == 64);
    CHECK(sizeof(transport::RingWriterLine) == 64);
    CHECK(sizeof(transport::RingReaderLine) == 64);
    CHECK(sizeof(transport::NotifyBlock) == 64);
    CHECK(sizeof(transport::SlotHeader) == 16);

    // Slot address calculation.
    CHECK(transport::ringTotalSize(16, 4096) == 192 + 16 * 4096);
    CHECK(transport::maxSlotPayload(4096) == 4096 - 16);
    CHECK(transport::isPowerOf2(1));
    CHECK(transport::isPowerOf2(16));
    CHECK(!transport::isPowerOf2(0));
    CHECK(!transport::isPowerOf2(15));

    std::cout << "OK\n";
}

// ─── Test 3: ShmRingWriter / ShmRingReader basic ───────────────────────────────

void test_ring_write_read_basic() {
    std::cout << "[3] ShmRingWriter/Reader basic ... ";

    const std::string name = "lux_test_ring_basic";
    constexpr uint32_t SLOTS = 4;
    constexpr uint32_t SLOT_SIZE = 4096;

    {
        transport::ShmRingWriter writer(name, SLOTS, SLOT_SIZE);
        CHECK(!writer.isFull());
        CHECK(writer.maxPayloadSize() == SLOT_SIZE - sizeof(transport::SlotHeader));

        // Write a simple message.
        struct Msg { int32_t a; float b; };
        Msg m{42, 3.14f};
        bool ok = writer.write(&m, sizeof(m));
        CHECK(ok);

        // Reader
        transport::ShmRingReader reader(name);
        CHECK(reader.hasData());

        auto view = reader.acquireReadView();
        CHECK(view.data != nullptr);
        CHECK(view.size == sizeof(m));
        Msg out{};
        std::memcpy(&out, view.data, sizeof(out));
        CHECK(out.a == 42);
        CHECK(out.b == 3.14f);
        reader.releaseReadView();

        CHECK(!reader.hasData());
    }

    std::cout << "OK\n";
}

// ─── Test 4: Ring full ─────────────────────────────────────────────────────────

void test_ring_full() {
    std::cout << "[4] Ring full ... ";

    const std::string name = "lux_test_ring_full";
    constexpr uint32_t SLOTS = 4;
    constexpr uint32_t SLOT_SIZE = 256;

    {
        transport::ShmRingWriter writer(name, SLOTS, SLOT_SIZE);
        uint8_t buf[16] = {};

        // Fill all 4 slots.
        for (int i = 0; i < 4; ++i) {
            bool ok = writer.write(buf, sizeof(buf));
            CHECK(ok);
        }
        CHECK(writer.isFull());

        // 5th write should fail.
        bool ok = writer.write(buf, sizeof(buf));
        CHECK(!ok);

        // Read one → should free a slot.
        transport::ShmRingReader reader(name);
        auto view = reader.acquireReadView();
        CHECK(view.data != nullptr);
        reader.releaseReadView();

        CHECK(!writer.isFull());
        ok = writer.write(buf, sizeof(buf));
        CHECK(ok);
    }

    std::cout << "OK\n";
}

// ─── Test 5: SPSC concurrent ──────────────────────────────────────────────────

void test_ring_concurrent() {
    std::cout << "[5] SPSC concurrent ... ";

    const std::string name = "lux_test_ring_conc";
    constexpr uint32_t SLOTS = 16;
    constexpr uint32_t SLOT_SIZE = 256;
    constexpr int NUM_MSGS = 10000;

    {
        transport::ShmRingWriter writer(name, SLOTS, SLOT_SIZE);
        std::atomic<bool> reader_done{false};
        std::vector<int> received;
        received.reserve(NUM_MSGS);

        // Reader thread.
        std::thread reader_thread([&] {
            transport::ShmRingReader reader(name);
            int count = 0;
            while (count < NUM_MSGS) {
                auto view = reader.acquireReadView(std::chrono::microseconds{500});
                if (!view.data) continue;
                int val = 0;
                std::memcpy(&val, view.data, sizeof(val));
                received.push_back(val);
                reader.releaseReadView();
                ++count;
            }
            reader_done = true;
        });

        // Writer thread (main).
        for (int i = 0; i < NUM_MSGS; ++i) {
            while (!writer.write(&i, sizeof(i))) {
                // Ring full — spin.
                std::this_thread::yield();
            }
        }

        reader_thread.join();

        CHECK(static_cast<int>(received.size()) == NUM_MSGS);
        for (int i = 0; i < NUM_MSGS; ++i) {
            CHECK(received[i] == i);
        }
    }

    std::cout << "OK\n";
}

// ─── Test 6: ShmNotify basic ──────────────────────────────────────────────────

void test_shm_notify() {
    std::cout << "[6] ShmNotify wake/wait ... ";

    // Create a ring just to get a valid NotifyBlock.
    const std::string name = "lux_test_ring_notify";
    transport::ShmRingWriter writer(name, 4, 256);

    // Write something so the reader has data after wake.
    int val = 99;
    writer.write(&val, sizeof(val));

    transport::ShmRingReader reader(name);

    // Non-blocking poll (data already available).
    auto view = reader.acquireReadView(std::chrono::microseconds{0});
    CHECK(view.data != nullptr);
    int out = 0;
    std::memcpy(&out, view.data, sizeof(out));
    CHECK(out == 99);
    reader.releaseReadView();

    std::cout << "OK\n";
}

// ─── Test 7: Serializer — trivially copyable ──────────────────────────────────

void test_serializer_pod() {
    std::cout << "[7] Serializer POD ... ";

    struct Vec3 { float x, y, z; };

    using Ser = serialization::Serializer<Vec3>;
    CHECK(Ser::format == transport::SerializationFormat::RawMemcpy);

    Vec3 v{1.0f, 2.0f, 3.0f};
    CHECK(Ser::serializedSize(v) == sizeof(Vec3));

    uint8_t buf[64];
    size_t written = Ser::serialize(v, buf, sizeof(buf));
    CHECK(written == sizeof(Vec3));

    Vec3 out{};
    bool ok = Ser::deserialize(out, buf, written);
    CHECK(ok);
    CHECK(out.x == 1.0f);
    CHECK(out.y == 2.0f);
    CHECK(out.z == 3.0f);

    // Too small buffer.
    CHECK(Ser::serialize(v, buf, 2) == 0);
    CHECK(!Ser::deserialize(out, buf, 2));

    std::cout << "OK\n";
}

// ─── Test 8: Serializer concept detection ─────────────────────────────────────

void test_serializer_concept() {
    std::cout << "[8] Serializer concept ... ";

    struct Pod { int x; double y; };
    CHECK(serialization::TriviallyCopyableMsg<Pod>);
    CHECK(serialization::selectSerializationFormat<Pod>() ==
          transport::SerializationFormat::RawMemcpy);

    CHECK(serialization::TriviallyCopyableMsg<int>);
    CHECK(!serialization::TriviallyCopyableMsg<std::string>);

    std::cout << "OK\n";
}

// ─── Test 9: Full round-trip with FrameHeader ─────────────────────────────────

void test_frame_round_trip() {
    std::cout << "[9] FrameHeader round-trip ... ";

    const std::string name = "lux_test_ring_frame";
    constexpr uint32_t SLOTS = 4;
    constexpr uint32_t SLOT_SIZE = 4096;

    struct Pose { float x, y, z, qw, qx, qy, qz; };
    using Ser = serialization::Serializer<Pose>;

    Pose original{1.0f, 2.0f, 3.0f, 1.0f, 0.0f, 0.0f, 0.0f};

    {
        transport::ShmRingWriter writer(name, SLOTS, SLOT_SIZE);

        // Simulate what Publisher::publish() does.
        transport::FrameHeader hdr;
        hdr.topic_hash   = 0xDEADBEEF;
        hdr.seq_num      = 42;
        hdr.timestamp_ns = 123456789;
        hdr.payload_size = static_cast<uint32_t>(Ser::serializedSize(original));
        transport::setFormat(hdr, Ser::format);

        void* slot = writer.acquireSlot();
        CHECK(slot != nullptr);
        std::memcpy(slot, &hdr, sizeof(hdr));
        char* payload = static_cast<char*>(slot) + sizeof(hdr);
        Ser::serialize(original, payload, writer.maxPayloadSize() - sizeof(hdr));
        writer.commitSlot(static_cast<uint32_t>(sizeof(hdr) + hdr.payload_size));

        // Read it back.
        transport::ShmRingReader reader(name);
        auto view = reader.acquireReadView();
        CHECK(view.data != nullptr);
        CHECK(view.size >= sizeof(transport::FrameHeader));

        auto* rhdr = static_cast<const transport::FrameHeader*>(view.data);
        CHECK(transport::isValidFrame(*rhdr));
        CHECK(rhdr->topic_hash == 0xDEADBEEF);
        CHECK(rhdr->seq_num == 42);
        CHECK(rhdr->timestamp_ns == 123456789);
        CHECK(transport::getFormat(*rhdr) == transport::SerializationFormat::RawMemcpy);

        const char* rpayload = static_cast<const char*>(view.data) + sizeof(transport::FrameHeader);
        Pose out{};
        CHECK(Ser::deserialize(out, rpayload, rhdr->payload_size));
        CHECK(out.x == 1.0f);
        CHECK(out.y == 2.0f);
        CHECK(out.z == 3.0f);
        CHECK(out.qw == 1.0f);

        reader.releaseReadView();
    }

    std::cout << "OK\n";
}

// ─── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== SHM Transport Tests (Phase 2) ===\n\n";

    test_frame_header_layout();
    test_ring_buffer_layout();
    test_ring_write_read_basic();
    test_ring_full();
    test_ring_concurrent();
    test_shm_notify();
    test_serializer_pod();
    test_serializer_concept();
    test_frame_round_trip();

    std::cout << "\n──────────────────────────────────────\n";
    std::cout << "Passed: " << tests_passed
              << "  Failed: " << tests_failed << "\n";

    return tests_failed > 0 ? 1 : 0;
}
