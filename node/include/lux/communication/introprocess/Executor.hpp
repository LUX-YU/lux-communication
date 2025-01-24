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
            cv_.wait(lock, [this] { return !running_.load() || checkRunnable(); });
        }

        // Notify/wake up
        void notifyCondition()
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            cv_.notify_all();
        }

        // Check if there is any runnable callback (subclass can implement details)
        virtual bool checkRunnable() { return false; }

        std::atomic<bool>       running_;

        std::mutex              cv_mutex_;
        std::condition_variable cv_;

        std::mutex              mutex_;
        // Store all callback groups
        std::unordered_set<std::shared_ptr<CallbackGroup>> callback_groups_;
    };

    // Example of a single-threaded executor
    class SingleThreadedExecutor : public Executor
    {
    public:
        SingleThreadedExecutor() = default;
        ~SingleThreadedExecutor() override{
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
                    if (!running_) break;
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
        {}

        ~MultiThreadedExecutor() override
        {
            stop();
            thread_pool_.close();  // Wait for all tasks in the thread pool to finish
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
                        if (!running_) break;
                        sub->takeAll();
                    }
                }
                else
                {
                    // Reentrant group: dispatch each callback to the thread pool
                    for (auto *sub : readySubs)
                    {
                        if (!running_) break;

                        // Submit the task to execute sub->takeAll() asynchronously
                        futures.push_back(
                            thread_pool_.submit(
                                [sub]{sub->takeAll();}
                            )
                        );
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
                notifyCondition();  // Wake up
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
        std::thread         spin_thread_;

        // A custom thread pool for parallel execution of Reentrant callbacks
        lux::cxx::ThreadPool thread_pool_;
    };

    void CallbackGroup::notify(ISubscriberBase* sub)
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

    std::vector<ISubscriberBase*> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Swap out the ready queue
        std::vector<ISubscriberBase*> result;
        result.swap(ready_list_);
        return result;
    }

} // namespace lux::communication::introprocess
