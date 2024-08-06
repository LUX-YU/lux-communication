#pragma once
#include <lux/communication/visibility.h>

#include "lux/communication/builtin_msgs/common_msgs/matrix3.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/transform.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct CameraParametersS
	{
		common_msgs::Matrix3S		intrinsics;
		geometry_msgs::TransformS	extrinsics;
	};
}
