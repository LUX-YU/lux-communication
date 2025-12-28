#pragma once
#include <functional>
#include <memory>
#include <lux/communication/Queue.hpp>
#include <lux/communication/ExecutorBase.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/visibility.h>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

#include "Topic.hpp"

#if __has_include(<moodycamel/concurrentqueue.h>) || __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>) || __has_include(<concurrentqueue/concurrentqueue.h>)
#   define LUX_SUBSCRIBER_USE_LOCKFREE_QUEUE
#endif

namespace lux::communication
{
    class CallbackGroupBase;
}

namespace lux::communication::intraprocess
{
    class Node; // Forward declaration
    template <typename T>
	class Subscriber : public lux::communication::SubscriberBase
    {
        static TopicSptr getTopic(Node* node, const std::string& topic)
        {
            return node->domain().createOrGetTopic<Topic<T>, T>(topic);
        }

        static CallbackGroupBase* getCallbackGroup(CallbackGroupBase* cgb, Node* node)
        {
            return cgb == nullptr ? node->defaultCallbackGroup() : cgb;
        }

        // Ordered item: (sequence number, message)
        struct OrderedItem {
            uint64_t seq;
            message_t<T> msg;
        };

#ifdef LUX_SUBSCRIBER_USE_LOCKFREE_QUEUE
        using ordered_queue_t = moodycamel::ConcurrentQueue<OrderedItem>;
        static bool try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_dequeue(out); }
        static void push_item(ordered_queue_t& q, OrderedItem v) { q.enqueue(std::move(v)); }
        static void close_queue(ordered_queue_t&) {}
        static size_t queue_size_approx(ordered_queue_t& q) { return q.size_approx(); }
        
        // Bulk dequeue for better performance (fewer atomic operations)
        static size_t try_pop_bulk(ordered_queue_t& q, OrderedItem* items, size_t max_count) {
            return q.try_dequeue_bulk(items, max_count);
        }
#else
        using ordered_queue_t = lux::cxx::BlockingQueue<OrderedItem>;
        static bool try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_pop(out); }
        static void push_item(ordered_queue_t& q, OrderedItem v) { q.push(std::move(v)); }
        static void close_queue(ordered_queue_t& q) { q.close(); }
        static size_t queue_size_approx(ordered_queue_t& q) { return q.size(); }
        
        // Fallback: no bulk support, use loop
        static size_t try_pop_bulk(ordered_queue_t& q, OrderedItem* items, size_t max_count) {
            size_t count = 0;
            while (count < max_count && q.try_pop(items[count])) {
                ++count;
            }
            return count;
        }
#endif

    public:
        using Callback = std::function<void(const message_t<T>)>;

        template<typename Func>
        Subscriber(const std::string& topic, Node* node, Func&& func, CallbackGroupBase* cgb = nullptr)
            :   SubscriberBase(getTopic(node, topic), node, getCallbackGroup(cgb, node)), callback_func_(std::forward<Func>(func))
        {}

        ~Subscriber() override {
            cleanup();
        }

        Subscriber(const Subscriber &) = delete;
        Subscriber &operator=(const Subscriber &) = delete;

        Subscriber(Subscriber&& rhs) = delete;
		Subscriber& operator=(Subscriber&& rhs) = delete;

        // The interface called by Topic when a new message arrives (with sequence number)
        void enqueue(uint64_t seq, message_t<T> msg)
        {
            push_item(queue_, OrderedItem{ seq, std::move(msg) });
            callbackGroup()->notify(this);
        }

        // Called by legacy executors (SingleThreadedExecutor, MultiThreadedExecutor)
        // Executes callbacks in local queue order (not global order)
        void takeAll() override
        {
            OrderedItem item;
            while (try_pop_item(queue_, item))
            {
                callback_func_(item.msg);
            }

            clearReady();

            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);
        }

    private:
        void cleanup() 
        {
			// Close the queue and join the thread if needed
			close_queue(queue_);
            clearReady();
        }

        // Called by TimeOrderedExecutor: drain all items with their sequence numbers
        void drainAll(std::vector<TimeExecEntry>& out) override
        {
            OrderedItem item;
            while (try_pop_item(queue_, item))
            {
                // capture 'msg' by move in the invoker
                auto invoker = [this, m = std::move(item.msg)]() mutable {
                    this->callback_func_(m);
                };
                // Use seq as the timestamp_ns field (TimeExecEntry is reused for ordering)
                out.push_back(TimeExecEntry{ item.seq, std::move(invoker) });
            }

            clearReady();

            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);
        }

        // Static trampoline function for ExecEntry (avoids std::function overhead)
        static void invokeTrampoline(void* obj, const std::shared_ptr<void>& msg) {
            auto* self = static_cast<Subscriber<T>*>(obj);
            auto typed_msg = std::static_pointer_cast<T>(msg);
            self->callback_func_(typed_msg);
        }

        // High-performance drain using function pointer trampoline and bulk dequeue
        void drainAllExec(std::vector<ExecEntry>& out) override
        {
            // Delegate to drainExecSome with unlimited count
            drainExecSome(out, SIZE_MAX);
        }

        /**
         * @brief Bounded drain: drain at most max_count entries.
         *        Uses bulk dequeue for efficiency but limits total drained.
         *        This keeps the reorder window small by enabling round-robin draining.
         */
        size_t drainExecSome(std::vector<ExecEntry>& out, size_t max_count) override
        {
            static constexpr size_t kBulkSize = 256;
            thread_local OrderedItem bulk_buffer[kBulkSize];
            
            size_t total_drained = 0;
            
            while (total_drained < max_count)
            {
                const size_t to_pop = std::min(kBulkSize, max_count - total_drained);
                const size_t count = try_pop_bulk(queue_, bulk_buffer, to_pop);
                if (count == 0) break;
                
                for (size_t i = 0; i < count; ++i)
                {
                    ExecEntry e;
                    e.seq = bulk_buffer[i].seq;
                    e.obj = this;
                    e.invoke = &Subscriber<T>::invokeTrampoline;
                    e.msg = std::static_pointer_cast<void>(bulk_buffer[i].msg);
                    out.push_back(std::move(e));
                    
                    // Clear the buffer slot to release reference
                    bulk_buffer[i].msg.reset();
                }
                total_drained += count;
            }

            clearReady();

            // Re-notify if more data remains (for round-robin scheduling)
            if (queue_size_approx(queue_) > 0)
                callbackGroup()->notify(this);
            
            return total_drained;
        }

    private:
        Callback         callback_func_{ nullptr };
        ordered_queue_t  queue_;
    };
}
