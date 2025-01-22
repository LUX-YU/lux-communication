#pragma once
#include "RcBuffer.hpp"

#if __has_include(<moodycamel/concurrentqueue.h>)
#   include <moodycamel/concurrentqueue.h>
#   define LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#elif __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
#   include <concurrentqueue/moodycamel/concurrentqueue.h>
#   define LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#endif

#if !__has_include(<moodycamel/concurrentqueue.h>) && !__has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
#   include <lux/cxx/concurrent/BlockingQueue.hpp>
#endif

namespace lux::communication::introprocess
{
    template<typename T> using message_t = std::unique_ptr<T, RcDeleter<T>>;
#if defined(LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE)
    template<typename T> using queue_t = moodycamel::ConcurrentQueue<message_t<T>>;
    template<typename T> static inline bool try_pop(queue_t<T> &q, message_t<T>& msg) { return q.try_dequeue(msg); }
    template<typename T> static inline void push(queue_t<T> &q, message_t<T> msg) { q.enqueue(std::move(msg)); }
    template<typename T> static inline void close(queue_t<T> &q) {}
#else
    template<typename T> using queue_t = lux::cxx::concurrent::BlockingQueue<std::unique_ptr<T, RcDeleter<T>>>;
    template<typename T> static inline bool try_pop(queue_t<T>& q, message_t<T>& msg) { return q.try_pop(msg); }
    template<typename T> static inline void push(queue_t<T>& q, message_t<T> msg) { q.push(std::move(msg)); }
    template<typename T> static inline void close(queue_t<T>& q) { q.close(); }
#endif
}
