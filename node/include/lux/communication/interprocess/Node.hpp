#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include "ZmqPubSub.hpp"
#include "../introprocess/Queue.hpp"
#include "../introprocess/CallbackGroup.hpp"
#include "../introprocess/SubscriberBase.hpp"

namespace lux::communication::interprocess {

class Node {
public:
    explicit Node(const std::string& name, int domain = 0)
        : name_(name), domain_(domain) {
        default_group_ = std::make_shared<introprocess::CallbackGroup>(introprocess::CallbackGroupType::MutuallyExclusive);
    }
    ~Node() { stop(); }

    template<typename T>
    std::shared_ptr<Publisher<T>> createPublisher(const std::string& topic) {
        auto pub = std::make_shared<Publisher<T>>(topic);
        std::lock_guard<std::mutex> lock(mutex_);
        pubs_.push_back(pub);
        return pub;
    }

    template<typename T, typename F>
    std::shared_ptr<introprocess::ISubscriberBase> createSubscriber(const std::string& topic, F&& cb,
            std::shared_ptr<introprocess::CallbackGroup> group = nullptr) {
        if(!group) group = default_group_;
        auto sub = std::make_shared<NodeSubscriber<T>>(topic, std::forward<F>(cb), group);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            subs_.push_back(sub);
        }
        return sub;
    }

    std::shared_ptr<introprocess::CallbackGroup> getDefaultCallbackGroup() const { return default_group_; }

    void stop() {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& s : subs_) {
            static_cast<NodeSubscriberBase*>(s.get())->stop();
        }
    }

private:
    class NodeSubscriberBase : public introprocess::ISubscriberBase {
    public:
        NodeSubscriberBase() : introprocess::ISubscriberBase(nextId()) {}
        virtual void stop() = 0;
    private:
        static int nextId() { static std::atomic<int> id{0}; return id++; }
    };

    template<typename T>
    class NodeSubscriber : public NodeSubscriberBase {
    public:
        using Callback = std::function<void(const T&)>;
        NodeSubscriber(const std::string& topic, Callback cb,
                       std::shared_ptr<introprocess::CallbackGroup> group)
            : callback_(std::move(cb)), group_(std::move(group)) {
            zmq_sub_ = std::make_unique<Subscriber<T, std::function<void(const T&)>>>(
                topic, [this](const T& m){ enqueue(m); });
        }
        ~NodeSubscriber() override { stop(); }
        void stop() override { if(zmq_sub_) zmq_sub_->stop(); }

        void enqueue(const T& msg) {
            introprocess::push(queue_, std::make_shared<T>(msg));
            if(group_ && setReadyIfNot()) group_->notify(this);
        }

        void takeAll() override {
            introprocess::message_t<T> m;
            while(introprocess::try_pop(queue_, m)) {
                if(callback_) callback_(*m);
            }
            clearReady();
        }

        bool setReadyIfNot() override {
            bool expected=false;
            return ready_.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }
        void clearReady() override { ready_.store(false, std::memory_order_release); }
    private:
        std::unique_ptr<Subscriber<T, std::function<void(const T&)>>> zmq_sub_;
        Callback callback_;
        std::shared_ptr<introprocess::CallbackGroup> group_;
        introprocess::queue_t<T> queue_;
        std::atomic<bool> ready_{false};

        void drainAll(std::vector<introprocess::TimeExecEntry>& out) override {
            introprocess::message_t<T> m;
            while(introprocess::try_pop(queue_, m)) {
                auto inv = [cb=callback_, msg=m]() mutable { if(cb) cb(*msg); };
                out.push_back({0, std::move(inv)});
            }
        }
    };

    std::string name_;
    int domain_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<void>> pubs_;
    std::vector<std::shared_ptr<introprocess::ISubscriberBase>> subs_;
    std::shared_ptr<introprocess::CallbackGroup> default_group_;
};

} // namespace lux::communication::interprocess
