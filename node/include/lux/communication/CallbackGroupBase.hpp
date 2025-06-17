#pragma once
#include <memory>
#include <mutex>
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    // Forward declarations
	class NodeBase;
    class Executor;
    class ExecutorBase;
    class SubscriberBase;

    enum class CallbackGroupType
    {
        MutuallyExclusive,  // Execution in this group is mutual exclusive
        Reentrant           // Execution in this group can be concurrent
    };

    inline constexpr size_t invalid_id = std::numeric_limits<size_t>::max();

    class LUX_COMMUNICATION_PUBLIC CallbackGroupBase
    {
        friend class NodeBase;
        friend class ExecutorBase;
    public:
        explicit CallbackGroupBase(NodeBase* node, CallbackGroupType type = CallbackGroupType::MutuallyExclusive);

        ~CallbackGroupBase();

        CallbackGroupType type() const;
        
        // This can be called by Executor
        // When a Subscriber receives new data, it notifies the callback group
        void addSubscriber(SubscriberBase* sub);

        void removeSubscriber(SubscriberBase* sub_id);

        void notify(SubscriberBase*);

        template<typename Func>
        void foreachSubscriber(Func&& func)
        {
            std::vector<SubscriberBase*> sub_buffers;
            {
                std::scoped_lock lck(mutex_);
                sub_buffers = subscribers_.values();
            }

            for (auto sub : sub_buffers)
            {
                func(sub);
            }
        }

        size_t idInNode() const
        {
            return id_in_node_;
        }

        void setExecutor(ExecutorBase* executor)
        {
            executor_ = executor;
        }

        ExecutorBase* executor()
        {
            return executor_;
        }

        const ExecutorBase* executor() const
        {
            return executor_;
        }

    private:
        void setIdInNode(size_t id)
        {
            id_in_node_ = id;
        }

        using CallbackGroupList = lux::cxx::AutoSparseSet<SubscriberBase*>;

        NodeBase*             node_;
        ExecutorBase*         executor_;
        size_t                id_in_node_{invalid_id};
        

        CallbackGroupType     type_;
        mutable std::mutex    mutex_;
        
        CallbackGroupList     subscribers_;
    };
} // namespace lux::communication::intraprocess
