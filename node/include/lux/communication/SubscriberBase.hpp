#pragma once
#include <functional>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class ITopicHolder;
	class CallbackGroup;
    class NodeBase;

    using TopicHolderSptr   = std::shared_ptr<ITopicHolder>;
    using CallbackGroupSptr = std::shared_ptr<CallbackGroup>;
    using NodeBaseSptr      = std::shared_ptr<NodeBase>;

    struct TimeExecEntry
    {
        uint64_t timestamp_ns;
        std::function<void()> invoker;

        bool operator<(const TimeExecEntry& rhs) const;
    };

	class LUX_COMMUNICATION_PUBLIC SubscriberBase 
        : public std::enable_shared_from_this<SubscriberBase>
    {
        friend class TimeOrderedExecutor;
        friend class NodeBase;
    public:
        SubscriberBase(NodeBaseSptr, TopicHolderSptr, CallbackGroupSptr);

        virtual ~SubscriberBase();

        virtual void takeAll() = 0;

        virtual bool setReadyIfNot() = 0;
        virtual void clearReady() = 0;

        size_t id() const;

        ITopicHolder& topic();
        const ITopicHolder& topic() const;

        CallbackGroup& callbackGroup();
        const CallbackGroup& callbackGroup() const;

    private:
        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;

        void setId(size_t id);

        size_t              id_{0};
        TopicHolderSptr     topic_;
        CallbackGroupSptr   callback_group_;
		NodeBaseSptr	    node_;
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
