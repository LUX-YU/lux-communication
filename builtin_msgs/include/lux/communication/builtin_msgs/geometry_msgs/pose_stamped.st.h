#pragma once
#include "pose.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct PoseStampedS
	{
		common_msgs::TimestampS timestamp;
		PoseS					pose;
	};
}