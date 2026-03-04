#pragma once
/// TransportSelector — decide the optimal transport ChannelKind for a
/// (local, remote) endpoint pair at runtime.

#include <lux/communication/ChannelKind.hpp>
#include <lux/communication/NodeOptions.hpp>
#include <lux/communication/discovery/DiscoveryService.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>

namespace lux::communication {

/// Select the best transport for communicating with a remote endpoint.
///
/// Decision order:
///   1. Same PID + same hostname → Intra  (shared_ptr zero-copy)
///   2. Same hostname            → Shm    (shared-memory ring)
///   3. Different hostname       → Net    (UDP / TCP)
inline ChannelKind selectTransport(
    const discovery::TopicEndpoint& remote,
    const NodeOptions& opts)
{
    const uint32_t my_pid  = platform::currentPid();
    const auto&    my_host = platform::currentHostname();

    // 1. Same process?
    if (opts.enable_intra &&
        remote.pid == my_pid &&
        remote.hostname == my_host)
    {
        return ChannelKind::Intra;
    }

    // 2. Same machine?
    if (opts.enable_shm &&
        remote.hostname == my_host)
    {
        return ChannelKind::Shm;
    }

    // 3. Cross-machine
    if (opts.enable_net &&
        !remote.net_endpoint.empty())
    {
        return ChannelKind::Net;
    }

    // Fallback: best effort intra (only works same-process)
    return ChannelKind::Intra;
}

} // namespace lux::communication
