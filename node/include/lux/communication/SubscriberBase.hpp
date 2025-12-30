#pragma once
#include <cstddef>
#include <memory>
#include <limits>

#include <lux/communication/TimeExecEntry.hpp>
#include <lux/communication/ExecEntry.hpp>
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

        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;

        // High-performance drain using function pointer trampoline (no std::function)
        virtual void drainAllExec(std::vector<ExecEntry>& out) = 0;

        /**
         * @brief Bounded drain: drain at most max_count entries.
         *        This is the preferred method for SeqOrderedExecutor to keep
         *        the reorder window small by round-robin draining subscribers.
         * @param out Output vector to append entries to
         * @param max_count Maximum number of entries to drain
         * @return Number of entries actually drained
         */
        virtual size_t drainExecSome(std::vector<ExecEntry>& out, size_t max_count) = 0;

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

        void clearReady()
        {
            ready_flag_.clear(std::memory_order_release);
        }

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

        size_t                          id_in_node_{std::numeric_limits<size_t>::max()};
        size_t                          id_in_callback_group_{std::numeric_limits<size_t>::max()};
        size_t                          id_in_topic_{std::numeric_limits<size_t>::max()};
        alignas(64) std::atomic_flag    ready_flag_ = ATOMIC_FLAG_INIT;
        std::shared_ptr<TopicBase>      topic_;
        NodeBase*                       node_;
        CallbackGroupBase*              callback_group_;
    };
}
