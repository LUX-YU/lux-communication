#pragma once
#include "lux/communication/builtin_msgs/common_msgs/vector3.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct AccelS
	{
		common_msgs::Vector3S linear;
		common_msgs::Vector3S angular;
	};
}
