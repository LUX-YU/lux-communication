#pragma once

#include <thread>
#include <functional>
#include <atomic>
#include <optional>
#include <chrono>

#include <lux/communication/Queue.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

#include "Node.hpp"

#if __has_include(<moodycamel/concurrentqueue.h>) || __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>) || __has_include(<concurrentqueue/concurrentqueue.h>)
#   define LUX_INTERPROCESS_SUBSCRIBER_USE_LOCKFREE_QUEUE
#endif

namespace lux::communication::interprocess 
{
    class SubscriberSocket
    {
    public:
        virtual ~SubscriberSocket() = default;
        virtual void connect(const std::string& endpoint) = 0;
        virtual bool receive(void* data, size_t size) = 0;
    };

    LUX_COMMUNICATION_PUBLIC std::unique_ptr<SubscriberSocket> createSubscriberSocket();
    LUX_COMMUNICATION_PUBLIC std::string defaultEndpoint(const std::string& topic);
    LUX_COMMUNICATION_PUBLIC std::optional<std::string> waitDiscovery(
        const std::string& topic,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{200});

    /**
     * @brief Interprocess Subscriber - receives messages via network socket.
     *        Follows the same pattern as intraprocess::Subscriber.
     */
    template<typename T>
    class Subscriber : public lux::communication::SubscriberBase
    {
        static CallbackGroupBase* getCallbackGroup(CallbackGroupBase* cgb, Node* node)
        {
            return cgb == nullptr ? node->defaultCallbackGroup() : cgb;
        }

        // Ordered item: (sequence number, message)
        struct OrderedItem {
            uint64_t seq;
            message_t<T> msg;
        };

#ifdef LUX_INTERPROCESS_SUBSCRIBER_USE_LOCKFREE_QUEUE
        using ordered_queue_t = moodycamel::ConcurrentQueue<OrderedItem>;
        static bool try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_dequeue(out); }
        static void push_item(ordered_queue_t& q, OrderedItem v) { q.enqueue(std::move(v)); }
        static void close_queue(ordered_queue_t&) {}
        static size_t queue_size_approx(ordered_queue_t& q) { return q.size_approx(); }
#else
        using ordered_queue_t = lux::cxx::BlockingQueue<OrderedItem>;
        static bool try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_pop(out); }
        static void push_item(ordered_queue_t& q, OrderedItem v) { q.push(std::move(v)); }
        static void close_queue(ordered_queue_t& q) { q.close(); }
        static size_t queue_size_approx(ordered_queue_t& q) { return q.size(); }
#endif

    public:
        using Callback = std::function<void(message_t<T>)>;

        template<typename Func>
        Subscriber(const std::string& topic, Node* node, Func&& func, CallbackGroupBase* cgb = nullptr)
            : SubscriberBase(nullptr, node, getCallbackGroup(cgb, node)),
              topic_name_(topic),
              callback_func_(std::forward<Func>(func)),
              socket_(createSubscriberSocket())
        {
            auto ep = waitDiscovery(topic);
            if (!ep) {
                endpoint_ = defaultEndpoint(topic);
            } else {
                endpoint_ = *ep;
            }
            socket_->connect(endpoint_);
            running_ = true;
            thread_ = std::thread([this]{ recvLoop(); });
        }

        ~Subscriber() override
        {
            cleanup();
        }

        Subscriber(const Subscriber&) = delete;
        Subscriber& operator=(const Subscriber&) = delete;
        Subscriber(Subscriber&&) = delete;
        Subscriber& operator=(Subscriber&&) = delete;

        void stop()
        {
            cleanup();
        }

        void takeAll() override
        {
            OrderedItem item;
            while (try_pop_item(queue_, item))
            {
                callback_func_(std::move(item.msg));
            }
            clearReady();

            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);
        }

    private:
        void cleanup()
        {
            if (running_.exchange(false))
            {
                if (thread_.joinable())
                    thread_.join();
            }
            close_queue(queue_);
            clearReady();
        }

        void recvLoop()
        {
            uint64_t seq = 0;
            while (running_)
            {
                T value{};
                bool ok = socket_->receive(&value, sizeof(T));
                if (!ok)
                {
                    continue; // timeout or error
                }
                auto ptr = std::make_shared<T>(std::move(value));
                
                // For stamped messages, extract timestamp as sequence
                uint64_t msg_seq = seq++;
                if constexpr (lux::communication::is_msg_stamped<T>)
                {
                    msg_seq = lux::communication::builtin_msgs::common_msgs::extract_timstamp(*ptr);
                }
                
                push_item(queue_, OrderedItem{ msg_seq, std::move(ptr) });
                callbackGroup()->notify(this);
            }
        }

        void drainAll(std::vector<TimeExecEntry>& out) override
        {
            OrderedItem item;
            while (try_pop_item(queue_, item))
            {
                auto invoker = [this, m = std::move(item.msg)]() mutable {
                    this->callback_func_(std::move(m));
                };
                out.push_back(TimeExecEntry{ item.seq, std::move(invoker) });
            }
            clearReady();

            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);
        }

        static void invokeTrampoline(void* obj, std::shared_ptr<void> msg) {
            auto* self = static_cast<Subscriber<T>*>(obj);
            auto typed_msg = std::static_pointer_cast<T>(std::move(msg));
            self->callback_func_(std::move(typed_msg));
        }

        void drainAllExec(std::vector<ExecEntry>& out) override
        {
            drainExecSome(out, SIZE_MAX);
        }

        size_t drainExecSome(std::vector<ExecEntry>& out, size_t max_count) override
        {
            size_t count = 0;
            OrderedItem item;
            while (count < max_count && try_pop_item(queue_, item))
            {
                ExecEntry e;
                e.seq = item.seq;
                e.obj = this;
                e.invoke = &Subscriber<T>::invokeTrampoline;
                e.msg = std::static_pointer_cast<void>(item.msg);
                out.push_back(std::move(e));
                ++count;
            }

            clearReady();

            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);

            return count;
        }

    private:
        std::string                         topic_name_;
        std::string                         endpoint_;
        Callback                            callback_func_;
        std::unique_ptr<SubscriberSocket>   socket_;
        std::thread                         thread_;
        std::atomic<bool>                   running_{false};
        ordered_queue_t                     queue_;
    };

} // namespace lux::communication::interprocess

