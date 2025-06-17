#pragma once

#include <vector>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <memory>
#include <lux/communication/TopicBase.hpp>
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
         * @brief Distribute messages (zero-copy)
         *        Just atomically load subs_ and iterate, no locking needed
         */
        void publish(message_t<T> msg)
        {
			// fast path: if only one subscriber, just move the message
            {
                std::scoped_lock lck(TopicBase::mutex_sub_);
                if (TopicBase::subscribers_.size() == 1)
                {
                    auto sub = static_cast<Subscriber<T>*>(TopicBase::subscribers_.at(0));
                    if (sub)
                    {
                        sub->enqueue(std::move(msg));
                        return;  // Early exit
                    }
                }
            }

            TopicBase::foreachSubscriber(
                [&msg](SubscriberBase* sub_base)
                {
                    auto sub = static_cast<Subscriber<T>*>(sub_base);
                    sub->enqueue(msg);
                }
            );
        }
    };
} // namespace lux::communication::intraprocess
