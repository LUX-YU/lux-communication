#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <limits>
#include <functional>

#include <lux/communication/TimeExecEntry.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class NodeBase;
	class TopicBase;
	class CallbackGroupBase;

    using TopicSptr = std::shared_ptr<TopicBase>;

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
        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;

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

        CallbackGroupBase* callbackGroup()
        {
            return callback_group_;
        }

        const CallbackGroupBase* callbackGroup() const
        {
            return callback_group_;
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
}
