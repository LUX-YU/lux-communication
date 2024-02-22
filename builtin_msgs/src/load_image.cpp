#include "lux/communication/builtin_msgs/load_image.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image_stamped.pb.h"
#include "lux/communication/builtin_msgs/timestamp_tools.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace lux::communication::builtin_msgs
{
	bool load_image(sensor_msgs::Image& image, const char* path)
	{
		int width, height, channels;
		auto data = stbi_load(
			path,
			&width,
			&height,
			&channels,
			0
		);

		if (!data)
			return false;

		image.set_channels(channels);
		image.set_width(width);
		image.set_height(height);
		image.set_data(data, width * height * channels);

		stbi_image_free(data);

		return true;
	}

	bool load_image(sensor_msgs::ImageStamped& image_stamped, const char* path, uint64_t timestamp)
	{
		set_timestamp(*image_stamped.mutable_timestamp(), timestamp);
		return load_image(*image_stamped.mutable_image(), path);
	}

	void image_free(sensor_msgs::Image& image)
	{
		stbi_image_free(image.mutable_data());
	}
}