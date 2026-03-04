#pragma once
/// Node implementation — supports intra-process, shared memory, and network
/// transport configured via NodeOptions.

#include <memory>
#include <vector>
#include <mutex>
#include <functional>

#include <lux/communication/visibility.h>
#include <lux/communication/Domain.hpp>
#include <lux/communication/NodeBase.hpp>
#include <lux/communication/NodeOptions.hpp>
#include <lux/communication/PublishOptions.hpp>
#include <lux/communication/SubscribeOptions.hpp>
#include <lux/communication/IoThread.hpp>
#include <lux/communication/transport/IoReactor.hpp>

namespace lux::communication
{
    class CallbackGroupBase;

    // Forward declarations of unified Pub/Sub (header-only templates).
    template<typename T> class Publisher;
    template<typename T> class Subscriber;

    /// Unified Node.
    class LUX_COMMUNICATION_PUBLIC Node : public NodeBase
    {
        template<typename T> friend class Publisher;
        template<typename T> friend class Subscriber;
    public:
        explicit Node(const std::string& name,
                      Domain& domain  = Domain::default_domain(),
                      const NodeOptions& opts = {});

        ~Node();

        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;

        /// Default CallbackGroup (MutuallyExclusive).
        CallbackGroupBase* defaultCallbackGroup();

        /// Create a typed publisher.
        template<typename T>
        std::shared_ptr<Publisher<T>> createPublisher(
            const std::string& topic_name,
            const PublishOptions& opts = {});

        /// Create a typed subscriber.
        template<typename T, typename Func>
        std::shared_ptr<Subscriber<T>> createSubscriber(
            const std::string& topic_name,
            Func&& callback,
            CallbackGroupBase* cbg = nullptr,
            const SubscribeOptions& opts = {},
            typename Subscriber<T>::ContentFilter filter = nullptr);

        /// Access the shared IoReactor.
        transport::IoReactor& reactor()  { return *reactor_; }

        /// Access the IoThread.
        IoThread& ioThread()             { return *io_thread_; }

        /// Node-level options.
        const NodeOptions& options() const { return opts_; }

        /// Stop all subscribers and the IO thread.
        void stop();

    private:
        NodeOptions                                   opts_;
        std::unique_ptr<CallbackGroupBase>            default_cbg_;
        std::shared_ptr<transport::IoReactor>         reactor_;
        std::unique_ptr<IoThread>                     io_thread_;

        // Owned endpoints — keep shared_ptrs alive as long as the Node lives.
        std::mutex                                    endpoints_mutex_;
        std::vector<std::shared_ptr<void>>            owned_endpoints_;

        // Subscriber stop functions (for orderly shutdown).
        std::mutex                                    stoppers_mutex_;
        std::vector<std::function<void()>>            subscriber_stoppers_;
    };

    // ── Free-function spin helpers (mirror existing patterns) ──
    LUX_COMMUNICATION_PUBLIC void spin(Node* node);
    LUX_COMMUNICATION_PUBLIC void spinUntil(Node* node, bool& flag);
    LUX_COMMUNICATION_PUBLIC void stopSpin();

} // namespace lux::communication

// ── Template implementation ─────────────────────────────────────────────
#include "Publisher.hpp"
#include "Subscriber.hpp"

namespace lux::communication {

template<typename T>
std::shared_ptr<Publisher<T>> Node::createPublisher(
    const std::string& topic_name, const PublishOptions& opts)
{
    auto pub = std::make_shared<Publisher<T>>(topic_name, this, opts);
    {
        std::lock_guard lock(endpoints_mutex_);
        owned_endpoints_.push_back(pub);
    }
    return pub;
}

template<typename T, typename Func>
std::shared_ptr<Subscriber<T>> Node::createSubscriber(
    const std::string& topic_name, Func&& callback,
    CallbackGroupBase* cbg, const SubscribeOptions& opts,
    typename Subscriber<T>::ContentFilter filter)
{
    auto sub = std::make_shared<Subscriber<T>>(
        topic_name, this, std::forward<Func>(callback), cbg, opts,
        std::move(filter));
    {
        std::lock_guard lock(endpoints_mutex_);
        owned_endpoints_.push_back(sub);
    }
    {
        std::lock_guard lock(stoppers_mutex_);
        std::weak_ptr<Subscriber<T>> weak = sub;
        subscriber_stoppers_.push_back([weak]() {
            if (auto s = weak.lock()) s->stop();
        });
    }
    return sub;
}

} // namespace lux::communication
