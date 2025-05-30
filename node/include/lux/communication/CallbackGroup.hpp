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
    class SubscriberBase;
	using SubscriberWptr = std::weak_ptr<SubscriberBase>;
	using SubscriberSptr = std::shared_ptr<SubscriberBase>;

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

        CallbackGroupType type() const;
        
        // This can be called by Executor
        // When a Subscriber receives new data, it notifies the callback group
        void addSubscriber(SubscriberSptr sub);

        void removeSubscriber(size_t sub_id);

        bool hasReadySubscribers() const;

        // When a particular Subscriber has new data
        // The purpose is to add the Subscriber to the "ready queue" and notify the Executor
        void notify(const SubscriberSptr& sub);

        // For Executor to collect all ready subscribers (take them in one go)
        std::vector<SubscriberSptr> collectReadySubscribers();
            
        // Set/get Executor (called by Executor::addNode())
        void setExecutor(std::shared_ptr<Executor> exec);
        std::shared_ptr<Executor> getExecutor() const;

    private:
        size_t                                      id_;

        CallbackGroupType                           type_;
        mutable std::mutex                          mutex_;

        // Use SparseSet<int, SubscriberBase*> for fast add/remove.
        // The 'int' key must come from something like sub->getId().
		lux::cxx::AutoSparseSet<SubscriberWptr>     subscribers_;

        // We still keep a simple vector as a "ready queue."
        // If you also wanted O(1) removal from the ready list, you could
        // store them in another SparseSet. Usually, we just pop them in FIFO order.
        std::deque<SubscriberSptr>                  ready_list_;

        // A weak reference to whichever Executor is responsible for this group
        std::weak_ptr<Executor>                     executor_;

        std::atomic<bool>                           has_ready_{ false };
    };

    inline std::shared_ptr<CallbackGroup> default_callback_group()
    {
        static auto instance = std::make_shared<CallbackGroup>();
        return instance;
    }
} // namespace lux::communication::intraprocess
