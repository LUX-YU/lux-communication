#pragma once
#include <lux/communication/core/Registry.hpp>

namespace lux::communication
{
    class NodeInterface;

    class CallbackGroupBase;
    using callback_group_handle_t = typename Registry<CallbackGroupBase>::Handle;

    enum class CallbackGroupType
    {
        MutuallyExclusive,  // Execution in this group is mutual exclusive
        Reentrant           // Execution in this group can be concurrent
    };

    class CallbackGroupInterface
    {
        friend class Subscriber;
    public:
        CallbackGroupInterface(CallbackGroupType type, NodeInterface& node);

    private:
        callback_group_handle_t handle_;
    };
}