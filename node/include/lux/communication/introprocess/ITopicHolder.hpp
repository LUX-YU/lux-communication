#pragma once

#include <string>
#include <typeindex>
#include <atomic>
#include <lux/cxx/compile_time/type_info.hpp>

namespace lux::communication::introprocess
{
    class ITopicHolder
    {
    public:
        virtual ~ITopicHolder() = default;

        // Each Topic has a unique name
        virtual const std::string &getTopicName() const = 0;

        // Used to distinguish different Topic types
        virtual lux::cxx::basic_type_info getType() const = 0;

        // Reference count +1
        void incRef()
        {
            ref_count_.fetch_add(1, std::memory_order_relaxed);
        }

        // Reference count -1
        void decRef()
        {
            ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            if (ref_count_.load(std::memory_order_acquire) == 0)
            {
                onNoRef();
            }
        }

        // Current reference count
        int refCount() const
        {
            return ref_count_;
        }

    protected:
        // When reference count reaches zero, let subclasses handle it
        virtual void onNoRef() = 0;

    protected:
        std::atomic<int> ref_count_{0};
    };
}
