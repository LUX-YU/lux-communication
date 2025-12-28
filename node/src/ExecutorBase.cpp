#include "lux/communication/ExecutorBase.hpp"
#include "lux/communication/NodeBase.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{  
    ExecutorBase::ExecutorBase() : spinning_(true) {}
    ExecutorBase::~ExecutorBase() { stop(); }

    void ExecutorBase::addNode(NodeBase* node)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto idx = nodes_.insert(node);
        node->setExecutor(idx, this);
    }

    void ExecutorBase::removeNode(NodeBase* node)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        if (nodes_.erase(node->idInExecutor()))
        {
            node->setExecutor(std::numeric_limits<size_t>::max(), nullptr);
        }
    }

    void ExecutorBase::waitCondition()
    {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait(
            lock,
            [this]
            {
                return !spinning_.load() || checkRunnable();
            }
        );
    }

    void ExecutorBase::notifyCondition()
    {
        std::lock_guard lk(cv_mutex_);
        cv_.notify_all();
    }

    bool ExecutorBase::checkRunnable()
    {
        return ready_queue_.size_approx() > 0;
    }

} // namespace lux::communication
