#pragma once
#include <cstdint>
#include <cstring>

namespace lux::communication::transport
{
    static constexpr uint32_t kFrameMagic = 0x4C555846; // "LUXF"

    /// Flags bits
    ///   bit 0    : compressed
    ///   bit 1    : encrypted
    ///   bit 2-3  : serialization format (SerializationFormat)
    ///   bit 4    : loaned  (message constructed in-place via loan())
    ///   bit 5    : pooled  (payload is a PoolDescriptor in ShmDataPool)

    enum class SerializationFormat : uint8_t
    {
        RawMemcpy = 0, // trivially copyable → direct memcpy
        Protobuf = 1,  // protobuf SerializeToArray / ParseFromArray
        Custom = 2,    // user-provided lux_serialize / lux_deserialize
        Reserved = 3,
    };

    /// Fixed-size message frame header (48 bytes).
    /// Placed at the beginning of every ring-buffer slot payload.
    struct FrameHeader
    {
        uint32_t magic = kFrameMagic; // 0x00
        uint16_t version = 1;         // 0x04
        uint16_t flags = 0;           // 0x06
        uint64_t topic_hash = 0;      // 0x08
        uint64_t seq_num = 0;         // 0x10
        uint64_t timestamp_ns = 0;    // 0x18
        uint32_t payload_size = 0;    // 0x20  (excludes this header)
        uint32_t header_crc = 0;      // 0x24  (optional, debug use)
        uint64_t reserved = 0;        // 0x28  pad to 48B
    };

    static_assert(sizeof(FrameHeader) == 48, "FrameHeader must be exactly 48 bytes");

    // ──── Flag helpers ────

    inline SerializationFormat getFormat(const FrameHeader &h)
    {
        return static_cast<SerializationFormat>((h.flags >> 2) & 0x3);
    }

    inline void setFormat(FrameHeader &h, SerializationFormat fmt)
    {
        h.flags = static_cast<uint16_t>(
            (h.flags & ~uint16_t(0x0C)) | (static_cast<uint16_t>(fmt) << 2));
    }

    inline bool isCompressed(const FrameHeader &h) { return (h.flags & 0x01) != 0; }
    inline bool isEncrypted(const FrameHeader &h) { return (h.flags & 0x02) != 0; }

    /// bit 4: message was constructed in-place via loan() (informational)
    static constexpr uint16_t kFlagLoaned = 0x10;
    /// bit 5: payload is a PoolDescriptor (real data in ShmDataPool)
    static constexpr uint16_t kFlagPooled = 0x20;

    inline bool isLoaned(const FrameHeader &h) { return (h.flags & kFlagLoaned) != 0; }
    inline void setLoaned(FrameHeader &h) { h.flags |= kFlagLoaned; }

    inline bool isPooled(const FrameHeader &h) { return (h.flags & kFlagPooled) != 0; }
    inline void setPooled(FrameHeader &h) { h.flags |= kFlagPooled; }

    /// bit 6: message was reassembled from UDP fragments (informational / debug)
    static constexpr uint16_t kFlagReassembled = 0x40;

    inline bool isReassembled(const FrameHeader &h) { return (h.flags & kFlagReassembled) != 0; }
    inline void setReassembled(FrameHeader &h) { h.flags |= kFlagReassembled; }

    /// bit 7: publisher marked the message as Reliable (Phase 6 QoS).
    static constexpr uint16_t kFlagReliable = 0x80;

    inline bool isReliable(const FrameHeader &h) { return (h.flags & kFlagReliable) != 0; }
    inline void setReliable(FrameHeader &h) { h.flags |= kFlagReliable; }

    /// bit 8: TCP heartbeat Ping (Publisher → Subscriber, payload_size == 0).
    static constexpr uint16_t kFlagPing = 0x0100;

    inline bool isPing(const FrameHeader &h) { return (h.flags & kFlagPing) != 0; }
    inline void setPing(FrameHeader &h) { h.flags |= kFlagPing; }

    /// bit 9: TCP heartbeat Pong (Subscriber → Publisher, reply to Ping).
    static constexpr uint16_t kFlagPong = 0x0200;

    inline bool isPong(const FrameHeader &h) { return (h.flags & kFlagPong) != 0; }
    inline void setPong(FrameHeader &h) { h.flags |= kFlagPong; }

    /// Control frames (Ping / Pong) carry no user payload.
    inline bool isControlFrame(const FrameHeader &h)
    {
        return (h.flags & (kFlagPing | kFlagPong)) != 0;
    }

    /// Build a minimal control frame (Ping or Pong).
    inline FrameHeader makeControlFrame(uint16_t flag)
    {
        FrameHeader hdr{};
        hdr.flags = flag;
        hdr.payload_size = 0;
        return hdr;
    }

    inline bool isValidFrame(const FrameHeader &h)
    {
        return h.magic == kFrameMagic && h.version == 1;
    }

} // namespace lux::communication::transport
