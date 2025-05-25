#pragma once
#include <functional>
#include <cstddef>
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    struct TimeExecEntry
    {
        uint64_t timestamp_ns;
        std::function<void ()> invoker;
        
        bool operator<(const TimeExecEntry &rhs) const;
    };

    class ISubscriberBase
    {
        friend class TimeOrderedExecutor;
    public:
        ISubscriberBase(int id);

        virtual ~ISubscriberBase() = default;

        virtual void takeAll() = 0;

        virtual bool setReadyIfNot() = 0;
        virtual void clearReady() = 0;

                int getId() const;

    private:
        virtual void drainAll(std::vector<TimeExecEntry>& out) = 0;
        int id_;
    };
}
