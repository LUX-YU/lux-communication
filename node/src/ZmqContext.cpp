#include "lux/communication/interprocess/ZmqPubSub.hpp"

namespace lux::communication::interprocess {
zmq::context_t& globalContext() {
    static zmq::context_t ctx{1};
    return ctx;
}
}
