#pragma once
#include "image.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImageStampedS
	{
		common_msgs::TimestampS timestamp;
		ImageS					image;
	};
}

