#pragma once
#include <memory>
#include <atomic>

namespace lux::communication::introprocess
{
    // You can customize the SBO buffer size according to your project
    constexpr std::size_t DEFAULT_SBO_SIZE = 40;

    template <typename T>
    struct RcBuffer
    {
        std::atomic<int> refCount{1};
        // Pointer to the actual object, whether it's in the internal buffer or allocated on the heap
        T * data = nullptr;

        // Whether using SBO
        bool inPlace = false;

        // Reserve an aligned storage
        static constexpr std::size_t SBO_SIZE = DEFAULT_SBO_SIZE;
        alignas(T) unsigned char sbo_[SBO_SIZE]; 
        // Or use std::aligned_storage_t<SBO_SIZE, alignof(T)> sbo_;

        // We can keep a default constructor here, but note that T must be constructed manually afterward
        RcBuffer() = default;

        // In order to use it uniformly in "makeRcUnique", we can also provide a variadic constructor
        template <class... Args>
        RcBuffer(Args&&... args)
        {
            construct(std::forward<Args>(args)...);
        }

        // Manually construct T
        template <class... Args>
        void construct(Args&&... args)
        {
            // Check if it fits in the SBO
            if constexpr (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_constructible_v<T, Args...>)
            {
                if (sizeof(T) <= SBO_SIZE && alignof(T) <= alignof(std::max_align_t))
                {
                    // Use SBO
                    data = reinterpret_cast<T*>(&sbo_[0]);
                    inPlace = true;
                    new (data) T(std::forward<Args>(args)...);  // placement new
                    return;
                }
            }
            // Otherwise, fallback to heap allocation
            data = new T(std::forward<Args>(args)...);
            inPlace = false;
        }

        // Destruction: if inPlace, only call T's destructor; otherwise delete
        ~RcBuffer()
        {
            if (inPlace)
            {
                data->~T();
            }
            else
            {
                delete data;
            }
        }
    };

    template <typename T>
    struct RcDeleter
    {
        RcBuffer<T> *rcBuf = nullptr;

        void operator()(T *ptr) noexcept
        {
            // When refCount reaches zero, release RcBuffer<T>
            if (rcBuf->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // Only then call RcBuffer<T>'s destructor
                delete rcBuf;
            }
        }
    };

    template <typename T, class... Args>
    std::unique_ptr<T, RcDeleter<T>> makeRcUnique(Args&&... args)
    {
        // Directly new a RcBuffer<T>, decide whether to use SBO during construction
        auto rc = new RcBuffer<T>(std::forward<Args>(args)...);

        // refCount is already 1 by default, we can set it once more here if needed
        rc->refCount.store(1, std::memory_order_relaxed);

        // Return a unique_ptr
        return std::unique_ptr<T, RcDeleter<T>>(rc->data, RcDeleter<T>{rc});
    }

    template <typename T>
    std::unique_ptr<T, RcDeleter<T>> rcAttach(RcBuffer<T> *rc)
    {
        rc->refCount.fetch_add(1, std::memory_order_relaxed);
        return std::unique_ptr<T, RcDeleter<T>>(rc->data, RcDeleter<T>{rc});
    }
}
