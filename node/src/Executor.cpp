#include "lux/communication/introprocess/Executor.hpp"
#include "lux/communication/introprocess/Node.hpp"

namespace lux::communication::introprocess {
Executor::~Executor() { stop(); }
void Executor::addNode(std::shared_ptr<Node> node) {
    if (!node) return;
    auto default_group = node->getDefaultCallbackGroup();
    if (default_group) {
        addCallbackGroup(default_group);
    }
}
}
