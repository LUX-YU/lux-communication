#include "lux/communication/introprocess/Node.hpp"
#include "lux/communication/introprocess/Core.hpp"

namespace lux::communication::introprocess 
{
	Node::Node(std::shared_ptr<Core> core, std::string_view name)
		: core_(std::move(core)), name_(name), parent_t(max_queue_size) {
		auto payload = std::make_unique<NodeRequestPayload>();
		payload->object = this;

		auto future = core_->request<ECommunicationEvent::NodeCreated>(std::move(payload));

		future.get();
	}

	Node::~Node() {
		exit_ = true;

		auto payload = std::make_unique<NodeRequestPayload>();
		payload->object = this;
		auto future = core_->request<ECommunicationEvent::NodeClosed>(std::move(payload));

		future.get();

		allPublisherStop();
		allSubscriberStop();

		parent_t::stop_event_handler();
	}

	Core& Node::core() {
		return *core_;
	}

	const Core& Node::core() const {
		return *core_;
	}

	const std::string& Node::name() const {
		return name_;
	}

	bool Node::spinOnce() {
		return parent_t::event_tick();
	}

	void Node::spin() {
		parent_t::event_loop();
	}

	bool Node::isStop() const {
		return !core().ok();
	}

	bool Node::handle(const CommunicationEvent& event) {
		auto payload_ptr = event.payload.get();
		switch (event.type) {
		case ECommunicationEvent::PublisherLeave:
		{
			auto payload = static_cast<PublisherRequestPayload*>(event.payload.get());
			removePubSub(payload->object);
			payload->promise.set_value();
			break;
		}
		case ECommunicationEvent::SubscriberLeave:
		{
			auto payload = static_cast<SubscriberRequestPayload*>(event.payload.get());
			removePubSub(payload->object);
			payload->promise.set_value();
			break;
		}
		case ECommunicationEvent::SubscriberNewData:
		{
			auto payload = static_cast<SubscriberPayload*>(event.payload.get());
			auto subscriber = static_cast<SubscriberBase*>(payload->object);
			subscriber->popAndDoCallback();
			break;
		}
		case ECommunicationEvent::Stop:
		{
			auto payload = static_cast<Stoppayload*>(event.payload.get());
			allPublisherStop();
			allSubscriberStop();
			payload->promise.set_value();
			parent_t::stop();
			return false;
		}
		}

		return true;
	}

	void Node::removePubSub(PubSubBase* ptr) {
		ptr->type() == EPubSub::Publisher ?
			removeFromList(publishers_, ptr) :
			removeFromList(subscirbers_, static_cast<SubscriberBase*>(ptr));
	}

	void Node::addPubSub(PubSubBase* ptr) {
		ptr->type() == EPubSub::Publisher ?
			addToList(publishers_, ptr) :
			addToList(subscirbers_, static_cast<SubscriberBase*>(ptr));
	}

	void Node::allPublisherStop() {
		for (auto& pub : publishers_) {
			pub->stop();
		}
	}

	void Node::allSubscriberStop() {
		for (auto& sub : subscirbers_) {
			sub->stop();
		}
	}

	PubSubBase::PubSubBase(node_ptr_t node, EPubSub type)
		: node_(std::move(node)), type_(type) {
		node_->addPubSub(this);
	}

	PubSubBase::~PubSubBase() {
		auto payload = std::make_unique<PublisherRequestPayload>();
		payload->object = this;
		auto future = node_->request<ECommunicationEvent::PublisherLeave>(std::move(payload));

		future.get();
	};
}