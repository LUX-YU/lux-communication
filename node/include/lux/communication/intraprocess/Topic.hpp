#pragma once

#include <lux/communication/TopicBase.hpp>
#include <lux/communication/Domain.hpp>
#include <lux/communication/Queue.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/ExecutorBase.hpp>

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
         * @brief Distribute messages (zero-copy) with per-executor sequence numbers.
         *        Uses Copy-On-Write snapshot for lock-free subscriber iteration.
         */
        void publish(message_t<T> msg)
        {
            // Lock-free read of subscriber snapshot (Copy-On-Write)
            auto snapshot = this->getSubscriberSnapshot();
            if (!snapshot || snapshot->empty())
                return;

            const size_t n = snapshot->size();

            // Allocate per-executor sequence numbers so that each executor
            // only sees a contiguous sequence stream for its own subscribers.
            for (size_t i = 0; i < n; ++i)
            {
                auto sub = static_cast<Subscriber<T> *>((*snapshot)[i]);
                auto *exec = sub->callbackGroup()->executor();
                const uint64_t seq = exec ? exec->allocateSeq() : 0;
                if (i + 1 < n)
                    sub->enqueue(seq, msg);           // copy for non-last
                else
                    sub->enqueue(seq, std::move(msg)); // move for last
            }
        }
    };
} // namespace lux::communication::intraprocess
