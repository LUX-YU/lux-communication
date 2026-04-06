#pragma once
/// Unified Publisher<T> — auto-selects intra / SHM / network transport per peer.
///
/// Inherits PublisherBase so it's tracked by NodeBase's publisher SparseSet,
/// and holds a Topic<T> for same-process zero-copy distribution.
///
/// For each remote (cross-process / cross-machine) Subscriber discovered via
/// DiscoveryService, a dedicated SHM ring or network channel is created.

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>

#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/PublishOptions.hpp>
#include <lux/communication/ExecutorBase.hpp>
#include <lux/communication/MessageTraits.hpp>
#include <lux/communication/ChannelKind.hpp>
#include <lux/communication/TransportSelector.hpp>
#include <lux/communication/Hash.hpp>
#include <lux/communication/intraprocess/Topic.hpp>

#include <lux/communication/serialization/Serializer.hpp>
#include <lux/communication/transport/FrameHeader.hpp>
#include <lux/communication/transport/ShmRingWriter.hpp>
#include <lux/communication/transport/ShmDataPool.hpp>
#include <lux/communication/transport/LoanedMessage.hpp>
#include <lux/communication/transport/UdpTransportWriter.hpp>
#include <lux/communication/transport/TcpTransportWriter.hpp>
#include <lux/communication/discovery/DiscoveryService.hpp>
#include <lux/communication/platform/PlatformDefs.hpp>
#include <lux/communication/TokenBucket.hpp>

// Unified Node (forward-declared to break circularity; full include below).
namespace lux::communication
{
    class Node;
}

namespace lux::communication
{
    namespace detail
    {
        /// Deterministic SHM ring name shared with Phase 2 interprocess::Publisher.
        inline std::string makeRingName(uint64_t domain_id, uint64_t topic_hash,
                                        uint32_t pub_pid, uint32_t sub_pid)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "lux_ring_%llu_%08llx_%u_%u",
                          static_cast<unsigned long long>(domain_id),
                          static_cast<unsigned long long>(topic_hash),
                          pub_pid, sub_pid);
            return buf;
        }

        inline std::string makePoolName(uint64_t domain_id, uint64_t topic_hash,
                                        uint32_t pub_pid)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "lux_pool_%llu_%08llx_%u",
                          static_cast<unsigned long long>(domain_id),
                          static_cast<unsigned long long>(topic_hash),
                          pub_pid);
            return buf;
        }
    } // namespace detail

    /// Unified Publisher.
    template <typename T>
    class Publisher : public PublisherBase
    {
        using TopicT = intraprocess::Topic<T>;
        using Ser = serialization::Serializer<T>;

        static TopicSptr getOrCreateTopic(Node *node, const std::string &topic_name);

    public:
        Publisher(const std::string &topic_name, Node *node,
                  const PublishOptions &opts = {});

        ~Publisher();

        Publisher(const Publisher &) = delete;
        Publisher &operator=(const Publisher &) = delete;

        // ── Publish API ────────────────────────────────────────────

        /// Serialize & distribute to all intra / SHM / net peers.
        void publish(const T &msg);

        /// Publish a pre-existing shared_ptr (avoids copy for intra path).
        void publish(std::shared_ptr<T> msg);

        /// In-place construct and publish.
        template <typename... Args>
        void emplace(Args &&...args);

        /// Zero-copy loan (TriviallyCopyableMsg only, SHM path).
        auto loan() -> transport::LoanedMessage<T>
            requires serialization::TriviallyCopyableMsg<T>;

        /// Commit a loaned message.
        void publish(transport::LoanedMessage<T> &&loaned)
            requires serialization::TriviallyCopyableMsg<T>;

        const std::string &topicName() const { return topic_name_; }

    private:
        // ── SHM peer management ──
        struct ShmPeer
        {
            uint32_t sub_pid;
            std::unique_ptr<transport::ShmRingWriter> writer;
        };

        // ── Net peer management ──
        struct NetPeer
        {
            std::string endpoint;
            std::unique_ptr<transport::UdpTransportWriter> udp;
            std::unique_ptr<transport::TcpTransportWriter> tcp;
        };

        void onPeerDiscovered(const discovery::TopicEndpoint &ep);
        void onPeerLost(const discovery::TopicEndpoint &ep);

        void publishIntra(stored_msg_t<T> msg);
        void publishShm(const T &msg, transport::FrameHeader &hdr, uint32_t ser_size);
        void publishShmViaPool(const T &msg, transport::FrameHeader &hdr,
                               uint32_t ser_size, uint32_t sub_count);
        void publishNet(const T &msg, transport::FrameHeader &hdr, uint32_t ser_size);
        void ensureDataPool();

        std::string topic_name_;
        Node *node_;
        PublishOptions opts_;
        uint64_t topic_hash_;
        uint64_t discovery_handle_ = 0;
        uint64_t listener_id_ = 0;

        std::mutex shm_mutex_;
        std::vector<ShmPeer> shm_peers_;

        std::mutex net_mutex_;
        std::vector<NetPeer> net_peers_;

        /// Fast-path: true when neither SHM nor Net transport is possible.
        /// Avoids per-message mutex locks for the common intra-only case.
        bool intra_only_ = false;

        /// Atomic peer-count flags (updated in onPeerDiscovered/onPeerLost)
        /// so publish() can skip mutex when no cross-process peers exist.
        std::atomic<bool> has_shm_peers_{false};
        std::atomic<bool> has_net_peers_{false};

        std::unique_ptr<transport::ShmDataPool> data_pool_;

        /// Phase 6 — bandwidth limiter (nullptr when bandwidth_limit == 0).
        std::unique_ptr<TokenBucket> bandwidth_limiter_;

        /// TCP heartbeat: IoThread poller handle (0 = not registered).
        uint64_t tcp_heartbeat_poller_ = 0;
        /// Last time a TCP Ping was sent (used for interval gating).
        std::chrono::steady_clock::time_point last_tcp_ping_{};
    };

} // namespace lux::communication

// ═══════════════════════════════════════════════════════════════════
//  Template implementation
// ═══════════════════════════════════════════════════════════════════
#include "Node.hpp"       // unified Node
#include "Subscriber.hpp" // unified Subscriber<T> — for publishIntra()
#include <lux/communication/Domain.hpp>

namespace lux::communication
{

    // ── Helper: get or create the Domain-level Topic<T> ──────────────

    template <typename T>
    TopicSptr Publisher<T>::getOrCreateTopic(Node *node, const std::string &topic_name)
    {
        return node->domain().template createOrGetTopic<TopicT, T>(topic_name);
    }

    // ── Constructor / Destructor ─────────────────────────────────────

    template <typename T>
    Publisher<T>::Publisher(const std::string &topic_name, Node *node,
                            const PublishOptions &opts)
        : PublisherBase(getOrCreateTopic(node, topic_name), node), topic_name_(topic_name), node_(node), opts_(opts), topic_hash_(fnv1a_64(topic_name))
    {
        const auto &nopts = node_->options();

        // Fast-path flag: skip SHM/Net per-message overhead when neither is possible.
        intra_only_ = (!nopts.enable_shm && !nopts.enable_net) || opts_.transport_hint == PublishTransportHint::IntraOnly;

        // Phase 6: Initialize bandwidth limiter if configured.
        if (opts_.qos.bandwidth_limit > 0)
        {
            bandwidth_limiter_ = std::make_unique<TokenBucket>(opts_.qos.bandwidth_limit);
        }

        // Register with DiscoveryService so cross-process subscribers find us.
        if (nopts.enable_discovery &&
            opts_.transport_hint != PublishTransportHint::IntraOnly)
        {
            auto &ds = discovery::DiscoveryService::getInstance(node_->domain().id());

            discovery_handle_ = ds.announcePublisher(
                topic_name_, typeid(T).name(), typeid(T).hash_code());

            listener_id_ = ds.addListener(topic_name_,
                                          [this](const discovery::DiscoveryEvent &ev)
                                          {
                                              if (ev.endpoint.role != discovery::TopicEndpoint::Role::Subscriber)
                                                  return;
                                              if (ev.type == discovery::DiscoveryEventType::EndpointDiscovered)
                                                  onPeerDiscovered(ev.endpoint);
                                              else
                                                  onPeerLost(ev.endpoint);
                                          });

            // Connect to already-known subscribers.
            auto existing = ds.lookup(topic_name_,
                                      discovery::TopicEndpoint::Role::Subscriber);
            for (auto &ep : existing)
                onPeerDiscovered(ep);
        }

        // ── TCP heartbeat poller (if network transport enabled) ──
        if (nopts.enable_net && nopts.tcp_ping_interval_ms > 0)
        {
            auto ping_interval = std::chrono::milliseconds{nopts.tcp_ping_interval_ms};
            auto ping_timeout = std::chrono::milliseconds{nopts.tcp_ping_timeout_ms};

            tcp_heartbeat_poller_ = node_->ioThread().registerPoller(
                [this, ping_interval, ping_timeout]()
                {
                    // Timer guard — only run at the configured interval.
                    auto now = std::chrono::steady_clock::now();
                    if (now - last_tcp_ping_ < ping_interval)
                        return;
                    last_tcp_ping_ = now;

                    std::lock_guard lock(net_mutex_);
                    for (auto &peer : net_peers_)
                    {
                        if (!peer.tcp)
                            continue;
                        peer.tcp->recvPongAll();
                        peer.tcp->sendPingAll();
                        peer.tcp->gcDeadConnections(ping_timeout);
                    }
                });
        }
    }

    template <typename T>
    Publisher<T>::~Publisher()
    {
        // Unregister TCP heartbeat poller first.
        if (tcp_heartbeat_poller_)
        {
            node_->ioThread().unregisterPoller(tcp_heartbeat_poller_);
            tcp_heartbeat_poller_ = 0;
        }

        const auto &nopts = node_->options();
        if (nopts.enable_discovery && listener_id_)
        {
            auto &ds = discovery::DiscoveryService::getInstance(node_->domain().id());
            ds.removeListener(listener_id_);
            if (discovery_handle_)
                ds.withdraw(discovery_handle_);
        }

        std::lock_guard lk1(shm_mutex_);
        shm_peers_.clear();

        std::lock_guard lk2(net_mutex_);
        net_peers_.clear();
    }

    // ── Peer discovery callbacks ─────────────────────────────────────

    template <typename T>
    void Publisher<T>::onPeerDiscovered(const discovery::TopicEndpoint &ep)
    {
        ChannelKind kind = selectTransport(ep, node_->options());

        switch (kind)
        {
        case ChannelKind::Intra:
            // Same-process subscriber is already in the Topic's COW snapshot.
            break;

        case ChannelKind::Shm:
        {
            std::lock_guard lock(shm_mutex_);
            for (const auto &p : shm_peers_)
                if (p.sub_pid == ep.pid)
                    return; // duplicate

            std::string ring_name = detail::makeRingName(
                node_->domain().id(), topic_hash_,
                platform::currentPid(), ep.pid);

            try
            {
                auto writer = std::make_unique<transport::ShmRingWriter>(
                    ring_name, opts_.shm_ring_slot_count, opts_.shm_ring_slot_size);
                shm_peers_.push_back(ShmPeer{ep.pid, std::move(writer)});
                has_shm_peers_.store(true, std::memory_order_release);

                // Re-announce with the SHM name so the subscriber can connect.
                auto &ds = discovery::DiscoveryService::getInstance(node_->domain().id());
                ds.withdraw(discovery_handle_);
                discovery_handle_ = ds.announcePublisher(
                    topic_name_, typeid(T).name(), typeid(T).hash_code(),
                    ring_name);
            }
            catch (const std::exception &)
            {
                // ShmRingWriter creation failed — will retry on next announce.
            }
            break;
        }

        case ChannelKind::Net:
        {
            std::lock_guard lock(net_mutex_);
            for (const auto &p : net_peers_)
                if (p.endpoint == ep.net_endpoint)
                    return; // duplicate

            try
            {
                // Parse endpoint "addr:port"
                auto colon = ep.net_endpoint.rfind(':');
                if (colon == std::string::npos)
                    break;
                std::string addr = ep.net_endpoint.substr(0, colon);
                uint16_t port = static_cast<uint16_t>(
                    std::stoi(ep.net_endpoint.substr(colon + 1)));

                auto udp = std::make_unique<transport::UdpTransportWriter>(addr, port);
                auto tcp = std::make_unique<transport::TcpTransportWriter>(
                    "0.0.0.0", 0, topic_hash_, typeid(T).hash_code());
                tcp->setSeqSupplier([this]()
                                    { return node_->domain().currentSeq(); });
                net_peers_.push_back(NetPeer{ep.net_endpoint, std::move(udp), std::move(tcp)});
                has_net_peers_.store(true, std::memory_order_release);
            }
            catch (const std::exception &)
            {
                // Network setup failed — will retry.
            }
            break;
        }
        }
    }

    template <typename T>
    void Publisher<T>::onPeerLost(const discovery::TopicEndpoint &ep)
    {
        ChannelKind kind = selectTransport(ep, node_->options());

        switch (kind)
        {
        case ChannelKind::Intra:
            break; // managed by Topic snapshot
        case ChannelKind::Shm:
        {
            std::lock_guard lock(shm_mutex_);
            std::erase_if(shm_peers_, [&](const ShmPeer &p)
                          { return p.sub_pid == ep.pid; });
            has_shm_peers_.store(!shm_peers_.empty(), std::memory_order_release);
            break;
        }
        case ChannelKind::Net:
        {
            std::lock_guard lock(net_mutex_);
            std::erase_if(net_peers_, [&](const NetPeer &p)
                          { return p.endpoint == ep.net_endpoint; });
            has_net_peers_.store(!net_peers_.empty(), std::memory_order_release);
            break;
        }
        }
    }

    // ── Publish implementations ──────────────────────────────────────

    template <typename T>
    void Publisher<T>::publish(const T &msg)
    {
        // 1. Intra path — same-process subscribers (always available).
        if constexpr (SmallValueMsg<T>)
        {
            publishIntra(msg); // pass T by value (trivially copyable, cheap)
        }
        else
        {
            auto ptr = std::make_shared<T>(msg);
            publishIntra(std::move(ptr));
        }

        // 2–3. SHM & Net paths require serialisation support.
        if constexpr (serialization::HasSerializer<T>)
        {
            // Fast exit: skip all cross-process work when intra-only.
            if (intra_only_)
                return;

            // Quick peer check via atomics — avoid mutex when no peers exist.
            const bool has_shm = has_shm_peers_.load(std::memory_order_relaxed);
            const bool has_net = has_net_peers_.load(std::memory_order_relaxed);
            if (!has_shm && !has_net)
                return;

            const uint32_t ser_size = static_cast<uint32_t>(Ser::serializedSize(msg));

            // Phase 6: Bandwidth limiting.
            if (bandwidth_limiter_)
            {
                if (opts_.qos.reliability == Reliability::Reliable)
                    bandwidth_limiter_->waitAndConsume(ser_size);
                else if (!bandwidth_limiter_->tryConsume(ser_size))
                    return; // BestEffort: drop
            }

            // Build FrameHeader (shared by SHM & Net paths).
            transport::FrameHeader hdr;
            hdr.topic_hash = topic_hash_;
            hdr.seq_num = node_->domain().allocateSeqRange(1);
            hdr.timestamp_ns = platform::steadyNowNs();
            transport::setFormat(hdr, Ser::format);
            hdr.payload_size = ser_size;

            // Phase 6: Set Reliable flag in header.
            if (opts_.qos.reliability == Reliability::Reliable)
                transport::setReliable(hdr);

            // 2. SHM path — same-machine cross-process.
            if (has_shm)
            {
                std::lock_guard lock(shm_mutex_);
                if (!shm_peers_.empty())
                {
                    const auto sub_count = static_cast<uint32_t>(shm_peers_.size());
                    if (sub_count > 1 && ser_size >= transport::kPoolThreshold)
                        publishShmViaPool(msg, hdr, ser_size, sub_count);
                    else
                        publishShm(msg, hdr, ser_size);
                }
            }

            // 3. Net path — cross-machine.
            if (has_net)
            {
                std::lock_guard lock(net_mutex_);
                if (!net_peers_.empty())
                    publishNet(msg, hdr, ser_size);
            }
        }
    }

    template <typename T>
    void Publisher<T>::publish(std::shared_ptr<T> msg)
    {
        // Intra path: direct zero-copy (always available).
        if constexpr (SmallValueMsg<T>)
        {
            publishIntra(*msg); // dereference to get T value
        }
        else
        {
            publishIntra(msg); // shared, not moved (might need it below)
        }

        // SHM + Net: serialize from the shared_ptr (requires Serializer<T>).
        if constexpr (serialization::HasSerializer<T>)
        {
            // Fast exit: skip all cross-process work when intra-only.
            if (intra_only_)
                return;

            // Quick peer check via atomics — avoid mutex when no peers exist.
            const bool has_shm = has_shm_peers_.load(std::memory_order_relaxed);
            const bool has_net = has_net_peers_.load(std::memory_order_relaxed);
            if (!has_shm && !has_net)
                return;

            const uint32_t ser_size = static_cast<uint32_t>(Ser::serializedSize(*msg));

            // Phase 6: Bandwidth limiting.
            if (bandwidth_limiter_)
            {
                if (opts_.qos.reliability == Reliability::Reliable)
                    bandwidth_limiter_->waitAndConsume(ser_size);
                else if (!bandwidth_limiter_->tryConsume(ser_size))
                    return; // BestEffort: drop
            }

            transport::FrameHeader hdr;
            hdr.topic_hash = topic_hash_;
            hdr.seq_num = node_->domain().allocateSeqRange(1);
            hdr.timestamp_ns = platform::steadyNowNs();
            transport::setFormat(hdr, Ser::format);
            hdr.payload_size = ser_size;

            if (opts_.qos.reliability == Reliability::Reliable)
                transport::setReliable(hdr);

            if (has_shm)
            {
                std::lock_guard lock(shm_mutex_);
                if (!shm_peers_.empty())
                {
                    const auto sub_count = static_cast<uint32_t>(shm_peers_.size());
                    if (sub_count > 1 && ser_size >= transport::kPoolThreshold)
                        publishShmViaPool(*msg, hdr, ser_size, sub_count);
                    else
                        publishShm(*msg, hdr, ser_size);
                }
            }
            if (has_net)
            {
                std::lock_guard lock(net_mutex_);
                if (!net_peers_.empty())
                    publishNet(*msg, hdr, ser_size);
            }
        }
    }

    template <typename T>
    template <typename... Args>
    void Publisher<T>::emplace(Args &&...args)
    {
        if constexpr (SmallValueMsg<T>)
        {
            T val(std::forward<Args>(args)...);
            publish(val);
        }
        else
        {
            auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
            publish(std::move(ptr));
        }
    }

    // ── Intra path — distribute to same-process unified Subscribers ──

    template <typename T>
    void Publisher<T>::publishIntra(stored_msg_t<T> msg)
    {
        auto snapshot = topicAs<TopicT>().getSubscriberSnapshot();
        if (!snapshot || snapshot->empty())
            return;

        const size_t n = snapshot->size();

        // Allocate per-executor sequence numbers so that each executor
        // only sees a contiguous sequence stream for its own subscribers,
        // avoiding cross-executor sequence gaps that block SeqOrderedExecutor.
        for (size_t i = 0; i < n; ++i)
        {
            auto *sub = static_cast<Subscriber<T> *>((*snapshot)[i]);
            auto *exec = sub->callbackGroup()->executor();
            const uint64_t seq = exec ? exec->allocateSeq() : 0;
            if (i + 1 < n)
                sub->enqueue(seq, msg);           // copy for non-last
            else
                sub->enqueue(seq, std::move(msg)); // move for last
        }
    }

    // ── SHM inline path ─────────────────────────────────────────────

    template <typename T>
    void Publisher<T>::publishShm(const T &msg, transport::FrameHeader &hdr,
                                  uint32_t ser_size)
    {
        for (auto &peer : shm_peers_)
        {
            void *slot = nullptr;

            if (opts_.qos.reliability == Reliability::Reliable)
            {
                // Spin-wait with timeout for Reliable QoS.
                auto deadline_tp = std::chrono::steady_clock::now() + opts_.shm_reliable_timeout;
                while (!slot && std::chrono::steady_clock::now() < deadline_tp)
                {
                    slot = peer.writer->acquireSlot();
                    if (!slot)
                        std::this_thread::yield();
                }
            }
            else
            {
                slot = peer.writer->acquireSlot();
            }

            if (!slot)
                continue; // ring full — drop (even Reliable times out)

            std::memcpy(slot, &hdr, sizeof(hdr));
            char *payload = static_cast<char *>(slot) + sizeof(hdr);
            Ser::serialize(msg, payload,
                           peer.writer->maxPayloadSize() - sizeof(hdr));
            peer.writer->commitSlot(
                static_cast<uint32_t>(sizeof(hdr) + ser_size));
        }
    }

    // ── SHM pool path (Phase 3 — large message 1:N) ─────────────────

    template <typename T>
    void Publisher<T>::publishShmViaPool(const T &msg, transport::FrameHeader &hdr,
                                         uint32_t ser_size, uint32_t sub_count)
    {
        ensureDataPool();

        auto alloc = data_pool_->allocate(ser_size, sub_count);
        if (!alloc.payload)
        {
            // Pool full — fallback to inline.
            publishShm(msg, hdr, ser_size);
            return;
        }

        Ser::serialize(msg, alloc.payload, ser_size);

        transport::PoolDescriptor desc;
        desc.pool_offset = alloc.pool_offset;
        desc.data_size = ser_size;
        desc.ref_count_offset = static_cast<uint32_t>(alloc.ref_count_offset);

        hdr.payload_size = sizeof(transport::PoolDescriptor);
        transport::setPooled(hdr);

        for (auto &peer : shm_peers_)
        {
            void *slot = peer.writer->acquireSlot();
            if (!slot)
            {
                data_pool_->release(alloc.ref_count_offset);
                continue;
            }
            std::memcpy(slot, &hdr, sizeof(hdr));
            std::memcpy(static_cast<char *>(slot) + sizeof(hdr), &desc, sizeof(desc));
            peer.writer->commitSlot(
                static_cast<uint32_t>(sizeof(hdr) + sizeof(desc)));
        }
    }

    template <typename T>
    void Publisher<T>::ensureDataPool()
    {
        if (data_pool_)
            return;
        std::string pool_name = detail::makePoolName(
            node_->domain().id(), topic_hash_, platform::currentPid());
        data_pool_ = std::make_unique<transport::ShmDataPool>(
            pool_name, opts_.shm_pool_capacity, 4096, opts_.shm_huge_pages);
    }

    // ── Net path ─────────────────────────────────────────────────────

    template <typename T>
    void Publisher<T>::publishNet(const T &msg, transport::FrameHeader &hdr,
                                  uint32_t ser_size)
    {
        // Serialize once into a contiguous buffer.
        const uint32_t frame_size = static_cast<uint32_t>(sizeof(hdr) + ser_size);
        thread_local std::vector<char> buf;
        buf.resize(frame_size);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        Ser::serialize(msg, buf.data() + sizeof(hdr), ser_size);

        for (auto &peer : net_peers_)
        {
            // Phase 6: Reliable → always use TCP.
            if (opts_.qos.reliability == Reliability::Reliable && peer.tcp)
            {
                peer.tcp->send(hdr, buf.data() + sizeof(hdr), ser_size);
            }
            else if (ser_size < opts_.net_large_threshold && peer.udp)
            {
                peer.udp->send(hdr, buf.data() + sizeof(hdr), ser_size);
            }
            else if (peer.tcp)
            {
                peer.tcp->send(hdr, buf.data() + sizeof(hdr), ser_size);
            }
        }
    }

    // ── Loan API (SHM, TriviallyCopyableMsg only) ────────────────────

    template <typename T>
    auto Publisher<T>::loan() -> transport::LoanedMessage<T>
        requires serialization::TriviallyCopyableMsg<T>
    {

        transport::FrameHeader hdr;
        hdr.topic_hash = topic_hash_;
        hdr.seq_num = node_->domain().allocateSeqRange(1);
        hdr.timestamp_ns = platform::steadyNowNs();
        hdr.payload_size = static_cast<uint32_t>(sizeof(T));
        transport::setFormat(hdr, transport::SerializationFormat::RawMemcpy);
        transport::setLoaned(hdr);

        std::lock_guard lock(shm_mutex_);
        if (shm_peers_.empty())
            return {}; // no SHM subscribers
        return transport::LoanedMessage<T>(shm_peers_[0].writer.get(), hdr);
    }

    template <typename T>
    void Publisher<T>::publish(transport::LoanedMessage<T> &&loaned)
        requires serialization::TriviallyCopyableMsg<T>
    {

        if (!loaned.valid())
            return;

        std::lock_guard lock(shm_mutex_);
        uint32_t total = static_cast<uint32_t>(sizeof(transport::FrameHeader) + sizeof(T));
        loaned.markCommitted();
        shm_peers_[0].writer->commitSlot(total);

        // Replicate to additional SHM peers.
        if (shm_peers_.size() > 1)
        {
            const void *src = loaned.slotBase();
            for (size_t i = 1; i < shm_peers_.size(); ++i)
            {
                void *dst = shm_peers_[i].writer->acquireSlot();
                if (!dst)
                    continue;
                std::memcpy(dst, src, total);
                shm_peers_[i].writer->commitSlot(total);
            }
        }

        // Also publish to intra subscribers (they need the data too).
        publishIntra(std::make_shared<T>(*loaned.get()));
    }

} // namespace lux::communication
