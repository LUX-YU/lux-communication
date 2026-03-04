#include "lux/communication/platform/SharedMemory.hpp"
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace lux::communication::platform {

struct SharedMemorySegment::Impl {
    HANDLE      hMap    = NULL;
    void*       mapped  = nullptr;
    size_t      sz      = 0;
    std::string nm;
    bool        created  = false;
    bool        is_huge  = false;
};

/// Try to enable SeLockMemoryPrivilege (required for SEC_LARGE_PAGES).
static bool enableLockMemoryPrivilege() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueA(NULL, "SeLockMemoryPrivilege",
                               &tp.Privileges[0].Luid)) {
        CloseHandle(hToken);
        return false;
    }

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    DWORD err = GetLastError();
    CloseHandle(hToken);
    return ok && err == ERROR_SUCCESS;
}

SharedMemorySegment* SharedMemorySegment::open(
    const std::string& name, size_t size, bool create, HugePageOption huge)
{
    HANDLE hMap = NULL;
    bool   created = false;
    bool   is_huge = false;

    if (create) {
        // ── Try huge-page path first ──
        if (huge == HugePageOption::TryHuge || huge == HugePageOption::ForceHuge) {
            if (enableLockMemoryPrivilege()) {
                SIZE_T large_min = GetLargePageMinimum();
                if (large_min > 0) {
                    SIZE_T aligned = (size + large_min - 1) & ~(large_min - 1);
                    DWORD sizeHigh = static_cast<DWORD>(aligned >> 32);
                    DWORD sizeLow  = static_cast<DWORD>(aligned & 0xFFFFFFFF);
                    hMap = CreateFileMappingA(
                        INVALID_HANDLE_VALUE, NULL,
                        PAGE_READWRITE | SEC_COMMIT | SEC_LARGE_PAGES,
                        sizeHigh, sizeLow, name.c_str());
                    if (hMap) {
                        void* mapped = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, aligned);
                        if (mapped) {
                            std::memset(mapped, 0, aligned);
                            auto* seg  = new SharedMemorySegment();
                            seg->impl_ = new Impl{hMap, mapped, aligned, name, true, true};
                            return seg;
                        }
                        CloseHandle(hMap);
                        hMap = NULL;
                    }
                }
            }
            if (huge == HugePageOption::ForceHuge)
                return nullptr;
            // Fall through to normal path.
        }

        // ── Normal path ──
        DWORD sizeHigh = static_cast<DWORD>(size >> 32);
        DWORD sizeLow  = static_cast<DWORD>(size & 0xFFFFFFFF);
        hMap = CreateFileMappingA(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            sizeHigh, sizeLow, name.c_str());
        if (hMap && GetLastError() != ERROR_ALREADY_EXISTS) {
            created = true;
        }
    } else {
        hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    }

    if (!hMap) return nullptr;

    void* mapped = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!mapped) {
        CloseHandle(hMap);
        return nullptr;
    }

    if (created) {
        std::memset(mapped, 0, size);
    }

    auto* seg    = new SharedMemorySegment();
    seg->impl_   = new Impl{hMap, mapped, size, name, created, false};
    return seg;
}

SharedMemorySegment::~SharedMemorySegment() {
    if (impl_) {
        if (impl_->mapped) UnmapViewOfFile(impl_->mapped);
        if (impl_->hMap)   CloseHandle(impl_->hMap);
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
    // Windows file mappings are reference-counted; they disappear
    // automatically when the last handle is closed.  Nothing to do here.
}

} // namespace lux::communication::platform
