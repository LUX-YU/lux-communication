#pragma once

#include <memory>
#include <string>

#include <vector>
#include <functional>

#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/interprocess/ZmqPubSub.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication::interprocess {

class Node {
public:
    explicit Node(const std::string& name)
        : name_(name)
    {
        default_group_ = std::make_shared<lux::communication::CallbackGroup>(
            lux::communication::CallbackGroupType::MutuallyExclusive);
    }

    ~Node() { stop(); }

    std::shared_ptr<lux::communication::CallbackGroup> getDefaultCallbackGroup() const
    {
        return default_group_;
    }

    template<typename T>
    std::shared_ptr<Publisher<T>> createPublisher(const std::string& topic)
    {
        return std::make_shared<Publisher<T>>(topic);
    }

    template<typename T, typename Callback>
    std::shared_ptr<Subscriber<T, std::decay_t<Callback>>> createSubscriber(
        const std::string& topic, Callback&& cb,
        std::shared_ptr<lux::communication::CallbackGroup> group = nullptr)
    {
        (void)group;
        auto sub = std::make_shared<Subscriber<T, std::decay_t<Callback>>>(
            topic, std::forward<Callback>(cb));
        subscriber_stoppers_.emplace_back([s=sub]{ s->stop(); });
        return sub;
    }

    void stop()
    {
        for (auto& fn : subscriber_stoppers_) { fn(); }
        subscriber_stoppers_.clear();
    }

private:
    std::string name_;
    std::shared_ptr<lux::communication::CallbackGroup> default_group_;
    std::vector<std::function<void()>> subscriber_stoppers_;
};

} // namespace lux::communication::interprocess
