#pragma once
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs
{
	namespace sensor_msgs
	{
		class Image;
		class ImageStamped;
	}

	LUX_COMMUNICATION_PUBLIC bool load_image(sensor_msgs::Image&, const char* path);

	LUX_COMMUNICATION_PUBLIC bool load_image(sensor_msgs::ImageStamped&, const char* path, uint64_t timestamp);

	LUX_COMMUNICATION_PUBLIC void image_free(sensor_msgs::Image&);
}