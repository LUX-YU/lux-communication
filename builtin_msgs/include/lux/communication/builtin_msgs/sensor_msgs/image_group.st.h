#pragma once
#include "image.st.h"
#include <vector>
#include <array>

namespace lux::communication::builtin_msgs::sensor_msgs
{
	using ImageGroupS = std::vector<ImageS>;
	using ImagePairS  = std::array<ImageS, 2>;
}
