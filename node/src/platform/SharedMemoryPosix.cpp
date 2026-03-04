#include "lux/communication/platform/SharedMemory.hpp"
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lux::communication::platform {

struct SharedMemorySegment::Impl {
    int         fd       = -1;
    void*       mapped   = nullptr;
    size_t      sz       = 0;
    std::string nm;
    bool        created  = false;
    bool        is_huge  = false;
};

SharedMemorySegment* SharedMemorySegment::open(
    const std::string& name, size_t size, bool create, HugePageOption huge)
{
    int  fd      = -1;
    bool created = false;

    if (create) {
        // Exclusive create — tells us whether we are the first process.
        fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd >= 0) {
            created = true;
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                ::close(fd);
                shm_unlink(name.c_str());
                return nullptr;
            }
        } else if (errno == EEXIST) {
            fd = shm_open(name.c_str(), O_RDWR, 0666);
        }
    } else {
        fd = shm_open(name.c_str(), O_RDWR, 0666);
    }

    if (fd < 0) return nullptr;

    // ── Try huge-page mmap if requested ──
    if (huge == HugePageOption::TryHuge || huge == HugePageOption::ForceHuge) {
#ifdef MAP_HUGETLB
        constexpr size_t kHugePageSize = 2 * 1024 * 1024;
        size_t aligned_size = (size + kHugePageSize - 1) & ~(kHugePageSize - 1);

        // ftruncate to aligned size for huge pages.
        if (created) {
            if (ftruncate(fd, static_cast<off_t>(aligned_size)) != 0) {
                // fallback — keep original size
                aligned_size = size;
            }
        }

        void* mapped = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_HUGETLB, fd, 0);
        if (mapped != MAP_FAILED) {
            if (created)
                std::memset(mapped, 0, aligned_size);

            auto* seg  = new SharedMemorySegment();
            seg->impl_ = new Impl{fd, mapped, aligned_size, name, created, true};
            return seg;
        }

        if (huge == HugePageOption::ForceHuge) {
            ::close(fd);
            if (created) shm_unlink(name.c_str());
            return nullptr;
        }
        // Fall through to normal mmap.
#else
        if (huge == HugePageOption::ForceHuge) {
            ::close(fd);
            if (created) shm_unlink(name.c_str());
            return nullptr;
        }
#endif
    }

    // ── Normal mmap path ──
    void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(fd);
        return nullptr;
    }

    if (created) {
        std::memset(mapped, 0, size);
    }

    auto* seg  = new SharedMemorySegment();
    seg->impl_ = new Impl{fd, mapped, size, name, created, false};
    return seg;
}

SharedMemorySegment::~SharedMemorySegment() {
    if (impl_) {
        if (impl_->mapped && impl_->mapped != MAP_FAILED) {
            munmap(impl_->mapped, impl_->sz);
        }
        if (impl_->fd >= 0) {
            ::close(impl_->fd);
        }
        delete impl_;
    }
}

void*       SharedMemorySegment::data()        { return impl_ ? impl_->mapped : nullptr; }
const void* SharedMemorySegment::data()  const { return impl_ ? impl_->mapped : nullptr; }
size_t      SharedMemorySegment::size()  const { return impl_ ? impl_->sz     : 0; }

const std::string& SharedMemorySegment::name() const {
    static const std::string empty;
    return impl_ ? impl_->nm : empty;
}

bool SharedMemorySegment::wasCreated() const { return impl_ && impl_->created; }
bool SharedMemorySegment::isHugePage() const { return impl_ && impl_->is_huge; }

void SharedMemorySegment::unlink() {
    if (impl_) {
        shm_unlink(impl_->nm.c_str());
    }
}

} // namespace lux::communication::platform
