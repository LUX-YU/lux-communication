#pragma once
/// Public API: lux::communication::Node
///
/// A single Node can create Publishers and Subscribers that transparently
/// communicate over intra-process (shared_ptr), shared memory, or network
/// depending on where the remote peer is located.
///
/// Usage:
///   lux::communication::Node node("my_node");
///   auto pub = node.createPublisher<MyMsg>("topic");
///   auto sub = node.createSubscriber<MyMsg>("topic", callback);

#include <lux/communication/unified/Node.hpp>
