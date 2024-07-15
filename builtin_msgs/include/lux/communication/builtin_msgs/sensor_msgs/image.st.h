#pragma once
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs
{
	class pb_st_converter;
}

namespace lux::communication::builtin_msgs::sensor_msgs
{
	class ImageS
	{
	public:
		friend class ::lux::communication::builtin_msgs::pb_st_converter;
		LUX_COMMUNICATION_PUBLIC ImageS();
		LUX_COMMUNICATION_PUBLIC ImageS(const char* path);
		// copy
		LUX_COMMUNICATION_PUBLIC ImageS(int width, int height, int channels, const void* data);
		LUX_COMMUNICATION_PUBLIC ImageS(const ImageS&);
		LUX_COMMUNICATION_PUBLIC ImageS& operator=(const ImageS&);
		LUX_COMMUNICATION_PUBLIC ImageS(ImageS&&) noexcept;
		LUX_COMMUNICATION_PUBLIC ImageS& operator=(ImageS&&) noexcept;
		LUX_COMMUNICATION_PUBLIC ~ImageS();

		LUX_COMMUNICATION_PUBLIC bool load(const char* path);
		LUX_COMMUNICATION_PUBLIC bool isLoaded() const;

		LUX_COMMUNICATION_PUBLIC int width() const;
		LUX_COMMUNICATION_PUBLIC int height() const;
		LUX_COMMUNICATION_PUBLIC int channels() const;
		LUX_COMMUNICATION_PUBLIC const void* data() const;
		LUX_COMMUNICATION_PUBLIC void* data();

	private:
		int		_width;
		int		_height;
		int		_channels;
		void*	_data;
	};
}
