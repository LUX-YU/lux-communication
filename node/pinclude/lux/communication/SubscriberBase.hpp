#pragma once
#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <lux/communication/visibility.h>
#include <lux/communication/Registry.hpp>
#include <lux/communication/Subscriber.hpp>

namespace lux::communication
{
	class TopicBase;
	class CallbackGroupBase;
    class NodeBase;
    class SubscriberBase;

    using subscriber_handle_t     = typename Registry<SubscriberBase>::Handle;
    using callback_group_handle_t = typename Registry<CallbackGroupBase>::Handle;
    using topic_handle_t          = typename QueryableRegistry<TopicBase>::Handle;
    using node_handle_t           = typename Registry<NodeBase>::Handle;

    struct TimeExecEntry
    {
        uint64_t timestamp_ns;
        std::function<void()> invoker;

        bool operator<(const TimeExecEntry& rhs) const;
    };

    class LUX_COMMUNICATION_PUBLIC SubscriberBase
    {
        friend class TimeOrderedExecutor;
        friend class CallbackGroupBase;
        friend class NodeBase;
    public:
        SubscriberBase(node_handle_t, topic_handle_t, callback_group_handle_t);

        virtual ~SubscriberBase();

        virtual void takeAll() = 0;

        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;

        bool setReadyIfNot()
        {
            bool expected = false;
            // If originally false, set to true and return true
            // If already true, return false
            return ready_flag_.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire
            );
        }

        void resetReady()
        {
            ready_flag_.store(false, std::memory_order_release);
        }

        size_t id() const;

        TopicBase& topic();
        const TopicBase& topic() const;

    private:
        void setId(size_t id);

        size_t                    id_{ 0 };
        topic_handle_t            topic_handle_;
        std::atomic<bool>         ready_flag_{ false };
        callback_group_handle_t   callback_group_;
        node_handle_t	          node_handle_;
    };

    template<typename T>
    class TSubscriberBase : public SubscriberBase
    {
    public:
		using msg_t    = T;
        using Callback = std::function<void(const T&)>;

        TSubscriberBase(NodeBaseSptr node, TopicHolderSptr topic, Callback callback, CallbackGroupSptr group)
			: SubscriberBase(std::move(node), std::move(topic), std::move(group)),
			callback_(std::move(callback)){}

		~TSubscriberBase() override = default;

		TSubscriberBase(const TSubscriberBase&) = delete;
		TSubscriberBase& operator=(const TSubscriberBase&) = delete;

		TSubscriberBase(TSubscriberBase&& rhs) noexcept
			: SubscriberBase(std::move(rhs)),
			callback_(std::move(rhs.callback_))
        {
			rhs.callback_ = nullptr; // Clear the moved-from callback
        }

		TSubscriberBase& operator=(TSubscriberBase&& rhs) noexcept
		{
			if (this != &rhs)
			{
				SubscriberBase::operator=(std::move(rhs));
				callback_ = std::move(rhs.callback_);
				rhs.callback_ = nullptr; // Clear the moved-from callback
			}
			return *this;
		}

        void invokeCallback(const T& msg)
        {
            if (callback_)
            {
                callback_(msg);
            }
        }

    protected:
		Callback callback_;
    };
}
