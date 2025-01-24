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
            _refCount.fetch_add(1, std::memory_order_relaxed);
        }

        // Reference count -1
        void decRef()
        {
            _refCount.fetch_sub(1, std::memory_order_acq_rel);
            if (_refCount.load(std::memory_order_acquire) == 0)
            {
                onNoRef();
            }
        }

        // Current reference count
        int refCount() const
        {
            return _refCount;
        }

    protected:
        // When reference count reaches zero, let subclasses handle it
        virtual void onNoRef() = 0;

    protected:
        std::atomic<int> _refCount{0};
    };
}
