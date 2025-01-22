#pragma once

#include <vector>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <memory>    // for std::unique_ptr
#include "ITopicHolder.hpp"
#include "RcBuffer.hpp"

namespace lux::communication::introprocess
{
    // 前置声明
    class Domain;
    template <typename T>
    class Subscriber;

    //-------------------------------------------
    // 1) 一个不可变的 SubscriberList + 引用计数
    //-------------------------------------------
    namespace detail
    {
        template <typename T>
        struct SubscriberList
        {
            // 引用计数
            std::atomic<int> refCount{1};

            // 实际的订阅者数组
            std::vector<Subscriber<T>*> subs;

            // 默认构造
            SubscriberList() = default;

            // 拷贝构造 (复制 subs，但 refCount 为 1)
            SubscriberList(const SubscriberList &other)
                : subs(other.subs)
            {
                refCount.store(1, std::memory_order_relaxed);
            }
        };

        // 帮助函数：增加引用
        template <typename T>
        inline void incRef(SubscriberList<T>* p)
        {
            if (p) {
                p->refCount.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // 帮助函数：减少引用，若减到 0 则 delete
        template <typename T>
        inline void decRef(SubscriberList<T>* p)
        {
            if (p) {
                int old = p->refCount.fetch_sub(1, std::memory_order_acq_rel);
                if (old == 1) {
                    delete p;
                }
            }
        }
    } // namespace detail

    /**
     * @brief 用于存放某个特定类型T的所有订阅者列表，并分发零拷贝消息
     *        采用 Copy-On-Write 方式管理订阅者数组
     */
    template <typename T>
    class Topic : public ITopicHolder
    {
    public:
        static constexpr auto type_info = lux::cxx::make_basic_type_info<T>();

        Topic(const std::string &name, Domain *owner)
            : name_(name), domain_(owner)
        {
            // refCount 初始为0，需要外部 incRef() 才能使用
            // 初始化一个空列表
            using ListType = detail::SubscriberList<T>;
            auto emptyList = new ListType();
            subs_.store(emptyList, std::memory_order_release);
        }

        ~Topic() override
        {
            // 析构时，释放当前 subs_ 指向的列表
            auto ptr = subs_.load(std::memory_order_acquire);
            detail::decRef(ptr);
        }

        // ITopicHolder 接口: topic 名称
        const std::string &getTopicName() const override
        {
            return name_;
        }

        // ITopicHolder 接口: topic 类型
        lux::cxx::basic_type_info getType() const override
        {
            return type_info;
        }

        /**
         * @brief 注册订阅者
         */
        void addSubscriber(Subscriber<T> *sub)
        {
            while (true) {
                auto oldPtr = subs_.load(std::memory_order_acquire);
                detail::incRef(oldPtr);

                // 复制一份
                auto newPtr = new detail::SubscriberList<T>(*oldPtr);
                // 在副本上添加
                newPtr->subs.push_back(sub);

                // 这里是 CAS
                if (subs_.compare_exchange_weak(oldPtr, newPtr,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    detail::decRef(oldPtr);
                    break; // success
                }

                // CAS 失败，说明有并发写，把 newPtr 删除
                delete newPtr;
                detail::decRef(oldPtr);
            }
        }

        /**
         * @brief 注销订阅者
         */
        void removeSubscriber(Subscriber<T> *sub)
        {
            while (true) {
                auto oldPtr = subs_.load(std::memory_order_acquire);
                detail::incRef(oldPtr);

                // 复制一份
                auto newPtr = new detail::SubscriberList<T>(*oldPtr);

                // 移除 sub (swap-and-pop)
                auto &vec = newPtr->subs;
                auto it = std::find(vec.begin(), vec.end(), sub);
                if (it != vec.end()) {
                    *it = vec.back();
                    vec.pop_back();
                }

                if (subs_.compare_exchange_weak(oldPtr, newPtr,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    // 成功了才 break
                    detail::decRef(oldPtr);
                    break;
                }
        
                // CAS 失败，说明有并发写，需要重试
                delete newPtr;
                detail::decRef(oldPtr);
            }
        }

        /**
         * @brief 分发消息 (零拷贝)
         *        只需要原子加载 subs_ 并遍历，无需加锁
         */
        void publish(std::unique_ptr<T, RcDeleter<T>> msg)
        {
            auto listPtr = subs_.load(std::memory_order_acquire);
            detail::incRef(listPtr);  // 防止并发 remove/替换导致 listPtr 被释放

            for (auto* sub : listPtr->subs)
            {
                if (sub)
                {
                    auto clonedPtr = rcAttach(msg.get_deleter().rcBuf);
                    sub->enqueue(std::move(clonedPtr));
                }
            }

            detail::decRef(listPtr);  // 遍历完释放
            // msg 出作用域 refCount -1
        }

    protected:
        /**
         * @brief 当引用计数归零时，由 Domain 移除
         *        需要 Domain 在 removeTopic(this) 里把这个 Topic 的资源清理掉
         */
        void onNoRef() override;

    private:
        std::string                name_;
        Domain*                    domain_;

        // 原子指针，指向不可变的 "SubscriberList"
        std::atomic<detail::SubscriberList<T>*> subs_;
    };
} // namespace lux::communication::introprocess