#pragma once

#include <vector>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <memory>    // for std::unique_ptr
#include <lux/communication/ITopicHolder.hpp>

namespace lux::communication { class Domain; }

namespace lux::communication::intraprocess
{
    // Forward declaration
    using ::lux::communication::Domain;
    template <typename T>
    class Subscriber;

    //-------------------------------------------
    // 1) An immutable SubscriberList + reference counting
    //-------------------------------------------
    namespace detail
    {
        template <typename T>
        struct SubscriberList
        {
            // Reference count
            std::atomic<int> refCount{1};

            // Actual array of subscribers
            std::vector<Subscriber<T>*> subs;

            // Default constructor
            SubscriberList() = default;

            // Copy constructor (copy subs, but refCount = 1)
            SubscriberList(const SubscriberList &other)
                : subs(other.subs)
            {
                refCount.store(1, std::memory_order_relaxed);
            }
        };

        // Helper function: add reference
        template <typename T>
        inline void incRef(SubscriberList<T>* p)
        {
            if (p) {
                p->refCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Helper function: decrease reference, if it drops to 0 then delete
        template <typename T>
        inline void decRef(SubscriberList<T>* p)
        {
            if (p) {
                int old = p->refCount.fetch_sub(1, std::memory_order_acq_rel);
                if (old == 1) {
                    delete p;
                }
            }
        }
    } // namespace detail

    /**
     * @brief Holds all subscribers of a specific type T, and distribute zero-copy messages.
     *        Uses a Copy-On-Write approach to manage subscriber arrays.
     */
    template <typename T>
    class Topic : public ITopicHolder
    {
    public:
        static constexpr auto static_type_info = lux::cxx::make_basic_type_info<T>();

        Topic()
        {
            // refCount starts at 0, external incRef() is required
            // Initialize an empty list
            using sub_list_t = detail::SubscriberList<T>;
            auto emptyList   = new sub_list_t();
            subs_.store(emptyList, std::memory_order_release);
        }

        ~Topic() override
        {
            // Release current subs_ pointer on destruction
            auto ptr = subs_.load(std::memory_order_acquire);
            detail::decRef(ptr);
        }

        /**
         * @brief Register a subscriber
         */
        void addSubscriber(Subscriber<T> *sub)
        {
            while (true) {
                auto old_ptr = subs_.load(std::memory_order_acquire);
                detail::incRef(old_ptr);

                // Make a copy
                auto new_ptr = new detail::SubscriberList<T>(*old_ptr);
                // Add subscriber to the copy
                new_ptr->subs.push_back(sub);

                // CAS
                if (subs_.compare_exchange_weak(old_ptr, new_ptr,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    detail::decRef(old_ptr);
                    break; // success
                }

                // CAS failed, which means there was concurrent modification
                delete new_ptr;
                detail::decRef(old_ptr);
            }
        }

        /**
         * @brief Unregister a subscriber
         */
        void removeSubscriber(Subscriber<T> *sub)
        {
            while (true) {
                auto old_ptr = subs_.load(std::memory_order_acquire);
                detail::incRef(old_ptr);

                // Make a copy
                auto new_ptr = new detail::SubscriberList<T>(*old_ptr);

                // Remove sub (swap-and-pop)
                auto &vec = new_ptr->subs;
                auto it = std::find(vec.begin(), vec.end(), sub);
                if (it != vec.end()) {
                    *it = vec.back();
                    vec.pop_back();
                }

                if (subs_.compare_exchange_weak(old_ptr, new_ptr,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    // Success
                    detail::decRef(old_ptr);
                    break;
                }
        
                // CAS failed, meaning concurrent modification, need to retry
                delete new_ptr;
                detail::decRef(old_ptr);
            }
        }

        /**
         * @brief Distribute messages (zero-copy)
         *        Just atomically load subs_ and iterate, no locking needed
         */
        void publish(std::shared_ptr<T> msg)
        {
            auto listPtr = subs_.load(std::memory_order_acquire);
            detail::incRef(listPtr);  // Prevent listPtr from being freed by concurrent remove

            for (auto* sub : listPtr->subs)
            {
                if (sub)
                {
                    sub->enqueue(msg);
                }
            }

            detail::decRef(listPtr);  // Done iterating
        }

    private:
        // Atomic pointer to an immutable "SubscriberList"
        std::atomic<detail::SubscriberList<T>*> subs_;
    };
} // namespace lux::communication::intraprocess
