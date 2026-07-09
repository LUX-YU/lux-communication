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
		// element_size = bytes per channel element (1=8U, 2=16U, 4=32F).
		ImageS(int width, int height, int channels, int element_size, const void* data);
		ImageS(const ImageS&);
		ImageS& operator=(const ImageS&);
		ImageS(ImageS&&) noexcept;
		ImageS& operator=(ImageS&&) noexcept;
		~ImageS();

		bool load(const char* path);
		// Load preserving native bit depth: 16-bit PNGs stay 16-bit (elementSize()==2),
		// everything else loads as 8-bit (elementSize()==1). Zero-copy: adopts the stb buffer.
		bool loadNative(const char* path);
		bool isLoaded() const;

		int width() const;
		int height() const;
		int channels() const;
		/// bytes per channel element (1=8U, 2=16U, 4=32F). 1 for legacy 8-bit images.
		int elementSize() const;
		const void* data() const;
		void* data();

	private:
		int		_width{ 0 };
		int		_height{ 0 };
		int		_channels{ 0 };
		int		_element_size{ 1 };   // bytes per channel element (1=8U,2=16U,4=32F)
		void*	_data{ nullptr };
	};
}
