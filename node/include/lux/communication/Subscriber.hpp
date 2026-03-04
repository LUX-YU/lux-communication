#pragma once
/// Public API: lux::communication::Subscriber<T>
///
/// Created via Node::createSubscriber<T>(topic_name, callback, ...).
/// Transparently receives messages over intra-process / SHM / network.

#include <lux/communication/unified/Subscriber.hpp>
