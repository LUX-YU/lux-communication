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
    // 前置声明
    class Executor;
    class ISubscriberBase;

    enum class CallbackGroupType
    {
        MutuallyExclusive,  // 互斥式：同一组中的回调按顺序执行
        Reentrant           // 可重入：同一组中的回调可并行执行
    };

    class CallbackGroup
    {
    public:
        explicit CallbackGroup(CallbackGroupType type = CallbackGroupType::MutuallyExclusive)
            : type_(type)
        {}

        ~CallbackGroup() = default;

        CallbackGroupType getType() const { return type_; }

        // 这里可以让 Executor 来调用
        // 当订阅者出现新消息时，会通过 notify() 通知回调组
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

        // 当某个Subscriber新消息到达时调用
        // 作用：把 Subscriber 放到“就绪队列”，并通知 Executor 
        void notify(ISubscriberBase* sub);

        // 给 Executor 获取所有就绪的订阅者（一次性取走）
        std::vector<ISubscriberBase*> collectReadySubscribers();

        // 设置/获取 Executor（由 Executor::addCallbackGroup() 调用）
        void setExecutor(std::shared_ptr<Executor> exec) { executor_ = exec; }
        std::shared_ptr<Executor> getExecutor() const { return executor_.lock(); }

    private:
        CallbackGroupType type_;
        std::mutex        mutex_;

        // 当前分组内的所有订阅者
        std::unordered_set<ISubscriberBase*> subscribers_;

        // 就绪的订阅者队列（有新消息待处理）
        std::vector<ISubscriberBase*> ready_list_;

        // 弱引用到 Executor
        std::weak_ptr<Executor> executor_;
    };

} // namespace lux::communication::introprocess
