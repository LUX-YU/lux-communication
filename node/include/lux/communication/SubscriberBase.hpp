#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class NodeBase;
	class TopicBase;
	class CallbackGroupBase;

    using TopicSptr = std::shared_ptr<TopicBase>;

    struct TimeExecEntry
    {
        uint64_t timestamp_ns;
        std::function<void()> invoker;

        bool operator<(const TimeExecEntry& rhs) const;
    };

	class LUX_COMMUNICATION_PUBLIC SubscriberBase
    {
        friend class TimeOrderedExecutor;
        friend class NodeBase;
        friend class TopicBase;
        friend class CallbackGroupBase;
    public:
        virtual ~SubscriberBase();

        virtual void takeAll() = 0;

        virtual bool setReadyIfNot() = 0;
        virtual void clearReady() = 0;

        size_t idInNode() const
        {
            return id_in_node_;
        }

        size_t idInCallbackGroup() const
        {
            return id_in_callback_group_;
        }

        size_t idInTopic() const
        {
            return id_in_topic_;
        }

        template<typename T>
        T& topicAs() requires std::is_base_of_v<TopicBase, T>
        {
            return static_cast<T&>(*topic_);
        }

    protected:
        SubscriberBase(TopicSptr topic, NodeBase* node, CallbackGroupBase* cgb);

    private:

        void setIdInNode(size_t id)
        {
            id_in_node_ = id;
        }

        void setIdIInCallbackGroup(size_t id)
        {
            id_in_callback_group_ = id;
        }

        void setIdInTopic(size_t id)
        {
            id_in_topic_ = id;
        }

        size_t                      id_in_node_{std::numeric_limits<size_t>::max()};
        size_t                      id_in_callback_group_{std::numeric_limits<size_t>::max()};
        size_t                      id_in_topic_{std::numeric_limits<size_t>::max()};
        std::shared_ptr<TopicBase>  topic_;
        NodeBase*                   node_;
        CallbackGroupBase*          callback_group_;
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
