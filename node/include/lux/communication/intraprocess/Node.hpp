#pragma once

#include <lux/communication/core/NodeInterface.hpp>

namespace lux::communication::intraprocess
{
	class Node : public lux::communication::NodeInterface
    {
    public:
        explicit Node(std::string name)
            : NodeInterface(std::move(name), ENodeType::INTRAPROCESS){ }
    };
}
