#pragma once
#include <string>
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication::platform {

/// Convert a logical name like "lux_discovery_0" to a platform-specific SHM name.
/// Linux:   "/lux_discovery_0"
/// Windows: "Local\\lux_discovery_0"
inline std::string shmPlatformName(const std::string& logical_name) {
#ifdef _WIN32
    return "Local\\" + logical_name;
#else
    return "/" + logical_name;
#endif
}

/// Get the current process ID.
LUX_COMMUNICATION_PUBLIC uint32_t currentPid();

/// Get the hostname of this machine.
LUX_COMMUNICATION_PUBLIC std::string currentHostname();

/// Get a monotonic (steady) clock timestamp in nanoseconds.
LUX_COMMUNICATION_PUBLIC uint64_t steadyNowNs();

} // namespace lux::communication::platform
