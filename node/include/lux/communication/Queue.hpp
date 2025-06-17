#pragma once
#include <memory>

#if __has_include(<moodycamel/concurrentqueue.h>)
#   include <moodycamel/concurrentqueue.h>
#   define LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#elif __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>)
#   include <concurrentqueue/moodycamel/concurrentqueue.h>
#   define LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#elif __has_include(<concurrentqueue/concurrentqueue.h>)
#   include <concurrentqueue/concurrentqueue.h>
#   define LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#endif

#ifndef LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE
#   include <lux/cxx/concurrent/BlockingQueue.hpp>
#endif

namespace lux::communication
{
    template<typename T> using message_t = std::shared_ptr<T>;
#if defined(LUX_HAS_MOODYCAMEL_CONCURRENTQUEUE)
    template<typename T> using queue_t = moodycamel::ConcurrentQueue<message_t<T>>;
    template<typename T> inline bool try_pop(queue_t<T> &q, message_t<T>& msg) { return q.try_dequeue(msg); }
    template<typename T> inline bool try_dequeue(queue_t<T>& q, message_t<T>& msg) { return q.try_dequeue(msg); }
    template<typename T, typename InputIt> inline std::size_t push_bulk(queue_t<T>& q,InputIt first, std::size_t count){ return q.enqueue_bulk(first, count) ? count : 0;}
    template<typename T, typename OutputIt>inline std::size_t pop_bulk(queue_t<T>& q, OutputIt dest, std::size_t max_count){ return q.try_dequeue_bulk(dest, max_count);}
    template<typename T> inline void push(queue_t<T> &q, message_t<T> msg) { q.enqueue(std::move(msg)); }
    template<typename T> inline void close(queue_t<T> &q) {}
#else
    template<typename T> using queue_t = lux::cxx::BlockingQueue<message_t<T>>;
    template<typename T> inline bool try_pop(queue_t<T>& q, message_t<T>& msg) { return q.try_pop(msg); }
    template<typename T> inline bool try_dequeue(queue_t<T>& q, message_t<T>& msg) { return q.try_pop(msg); }
    template<typename T, typename InputIt> inline std::size_t push_bulk(queue_t<T>& q, InputIt first, std::size_t count){ return q.try_push_bulk(first, count);}
    template<typename T, typename OutputIt>inline std::size_t pop_bulk(queue_t<T>& q, OutputIt dest, std::size_t max_count){ return q.try_pop_bulk(dest, max_count);}
    template<typename T> inline void push(queue_t<T>& q, message_t<T> msg) { q.push(std::move(msg)); }
    template<typename T> inline void close(queue_t<T>& q) { q.close(); }
#endif
}
