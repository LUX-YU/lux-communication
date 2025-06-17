#pragma once
#include <functional>
#include <memory>
#include <lux/communication/Queue.hpp>
#include "Topic.hpp"
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/visibility.h>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

namespace lux::communication::intraprocess
{
    class Node; // Forward declaration
    template <typename T>
	class Subscriber : public lux::communication::TSubscriberBase<T>
    {
    public:
		using Parent   = lux::communication::TSubscriberBase<T>;
        using Callback = std::function<void(const T &)>;

        friend class Node; // or friend class Node (depending on design)

        Subscriber(std::shared_ptr<Node> node, std::shared_ptr<Topic<T>> topic, Callback callback, CallbackGroupSptr group)
            : TSubscriberBase<T>(std::move(node), std::move(topic), std::move(callback), std::move(group))
        {
            Subscriber::topic().addSubscriber(this);
        }

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

        void takeAll() override
        {
            message_t<T> msg;
            while (try_pop(queue_, msg))
            {
                Parent::invokeCallback(*msg);
            }

            clearReady();
        }

    private:
		Topic<T>& topic()
		{
			return static_cast<Topic<T>&>(SubscriberBase::topic());
		}

        void cleanup() {
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
        queue_t<T> queue_;
    };
}
