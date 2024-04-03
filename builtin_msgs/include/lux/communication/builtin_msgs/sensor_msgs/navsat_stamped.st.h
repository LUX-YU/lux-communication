#pragma once
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/navsat.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct NavsatStampedS
	{
		common_msgs::TimestampS  timestamp;
		NavsatS					 navsat;
	};
}
