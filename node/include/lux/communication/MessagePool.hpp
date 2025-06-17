#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace lux::communication
{
    // --------------------------------------------------
    // 1. MessagePool – lock‑free fixed‑capacity object pool
    // --------------------------------------------------

    /// @tparam T          message/data type
    /// @tparam Capacity   maximum number of in‑flight messages
    template<class T, std::size_t Capacity = 1024>
    class MessagePool
    {
        static_assert(Capacity > 0, "Capacity must be > 0");

        struct alignas(64) Entry
        {
            std::atomic<uint32_t> ref{ 0 };                 ///< refcount; 0 == free slot
            std::byte storage[sizeof(T)];
        };

    public:
        MessagePool() = default;
        MessagePool(const MessagePool&) = delete;
        MessagePool& operator=(const MessagePool&) = delete;

        ~MessagePool()
        {
            // Destroy any still‑alive objects (should be none in correct usage)
            for (auto& e : slots_)
            {
                if (e.ref.load(std::memory_order_relaxed))
                {
                    destroy(&e);
                }
            }
        }

        /// Allocate a fresh object in‑place and return a unique handle
        template<class... Args>
        auto allocate(Args&&... args)
        {
            for (std::size_t attempt = 0; attempt < Capacity; ++attempt)
            {
                std::size_t idx = (head_.fetch_add(1, std::memory_order_relaxed) + attempt) % Capacity;
                Entry& e = slots_[idx];

                uint32_t expect = 0;
                if (e.ref.compare_exchange_strong(expect, 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                {
                    // Construct object in storage
                    T* p = std::construct_at(reinterpret_cast<T*>(e.storage), std::forward<Args>(args)...);
                    return LoanedMessage<T>(p, this, static_cast<uint32_t>(idx));
                }
            }
            throw std::bad_alloc();   // pool exhausted
        }

        /// Increase refcount (used when message fan‑out到多个 subscriber)
        void add_ref(uint32_t idx)
        {
            slots_[idx].ref.fetch_add(1, std::memory_order_acq_rel);
        }

        /// Return slot; destroy object when最后一个句柄归还
        void release(uint32_t idx)
        {
            Entry& e = slots_[idx];
            if (e.ref.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                destroy(&e);
                e.ref.store(0, std::memory_order_release);
            }
        }

    private:
        static void destroy(Entry* e)
        {
            std::destroy_at(reinterpret_cast<T*>(e->storage));
        }

    private:
        std::array<Entry, Capacity> slots_{};
        std::atomic<std::size_t>   head_{ 0 };
    };

    template<class T>
    class LoanedMessage
    {
        template<class, std::size_t> friend class MessagePool;

    public:
        using element_type = std::conditional_t<std::is_const_v<T>, const std::remove_const_t<T>, T>;

        LoanedMessage() = default;

        // movable, not copyable
        LoanedMessage(const LoanedMessage&) = delete;
        LoanedMessage& operator=(const LoanedMessage&) = delete;

        LoanedMessage(LoanedMessage&& rhs) noexcept { move_from(rhs); }
        LoanedMessage& operator=(LoanedMessage&& rhs) noexcept
        {
            if (this != &rhs)
            {
                reset();
                move_from(rhs);
            }
            return *this;
        }

        ~LoanedMessage() { reset(); }

        /* --------------- pointer‑like API --------------- */
        element_type* get() { return ptr_; }
        const element_type* get() const { return ptr_; }
        element_type& operator*() { return *ptr_; }
        const element_type& operator*() const { return *ptr_; }
        element_type* operator->() { return ptr_; }
        const element_type* operator->() const { return ptr_; }

        explicit operator bool() const { return ptr_ != nullptr; }

    private:
        // construct only via MessagePool
        LoanedMessage(element_type* p, void* pool, uint32_t idx)
            : ptr_(p), pool_(pool), idx_(idx) {
        }

        void reset()
        {
            if (ptr_)
            {
                static_cast<MessagePool<std::remove_const_t<element_type>>*>(pool_)->release(idx_);
                ptr_ = nullptr;
            }
        }

        void move_from(LoanedMessage& rhs) noexcept
        {
            ptr_ = rhs.ptr_;
            pool_ = rhs.pool_;
            idx_ = rhs.idx_;
            rhs.ptr_ = nullptr;
        }

    private:
        element_type* ptr_{ nullptr };
        void* pool_{ nullptr };
        uint32_t      idx_{ 0 };
    };
}
