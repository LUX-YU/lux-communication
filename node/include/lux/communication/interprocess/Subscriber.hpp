#pragma once

#include <memory>
#include <string>
#include <thread>
#include <functional>
#include <atomic>
#include <optional>

#include <lux/communication/visibility.h>
#include "lux/communication/interprocess/Publisher.hpp"
#include "lux/communication/UdpMultiCast.hpp"
#include <lux/communication/Queue.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

namespace lux::communication::interprocess {

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
    class Subscriber : public lux::communication::ISubscriberBase
    {
    public:
        using Callback = std::function<void(const T&)>;
    
        Subscriber(const std::string& topic,
                   Callback cb,
                   std::shared_ptr<lux::communication::CallbackGroup> group,
                   int id)
            : ISubscriberBase(id),
              topic_(topic),
              callback_(std::move(cb)),
              callback_group_(std::move(group)),
              socket_(createSubscriberSocket())
        {
            auto ep = waitDiscovery(topic_);
            if (!ep) {
                endpoint_ = defaultEndpoint(topic_);
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
                std::memory_order_acq_rel, std::memory_order_acquire);
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
                if (callback_group_)
                {
                    callback_group_->notify(this);
                }
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
            if (callback_group_)
            {
                callback_group_->removeSubscriber(this);
            }
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
    
        std::string topic_;
        std::string endpoint_;
        std::unique_ptr<SubscriberSocket> socket_;
        Callback callback_;
        std::shared_ptr<lux::communication::CallbackGroup> callback_group_;
        std::thread thread_;
        std::atomic<bool> running_{false};
    
        lux::communication::queue_t<T> queue_;
        std::atomic<bool> ready_flag_{false};
    };

} // namespace lux::communication::interprocess

