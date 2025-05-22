#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <thread>
#include <unordered_set>

#include <lux/communication/visibility.h>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/SubscriberBase.hpp>

#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>
#include <lux/cxx/concurrent/ThreadPool.hpp>

namespace lux::communication
{
    // Forward declaration of Node class.
    namespace introprocess { class Node; }

    /**
     * @brief Base Executor class responsible for scheduling and executing callbacks.
     *
     * This class manages a collection of callback groups and provides the core functionality
     * for executing callbacks. It handles waiting for new tasks and waking up when new data
     * arrives.
     */
    class LUX_COMMUNICATION_PUBLIC Executor : public std::enable_shared_from_this<Executor>
    {
    public:
        Executor() : running_(false) {}
        virtual ~Executor() { stop(); }

        /**
         * @brief Adds a callback group to the executor.
         *
         * Inserts the callback group into the executor's internal set and informs the group
         * about its associated executor.
         *
         * @param group Shared pointer to the callback group to be added.
         */
        virtual void addCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_groups_.insert(group);
            // Inform the callback group about the Executor it is attached to.
            group->setExecutor(shared_from_this());
        }

        /**
         * @brief Removes a callback group from the executor.
         *
         * Erases the specified callback group from the internal set.
         * Optionally, one might call group->setExecutor(nullptr) before removal.
         *
         * @param group Shared pointer to the callback group to be removed.
         */
        virtual void removeCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Optionally: group->setExecutor(nullptr);
            callback_groups_.erase(group);
        }

        /**
         * @brief Adds a Node's callback groups to the executor.
         *
         * This function allows a Node to register its callback groups with the Executor.
         * The implementation should traverse the Node's subscribers and add their callback groups.
         *
         * @param node Shared pointer to the Node to add.
         */
        virtual void addNode(std::shared_ptr<introprocess::Node> node);

        /**
         * @brief Processes available callbacks once.
         *
         * This pure virtual function should be implemented by subclasses to collect all
         * ready subscribers from callback groups and execute their callbacks.
         */
        virtual void spinSome() = 0;

        /**
         * @brief Continuously executes callbacks.
         *
         * Enters a loop that repeatedly calls spinSome() to process ready callbacks.
         * If no callbacks are ready, it waits using a condition variable.
         */
        virtual void spin()
        {
            if (running_)
                return;
            running_ = true;

            // Loop in the current thread.
            while (running_)
            {
                spinSome();
                // If still running, block until new messages or callbacks become available.
                if (running_)
                {
                    waitCondition();
                }
            }
        }

        /**
         * @brief Stops the executor.
         *
         * Sets the running flag to false and notifies waiting threads to terminate.
         */
        virtual void stop()
        {
            running_ = false;
            notifyCondition();
        }

        /**
         * @brief Wakes up the executor.
         *
         * Notifies the executor to exit any waiting state, allowing it to process new callbacks.
         */
        virtual void wakeup()
        {
            notifyCondition();
        }

    protected:
        /**
         * @brief Waits for a runnable callback or a stop condition.
         *
         * Blocks the current thread until either the executor stops running or there is at least
         * one callback available to execute.
         */
        void waitCondition()
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait(lock, [this]
                { return !running_.load() || checkRunnable(); });
        }

        /**
         * @brief Notifies waiting threads.
         *
         * Wakes up all threads that are waiting on the condition variable.
         */
        void notifyCondition()
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            cv_.notify_all();
        }

        /**
         * @brief Checks if any callback is ready to run.
         *
         * Iterates over all registered callback groups and checks if any group has ready subscribers.
         * Derived classes may override this method to implement custom checking logic.
         *
         * @return true if at least one callback is ready, false otherwise.
         */
        virtual bool checkRunnable() {
            for (auto& g : callback_groups_)
            {
                if (g->hasReadySubscribers())
                {
                    return true;
                }
            }
            return false;
        }

        std::atomic<bool> running_;  ///< Flag indicating if the executor is running.
        std::mutex cv_mutex_;        ///< Mutex used with the condition variable.
        std::condition_variable cv_; ///< Condition variable for waiting on new callbacks.
        std::mutex mutex_;           ///< Mutex to protect access to the callback groups.
        std::unordered_set<std::shared_ptr<CallbackGroup>> callback_groups_; ///< Set of registered callback groups.
    };

    /**
     * @brief Single-threaded Executor.
     *
     * Processes callbacks sequentially in a single thread. It iterates over all callback groups,
     * collects ready subscribers, and executes their callbacks by invoking takeAll().
     */
    class LUX_COMMUNICATION_PUBLIC SingleThreadedExecutor : public Executor
    {
    public:
        SingleThreadedExecutor() = default;
        ~SingleThreadedExecutor() override
        {
            stop();
        }

        /**
         * @brief Processes ready callbacks.
         *
         * Copies the callback groups to minimize lock holding time, then for each group,
         * collects ready subscribers and executes their callbacks sequentially.
         */
        void spinSome() override
        {
            // Copy callback groups to a local vector.
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto& g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // Process each group's ready subscribers sequentially.
            for (auto& group : groups_copy)
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
    };

    /**
     * @brief Multi-threaded Executor.
     *
     * Utilizes a thread pool to execute callbacks concurrently. For each callback group:
     * - Mutually exclusive groups are processed sequentially in the current thread.
     * - Reentrant groups have their callbacks dispatched to the thread pool for parallel execution.
     */
    class LUX_COMMUNICATION_PUBLIC MultiThreadedExecutor : public Executor
    {
    public:
        /**
         * @brief Constructor.
         *
         * @param threadNum Number of threads in the thread pool (default is 2).
         */
        explicit MultiThreadedExecutor(size_t threadNum = 2)
            : thread_pool_(threadNum)
        {
        }

        ~MultiThreadedExecutor() override
        {
            stop();
        }

        /**
         * @brief Processes ready callbacks.
         *
         * The procedure is as follows:
         * 1) Copy the callback groups to minimize lock holding.
         * 2) For each callback group:
         *    - If the group is mutually exclusive, execute callbacks sequentially in the current thread.
         *    - If the group is reentrant, dispatch each callback to the thread pool.
         * 3) Wait for all dispatched reentrant callbacks to complete before proceeding.
         */
        void spinSome() override
        {
            // 1) Copy callback groups to a local vector.
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto& g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // 2) Process each group and collect ready subscribers.
            std::vector<std::future<void>> futures;
            futures.reserve(64); // Pre-allocate for efficiency.

            for (auto& group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                if (readySubs.empty())
                    continue;

                if (group->getType() == CallbackGroupType::MutuallyExclusive)
                {
                    // Process mutually exclusive group sequentially in the current thread.
                    for (auto* sub : readySubs)
                    {
                        if (!running_)
                            break;
                        sub->takeAll();
                    }
                }
                else
                {
                    // For reentrant groups, dispatch each callback to the thread pool.
                    for (auto* sub : readySubs)
                    {
                        if (!running_)
                            break;
                        // Submit the callback execution asynchronously.
                        futures.push_back(
                            thread_pool_.submit(
                                [sub] { sub->takeAll(); })
                        );
                    }
                }
            }

            // 3) Wait for all asynchronously dispatched callbacks to finish.
            for (auto& f : futures)
            {
                f.wait();
            }
        }

        /**
         * @brief Stops the executor and shuts down the thread pool.
         *
         * Marks the executor as stopped, notifies waiting threads, and closes the thread pool to
         * release resources.
         */
        void stop() override
        {
            if (running_.exchange(false))
            {
                notifyCondition(); // Wake up waiting threads.
            }
            thread_pool_.close();
        }

    private:
        lux::cxx::ThreadPool thread_pool_; ///< Thread pool for executing reentrant callbacks concurrently.
    };

    /**
     * @brief Single-threaded Time-Ordered Executor.
     *
     * Processes callbacks sequentially in a single thread while ordering them by timestamp.
     * This executor only accepts MutuallyExclusive type callback groups and uses an optional delay
     * window (time_offset) to account for late-arriving messages.
     */
    class LUX_COMMUNICATION_PUBLIC TimeOrderedExecutor : public Executor
    {
    public:
        /**
         * @brief Constructor.
         *
         * @param time_offset Initial delay window (in nanoseconds). A value of 0 indicates immediate execution
         *                    without waiting for potential late messages.
         */
        explicit TimeOrderedExecutor(std::chrono::nanoseconds time_offset = std::chrono::nanoseconds{ 0 })
            : time_offset_(time_offset)
        {
            running_.store(false);
        }

        ~TimeOrderedExecutor() override
        {
            stop();
        }

        // ----------------------------------------------------------------
        // Override Executor interface methods
        // ----------------------------------------------------------------

        /**
         * @brief Adds a callback group to the executor.
         *
         * Only MutuallyExclusive callback groups are supported. Adding a Reentrant group
         * will throw a runtime error.
         *
         * @param group Shared pointer to the callback group to be added.
         */
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

        /**
         * @brief Processes ready callbacks in time order.
         *
         * This function performs the following steps:
         * 1) Fetches ready subscribers and drains their execution entries into a local buffer.
         * 2) Processes and executes any entries that are ready based on their timestamp.
         */
        void spinSome() override
        {
            // Step 1: Collect ready subscribers and drain their messages into the local buffer.
            fetchReadyEntries();

            // Step 2: Process entries from the buffer that are ready for execution.
            processReadyEntries();
        }

        /**
         * @brief Continuously processes callbacks in time order.
         *
         * Runs a loop that repeatedly calls spinSome() and then waits appropriately until new messages arrive
         * or the next message is ready for execution.
         */
        void spin() override
        {
            if (running_.exchange(true))
                return; // Already running.

            while (running_)
            {
                spinSome();
                if (!running_)
                    break;

                // Step 3: Decide on the waiting strategy:
                // - If no data is available, wait indefinitely until notified.
                // - Otherwise, perform a timed wait until the earliest entry is ready.
                doWait();
            }
        }

        /**
         * @brief Stops the time-ordered executor.
         *
         * Sets the running flag to false and notifies any waiting threads.
         */
        void stop() override
        {
            if (running_.exchange(false))
            {
                notifyCondition(); // Wake up waiting threads.
            }
        }

        // ----------------------------------------------------------------
        // Methods to set and get the delay window (time offset)
        // ----------------------------------------------------------------

        /**
         * @brief Sets the delay window (time offset).
         *
         * @param offset Delay window in nanoseconds.
         */
        void setTimeOffset(std::chrono::nanoseconds offset)
        {
            time_offset_ = offset;
        }

        /**
         * @brief Gets the current delay window (time offset).
         *
         * @return Current delay window in nanoseconds.
         */
        std::chrono::nanoseconds getTimeOffset() const
        {
            return time_offset_;
        }

    protected:
        /**
         * @brief Checks if there is any executable entry in the internal buffer.
         *
         * @return true if the buffer is not empty, false otherwise.
         */
        bool checkRunnable() override
        {
            return !buffer_.empty();
        }

    private:
        // ----------------------------------------------------------------
        // Internal Methods for TimeOrderedExecutor
        // ----------------------------------------------------------------

        /**
         * @brief Collects ready subscribers and drains their execution entries.
         *
         * Creates a shallow copy of the callback groups, iterates through each group, collects ready subscribers,
         * and drains their messages (execution entries) into a local vector. These entries are then pushed into the
         * internal priority queue for time-ordered processing.
         */
        void fetchReadyEntries()
        {
            // Create a shallow copy of callback groups.
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto& g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // Traverse each group and retrieve ready subscribers.
            std::vector<TimeExecEntry> newEntries;
            newEntries.reserve(64);

            for (auto& group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                for (auto* subBase : readySubs)
                {
                    subBase->drainAll(newEntries);
                }
            }

            // Insert new entries into the internal priority queue.
            for (auto& e : newEntries)
            {
                buffer_.push(std::move(e));
            }
        }

        /**
         * @brief Processes and executes entries that are ready based on their timestamps.
         *
         * Determines the current time and computes the cutoff timestamp (current time minus delay window).
         * Then it processes and executes all entries in the buffer with timestamps less than or equal to the cutoff.
         */
        void processReadyEntries()
        {
            // Get the current time in nanoseconds.
            auto now_tp = std::chrono::steady_clock::now();
            auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();

            // Calculate the cutoff timestamp.
            uint64_t cutoff = 0;
            if (time_offset_.count() == 0)
            {
                cutoff = UINT64_MAX; // No delay: process all entries immediately.
            }
            else
            {
                cutoff = now_ns - static_cast<uint64_t>(time_offset_.count());
            }

            // Process entries in the buffer without additional locking (single-threaded).
            while (!buffer_.empty())
            {
                const auto& top = buffer_.top();
                if (top.timestamp_ns <= cutoff)
                {
                    // Execute the callback for the top entry.
                    auto entry = std::move(const_cast<TimeExecEntry&>(top));
                    buffer_.pop();
                    entry.invoker();
                }
                else
                {
                    // The top entry is not yet ready; break out of the loop.
                    break;
                }
            }
        }

        /**
         * @brief Determines the waiting strategy before processing the next entry.
         *
         * If the internal buffer is empty, waits indefinitely until new data arrives.
         * Otherwise, calculates the duration until the earliest entry is ready and performs a timed wait.
         */
        void doWait()
        {
            // If the buffer is empty, wait indefinitely until notified.
            if (buffer_.empty())
            {
                waitCondition();
                return;
            }

            // Otherwise, inspect the earliest entry.
            const auto& top = buffer_.top();
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

            // If the top entry is ready to execute, return immediately.
            if (top.timestamp_ns <= cutoff)
            {
                return;
            }

            // Calculate the wait duration until the top entry is ready.
            // Execution time for the top entry = top.timestamp_ns + time_offset.
            // Wait duration = (top.timestamp_ns + time_offset) - now_ns.
            uint64_t earliest_ns = 0;
            if (time_offset_.count() == 0)
            {
                earliest_ns = top.timestamp_ns - now_ns;
            }
            else
            {
                earliest_ns = (top.timestamp_ns + static_cast<uint64_t>(time_offset_.count())) - now_ns;
            }

            // Sanity check: if the wait duration is unreasonably large, simply wait indefinitely.
            if (earliest_ns > 10ull * 365ull * 24ull * 3600ull * 1'000'000'000ull)
            {
                waitCondition();
                return;
            }

            // Perform a timed wait for the calculated duration.
            auto wait_dur = std::chrono::nanoseconds(earliest_ns);
            std::unique_lock<std::mutex> lk(cv_mutex_);
            cv_.wait_for(lk, wait_dur,
                [this] { return !running_.load() || !buffer_.empty(); }
            );
        }

    private:
        std::priority_queue<TimeExecEntry> buffer_; ///< Global priority queue for time-ordered execution (single-threaded, no locking required).
        std::chrono::nanoseconds time_offset_;         ///< Delay window (in nanoseconds) to handle late-arriving messages.
    };

    // Implementation of CallbackGroup member functions:

    /**
     * @brief Notifies that a subscriber within the callback group is ready.
     *
     * Adds the subscriber to the group's ready list and notifies the associated executor.
     *
     * @param sub Pointer to the subscriber that is ready.
     */
    void CallbackGroup::notify(ISubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Add the subscriber to the ready list.
        ready_list_.push_back(sub);
        // Notify the associated Executor to wake up and process callbacks.
        auto exec = executor_.lock();
        if (exec)
        {
            exec->wakeup();
        }
    }

    /**
     * @brief Collects all ready subscribers.
     *
     * Swaps out the current ready list with an empty vector and returns the subscribers that are ready.
     *
     * @return A vector of pointers to ready subscribers.
     */
    std::vector<ISubscriberBase*> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ISubscriberBase*> result;
        result.swap(ready_list_);
        return result;
    }

    /**
     * @brief Collects all subscribers within the callback group.
     *
     * Returns a vector containing pointers to all subscribers registered with the callback group.
     *
     * @return A vector of pointers to all subscribers.
     */
    std::vector<ISubscriberBase*> CallbackGroup::collectAllSubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ISubscriberBase*> result;
        result.reserve(subscribers_.size());
        for (auto& sb : subscribers_.values())
        {
            result.push_back(sb);
        }
        return result;
    }

} // namespace lux::communication
