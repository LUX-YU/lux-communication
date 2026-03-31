#pragma once
#include <cstdint>
#include <lux/communication/transport/FrameHeader.hpp>

namespace lux::communication::transport
{
    /// IP + UDP header overhead (20 B IP + 8 B UDP).
    static constexpr uint32_t kIpUdpOverhead = 28;

    /// Maximum safe UDP payload to avoid IP fragmentation (Ethernet MTU 1500).
    static constexpr uint32_t kMaxUdpPayload = 1472; // 1500 - 28

    /// Maximum payload that fits in a single UDP datagram alongside a FrameHeader.
    static constexpr uint32_t kNetSmallMsgThreshold =
        kMaxUdpPayload - static_cast<uint32_t>(sizeof(FrameHeader)); // ≈ 1424

    /// Maximum message size supported by UDP fragmentation (64 MB).
    static constexpr uint32_t kMaxFragmentedMsgSize = 64u * 1024u * 1024u;

    /// Fragment header size (bytes).
    static constexpr uint32_t kFragHeaderSize = 24;

    /// Maximum payload per fragment.
    static constexpr uint32_t kMaxFragPayload =
        kMaxUdpPayload - kFragHeaderSize; // ≈ 1448

    /// Default TCP send / recv buffer size.
    static constexpr int kTcpBufferSize = 2 * 1024 * 1024; // 2 MB

    /// Default UDP recv buffer size.
    static constexpr int kUdpRecvBufferSize = 4 * 1024 * 1024; // 4 MB

    /// Fragment reassembly timeout (ms).
    static constexpr int kFragmentTimeoutMs = 200;

} // namespace lux::communication::transport
