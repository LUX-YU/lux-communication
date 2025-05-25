#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <functional>
#include <cassert>
#include <unordered_set>
#include <condition_variable>
#include <lux/cxx/container/SparseSet.hpp>
#include <lux/communication/visibility.h>
#include <lux/communication/SubscriberBase.hpp>


namespace lux::communication
{
    // Forward declarations
    class Executor;
    class ISubscriberBase;

    enum class CallbackGroupType
    {
        MutuallyExclusive,  // Execution in this group is mutual exclusive
        Reentrant           // Execution in this group can be concurrent
    };

    class LUX_COMMUNICATION_PUBLIC CallbackGroup
    {
    public:
        explicit CallbackGroup(CallbackGroupType type = CallbackGroupType::MutuallyExclusive);

        ~CallbackGroup();

        CallbackGroupType getType() const;

        // This can be called by Executor
        // When a Subscriber receives new data, it notifies the callback group
        void addSubscriber(ISubscriberBase* sub);

        void removeSubscriber(ISubscriberBase* sub);

        bool hasReadySubscribers() const;

        // When a particular Subscriber has new data
        // The purpose is to add the Subscriber to the "ready queue" and notify the Executor
        void notify(ISubscriberBase* sub);

        // For Executor to collect all ready subscribers (take them in one go)
        std::vector<ISubscriberBase*> collectReadySubscribers();

        std::vector<ISubscriberBase*> collectAllSubscribers();

        // Set/get Executor (called by Executor::addNode())
        void setExecutor(std::shared_ptr<Executor> exec);
        std::shared_ptr<Executor> getExecutor() const;

    private:
        CallbackGroupType                           type_;
        mutable std::mutex                          mutex_;

        // Use SparseSet<int, ISubscriberBase*> for fast add/remove.
        // The 'int' key must come from something like sub->getId().
        lux::cxx::SparseSet<int, ISubscriberBase*>  subscribers_;

        // We still keep a simple vector as a "ready queue."
        // If you also wanted O(1) removal from the ready list, you could
        // store them in another SparseSet. Usually, we just pop them in FIFO order.
        std::vector<ISubscriberBase*>               ready_list_;

        // A weak reference to whichever Executor is responsible for this group
        std::weak_ptr<Executor>                     executor_;

        std::atomic<bool>                           has_ready_{ false };
    };

} // namespace lux::communication::intraprocess
