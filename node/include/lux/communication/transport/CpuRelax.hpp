#pragma once

#include <thread>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

#if defined(__ARM_ACLE)
#include <arm_acle.h>
#endif

namespace lux::communication::transport::detail
{
    inline void cpuRelax() noexcept
    {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        // MSVC on x86/x64
        _mm_pause();

#elif defined(__i386__) || defined(__x86_64__)
        // GCC/Clang on x86/x86_64
        __builtin_ia32_pause();

#elif defined(__aarch64__) || defined(__arm__)
        // Arm / AArch64
        // Prefer ACLE intrinsic if the toolchain exposes it.
#if defined(__ARM_ACLE)
        __yield();
#else
        __asm__ __volatile__("yield");
#endif

#else
        // Generic fallback
        std::this_thread::yield();
#endif
    }
} // namespace lux::communication::transport::detail