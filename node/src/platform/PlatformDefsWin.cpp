#include "lux/communication/platform/PlatformDefs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace lux::communication::platform
{
    uint32_t currentPid()
    {
        return static_cast<uint32_t>(GetCurrentProcessId());
    }

    std::string currentHostname()
    {
        char buf[256]{};
        DWORD size = sizeof(buf);
        if (GetComputerNameExA(ComputerNameDnsHostname, buf, &size))
        {
            return std::string(buf, size);
        }
        return "unknown";
    }

    uint64_t steadyNowNs()
    {
        static const LARGE_INTEGER freq = []
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return f;
        }();

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<uint64_t>(now.QuadPart) * 1000000000ULL / static_cast<uint64_t>(freq.QuadPart);
    }

} // namespace lux::communication::platform
