#pragma once
#include "point.st.h"
#include "quaternion.st.h"

namespace lux::communication::builtin_msgs::geometry_msgs
{
	struct PoseS
	{
		PointS		position;
		QuaternionS	orientation;
	};
}
