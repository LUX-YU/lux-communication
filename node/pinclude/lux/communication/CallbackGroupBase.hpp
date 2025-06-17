#pragma once
#include <lux/communication/visibility.h>

#include <lux/communication/Registry.hpp>
#include <lux/communication/core/CallbackGroupInterface.hpp>

namespace lux::communication
{
	class NodeBase;
	using node_registry_t			= Registry<NodeBase>;
    using node_handle_t             = typename node_registry_t::Handle;

    class LUX_COMMUNICATION_PUBLIC CallbackGroupBase final
    {
        friend class NodeBase;
        friend class Executor;
    public:
        explicit CallbackGroupBase(node_handle_t handle, CallbackGroupType type = CallbackGroupType::MutuallyExclusive)
            : node_handle_(handle)
        {

        }

        ~CallbackGroupBase();

        CallbackGroupType type() const
        {
            return type_;
        }
        
        // This can be called by Executor
        // When a Subscriber receives new data, it notifies the callback group
        void addSubscriber(subscriber_handle_t sub);

        void removeSubscriber(size_t sub_id);

        // When a particular Subscriber has new data
        // The purpose is to add the Subscriber to the "ready queue" and notify the Executor
        void notify(subscriber_handle_t sub);

        size_t execId() const
        {
            return exec_id_;
        }
    private:

        node_handle_t       node_handle_;
        CallbackGroupType   type_;
    };
} // namespace lux::communication::intraprocess
