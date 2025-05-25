#pragma once

#include <string>
#include <typeindex>
#include <atomic>
#include <lux/cxx/compile_time/type_info.hpp>

namespace lux::communication::intraprocess
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
        void incRef();

        // Reference count -1
        void decRef();

        // Current reference count
        int refCount() const;

    protected:
        // When reference count reaches zero, let subclasses handle it
        virtual void onNoRef() = 0;

    protected:
        std::atomic<int> ref_count_{0};
    };
}
