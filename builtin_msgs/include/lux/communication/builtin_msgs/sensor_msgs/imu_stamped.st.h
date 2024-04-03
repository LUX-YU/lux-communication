#pragma once
#include "imu.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImuStampedS
	{
		common_msgs::TimestampS timestamp;
		ImuS					imu;
	};
}
