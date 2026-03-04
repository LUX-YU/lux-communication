#pragma once
/// Public API: lux::communication::Publisher<T>
///
/// Created via Node::createPublisher<T>(topic_name, opts).
/// Transparently routes messages over intra-process / SHM / network.

#include <lux/communication/unified/Publisher.hpp>
