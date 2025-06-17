#pragma once
#include <lux/communication/Registry.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class NodeBase;
    class TopicBase;

    using topic_registry_t = QueryableRegistry<TopicBase>;
    using node_registry_t  = Registry<NodeBase>;

    using topic_handle_t   = typename topic_registry_t::Handle;
    using node_handle_t    = typename node_registry_t::Handle;

    class LUX_COMMUNICATION_PUBLIC Domain
    {
        friend class NodeBase;
    public:
        explicit Domain(size_t id)
            : domain_id_(id){}

        size_t id() const
        {
            return domain_id_;
        }

        topic_handle_t createOrGetTopic(const std::string& name);
        node_handle_t  assignNode(const std::string& name);

        static inline Domain& default_domain()
        {
            static Domain default_domain_instance{ 0 };
            return default_domain_instance;
        }

    private:
        size_t           domain_id_;
        node_registry_t  node_reg_;
        topic_registry_t topic_reg_;
    };
}
