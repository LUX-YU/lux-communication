#pragma once

#include <vector>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <memory>
#include <lux/communication/TopicBase.hpp>
#include <lux/communication/Domain.hpp>
#include <lux/communication/Queue.hpp>

namespace lux::communication::intraprocess
{
    template <typename T> class Subscriber;

    /**
     * @brief Holds all subscribers of a specific type T, and distribute zero-copy messages.
     *        Uses a Copy-On-Write approach to manage subscriber arrays.
     */
    template <typename T>
    class Topic : public TopicBase
    {
    public:
        static constexpr auto static_type_info = lux::cxx::make_basic_type_info<T>();

        ~Topic() override = default;

        /**
         * @brief Distribute messages (zero-copy) with global sequence numbers
         *        Each subscriber receives a unique sequence number for strict ordering
         */
        void publish(message_t<T> msg)
        {
            // Get all subscribers
            std::vector<SubscriberBase*> subs;
            {
                std::scoped_lock lck(TopicBase::mutex_sub_);
                subs = TopicBase::subscribers_.values();
            }

            const size_t n = subs.size();
            if (n == 0) return;

            // Allocate a range of consecutive sequence numbers
            const uint64_t base = this->domain().allocateSeqRange(n);

            // Distribute message with consecutive sequence numbers
            for (size_t i = 0; i < n; ++i)
            {
                auto sub = static_cast<Subscriber<T>*>(subs[i]);
                sub->enqueue(base + static_cast<uint64_t>(i), msg);
            }
        }
    };
} // namespace lux::communication::intraprocess
