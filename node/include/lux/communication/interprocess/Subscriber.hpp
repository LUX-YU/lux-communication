#pragma once

#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <atomic>
#include <optional>

#include <lux/communication/visibility.h>
#include "lux/communication/ITopicHolder.hpp"
#include "lux/communication/interprocess/Publisher.hpp"
#include "lux/communication/UdpMultiCast.hpp"
#include <lux/communication/Queue.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

namespace lux::communication::interprocess 
{
    class Node;

    class SubscriberSocket
    {
    public:
        virtual ~SubscriberSocket() = default;
        virtual void connect(const std::string& endpoint) = 0;
        virtual bool receive(void* data, size_t size) = 0;
    };

    std::unique_ptr<SubscriberSocket> createSubscriberSocket();

    std::string defaultEndpoint(const std::string& topic);

    std::optional<std::string> waitDiscovery(const std::string& topic,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{200});
    
    template<typename T>
	class Subscriber : public lux::communication::TSubscriberBase<T>
    {
    public:
        using Callback = std::function<void(const T&)>;
    
        Subscriber(std::shared_ptr<Node> node, TopicHolderSptr topic, Callback cb, std::shared_ptr<CallbackGroup> group)
            : TSubscriberBase(std::move(node), std::move(topic), std::move(cb), std::move(group)),
              socket_(createSubscriberSocket())
        {
            auto ep = waitDiscovery(topic->name());
            if (!ep) {
                endpoint_ = defaultEndpoint(topic->name());
            } else {
                endpoint_ = *ep;
            }
            socket_->connect(endpoint_);
            running_ = true;
            thread_ = std::thread([this]{ recvLoop(); });
        }
    
        ~Subscriber()
        {
            cleanup();
        }
    
        void stop()
        {
            cleanup();
        }
    
        void takeAll() override
        {
            message_t<T> msg;
            while (try_pop(queue_, msg))
            {
                if (callback_)
                {
                    callback_(*msg);
                }
            }
            clearReady();
        }
    
        bool setReadyIfNot() override
        {
            bool expected = false;
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
        void recvLoop()
        {
            while (running_)
            {
                T value{};
                bool ok = socket_->receive(&value, sizeof(T));
                if (!ok)
                {
                    continue; // timeout
                }
                auto ptr = std::make_shared<T>(value);
                push(queue_, std::move(ptr));
                SubscriberBase::callbackGroup().notify(this);
            }
        }
    
        void cleanup()
        {
            if (running_)
            {
                running_ = false;
                if (thread_.joinable())
                    thread_.join();
            }
            close(queue_);
        }
    
        void drainAll(std::vector<lux::communication::TimeExecEntry>& out) override
        {
            if constexpr(lux::communication::is_msg_stamped<T>)
            {
                message_t<T> msg;
                while (try_pop(queue_, msg))
                {
                    uint64_t ts_ns = lux::communication::builtin_msgs::common_msgs::extract_timstamp(*msg);
                    auto invoker = [cb=callback_, m=std::move(msg)]() mutable {
                        if (cb) { cb(*m); }
                    };
                    out.push_back(lux::communication::TimeExecEntry{ ts_ns, std::move(invoker) });
                }
            }
            else
            {
                throw std::runtime_error("Subscriber<T> does not support non-stamped message type T");
            }
        }
    
        std::string                         endpoint_;
        std::unique_ptr<SubscriberSocket>   socket_;
        Callback                            callback_;
        std::thread                         thread_;
        std::atomic<bool>                   running_{false};
        lux::communication::queue_t<T>      queue_;
        std::atomic<bool>                   ready_flag_{false};
    };

} // namespace lux::communication::interprocess

