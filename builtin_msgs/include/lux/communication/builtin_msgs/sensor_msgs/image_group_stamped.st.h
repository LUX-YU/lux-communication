#pragma once
#include "image_group.st.h"
#include "lux/communication/builtin_msgs/common_msgs/timestamp.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct ImageGroupStampedS
	{
		common_msgs::TimestampS timestamp;
		ImageGroupS				images;
	};

	struct ImagePairStampedS
	{
		common_msgs::TimestampS timestamp;
		ImagePairS				images;
	};
}
