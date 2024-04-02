#include "lux/communication/builtin_msgs/sensor_msgs/image.st.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace lux::communication::builtin_msgs::sensor_msgs
{
	ImageS::ImageS()
	{
		_width = 0;
		_height = 0;
		_channels = 0;
		_data = nullptr;
	}

	ImageS::ImageS(const char* path)
	{
		_data = stbi_load(
			path,
			&_width,
			&_height,
			&_channels,
			0
		);

		if (!_data)
		{
			_width	  = 0;
			_height   = 0;
			_channels = 0;
		}
	}

	ImageS::ImageS(int width, int height, int channels, const void* data)
	{
		_width		= width;
		_height		= height;
		_channels	= channels;
		size_t image_size = _width * _height * _channels;

		_data = STBI_MALLOC(image_size);
		memcpy(_data, data, image_size);
	}

	ImageS::ImageS(const ImageS& other)
	{
		_width		= other._width;
		_height		= other._height;
		_channels	= other._channels;
		size_t image_size = _width * _height * _channels;

		_data = STBI_MALLOC(image_size);
		memcpy(_data, other.data(), image_size);
	}

	ImageS& ImageS::operator=(const ImageS& other)
	{
		size_t image_size = other._width * other._height * other._channels;
		if (_width == other._width && _height == other._height && _channels == other._channels)
		{
			if (!_data)
			{
				_data = STBI_MALLOC(image_size);
			}
			memcpy(_data, other.data(), image_size);
			return *this;
		}

		_width		= other._width;
		_height		= other._height;
		_channels	= other._channels;

		if (_data)
		{
			STBI_FREE(_data);
		}
		_data = STBI_MALLOC(image_size);

		memcpy(_data, other.data(), _width * _height * _channels);
		return *this;
	}

	ImageS::ImageS(ImageS&& other) noexcept
	{
		if (_data)
		{
			STBI_FREE(_data);
		}
		_width		= other._width;
		_height		= other._height;
		_channels	= other._channels;
		_data		= other._data;

		other._width	= 0;
		other._height	= 0;
		other._channels = 0;
		other._data		= nullptr;
	}

	ImageS& ImageS::operator=(ImageS&& other) noexcept
	{
		if (_data)
		{
			STBI_FREE(_data);
		}

		_width	  = other._width;
		_height	  = other._height;
		_channels = other._channels;
		_data	  = other._data;

		other._width	= 0;
		other._height	= 0;
		other._channels = 0;
		other._data		= nullptr;

		return *this;
	}

	ImageS::~ImageS()
	{
		stbi_image_free(_data);
	}

	bool ImageS::isLoaded() const
	{
		return !_data;
	}

	int ImageS::width() const
	{
		return _width;
	}

	int ImageS::height() const
	{
		return _height;
	}

	int ImageS::channels() const
	{
		return _channels;
	}

	const void* ImageS::data() const
	{
		return _data;
	}

	void* ImageS::data()
	{
		return _data;
	}
}

#include <lux/communication/pb_st_converter.hpp>

namespace lux::communication
{
	namespace
	{
		using PBImage = builtin_msgs::sensor_msgs::Image;
		using STImage = builtin_msgs::sensor_msgs::ImageS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImage& in, STImage& out)
	{
		size_t image_size = in.height() * in.width() * in.channels();
		if (out.width() == in.width() && out.height() == in.height() && out.channels() == in.channels())
		{
			if (!out._data)
			{
				out._data = STBI_MALLOC(image_size);
			}
			memcpy(out._data, in.data().data(), image_size);
			return;
		}
		out._width		= in.width();
		out._height		= in.height();
		out._channels	= in.channels();

		if (out._data)
		{
			STBI_FREE(out._data);
		}
		out._data = STBI_MALLOC(image_size);

		memcpy(out._data, in.data().data(), image_size);
	}

	template<> LUX_COMMUNICATION_PUBLIC  void pb_st_converter::st2pb(const STImage& in, PBImage& out)
	{
		size_t image_size = in.height() * in.width() * in.channels();
		if (out.width() == in.width() && out.height() == in.height() && out.channels() == in.channels())
		{
			memcpy(out.mutable_data()->data(), in._data, image_size);
			return;
		}
		out.set_width(in._width);
		out.set_height(in._height);
		out.set_channels(in._channels);
		out.mutable_data()->resize(image_size);
		
		memcpy(out.mutable_data()->data(), in._data, image_size);
	}
}
