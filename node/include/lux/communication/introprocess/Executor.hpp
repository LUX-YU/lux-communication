#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>

#include "CallbackGroup.hpp"
#include "SubscriberBase.hpp"

#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication::introprocess
{
    class Node; // Forward declaration

    class Executor : public std::enable_shared_from_this<Executor>
    {
    public:
        Executor() : running_(false) {}
        virtual ~Executor() { stop(); }

        // Add a callback group
        virtual void addCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_groups_.insert(group);
            // Let the callback group know which Executor it belongs to
            group->setExecutor(shared_from_this());
        }

        // Remove a callback group
        virtual void removeCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Before removing, you could call group->setExecutor(nullptr);
            callback_groups_.erase(group);
        }

        // If you want the Node to run on this Executor, you can add an addNode
        // You can choose to add the callback groups of all Subscribers under the Node
        virtual void addNode(std::shared_ptr<Node> node);

        // Execute once: retrieve all ready Subscribers and run their callbacks
        virtual void spinSome() = 0;

        // Keep executing
        virtual void spin()
        {
            if (running_)
                return;
            running_ = true;

            // Loop in the current thread
            while (running_)
            {
                spinSome();
                // We can block here and continue when new messages arrive
                if (running_)
                {
                    waitCondition();
                }
            }
        }

        // Stop execution
        virtual void stop()
        {
            running_ = false;
            notifyCondition();
        }

        // Wake up the Executor
        virtual void wakeup()
        {
            notifyCondition();
        }

    protected:
        // For subclasses: wait for condition
        void waitCondition()
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait(lock, [this]
                     { return !running_.load() || checkRunnable(); });
        }

        // Notify/wake up
        void notifyCondition()
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            cv_.notify_all();
        }

        // Check if there is any runnable callback (subclass can implement details)
        virtual bool checkRunnable() { return false; }

        std::atomic<bool> running_;

        std::mutex cv_mutex_;
        std::condition_variable cv_;

        std::mutex mutex_;
        // Store all callback groups
        std::unordered_set<std::shared_ptr<CallbackGroup>> callback_groups_;
    };

    // Example of a single-threaded executor
    class SingleThreadedExecutor : public Executor
    {
    public:
        SingleThreadedExecutor() = default;
        ~SingleThreadedExecutor() override
        {
            stop();
        }

        void spinSome() override
        {
            // Iterate all callback groups
            // Collect ready subscribers and execute
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto &g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            for (auto &group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                for (auto sub : readySubs)
                {
                    if (!running_)
                        break;
                    sub->takeAll();
                }
            }
        }

    protected:
        bool checkRunnable() override
        {
            // Determine if any callback group has a non-empty ready_list
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &g : callback_groups_)
            {
                // If g->collectReadySubscribers() doesn't actually clear, we need an additional interface to check
                // For demonstration, just return true. Actual logic can be more detailed.
                // A more detailed approach might call group->hasReadySubscribers().
            }
            return true;
        }
    };

    class MultiThreadedExecutor : public Executor
    {
    public:
        explicit MultiThreadedExecutor(size_t threadNum = 2)
            : thread_pool_(threadNum)
        {
        }

        ~MultiThreadedExecutor() override
        {
            stop();
            thread_pool_.close(); // Wait for all tasks in the thread pool to finish
        }

        void spinSome() override
        {
            // 1) Copy the list of callback groups to avoid holding the lock too long
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto &g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // 2) Traverse each group, retrieve ready subscribers
            //    - For MutuallyExclusive groups: execute in the current thread serially
            //    - For Reentrant groups: distribute to the thread pool for parallel execution
            std::vector<std::future<void>> futures;
            futures.reserve(64); // Pre-allocate if desired

            for (auto &group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                if (readySubs.empty())
                    continue;

                if (group->getType() == CallbackGroupType::MutuallyExclusive)
                {
                    // Mutually exclusive group: execute in current thread sequentially
                    for (auto *sub : readySubs)
                    {
                        if (!running_)
                            break;
                        sub->takeAll();
                    }
                }
                else
                {
                    // Reentrant group: dispatch each callback to the thread pool
                    for (auto *sub : readySubs)
                    {
                        if (!running_)
                            break;

                        // Submit the task to execute sub->takeAll() asynchronously
                        futures.push_back(
                            thread_pool_.submit(
                                [sub]
                                { sub->takeAll(); }));
                    }
                }
            }

            // 3) Wait until all Reentrant callbacks in this round finish
            //    so that we don't mix with the next round of ready subscribers.
            //    If you want a true "pipelined" parallel, you could skip waiting,
            //    but it requires more complex synchronization.
            for (auto &f : futures)
            {
                f.wait();
            }
        }

        void stop() override
        {
            // Mark stop
            if (running_.exchange(false))
            {
                notifyCondition(); // Wake up
                if (spin_thread_.joinable())
                {
                    spin_thread_.join();
                }
            }
        }

    protected:
        bool checkRunnable() override
        {
            // Simple example: always runnable
            return true;
        }

    private:
        // We only use one thread to loop calling spinSome(),
        // but we distribute Reentrant callbacks to the thread pool for parallel processing.
        std::thread spin_thread_;

        // A custom thread pool for parallel execution of Reentrant callbacks
        lux::cxx::ThreadPool thread_pool_;
    };

    /**
     * @brief 单线程按时间戳排序执行器
     *
     * - 只接受 MutuallyExclusive 类型的 CallbackGroup
     * - 单线程执行，不需对内部缓冲加锁
     * - 可选支持“延迟窗口”(time_offset_ns_)，方便处理迟到消息
     */
    class TimeOrderedExecutor : public Executor
    {
    public:
        /**
         * @param time_offset 初始延迟窗口 (纳秒)。默认0表示拿到就立即执行，没有等待迟到消息的机会。
         */
        explicit TimeOrderedExecutor(std::chrono::nanoseconds time_offset = std::chrono::nanoseconds{0})
            : time_offset_(time_offset)
        {
            running_.store(false);
        }

        ~TimeOrderedExecutor() override
        {
            stop();
        }

        // ---------------------------------------------------
        // 覆盖 Executor 接口
        // ---------------------------------------------------
        void addCallbackGroup(std::shared_ptr<CallbackGroup> group) override
        {
            if (!group)
                return;
            if (group->getType() == CallbackGroupType::Reentrant)
            {
                throw std::runtime_error("[TimeOrderedExecutor] Reentrant group not supported in single-thread time-order mode!");
            }
            Executor::addCallbackGroup(group);
        }

        void spinSome() override
        {
            // 1) 收集 ready subscribers 并把它们的消息drain到本地
            fetchReadyEntries();

            // 2) process if any entry is ready
            processReadyEntries();
        }

        void spin() override
        {
            if (running_.exchange(true))
                return; // already running

            while (running_)
            {
                spinSome();
                if (!running_)
                    break;

                // 3) decide how to wait
                //    - if no data => waitCondition() until new notify
                //    - else => check earliest entry time => do timed wait
                doWait();
            }
        }

        void stop() override
        {
            if (running_.exchange(false))
            {
                notifyCondition(); // wake up
            }
        }

        // ---------------------------------------------------
        // 设置/获取延迟窗口
        // ---------------------------------------------------
        void setTimeOffset(std::chrono::nanoseconds offset)
        {
            time_offset_ = offset;
        }

        std::chrono::nanoseconds getTimeOffset() const
        {
            return time_offset_;
        }

    protected:
        bool checkRunnable() override
        {
            // 如果队列非空，或者正在running_ = false
            // 事实上，这里单线程版本只需返回 !buffer_.empty() 也可以。
            // 但我们下一步要看 doWait() 逻辑
            return true;
        }

    private:
        // ---------------------------------------------------
        // 1) 收集 ready subscribers
        // ---------------------------------------------------
        void fetchReadyEntries()
        {
            // 浅拷贝 callback_groups_
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto &g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // 遍历每组，取 ready 的 subscriber
            std::vector<TimeExecEntry> newEntries;
            newEntries.reserve(64);

            for (auto &group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                for (auto *subBase : readySubs)
                {
                    subBase->drainAll(newEntries);
                }
            }

            // 放入优先队列
            for (auto &e : newEntries)
            {
                buffer_.push(std::move(e));
            }
        }

        // ---------------------------------------------------
        // 2) 按时间戳处理可执行条目
        // ---------------------------------------------------
        void processReadyEntries()
        {
            // 计算当前时间点
            auto now_tp = std::chrono::steady_clock::now();
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();

            // 计算可以处理的最晚时间戳
            uint64_t cutoff = 0;
            if (time_offset_.count() == 0)
            {
                cutoff = UINT64_MAX; // 没有延迟
            }
            else
            {
                cutoff = now_ns - static_cast<uint64_t>(time_offset_.count());
            }

            // 单线程 => 无需锁
            while (!buffer_.empty())
            {
                const auto &top = buffer_.top();
                if (top.timestamp_ns <= cutoff)
                {
                    // 执行
                    auto entry = std::move(const_cast<TimeExecEntry &>(top));
                    buffer_.pop();
                    entry.invoker();
                }
                else
                {
                    // 该消息还太新, 等后面再处理
                    break;
                }
            }
        }

        // ---------------------------------------------------
        // 3) 决定如何等待:
        //    - 如果 buffer_ 为空 => 无数据 => 无需处理 => waitCondition()无限等待
        //    - 如果 buffer_ 不空 => 看队首时间戳 => 如果满足延迟窗口，还没到执行时刻 => 用 timed_wait
        // ---------------------------------------------------
        void doWait()
        {
            // 如果队列空，则说明暂时没事可做 => waitCondition() 等待新消息
            if (buffer_.empty())
            {
                // 直接 waitCondition(), 等 subscriber->notify() 后唤醒
                waitCondition();
                return;
            }

            // 否则队列不空 => 看最早一条消息
            const auto &top = buffer_.top();
            auto now_tp = std::chrono::steady_clock::now();
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();

            uint64_t cutoff;
            if (time_offset_.count() == 0)
            {
                cutoff = UINT64_MAX;
            }
            else
            {
                cutoff = now_ns - static_cast<uint64_t>(time_offset_.count());
            }

            // 如果队首消息可以立即执行，则不用等
            if (top.timestamp_ns <= cutoff)
            {
                // 下次 spinSome() 立刻可处理
                // 这里可以直接 return，让 spin 循环继续
                return;
            }

            // 需要等到 t = top.timestamp_ns + offset
            // => t - now_ns = (top.timestamp_ns - (now_ns - offset))
            //   = top.timestamp_ns - (now_ns - time_offset_)
            //   = (top.timestamp_ns + time_offset_) - now_ns
            // 下面换算成chrono::nanoseconds
            uint64_t earliest_ns = 0;
            if (time_offset_.count() == 0)
            {
                earliest_ns = top.timestamp_ns - now_ns;
            }
            else
            {
                earliest_ns = (top.timestamp_ns + (uint64_t)time_offset_.count()) - now_ns;
            }

            // 做一个 sanity check
            if (earliest_ns > 10ull * 365ull * 24ull * 3600ull * 1'000'000'000ull)
            {
                // 太离谱，直接等到下一次notify
                waitCondition();
                return;
            }

            // 进行 timed_wait
            // earliest_ns是纳秒
            auto wait_dur = std::chrono::nanoseconds(earliest_ns);

            // 使用unique_lock
            std::unique_lock<std::mutex> lk(cv_mutex_);
            // 如果在这段时间内，新消息到了(会notify)，则会提前唤醒
            // 如果没有新消息，就等到时间到了，超时唤醒后可以处理
            cv_.wait_for(lk, wait_dur, [this]
                         { return !running_.load() || !buffer_.empty(); });
        }

    private:
        // 全局优先队列(单线程, 无需锁)
        std::priority_queue<TimeExecEntry> buffer_;

        // 延迟窗口
        std::chrono::nanoseconds time_offset_;
    };

    void CallbackGroup::notify(ISubscriberBase *sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Add to the ready queue
        ready_list_.push_back(sub);

        // Notify Executor
        auto exec = executor_.lock();
        if (exec)
        {
            exec->wakeup();
        }
    }

    std::vector<ISubscriberBase *> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Swap out the ready queue
        std::vector<ISubscriberBase *> result;
        result.swap(ready_list_);
        return result;
    }

    std::vector<ISubscriberBase *> CallbackGroup::collectAllSubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ISubscriberBase *> result;
        for (auto sub : subscribers_)
        {
            result.push_back(sub);
        }
        return result;
    }

} // namespace lux::communication::introprocess
