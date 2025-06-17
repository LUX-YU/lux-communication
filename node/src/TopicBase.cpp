#include "lux/communication/TopicBase.hpp"
#include "lux/communication/Domain.hpp"

namespace lux::communication
{
	TopicBase::~TopicBase()
	{
		if (domain_)
		{
			domain_->removeTopic(this);
		}
	}
} // namespace lux::communication::intraprocess
