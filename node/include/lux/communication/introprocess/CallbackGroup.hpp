#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <cassert>
#include <unordered_set>
#include <condition_variable>

namespace lux::communication::introprocess
{
    // Forward declarations
    class Executor;
    class ISubscriberBase;

    enum class CallbackGroupType
    {
        MutuallyExclusive,  // Execution in this group is mutual exclusive
        Reentrant           // Execution in this group can be concurrent
    };

    class CallbackGroup
    {
    public:
        explicit CallbackGroup(CallbackGroupType type = CallbackGroupType::MutuallyExclusive)
            : type_(type)
        {}

        ~CallbackGroup() = default;

        CallbackGroupType getType() const { return type_; }

        // This can be called by Executor
        // When a Subscriber receives new data, it notifies the callback group
        void addSubscriber(ISubscriberBase* sub)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscribers_.insert(sub);
        }

        void removeSubscriber(ISubscriberBase* sub)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subscribers_.erase(sub);
        }

        // When a particular Subscriber has new data
        // The purpose is to add the Subscriber to the "ready queue" and notify the Executor
        void notify(ISubscriberBase* sub);

        // For Executor to collect all ready subscribers (take them in one go)
        std::vector<ISubscriberBase*> collectReadySubscribers();

        // Set/get Executor (called by Executor::addCallbackGroup())
        void setExecutor(std::shared_ptr<Executor> exec) { executor_ = exec; }
        std::shared_ptr<Executor> getExecutor() const { return executor_.lock(); }

    private:
        CallbackGroupType type_;
        std::mutex        mutex_;

        // All subscribers in this group
        std::unordered_set<ISubscriberBase*> subscribers_;

        // The queue of ready subscribers (with new data)
        std::vector<ISubscriberBase*> ready_list_;

        // A weak reference to Executor
        std::weak_ptr<Executor> executor_;
    };

} // namespace lux::communication::introprocess
