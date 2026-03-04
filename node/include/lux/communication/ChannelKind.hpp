#pragma once
#include <cstdint>

namespace lux::communication {

/// Transport channel type (runtime tag).
///   Intra — same-process shared_ptr zero-copy
///   Shm   — same-machine shared-memory ring
///   Net   — cross-machine UDP / TCP
enum class ChannelKind : uint8_t {
    Intra = 0,
    Shm   = 1,
    Net   = 2,
};

} // namespace lux::communication
