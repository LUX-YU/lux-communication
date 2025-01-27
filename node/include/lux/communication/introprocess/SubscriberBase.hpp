#pragma once
#include <functional>
#include <cstddef>
#include <lux/cxx/compile_time/move_only_function.hpp>

namespace lux::communication::introprocess
{
    struct TimeExecEntry
    {
        uint64_t timestamp_ns;
        lux::cxx::move_only_function<void ()> invoker;
        
        bool operator<(const TimeExecEntry &rhs) const
        {
            return timestamp_ns > rhs.timestamp_ns;
        }
    };

    class ISubscriberBase
    {
        friend class TimeOrderedExecutor;
    public:
        ISubscriberBase(int id) : id_(id){}

        virtual ~ISubscriberBase() = default;

        virtual void takeAll() = 0;

        virtual bool setReadyIfNot() = 0;
        virtual void clearReady() = 0;

		int getId() const { return id_; }

    private:
        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;
        int id_;
    };
}
