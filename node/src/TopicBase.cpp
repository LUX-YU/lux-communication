#include <lux/communication/TopicBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>

#include <memory>
#include <vector>
#include <limits>

namespace lux::communication
{
    static inline constexpr size_t invalid_id = std::numeric_limits<size_t>::max();

    TopicBase::TopicBase()
        : sub_snapshot_(std::make_shared<const std::vector<SubscriberBase*>>())
    {
    }

    TopicBase::~TopicBase() = default;

    void TopicBase::addPublisher(PublisherBase* pub)
    {
        if (pub == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lck(mutex_pub_);
        if (publishers_.contains(pub->idInNode()))
        {
            return;
        }

        auto idx = publishers_.insert(pub);
        pub->setIdInNode(idx);
    }

    void TopicBase::addSubscriber(SubscriberBase* sub)
    {
        if (sub == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lck(mutex_sub_);
        if (subscribers_.contains(sub->idInNode()))
        {
            return;
        }

        auto idx = subscribers_.insert(sub);
        sub->setIdInNode(idx);

        // Rebuild COW snapshot under lock
        rebuildSubscriberSnapshot();
    }

    void TopicBase::removePublisher(PublisherBase* pub)
    {
        if (pub == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lck(mutex_pub_);
        if (!publishers_.contains(pub->idInNode()))
        {
            return;
        }

        publishers_.erase(pub->idInNode());
        pub->setIdInNode(invalid_id);
    }

    void TopicBase::removeSubscriber(SubscriberBase* sub)
    {
        if (sub == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lck(mutex_sub_);
        if (!subscribers_.contains(sub->idInNode()))
        {
            return;
        }

        subscribers_.erase(sub->idInNode());
        sub->setIdInNode(invalid_id);

        // Rebuild COW snapshot under lock
        rebuildSubscriberSnapshot();
    }

    void TopicBase::rebuildSubscriberSnapshot()
    {
        // Must be called under mutex_sub_ lock

        // 假设 subscribers_.values() 返回可复制到 std::vector<SubscriberBase*> 的容器/值
        auto new_vec = std::make_shared<std::vector<SubscriberBase*>>(subscribers_.values());

        // 转成不可变快照
        SubscriberSnapshot new_snapshot = new_vec;

        std::atomic_store_explicit(&sub_snapshot_, new_snapshot, std::memory_order_release);
    }
}