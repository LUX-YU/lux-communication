#pragma once
#include <lux/communication/visibility.h>

#include "lux/communication/builtin_msgs/common_msgs/matrix3.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector2.st.h"
#include "lux/communication/builtin_msgs/common_msgs/vector5.st.h"
#include "lux/communication/builtin_msgs/geometry_msgs/transform.st.h"

namespace lux::communication::builtin_msgs::sensor_msgs
{
	struct CameraParametersS
	{
		common_msgs::Vector2S		resolution;
		common_msgs::Matrix3S		intrinsics;
		geometry_msgs::TransformS	extrinsics;
		common_msgs::Vector5S		distortion;

		double fx() const noexcept
		{
			return intrinsics(0, 0);
		}

		double fy() const noexcept
		{
			return intrinsics(1, 1);
		}

		double cx() const noexcept
		{
			return intrinsics(0, 2);
		}

		double cy() const noexcept
		{
			return intrinsics(1, 2);
		}
	};
}
