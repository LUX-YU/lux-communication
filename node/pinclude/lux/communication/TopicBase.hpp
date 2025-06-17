#pragma once

#include <string>
#include <typeindex>
#include <atomic>
#include <lux/cxx/compile_time/type_info.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
    class Domain;
    class LUX_COMMUNICATION_PUBLIC TopicBase
    {
    public:
		friend class Domain;

		TopicBase() = default;

        virtual ~TopicBase();

		const std::string& name() const
		{
			return topic_name_;
		}

		// Get the Domain that owns this Topic
		const Domain* domain() const
		{
			return domain_;
		}

		size_t index() const
		{
			return index_;
		}

		// Get the type info for this Topic
		const lux::cxx::basic_type_info& typeInfo() const
		{
			return type_info_;
		}

    protected:
		void setDomain(Domain* domain)
		{
			domain_ = domain;
		}

		void setTopicName(const std::string& name)
		{
			topic_name_ = name;
		}

		void setIndex(size_t index)
		{
			index_ = index;
		}

		// Set the type info for this Topic
		void setTypeInfo(const lux::cxx::basic_type_info& type_info)
		{
			type_info_ = type_info;
		}

    protected:
		lux::cxx::basic_type_info	type_info_;
		size_t						index_{ 0 };
		std::string					topic_name_;
		Domain*						domain_{ nullptr }; // Pointer to the Domain that owns this Topic
    };
}
