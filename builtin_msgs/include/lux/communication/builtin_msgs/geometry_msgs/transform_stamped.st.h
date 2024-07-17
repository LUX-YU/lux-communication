#pragma once
#include "transform.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct TransformStampedS
	{
		common_msgs::TimestampS timestamp;
		TransformS				transform;
	};
}