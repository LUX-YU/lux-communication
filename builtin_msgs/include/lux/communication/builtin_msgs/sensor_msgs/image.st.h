#pragma once
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs
{
	class pb_st_converter;
}

namespace lux::communication::builtin_msgs::sensor_msgs
{
	class LUX_COMMUNICATION_PUBLIC ImageS
	{
	public:
		friend class ::lux::communication::builtin_msgs::pb_st_converter;
		ImageS();
		explicit ImageS(const char* path);
		// copy
		ImageS(int width, int height, int channels, const void* data);
		ImageS(const ImageS&);
		ImageS& operator=(const ImageS&);
		ImageS(ImageS&&) noexcept;
		ImageS& operator=(ImageS&&) noexcept;
		~ImageS();

		bool load(const char* path);
		bool isLoaded() const;

		int width() const;
		int height() const;
		int channels() const;
		const void* data() const;
		void* data();

	private:
		int		_width{ 0 };
		int		_height{ 0 };
		int		_channels{ 0 };
		void*	_data{ nullptr };
	};
}
