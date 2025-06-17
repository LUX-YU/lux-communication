#pragma once
#include <functional>
#include <memory>
#include <lux/communication/Queue.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/visibility.h>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

#include "Topic.hpp"

namespace lux::communication::intraprocess
{
    class Node; // Forward declaration
    template <typename T>
	class Subscriber : public lux::communication::SubscriberBase
    {
        static TopicSptr getTopic(NodeBase* node)
        {
            return node->domain().createOrGetTopic<Topic<T>, T>();
        }

        static CallbackGroupBase getCallbackGroup(CallbackGroupBase* cgb, NodeBase* node)
        {
            return cgb == nullptr ? node->defaultCallbackGroup() : cgb;
        }

    public:
        using Callback = std::function<void(const message_t<T>)>;

        template<typename Func>
        Subscriber(const std::string& topic, Node* node, Func&& func, CallbackGroupBase* cgb = nullptr)
            :   SubscriberBase(getTopic(node), node, getCallbackGroup(cgb, node)), callback_func_(std::forward<Func>(func))
        {}

        ~Subscriber() override {
            cleanup();
        }

        Subscriber(const Subscriber &) = delete;
        Subscriber &operator=(const Subscriber &) = delete;

        Subscriber(Subscriber&& rhs) = delete;
		Subscriber& operator=(Subscriber&& rhs) = delete;

        // The interface called by Topic when a new message arrives
        void enqueue(message_t<T> msg)
        {
            push(queue_, std::move(msg));

            SubscriberBase::callbackGroup().notify(SubscriberBase::shared_from_this());
        }

        // Called by Node spinOnce()
        void takeAll() override
        {
            message_t<T> msg;
            while (try_pop(queue_, msg))
            {
                Parent::invokeCallback(*msg);
            }

            clearReady();
        }

        bool setReadyIfNot() override
        {
            bool expected = false;
            // If originally false, set to true and return true
            // If already true, return false
            return ready_flag_.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel, std::memory_order_acquire
            );
        }

        void clearReady() override
        {
            ready_flag_.store(false, std::memory_order_release);
        }

    private:
        void cleanup() 
        {
			// Close the queue and join the thread if needed
			close(queue_);
			ready_flag_.store(false, std::memory_order_release);
			Subscriber::topic().removeSubscriber(this);
        }

        void drainAll(std::vector<TimeExecEntry> &out) override
        {
            // static_assert(lux::communication::is_msg_stamped<T>, "Subscriber<T> does not support non-stamped message type T");
            if constexpr(lux::communication::is_msg_stamped<T>)
            {
                std::shared_ptr<T> msg;
                while (try_pop(queue_, msg))
                {
                    auto ts_ns = lux::communication::builtin_msgs::common_msgs::extract_timstamp(*msg);
                    // capture 'msg' by move in the invoker
                    // Capture user callback set when subscribing
                    auto invoker = [this, m=std::move(msg)]() mutable {
						this->invokeCallback(*m);
                    };
                    out.push_back(TimeExecEntry{ ts_ns, std::move(invoker) });
                }
            }
            else{
                throw std::runtime_error("Subscriber<T> does not support non-stamped message type T");
            }
        }

    private:
        Callback                        callback_func_{ nullptr };
        std::atomic<bool>               ready_flag_{false};
        queue_t<T>                      queue_;
    };
}
