#include "lux/communication/platform/PlatformDefs.hpp"
#include <unistd.h>
#include <time.h>
#include <climits>

namespace lux::communication::platform
{
    uint32_t currentPid()
    {
        return static_cast<uint32_t>(getpid());
    }

    std::string currentHostname()
    {
        char buf[_POSIX_HOST_NAME_MAX + 1]{};
        if (gethostname(buf, sizeof(buf)) == 0)
        {
            buf[sizeof(buf) - 1] = '\0';
            return std::string(buf);
        }
        return "unknown";
    }

    uint64_t steadyNowNs()
    {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }
} // namespace lux::communication::platform
