#pragma once
/// QoS compatibility checker.
///
/// Called when a Publisher and Subscriber discover each other.
/// Logs a warning when the combination is likely to cause message loss.

#include <string>
#include <cstdio>
#include <lux/communication/QoSProfile.hpp>

namespace lux::communication {

/// Check QoS compatibility between a publisher and subscriber.
/// Does NOT block the connection — only emits a diagnostic warning.
inline void checkQoSCompatibility(const QoSProfile& pub_qos,
                                   const QoSProfile& sub_qos,
                                   const std::string& topic_name)
{
    if (pub_qos.reliability == Reliability::BestEffort &&
        sub_qos.reliability == Reliability::Reliable)
    {
        std::fprintf(stderr,
            "[QoS WARNING] Topic '%s': Publisher is BestEffort but "
            "Subscriber expects Reliable — messages may be lost.\n",
            topic_name.c_str());
    }
}

} // namespace lux::communication
