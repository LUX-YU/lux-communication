#pragma once

namespace lux::communication::introprocess
{
    class ISubscriberBase
    {
    public:
        virtual ~ISubscriberBase() = default;

        virtual void takeAll() = 0;

        virtual bool setReadyIfNot() = 0;
        virtual void clearReady() = 0;
    };
}
