#include "lux/communication/introprocess/Core.hpp"

namespace lux::communication::introprocess 
{
    Core::Core(const MakeSharedCoreHelper& helper, int argc, char* argv[])
        : Core(argc, argv) {}

    Core::~Core() {
        if (ok_) {
            shutdown();
        }
        stop();
        if (core_thread_.joinable()) {
            core_thread_.join();
        }
    }

    // Static factory method to create a shared pointer to a new Core instance.
    std::shared_ptr<Core> Core::create(int argc, char* argv[]) {
        return std::make_shared<Core>(MakeSharedCoreHelper{}, argc, argv);
    }

    bool Core::ok() const {
        return ok_;
    }

    void Core::init() {
        std::thread([this] {event_loop(); }).swap(core_thread_);
    }

    void Core::shutdown() {
        ok_ = false;
        for (std::weak_ptr<EventHandlerBase> domain : topic_domains_) {
            domain.lock()->stop();
        }

        for (EventHandlerBase* node : nodes_) {
            node->stop();
        }
    }

    void Core::shutdown_wait() {
        ok_ = false;
        auto future = stop_wait();

        future.get();
    }

    bool Core::isStop() const {
        return !ok_;
    }

    bool Core::handle(const CommunicationEvent& event) {
        switch (event.type) {
        case ECommunicationEvent::DomainClosed: {
            auto payload = static_cast<DomainRequestPayload*>(event.payload.get());
            auto domain = static_cast<EventHandlerBase*>(payload->object);
            remove_topic_domain(domain);

            payload->promise.set_value();
            break;
        }
        case ECommunicationEvent::NodeCreated:
        {
            auto payload = static_cast<NodeRequestPayload*>(event.payload.get());
            auto node = static_cast<EventHandlerBase*>(payload->object);

            nodes_.push_back(node);

            payload->promise.set_value();
            break;
        }
        case ECommunicationEvent::NodeClosed:
        {
            auto payload = static_cast<NodeRequestPayload*>(event.payload.get());
            auto node = static_cast<EventHandlerBase*>(payload->object);

            remove_node(node);

            payload->promise.set_value();
            break;
        }
        case ECommunicationEvent::Stop: {
            auto payload = static_cast<Stoppayload*>(event.payload.get());
            // stop all sub node
            handle_stop_event();

            payload->promise.set_value();
            return false;
        }
        }

        return true;
    }

    void Core::handle_stop_event() {
        std::vector<std::future<void>> future_list;

        {
            std::scoped_lock lck(mutex_);
            future_list.reserve(topic_domains_.size());
            for (std::weak_ptr<EventHandlerBase> domain : topic_domains_) {
                future_list.push_back(domain.lock()->stop_wait());
            }

            for (auto& future : future_list) {
                future.get();
            }
            future_list.clear();
        }

        {
            std::scoped_lock lck(node_mutex_);
            future_list.reserve(nodes_.size());
            for (EventHandlerBase* node : nodes_) {
                future_list.push_back(node->stop_wait());
            }
            for (auto& future : future_list) {
                future.get();
            }
        }
    }

    void Core::remove_topic_domain(EventHandlerBase* ptr) {
        std::scoped_lock lck(mutex_);
        auto iter = std::remove_if(
            topic_domains_.begin(), topic_domains_.end(),
            [ptr](std::weak_ptr<EventHandlerBase>& p) {
                auto domain_ptr = p.lock();
                return !domain_ptr || ptr == domain_ptr.get();
            }
        );
        topic_domains_.erase(iter, topic_domains_.end());
    }

    void Core::remove_node(EventHandlerBase* ptr) {
        std::scoped_lock lck(node_mutex_);
        auto iter = std::remove_if(
            nodes_.begin(), nodes_.end(),
            [ptr](EventHandlerBase* p) {
                return !p || ptr == p;
            }
        );

        nodes_.erase(iter, nodes_.end());
    }

    Core::Core(int argc, char* argv[])
        : parent_t(max_queue_size) {}
}