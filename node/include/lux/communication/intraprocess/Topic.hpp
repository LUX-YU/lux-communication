#pragma once

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
         * @brief Distribute messages (zero-copy) with global sequence numbers.
         *        Uses Copy-On-Write snapshot for lock-free subscriber iteration.
         */
        void publish(message_t<T> msg)
        {
            // Lock-free read of subscriber snapshot (Copy-On-Write)
            auto snapshot = this->getSubscriberSnapshot();
            if (!snapshot || snapshot->empty()) return;

            const size_t n = snapshot->size();

            // Fast path: single subscriber, just move the message
            if (n == 1)
            {
                auto sub = static_cast<Subscriber<T>*>((*snapshot)[0]);
                // Single subscriber gets seq from domain with n=1
                const uint64_t seq = this->domain().allocateSeqRange(1);
                sub->enqueue(seq, std::move(msg));
                return;
            }

            // Allocate a range of consecutive sequence numbers
            const uint64_t base = this->domain().allocateSeqRange(n);

            // Distribute message with consecutive sequence numbers
            for (size_t i = 0; i < n; ++i)
            {
                auto sub = static_cast<Subscriber<T>*>((*snapshot)[i]);
                sub->enqueue(base + static_cast<uint64_t>(i), msg);
            }
        }
    };
} // namespace lux::communication::intraprocess
