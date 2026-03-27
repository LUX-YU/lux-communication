#pragma once
/// Unified Subscriber<T> — receives from intra / SHM / network channels.
///
/// - Intra: called by Topic<T>::publish() → enqueue()  (same-process, zero-copy)
/// - SHM:   polled by IoThread → pollShmReaders() → deserialize → enqueue()
/// - Net:   IoReactor callback → deserialize → enqueue()
///
/// All paths converge into one ordered_queue_t, fed through the standard
/// CallbackGroup → Executor pipeline.

#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>

#include <lux/communication/Queue.hpp>
#include <lux/communication/MessageTraits.hpp>
#include <lux/communication/SubscriberBase.hpp>
#include <lux/communication/SubscribeOptions.hpp>
#include <lux/communication/ChannelKind.hpp>
#include <lux/communication/TransportSelector.hpp>
#include <lux/communication/CallbackGroupBase.hpp>
#include <lux/communication/Hash.hpp>
#include <lux/communication/intraprocess/Topic.hpp>

#include <lux/communication/serialization/Serializer.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/ShmRingReader.hpp>
#include <lux/communication/transport/ShmDataPool.hpp>
#include <lux/communication/transport/IoReactor.hpp>
#include <lux/communication/transport/UdpTransportReader.hpp>
#include <lux/communication/transport/TcpTransportReader.hpp>
#include <lux/communication/discovery/DiscoveryService.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.st.h>

// Conditional lockfree queue detection.
// Primary gate: CMake __MACRO_USE_LOCKFREE_QUEUE__; fallback: __has_include.
#if defined(__MACRO_USE_LOCKFREE_QUEUE__) || __has_include(<moodycamel/concurrentqueue.h>) || \
    __has_include(<concurrentqueue/moodycamel/concurrentqueue.h>) || \
    __has_include(<concurrentqueue/concurrentqueue.h>)
#   define LUX_UNIFIED_SUBSCRIBER_USE_LOCKFREE_QUEUE
#endif

namespace lux::communication { class Node; }

namespace lux::communication {

/// Unified Subscriber.
template<typename T>
class Subscriber : public SubscriberBase
{
    using TopicT = intraprocess::Topic<T>;
    using Ser    = serialization::Serializer<T>;

    static TopicSptr getOrCreateTopic(Node* node, const std::string& topic_name);

    static CallbackGroupBase* resolveCallbackGroup(CallbackGroupBase* cbg, Node* node);

    // ── Ordered item ──
    struct OrderedItem {
        uint64_t seq;
        uint64_t timestamp_ns;   // publish-time (for lifespan checking)
        stored_msg_t<T> msg;
    };

#ifdef LUX_UNIFIED_SUBSCRIBER_USE_LOCKFREE_QUEUE
    using ordered_queue_t = moodycamel::ConcurrentQueue<OrderedItem>;
    static bool   try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_dequeue(out); }
    static void   push_item(ordered_queue_t& q, OrderedItem v)      { q.enqueue(std::move(v)); }
    static void   close_queue(ordered_queue_t&)                      {}
    static size_t queue_size_approx(ordered_queue_t& q)              { return q.size_approx(); }

    static size_t try_pop_bulk(ordered_queue_t& q, OrderedItem* items, size_t max_count) {
        return q.try_dequeue_bulk(items, max_count);
    }
#else
    using ordered_queue_t = lux::cxx::BlockingQueue<OrderedItem>;
    static bool   try_pop_item(ordered_queue_t& q, OrderedItem& out) { return q.try_pop(out); }
    static void   push_item(ordered_queue_t& q, OrderedItem v)      { q.push(std::move(v)); }
    static void   close_queue(ordered_queue_t& q)                    { q.close(); }
    static size_t queue_size_approx(ordered_queue_t& q)              { return q.size(); }

    static size_t try_pop_bulk(ordered_queue_t& q, OrderedItem* items, size_t max_count) {
        size_t count = 0;
        while (count < max_count && q.try_pop(items[count])) ++count;
        return count;
    }
#endif

public:
    using Callback = std::function<void(callback_arg_t<T>)>;
    using ContentFilter = std::function<bool(const T&)>;

    template<typename Func>
    Subscriber(const std::string& topic_name, Node* node, Func&& func,
               CallbackGroupBase* cbg = nullptr,
               const SubscribeOptions& opts = {},
               ContentFilter filter = nullptr);

    ~Subscriber() override;

    Subscriber(const Subscriber&)            = delete;
    Subscriber& operator=(const Subscriber&) = delete;
    Subscriber(Subscriber&&)                 = delete;
    Subscriber& operator=(Subscriber&&)      = delete;

    /// Stop the subscriber (unregister discovery, SHM pollers, etc.).
    void stop();

    // ── Intra path entry point (called by Topic<T>::publish) ──
    void enqueue(uint64_t seq, stored_msg_t<T> msg);

    // ── SubscriberBase interface (called by Executors) ──
    void   takeAll() override;
    void   drainAll(std::vector<TimeExecEntry>& out) override;
    void   drainAllExec(std::vector<ExecEntry>& out) override;
    size_t drainExecSome(std::vector<ExecEntry>& out, size_t max_count) override;

    /// Poll all SHM readers.  Called by IoThread.
    void pollShmReaders();

    const std::string& topicName() const { return topic_name_; }

private:
    void cleanup();

    // ── Discovery callbacks ──
    void onPeerDiscovered(const discovery::TopicEndpoint& ep);
    void onPeerLost(const discovery::TopicEndpoint& ep);

    // ── SHM reader management ──
    struct ShmPeer {
        uint32_t pub_pid;
        std::string shm_name;
        std::unique_ptr<transport::ShmRingReader> reader;
    };

    // ── Net reader management ──
    struct NetPeer {
        std::string endpoint;
        std::unique_ptr<transport::UdpTransportReader> udp;
        std::unique_ptr<transport::TcpTransportReader> tcp;
    };

    void processReadView(ShmPeer& entry);
    void ensurePool(const ShmPeer& entry);

    /// Process a received network frame (TCP or UDP).
    void processNetFrame(const transport::FrameHeader& hdr,
                         const void* payload, uint32_t payload_size);

    /// Unregister all net peer fds from the IoReactor.
    void unregisterNetFds();

    static void invokeTrampoline(void* obj, std::shared_ptr<void> msg);

    // ── Members ──
    std::string       topic_name_;
    Node*             node_;
    Callback          callback_func_;
    ContentFilter     content_filter_;
    SubscribeOptions  opts_;
    uint64_t          topic_hash_;
    uint64_t          discovery_handle_ = 0;
    uint64_t          listener_id_     = 0;
    uint64_t          io_poll_handle_  = 0;

    std::mutex              shm_mutex_;
    std::vector<ShmPeer>    shm_peers_;

    std::mutex              net_mutex_;
    std::vector<NetPeer>    net_peers_;

    ordered_queue_t         queue_;
    std::atomic<bool>       stopped_{false};

    std::unique_ptr<transport::ShmDataPool> data_pool_;

    // ── Net: UDP fragment GC poller handle ──
    uint64_t                    udp_gc_handle_ = 0;

    // ── QoS: Deadline detection ──
    std::atomic<std::chrono::steady_clock::time_point> last_message_time_{
        std::chrono::steady_clock::now()};
    std::atomic<bool>           deadline_fired_{false};
    uint64_t                    deadline_poll_handle_ = 0;
    std::function<void()>       deadline_missed_cb_;

    // ── QoS helpers ──
    bool shouldDiscard(const OrderedItem& item) const;
    void checkDeadline();
};

} // namespace lux::communication

// ═══════════════════════════════════════════════════════════════════
//  Template implementation
// ═══════════════════════════════════════════════════════════════════
#include "Node.hpp"
#include <lux/communication/Domain.hpp>
#include <lux/communication/IoThread.hpp>

namespace lux::communication {

// ── Helpers ──────────────────────────────────────────────────────

template<typename T>
TopicSptr Subscriber<T>::getOrCreateTopic(Node* node, const std::string& topic_name)
{
    return node->domain().template createOrGetTopic<TopicT, T>(topic_name);
}

template<typename T>
CallbackGroupBase* Subscriber<T>::resolveCallbackGroup(CallbackGroupBase* cbg, Node* node)
{
    return cbg ? cbg : node->defaultCallbackGroup();
}

// ── Constructor ──────────────────────────────────────────────────

template<typename T>
template<typename Func>
Subscriber<T>::Subscriber(const std::string& topic_name, Node* node,
                           Func&& func, CallbackGroupBase* cbg,
                           const SubscribeOptions& opts,
                           ContentFilter filter)
    : SubscriberBase(getOrCreateTopic(node, topic_name),
                     node,
                     resolveCallbackGroup(cbg, node))
    , topic_name_(topic_name)
    , node_(node)
    , callback_func_(std::forward<Func>(func))
    , content_filter_(std::move(filter))
    , opts_(opts)
    , topic_hash_(fnv1a_64(topic_name))
    , deadline_missed_cb_(opts.on_deadline_missed)
{
    const auto& nopts = node_->options();

    // ── Discovery (for SHM / Net peers) ──
    if (nopts.enable_discovery &&
        opts_.transport_hint != SubscribeTransportHint::IntraOnly)
    {
        auto& ds = discovery::DiscoveryService::getInstance(node_->domain().id());

        discovery_handle_ = ds.announceSubscriber(
            topic_name_, typeid(T).name(), typeid(T).hash_code());

        listener_id_ = ds.addListener(topic_name_,
            [this](const discovery::DiscoveryEvent& ev) {
                if (ev.endpoint.role != discovery::TopicEndpoint::Role::Publisher)
                    return;
                if (ev.type == discovery::DiscoveryEventType::EndpointDiscovered)
                    onPeerDiscovered(ev.endpoint);
                else
                    onPeerLost(ev.endpoint);
            });

        // Connect to already-known publishers.
        auto existing = ds.lookup(topic_name_,
                                  discovery::TopicEndpoint::Role::Publisher);
        for (auto& ep : existing)
            onPeerDiscovered(ep);
    }

    // ── Register SHM poller with IoThread ──
    if (nopts.enable_shm &&
        opts_.transport_hint != SubscribeTransportHint::IntraOnly &&
        opts_.transport_hint != SubscribeTransportHint::NetOnly)
    {
        io_poll_handle_ = node_->ioThread().registerPoller(
            [this]() { pollShmReaders(); });
    }

    // ── Register deadline checker (Phase 6 QoS) ──
    if (opts_.qos.deadline.count() > 0 && deadline_missed_cb_)
    {
        deadline_poll_handle_ = node_->ioThread().registerPoller(
            [this]() { checkDeadline(); });
    }
}

// ── Destructor ───────────────────────────────────────────────────

template<typename T>
Subscriber<T>::~Subscriber()
{
    cleanup();
}

template<typename T>
void Subscriber<T>::stop()
{
    cleanup();
}

template<typename T>
void Subscriber<T>::cleanup()
{
    if (stopped_.exchange(true))
        return; // already stopped

    // Unregister SHM poller.
    if (io_poll_handle_)
    {
        node_->ioThread().unregisterPoller(io_poll_handle_);
        io_poll_handle_ = 0;
    }

    // Unregister deadline poller.
    if (deadline_poll_handle_)
    {
        node_->ioThread().unregisterPoller(deadline_poll_handle_);
        deadline_poll_handle_ = 0;
    }

    // Unregister UDP GC poller.
    if (udp_gc_handle_)
    {
        node_->ioThread().unregisterPoller(udp_gc_handle_);
        udp_gc_handle_ = 0;
    }

    // Unregister net peer fds from IoReactor.
    unregisterNetFds();

    // Unregister from DiscoveryService.
    const auto& nopts = node_->options();
    if (nopts.enable_discovery)
    {
        auto& ds = discovery::DiscoveryService::getInstance(node_->domain().id());
        if (listener_id_) { ds.removeListener(listener_id_); listener_id_ = 0; }
        if (discovery_handle_) { ds.withdraw(discovery_handle_); discovery_handle_ = 0; }
    }

    close_queue(queue_);
    clearReady();
}

// ── Discovery callbacks ──────────────────────────────────────────

template<typename T>
void Subscriber<T>::onPeerDiscovered(const discovery::TopicEndpoint& ep)
{
    ChannelKind kind = selectTransport(ep, node_->options());

    switch (kind) {
    case ChannelKind::Intra:
        // Same-process publisher distributes via Topic snapshot.
        break;

    case ChannelKind::Shm: {
        if (ep.shm_segment_name.empty())
            return; // ring not yet created

        std::lock_guard lock(shm_mutex_);
        for (const auto& p : shm_peers_)
            if (p.pub_pid == ep.pid) return;

        try {
            auto reader = std::make_unique<transport::ShmRingReader>(ep.shm_segment_name);
            shm_peers_.push_back(ShmPeer{ep.pid, ep.shm_segment_name, std::move(reader)});
        } catch (const std::exception&) {
            // Retry on next announce.
        }
        break;
    }

    case ChannelKind::Net: {
        if (ep.net_endpoint.empty())
            return;

        std::lock_guard lock(net_mutex_);
        for (const auto& p : net_peers_)
            if (p.endpoint == ep.net_endpoint) return;

        try {
            // Parse endpoint "addr:port"
            auto colon = ep.net_endpoint.rfind(':');
            if (colon == std::string::npos) break;
            std::string addr = ep.net_endpoint.substr(0, colon);
            uint16_t port = static_cast<uint16_t>(
                std::stoi(ep.net_endpoint.substr(colon + 1)));

            auto udp = std::make_unique<transport::UdpTransportReader>();
            auto tcp = std::make_unique<transport::TcpTransportReader>(
                addr, port, topic_hash_, typeid(T).hash_code(),
                platform::currentPid(), platform::currentHostname());

            // ── Register TCP fd with IoReactor for event-driven recv ──
            if (tcp->connect()) {
                auto* tcp_raw = tcp.get();
                node_->reactor().addFd(
                    tcp_raw->nativeFd(),
                    transport::IoReactor::Readable,
                    [this, tcp_raw](platform::socket_t, uint8_t events) {
                        if (events & transport::IoReactor::Error) return;
                        tcp_raw->onDataReady(
                            [this](const transport::FrameHeader& hdr,
                                   const void* payload, uint32_t sz) {
                                processNetFrame(hdr, payload, sz);
                            });
                    });
            }

            // ── Register UDP fd with IoReactor for event-driven recv ──
            auto* udp_raw = udp.get();
            node_->reactor().addFd(
                udp_raw->nativeFd(),
                transport::IoReactor::Readable,
                [this, udp_raw](platform::socket_t, uint8_t events) {
                    if (events & transport::IoReactor::Error) return;
                    udp_raw->pollOnce(
                        [this](const transport::FrameHeader& hdr,
                               const void* payload, uint32_t sz) {
                            processNetFrame(hdr, payload, sz);
                        });
                });

            // ── Register periodic UDP fragment GC if not already done ──
            if (udp_gc_handle_ == 0) {
                udp_gc_handle_ = node_->ioThread().registerPoller(
                    [this]() {
                        std::lock_guard lk(net_mutex_);
                        for (auto& p : net_peers_) {
                            if (p.udp) p.udp->gc();
                        }
                    });
            }

            net_peers_.push_back(NetPeer{ep.net_endpoint, std::move(udp), std::move(tcp)});
        } catch (const std::exception&) {
            // Retry on next announce.
        }
        break;
    }
    }
}

template<typename T>
void Subscriber<T>::onPeerLost(const discovery::TopicEndpoint& ep)
{
    ChannelKind kind = selectTransport(ep, node_->options());

    switch (kind) {
    case ChannelKind::Intra:
        break;
    case ChannelKind::Shm: {
        std::lock_guard lock(shm_mutex_);
        std::erase_if(shm_peers_, [&](const ShmPeer& p) { return p.pub_pid == ep.pid; });
        break;
    }
    case ChannelKind::Net: {
        std::lock_guard lock(net_mutex_);
        std::erase_if(net_peers_, [&](const NetPeer& p) {
            if (p.endpoint != ep.net_endpoint) return false;
            if (p.tcp && p.tcp->isConnected())
                node_->reactor().removeFd(p.tcp->nativeFd());
            if (p.udp && p.udp->isValid())
                node_->reactor().removeFd(p.udp->nativeFd());
            return true;
        });
        break;
    }
    }
}

// ── Intra path (called synchronously by Topic<T>::publish) ───────

template<typename T>
void Subscriber<T>::enqueue(uint64_t seq, stored_msg_t<T> msg)
{
    // Content filter — reject before enqueue to avoid Executor overhead.
    if constexpr (SmallValueMsg<T>) {
        if (content_filter_ && !content_filter_(msg)) return;
    } else {
        if (content_filter_ && !content_filter_(*msg)) return;
    }

    // Lazy timestamp: only call steadyNowNs() when lifespan QoS is active.
    const uint64_t ts = (opts_.qos.lifespan.count() > 0)
                      ? platform::steadyNowNs()
                      : 0;
    push_item(queue_, OrderedItem{seq, ts, std::move(msg)});

    // KeepLast depth enforcement (approximate — ConcurrentQueue size is best-effort).
    if (opts_.qos.history == History::KeepLast && opts_.qos.depth > 0)
    {
        while (queue_size_approx(queue_) > opts_.qos.depth)
        {
            OrderedItem discard;
            if (!try_pop_item(queue_, discard)) break;
        }
    }

    // Deadline tracking — reset timer on every received message.
    if (opts_.qos.deadline.count() > 0)
    {
        last_message_time_.store(std::chrono::steady_clock::now(),
                                 std::memory_order_relaxed);
        deadline_fired_.store(false, std::memory_order_relaxed);
    }

    callbackGroup()->notify(this);
}

// ── SHM poll (called by IoThread) ────────────────────────────────

template<typename T>
void Subscriber<T>::pollShmReaders()
{
    std::lock_guard lock(shm_mutex_);
    for (auto& entry : shm_peers_)
    {
        using namespace std::chrono;
        auto view = entry.reader->acquireReadView(microseconds{0});
        if (!view.data) continue;

        processReadView(entry);
    }
}

template<typename T>
void Subscriber<T>::processReadView(ShmPeer& entry)
{
    if constexpr (!serialization::HasSerializer<T>) return;
    else {
    // May be called repeatedly while data is available.
    for (;;)
    {
        auto view = entry.reader->acquireReadView(std::chrono::microseconds{0});
        if (!view.data) break;

        if (view.size < sizeof(transport::FrameHeader)) {
            entry.reader->releaseReadView();
            continue;
        }

        auto* hdr = static_cast<const transport::FrameHeader*>(view.data);
        if (!transport::isValidFrame(*hdr)) {
            entry.reader->releaseReadView();
            continue;
        }

        // ── Pool path ──
        if (transport::isPooled(*hdr)) {
            if (hdr->payload_size < sizeof(transport::PoolDescriptor)) {
                entry.reader->releaseReadView();
                continue;
            }
            const auto* desc = reinterpret_cast<const transport::PoolDescriptor*>(
                static_cast<const char*>(view.data) + sizeof(transport::FrameHeader));

            ensurePool(entry);
            if (!data_pool_) {
                entry.reader->releaseReadView();
                continue;
            }

            const void* pool_data = data_pool_->read(desc->pool_offset);
            if (!pool_data) {
                data_pool_->release(desc->ref_count_offset);
                entry.reader->releaseReadView();
                continue;
            }

            stored_msg_t<T> msg_storage{};
            T* raw_ptr;
            if constexpr (SmallValueMsg<T>) {
                raw_ptr = &msg_storage;
            } else {
                msg_storage = std::make_shared<T>();
                raw_ptr = msg_storage.get();
            }
            bool ok = Ser::deserialize(*raw_ptr, pool_data, desc->data_size);
            data_pool_->release(desc->ref_count_offset);
            entry.reader->releaseReadView();

            if (ok) {
                // Content filter (Phase 6).
                if (content_filter_ && !content_filter_(*raw_ptr))
                    continue;

                uint64_t msg_seq = hdr->seq_num;
                if constexpr (is_msg_stamped<T>) {
                    msg_seq = builtin_msgs::common_msgs::extract_timstamp(*raw_ptr);
                }
                push_item(queue_, OrderedItem{msg_seq, hdr->timestamp_ns, std::move(msg_storage)});

                // KeepLast depth enforcement.
                if (opts_.qos.history == History::KeepLast && opts_.qos.depth > 0)
                {
                    while (queue_size_approx(queue_) > opts_.qos.depth)
                    {
                        OrderedItem discard;
                        if (!try_pop_item(queue_, discard)) break;
                    }
                }

                // Deadline tracking.
                if (opts_.qos.deadline.count() > 0)
                {
                    last_message_time_.store(std::chrono::steady_clock::now(),
                                             std::memory_order_relaxed);
                    deadline_fired_.store(false, std::memory_order_relaxed);
                }

                callbackGroup()->notify(this);
            }
            continue;
        }

        // ── Inline path ──
        const char* payload =
            static_cast<const char*>(view.data) + sizeof(transport::FrameHeader);
        stored_msg_t<T> msg_storage{};
        T* raw_ptr;
        if constexpr (SmallValueMsg<T>) {
            raw_ptr = &msg_storage;
        } else {
            msg_storage = std::make_shared<T>();
            raw_ptr = msg_storage.get();
        }
        if (Ser::deserialize(*raw_ptr, payload, hdr->payload_size)) {
            // Content filter (Phase 6).
            if (content_filter_ && !content_filter_(*raw_ptr)) {
                entry.reader->releaseReadView();
                continue;
            }

            uint64_t msg_seq = hdr->seq_num;
            if constexpr (is_msg_stamped<T>) {
                msg_seq = builtin_msgs::common_msgs::extract_timstamp(*raw_ptr);
            }
            push_item(queue_, OrderedItem{msg_seq, hdr->timestamp_ns, std::move(msg_storage)});

            // KeepLast depth enforcement.
            if (opts_.qos.history == History::KeepLast && opts_.qos.depth > 0)
            {
                while (queue_size_approx(queue_) > opts_.qos.depth)
                {
                    OrderedItem discard;
                    if (!try_pop_item(queue_, discard)) break;
                }
            }

            // Deadline tracking.
            if (opts_.qos.deadline.count() > 0)
            {
                last_message_time_.store(std::chrono::steady_clock::now(),
                                         std::memory_order_relaxed);
                deadline_fired_.store(false, std::memory_order_relaxed);
            }

            callbackGroup()->notify(this);
        }
        entry.reader->releaseReadView();
    }
    } // else (HasSerializer<T>)
}

template<typename T>
void Subscriber<T>::ensurePool(const ShmPeer& entry)
{
    if (data_pool_) return;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "lux_pool_%llu_%08llx_%u",
                  static_cast<unsigned long long>(node_->domain().id()),
                  static_cast<unsigned long long>(topic_hash_),
                  entry.pub_pid);
    try {
        data_pool_ = transport::ShmDataPool::openExisting(std::string(buf));
    } catch (const std::exception&) { /* retry later */ }
}

// ── Event-driven network receive ─────────────────────────────────

template<typename T>
void Subscriber<T>::processNetFrame(const transport::FrameHeader& hdr,
                                     const void* payload, uint32_t payload_size)
{
    if constexpr (!serialization::HasSerializer<T>) return;
    else {
    if (!transport::isValidFrame(hdr)) return;

    stored_msg_t<T> msg_storage{};
    T* raw_ptr;
    if constexpr (SmallValueMsg<T>) {
        raw_ptr = &msg_storage;
    } else {
        msg_storage = std::make_shared<T>();
        raw_ptr = msg_storage.get();
    }
    if (!Ser::deserialize(*raw_ptr, payload, payload_size)) return;

    // Content filter.
    if (content_filter_ && !content_filter_(*raw_ptr)) return;

    uint64_t msg_seq = hdr.seq_num;
    if constexpr (is_msg_stamped<T>) {
        msg_seq = builtin_msgs::common_msgs::extract_timstamp(*raw_ptr);
    }

    const uint64_t ts = (opts_.qos.lifespan.count() > 0)
                      ? platform::steadyNowNs() : hdr.timestamp_ns;
    push_item(queue_, OrderedItem{msg_seq, ts, std::move(msg_storage)});

    // KeepLast depth enforcement.
    if (opts_.qos.history == History::KeepLast && opts_.qos.depth > 0)
    {
        while (queue_size_approx(queue_) > opts_.qos.depth)
        {
            OrderedItem discard;
            if (!try_pop_item(queue_, discard)) break;
        }
    }

    // Deadline tracking.
    if (opts_.qos.deadline.count() > 0)
    {
        last_message_time_.store(std::chrono::steady_clock::now(),
                                 std::memory_order_relaxed);
        deadline_fired_.store(false, std::memory_order_relaxed);
    }

    callbackGroup()->notify(this);
    } // else (HasSerializer<T>)
}

template<typename T>
void Subscriber<T>::unregisterNetFds()
{
    std::lock_guard lock(net_mutex_);
    for (auto& p : net_peers_) {
        if (p.tcp && p.tcp->isConnected())
            node_->reactor().removeFd(p.tcp->nativeFd());
        if (p.udp && p.udp->isValid())
            node_->reactor().removeFd(p.udp->nativeFd());
    }
    net_peers_.clear();
}

// ── Executor interface ───────────────────────────────────────────

template<typename T>
void Subscriber<T>::takeAll()
{
    OrderedItem item;
    while (try_pop_item(queue_, item))
    {
        if (shouldDiscard(item)) continue;
        if constexpr (SmallValueMsg<T>) {
            callback_func_(item.msg);            // const T&
        } else {
            callback_func_(std::move(item.msg));  // shared_ptr<T>
        }
    }

    clearReady();
    if (queue_size_approx(queue_) > 0)
        callbackGroup()->notify(this);
}

template<typename T>
void Subscriber<T>::drainAll(std::vector<TimeExecEntry>& out)
{
    OrderedItem item;
    while (try_pop_item(queue_, item))
    {
        if (shouldDiscard(item)) continue;

        if constexpr (SmallValueMsg<T>) {
            auto invoker = [this, m = item.msg]() mutable {
                this->callback_func_(m);
            };
            out.push_back(TimeExecEntry{item.seq, std::move(invoker)});
        } else {
            auto invoker = [this, m = std::move(item.msg)]() mutable {
                this->callback_func_(std::move(m));
            };
            out.push_back(TimeExecEntry{item.seq, std::move(invoker)});
        }
    }
    clearReady();
    if (queue_size_approx(queue_) > 0)
        callbackGroup()->notify(this);
}

template<typename T>
void Subscriber<T>::invokeTrampoline(void* obj, std::shared_ptr<void> msg)
{
    auto* self = static_cast<Subscriber<T>*>(obj);
    if constexpr (SmallValueMsg<T>) {
        auto* typed = static_cast<T*>(msg.get());
        self->callback_func_(*typed);  // const T&
    } else {
        auto typed_msg = std::static_pointer_cast<T>(std::move(msg));
        self->callback_func_(std::move(typed_msg));
    }
}

template<typename T>
void Subscriber<T>::drainAllExec(std::vector<ExecEntry>& out)
{
    drainExecSome(out, SIZE_MAX);
}

template<typename T>
size_t Subscriber<T>::drainExecSome(std::vector<ExecEntry>& out, size_t max_count)
{
    static constexpr size_t kBulkSize = 256;
    thread_local OrderedItem bulk_buffer[kBulkSize];

    size_t total = 0;
    while (total < max_count)
    {
        const size_t to_pop = std::min(kBulkSize, max_count - total);
        const size_t count = try_pop_bulk(queue_, bulk_buffer, to_pop);
        if (count == 0) break;

        for (size_t i = 0; i < count; ++i)
        {
            if (shouldDiscard(bulk_buffer[i])) {
                if constexpr (!SmallValueMsg<T>) bulk_buffer[i].msg.reset();
                continue;
            }

            ExecEntry e;
            e.seq    = bulk_buffer[i].seq;
            e.obj    = this;
            e.invoke = &Subscriber<T>::invokeTrampoline;
            if constexpr (SmallValueMsg<T>) {
                // Wrap value in shared_ptr for type-erased ExecEntry.
                e.msg = std::make_shared<T>(bulk_buffer[i].msg);
            } else {
                e.msg = std::static_pointer_cast<void>(bulk_buffer[i].msg);
                bulk_buffer[i].msg.reset();
            }
            out.push_back(std::move(e));
        }
        total += count;
    }

    clearReady();
    if (queue_size_approx(queue_) > 0)
        callbackGroup()->notify(this);

    return total;
}

// ── QoS helpers ──────────────────────────────────────────────────

template<typename T>
bool Subscriber<T>::shouldDiscard(const OrderedItem& item) const
{
    if (opts_.qos.lifespan.count() <= 0) return false;
    const uint64_t now = platform::steadyNowNs();
    const uint64_t age_ns = now - item.timestamp_ns;
    const uint64_t lifespan_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(opts_.qos.lifespan).count());
    return age_ns > lifespan_ns;
}

template<typename T>
void Subscriber<T>::checkDeadline()
{
    if (opts_.qos.deadline.count() <= 0) return;

    auto now = std::chrono::steady_clock::now();
    auto last = last_message_time_.load(std::memory_order_relaxed);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);

    if (elapsed > opts_.qos.deadline && !deadline_fired_.exchange(true))
    {
        if (deadline_missed_cb_) deadline_missed_cb_();
    }
}

} // namespace lux::communication
