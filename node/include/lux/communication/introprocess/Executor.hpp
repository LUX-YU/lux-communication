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
    class Node; // 前置声明

    class Executor : public std::enable_shared_from_this<Executor>
    {
    public:
        Executor() : running_(false) {}
        virtual ~Executor() { stop(); }

        // 添加一个回调组
        virtual void addCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_groups_.insert(group);
            // 让回调组知道它属于哪个 Executor
            group->setExecutor(shared_from_this());
        }

        // 移除一个回调组
        virtual void removeCallbackGroup(std::shared_ptr<CallbackGroup> group)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 在移除之前，可以先把 group->setExecutor(nullptr);
            callback_groups_.erase(group);
        }

        // 如果想让 Node 在 Executor 上跑，就可以加个 addNode
        // 这里可以选择把 Node 内所有 Subscriber 的回调组都加进来
        virtual void addNode(std::shared_ptr<Node> node);

        // 单次执行：取出所有就绪的 Subscriber 并执行其回调
        virtual void spinSome() = 0;

        // 持续执行
        virtual void spin() = 0;

        // 停止执行
        virtual void stop() 
        {
            running_ = false;
            notifyCondition();
        }

        // 唤醒 Executor
        virtual void wakeup()
        {
            notifyCondition();
        }

    protected:
        // 给子类用的：等待条件
        void waitCondition()
        {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            cv_.wait(lock, [this] { return !running_.load() || checkRunnable(); });
        }

        // 唤醒
        void notifyCondition()
        {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            cv_.notify_all();
        }

        // 检查是否有可运行的回调（子类可实现具体逻辑）
        virtual bool checkRunnable() { return false; }

        std::atomic<bool>       running_;

        std::mutex              cv_mutex_;
        std::condition_variable cv_;

        std::mutex              mutex_;
        // 记录所有回调组
        std::unordered_set<std::shared_ptr<CallbackGroup>> callback_groups_;
    };

    // 单线程执行器示例
    class SingleThreadedExecutor : public Executor
    {
    public:
        SingleThreadedExecutor() = default;
        ~SingleThreadedExecutor() override{
            stop();
        }

        void spin() override
        {
            if (running_)
                return;
            running_ = true;

            // 在当前线程中循环
            while (running_)
            {
                spinSome();
                // 可在这里阻塞，等有新的消息时再继续
                if (running_)
                {
                    waitCondition();
                }
            }
        }

        void spinSome() override
        {
            // 遍历所有 callback group
            // 收集就绪的 subscriber 并执行
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
            // 判断是否有任何callback group的ready_list非空
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto &g : callback_groups_)
            {
                // 如果 g->collectReadySubscribers() 不做真正清空，得额外接口判断
                // 这里简单示意，可以改成 group->hasReadySubscribers() 之类
                // 为了演示，就返回 true，实际可更精细
                // 需要更精细可以在 CallbackGroup 里加一个 isEmptyReadyList() 。
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
            thread_pool_.close();  // 等待线程池里的所有任务结束
        }

        void spin() override
        {
            // 防止重复启动
            if (running_)
                return;
            running_ = true;

            while (running_)
            {
                spinSome();  // 执行（或分发）回调
                if (running_)
                {
                    // 若依然在运行，则阻塞等待下一次唤醒
                    waitCondition();
                }
            }
        }

        void spinSome() override
        {
            // 1) 拷贝回调组列表，以免加锁时间过长
            std::vector<std::shared_ptr<CallbackGroup>> groups_copy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                groups_copy.reserve(callback_groups_.size());
                for (auto &g : callback_groups_)
                {
                    groups_copy.push_back(g);
                }
            }

            // 2) 逐个遍历回调组，取出就绪的Subscriber
            //    - 对MutuallyExclusive: 在当前线程串行执行
            //    - 对Reentrant: 分发到线程池并行执行
            std::vector<std::future<void>> futures; 
            futures.reserve(64); // 根据需要预估或留空也行

            for (auto &group : groups_copy)
            {
                auto readySubs = group->collectReadySubscribers();
                if (readySubs.empty())
                    continue;

                if (group->getType() == CallbackGroupType::MutuallyExclusive)
                {
                    // 互斥分组：在当前线程顺序执行
                    for (auto *sub : readySubs)
                    {
                        if (!running_) break;
                        sub->takeAll();
                    }
                }
                else
                {
                    // Reentrant 分组：把各回调投递到线程池并行处理
                    for (auto *sub : readySubs)
                    {
                        if (!running_) break;

                        // 将执行sub->takeAll()的任务异步提交到线程池
                        futures.push_back(
                            thread_pool_.submit(
                                [sub]{sub->takeAll();}
                            )
                        );
                    }
                }
            }

            // 3) 等待本轮所有 Reentrant 回调都执行完
            //    这样在下一次 spinSome() 收集到新一批就绪订阅者前，
            //    不会跟这一次的回调还在处理中“抢”同一批消息。
            //    如果你想真正做到“流水线式”并行，也可不等待，
            //    但那就需要更复杂的去重/同步逻辑。
            for (auto &f : futures)
            {
                f.wait();
            }
        }

        void stop() override
        {
            // 标记停止
            if (running_.exchange(false))
            {
                notifyCondition();  // 唤醒一下
                if (spin_thread_.joinable())
                {
                    spin_thread_.join();
                }
            }
        }

    protected:
        bool checkRunnable() override
        {
            // 简单示例：随时可跑
            return true;
        }

    private:
        // 我们只用一个线程来循环调用 spinSome()，
        // 但是把 Reentrant 回调分发到线程池并行处理。
        std::thread         spin_thread_;

        // 你的自定义线程池，用于并行执行 Reentrant 回调
        lux::cxx::ThreadPool thread_pool_;
    };

    void CallbackGroup::notify(ISubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 加入就绪队列
        ready_list_.push_back(sub);

        // 通知 Executor
        auto exec = executor_.lock();
        if (exec)
        {
            exec->wakeup(); 
        }
    }

    std::vector<ISubscriberBase*> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 将就绪队列复制出来并清空
        std::vector<ISubscriberBase*> result;
        result.swap(ready_list_);
        return result;
    }

} // namespace lux::communication::introprocess
