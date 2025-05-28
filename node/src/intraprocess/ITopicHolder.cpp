#include "lux/communication/ITopicHolder.hpp"
#include "lux/communication/Domain.hpp"

namespace lux::communication
{
	ITopicHolder::~ITopicHolder()
	{
		if (domain_)
		{
			domain_->removeTopic(this);
		}
	}
} // namespace lux::communication::intraprocess
