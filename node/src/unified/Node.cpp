#include "lux/communication/Node.hpp"
#include "lux/communication/CallbackGroupBase.hpp"
#include "lux/communication/discovery/DiscoveryService.hpp"
#include "lux/communication/executor/SingleThreadedExecutor.hpp"

namespace lux::communication {

Node::Node(const std::string& name, Domain& domain, const NodeOptions& opts)
    : NodeBase(name, domain)
    , opts_(opts)
{
    // 1. Default callback group.
    default_cbg_ = std::make_unique<CallbackGroupBase>(
        this, CallbackGroupType::MutuallyExclusive);

    // 2. IoReactor for network IO.
    reactor_ = std::make_shared<transport::IoReactor>();

    // 3. IoThread for SHM polling + reactor driving.
    io_thread_ = std::make_unique<IoThread>(*reactor_, opts_);
    // IoThread starts lazily on first registerPoller() call,
    // avoiding an idle-spinning thread for pure intra-process workloads.

    // 4. Discovery service (if enabled).
    if (opts_.enable_discovery)
    {
        auto& ds = discovery::DiscoveryService::getInstance(domain.id());
        ds.start();
    }
}

Node::~Node()
{
    stop();
}

CallbackGroupBase* Node::defaultCallbackGroup()
{
    return default_cbg_.get();
}

void Node::stop()
{
    // 1. Stop all subscribers.
    {
        std::lock_guard lock(stoppers_mutex_);
        for (auto& fn : subscriber_stoppers_)
            fn();
        subscriber_stoppers_.clear();
    }

    // 2. Stop the IO thread.
    if (io_thread_)
        io_thread_->stop();

    // 3. Stop the reactor.
    if (reactor_)
        reactor_->stop();
}

// ── Static default executor for spin helpers ──

static SingleThreadedExecutor& default_executor()
{
    static SingleThreadedExecutor instance;
    return instance;
}

void spin(Node* node)
{
    auto& exec = default_executor();
    exec.addNode(node);
    exec.spin();
    exec.removeNode(node);
}

void spinUntil(Node* node, bool& flag)
{
    auto& exec = default_executor();
    exec.addNode(node);
    while (flag) {
        exec.spinSome();
    }
    exec.removeNode(node);
}

void stopSpin()
{
    auto& exec = default_executor();
    exec.stop();
}

} // namespace lux::communication
