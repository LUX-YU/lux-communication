#pragma once
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>
#include <memory>
#include <utility>
#include <lux/cxx/container/SparseSet.hpp>

namespace lux::communication 
{
    template<typename T>
    class Registry
    {
        struct ObjStorage {
            template<typename... Args>
            explicit ObjStorage(Args&&... args)
                : obj(std::forward<Args>(args)...), ref_cnt(0){}

            T                   obj;
            std::atomic<size_t> ref_cnt;
        };
        using StorageBlockPtr = std::unique_ptr<ObjStorage>;

    public:
        class Handle
        {
            template<typename V> friend class Registry;

        public:
            Handle() noexcept = default;

            Handle(Registry& registry, size_t idx, ObjStorage* storage) noexcept
                : reg_(&registry), idx_(idx), storage_(storage)
            {
                add_ref();
            }

            Handle(const Handle& rhs) noexcept
                : reg_(rhs.reg_), idx_(rhs.idx_), storage_(rhs.storage_)
            {
                add_ref();
            }

            Handle(Handle&& rhs) noexcept
                : reg_(rhs.reg_), idx_(rhs.idx_), storage_(rhs.storage_)
            {
                rhs.reg_ = nullptr;
                rhs.storage_ = nullptr;
                rhs.idx_ = 0;
            }

            Handle& operator=(const Handle& rhs) noexcept
            {
                if (this != &rhs) {
                    reset();
                    reg_ = rhs.reg_;
                    idx_ = rhs.idx_;
                    storage_ = rhs.storage_;
                    add_ref();
                }
                return *this;
            }

            Handle& operator=(Handle&& rhs) noexcept
            {
                if (this != &rhs) {
                    reset();
                    reg_ = rhs.reg_;
                    idx_ = rhs.idx_;
                    storage_ = rhs.storage_;
                    rhs.reg_ = nullptr;
                    rhs.storage_ = nullptr;
                    rhs.idx_ = 0;
                }
                return *this;
            }

            ~Handle() { reset(); }

            T* operator->()       noexcept { return &storage_->obj; }
            const T* operator->() const noexcept { return &storage_->obj; }

            T& operator* ()       noexcept { return storage_->obj; }
            const T& operator* () const noexcept { return storage_->obj; }

            explicit operator bool() const noexcept { return storage_ != nullptr; }

            void reset() noexcept
            {
                if (storage_) {
                    reg_->dec_ref(idx_, storage_);
                    reg_ = nullptr;
                    storage_ = nullptr;
                    idx_ = 0;
                }
            }

            size_t index() const noexcept { return idx_; }

        private:
            void add_ref() noexcept
            {
                if (storage_) storage_->ref_cnt.fetch_add(1, std::memory_order_relaxed);
            }

            Registry<T>*          reg_{ nullptr };
            size_t                idx_{ 0 };
            ObjStorage*           storage_{ nullptr };
        };

        template<typename... Args>
        Handle emplace(Args&&... args)
        {
            std::scoped_lock lk(mtx_);
            size_t idx = obj_storage_.emplace(
                std::make_unique<ObjStorage>(std::forward<Args>(args)...)
            );
            ObjStorage* st = obj_storage_.at(idx).get();
            return Handle(*this, idx, st);
        }

        template<typename U>
        Handle insert(U&& obj)
        {
            std::scoped_lock lk(mtx_);
            size_t idx = obj_storage_.emplace(
                std::make_unique<ObjStorage>(std::forward<U>(obj))
            );
            ObjStorage* st = obj_storage_.at(idx).get();
            return Handle(*this, idx, st);
        }

        template<typename Func>
        void foreach(Func&& func)
        {
            std::scoped_lock lk(mtx_);
            for (const auto& storage : obj_storage_.values())
            {
                func(storage->obj);
            }
        }

    private:
        void dec_ref(size_t idx, ObjStorage* st) noexcept
        {
            if (st->ref_cnt.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::scoped_lock lk(mtx_);
                if (st->ref_cnt.load(std::memory_order_relaxed) == 0) {
                    obj_storage_.erase(idx);
                }
            }
        }

    private:

        std::mutex                               mtx_;
        lux::cxx::AutoSparseSet<StorageBlockPtr> obj_storage_;
    };

    template<typename T>
    class QueryableRegistry
    {
        struct ObjStorage {
            template<typename... Args>
            explicit ObjStorage(std::string n, Args&&... args)
                : obj(std::forward<Args>(args)...), ref_cnt(0), name(std::move(n)) {
            }

            T                   obj;
            std::atomic<size_t> ref_cnt;
            std::string         name;
        };
        using StorageBlockPtr = std::unique_ptr<ObjStorage>;

    public:
        class Handle
        {
            template<typename V> friend class QueryableRegistry;

        public:
            Handle() noexcept = default;

            Handle(QueryableRegistry& registry, size_t idx, ObjStorage* storage) noexcept
                : reg_(&registry), idx_(idx), storage_(storage)
            {
                add_ref();
            }

            Handle(const Handle& rhs) noexcept
                : reg_(rhs.reg_), idx_(rhs.idx_), storage_(rhs.storage_)
            {
                add_ref();
            }

            Handle(Handle&& rhs) noexcept
                : reg_(rhs.reg_), idx_(rhs.idx_), storage_(rhs.storage_)
            {
                rhs.reg_ = nullptr;
                rhs.storage_ = nullptr;
                rhs.idx_ = 0;
            }

            Handle& operator=(const Handle& rhs) noexcept
            {
                if (this != &rhs) {
                    reset();
                    reg_ = rhs.reg_;
                    idx_ = rhs.idx_;
                    storage_ = rhs.storage_;
                    add_ref();
                }
                return *this;
            }

            Handle& operator=(Handle&& rhs) noexcept
            {
                if (this != &rhs) {
                    reset();
                    reg_ = rhs.reg_;
                    idx_ = rhs.idx_;
                    storage_ = rhs.storage_;
                    rhs.reg_ = nullptr;
                    rhs.storage_ = nullptr;
                    rhs.idx_ = 0;
                }
                return *this;
            }

            ~Handle() { reset(); }

            T* operator->()       noexcept { return &storage_->obj; }
            const T* operator->() const noexcept { return &storage_->obj; }

            T& operator* ()       noexcept { return storage_->obj; }
            const T& operator* () const noexcept { return storage_->obj; }

            explicit operator bool() const noexcept { return storage_ != nullptr; }

            void reset() noexcept
            {
                if (storage_) {
                    reg_->dec_ref(idx_, storage_);
                    reg_ = nullptr;
                    storage_ = nullptr;
                    idx_ = 0;
                }
            }

            size_t index() const noexcept { return idx_; }

        private:
            void add_ref() noexcept
            {
                if (storage_) storage_->ref_cnt.fetch_add(1, std::memory_order_relaxed);
            }

            QueryableRegistry<T>* reg_{ nullptr };
            size_t                idx_{ 0 };
            ObjStorage*           storage_{ nullptr };
        };

        template<typename... Args>
        Handle emplace(const std::string& name, Args&&... args)
        {
            std::scoped_lock lk(mtx_);

            auto it = obj_map_.find(name);
            if (it != obj_map_.end()) 
            {
                size_t idx = it->second;
                ObjStorage* st = obj_storage_.at(idx).get();
                return Handle(*this, idx, st);
            }

            size_t idx = obj_storage_.emplace(
                std::make_unique<ObjStorage>(name, std::forward<Args>(args)...)
            );
            obj_map_[name] = idx;
            ObjStorage* st = obj_storage_.at(idx).get();
            return Handle(*this, idx, st);
        }

        bool contains(const std::string& name)
        {
            std::scoped_lock lk(mtx_);
            return obj_map_.contains(name);
        }

        Handle at(const std::string& name)
        {
            std::scoped_lock lk(mtx_);
            if (obj_map_.contains(name))
            {
                auto idx = obj_map_[name];
                ObjStorage* st = obj_storage_.at(idx).get();
                return Handle{*this, idx, st};
            }

            return Handle{};
        }

        template<typename Func>
        void foreach(Func&& func)
        {
            std::scoped_lock lk(mtx_);
            for (const auto& storage : obj_storage_.values())
            {
                func(storage->obj);
            }
        }

    private:
        void dec_ref(size_t idx, ObjStorage* st) noexcept
        {
            if (st->ref_cnt.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::scoped_lock lk(mtx_);
                if (st->ref_cnt.load(std::memory_order_relaxed) == 0) {
                    obj_map_.erase(st->name);
                    obj_storage_.erase(idx);
                }
            }
        }

        std::mutex                                  mtx_;
        lux::cxx::AutoSparseSet<StorageBlockPtr>    obj_storage_;
        std::unordered_map<std::string, size_t>     obj_map_;
    };
} // namespace lux::communication
