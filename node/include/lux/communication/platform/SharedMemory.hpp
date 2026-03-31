#pragma once
#include <cstddef>
#include <string>
#include <lux/communication/visibility.h>

namespace lux::communication::platform
{
    /// Huge page selection.
    enum class HugePageOption
    {
        None,      ///< Standard pages (4KB)
        TryHuge,   ///< Try huge pages, fallback to standard if unavailable
        ForceHuge, ///< Require huge pages, return nullptr if unavailable
    };

    /// RAII wrapper for a named shared memory segment.
    /// Provides cross-platform create/open/map functionality.
    ///   Linux:   shm_open + mmap
    ///   Windows: CreateFileMapping + MapViewOfFile
    class LUX_COMMUNICATION_PUBLIC SharedMemorySegment
    {
    public:
        /// Create or open a named shared memory segment.
        /// @param name    Platform-specific name (use shmPlatformName() to generate)
        /// @param size    Byte size of the segment
        /// @param create  If true, create the segment (or open if it already exists).
        ///                If false, only open an existing segment (returns nullptr on failure).
        /// @param huge    Huge pages option (default: None — standard pages).
        /// @return A heap-allocated segment, or nullptr on failure. Caller owns the pointer.
        static SharedMemorySegment *open(const std::string &name, size_t size, bool create,
                                         HugePageOption huge = HugePageOption::None);

        ~SharedMemorySegment();

        SharedMemorySegment(const SharedMemorySegment &) = delete;
        SharedMemorySegment &operator=(const SharedMemorySegment &) = delete;

        /// Mapped memory address.
        void *data();
        const void *data() const;

        /// Size of the mapped region.
        size_t size() const;

        /// Segment name.
        const std::string &name() const;

        /// True if this call was the one that created the segment (vs opened existing).
        bool wasCreated() const;

        /// True if this segment is backed by huge pages.
        bool isHugePage() const;

        /// Remove the underlying OS shared memory object.
        /// On Linux: shm_unlink.  On Windows: no-op (auto-cleaned when all handles close).
        void unlink();

    private:
        SharedMemorySegment() = default;
        struct Impl;
        Impl *impl_ = nullptr;
    };

} // namespace lux::communication::platform
