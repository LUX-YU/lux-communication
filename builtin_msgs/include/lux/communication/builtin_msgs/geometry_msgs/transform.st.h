#pragma once
#include "quaternion.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct TransformS
	{
		Eigen::Vector3d	translation;
		QuaternionS		rotation;
	};
}
