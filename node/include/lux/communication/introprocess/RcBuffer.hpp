#pragma once
#include <memory>
#include <atomic>

namespace lux::communication::introprocess
{
    // 你可以根据项目情况自定义 SBO 缓冲大小
    constexpr std::size_t DEFAULT_SBO_SIZE = 40;

    template <typename T>
    struct RcBuffer
    {
        std::atomic<int> refCount{1};
        // 指向实际对象的指针，无论是在内置缓冲中还是堆上分配，都会赋给 data
        T * data = nullptr;

        // 是否使用SBO
        bool inPlace = false;

        // 预留一块对齐存储
        static constexpr std::size_t SBO_SIZE = DEFAULT_SBO_SIZE;
        alignas(T) unsigned char sbo_[SBO_SIZE]; 
        // 或者用 std::aligned_storage_t<SBO_SIZE, alignof(T)> sbo_;

        // 这里可以保留一个默认构造函数，但要注意必须之后再手动构造 T
        RcBuffer() = default;

        // 为了在 "makeRcUnique" 里统一使用，我们也可以给一个可变参构造
        template <class... Args>
        RcBuffer(Args&&... args)
        {
            construct(std::forward<Args>(args)...);
        }

        // 手动构造T
        template <class... Args>
        void construct(Args&&... args)
        {
            // 判断是否符合在SBO里放置
            if constexpr (std::is_nothrow_move_constructible_v<T> || std::is_nothrow_constructible_v<T, Args...>)
            {
                if (sizeof(T) <= SBO_SIZE && alignof(T) <= alignof(std::max_align_t))
                {
                    // 进入SBO
                    data = reinterpret_cast<T*>(&sbo_[0]);
                    inPlace = true;
                    new (data) T(std::forward<Args>(args)...);  // placement new
                    return;
                }
            }
            // 否则 fallback 到堆分配
            data = new T(std::forward<Args>(args)...);
            inPlace = false;
        }

        // 析构：若 inPlace 则只调用 T 的析构，否则 delete
        ~RcBuffer()
        {
            if (inPlace)
            {
                data->~T();      // 手动析构
            }
            else
            {
                delete data;     // 删除堆内对象
            }
        }
    };

    template <typename T>
    struct RcDeleter
    {
        RcBuffer<T> *rcBuf = nullptr;

        void operator()(T *ptr) noexcept
        {
            // refCount 归零时，释放 RcBuffer<T>
            if (rcBuf->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // 这时才调用 RcBuffer<T> 的析构
                delete rcBuf;
            }
        }
    };

    template <typename T, class... Args>
    std::unique_ptr<T, RcDeleter<T>> makeRcUnique(Args&&... args)
    {
        // 直接 new 一个 RcBuffer<T>，构造时就决定是否 SBO
        auto rc = new RcBuffer<T>(std::forward<Args>(args)...);

        // refCount 默认就是 1，这里可以再设一次
        rc->refCount.store(1, std::memory_order_relaxed);

        // 返回 unique_ptr
        return std::unique_ptr<T, RcDeleter<T>>(rc->data, RcDeleter<T>{rc});
    }

    template <typename T>
    std::unique_ptr<T, RcDeleter<T>> rcAttach(RcBuffer<T> *rc)
    {
        rc->refCount.fetch_add(1, std::memory_order_relaxed);
        return std::unique_ptr<T, RcDeleter<T>>(rc->data, RcDeleter<T>{rc});
    }
}