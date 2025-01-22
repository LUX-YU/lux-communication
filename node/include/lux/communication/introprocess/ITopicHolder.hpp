#pragma once

#include <string>
#include <typeindex>
#include <lux/cxx/compile_time/type_info.hpp>

namespace lux::communication::introprocess
{
    class ITopicHolder
    {
    public:
        virtual ~ITopicHolder() = default;

        // 每个 Topic 有一个唯一名称
        virtual const std::string &getTopicName() const = 0;

        // 用于区分不同类型的Topic
        virtual lux::cxx::basic_type_info getType() const = 0;

        // 引用计数 +1
        void incRef()
        {
            ++_refCount;
        }

        // 引用计数 -1
        void decRef()
        {
            if (--_refCount == 0)
            {
                onNoRef();
            }
        }

        // 当前引用计数
        int refCount() const
        {
            return _refCount;
        }

    protected:
        // 当引用计数归零时，交给子类做处理
        virtual void onNoRef() = 0;

    protected:
        int _refCount{0};
    };
}