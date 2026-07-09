#include "lux/communication/builtin_msgs/pb_st_converter.hpp"
#include "lux/communication/builtin_msgs/sensor_msgs/image.st.h"
#include "lux/communication/builtin_msgs/sensor_msgs/image.pb.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <fstream>
#include <iterator>
#include <vector>

// libjpeg-turbo is optional (LUX_HAVE_LIBJPEG defined by CMake when found). When absent, the
// JPEG path falls back to the bundled stb_image decoder below.
#ifdef LUX_HAVE_LIBJPEG
#include <jpeglib.h>
#include <csetjmp>
#include <cstdio>
#endif

namespace lux::communication::builtin_msgs::sensor_msgs
{
	namespace
	{
		// Read a whole file into memory (one IO; shared by magic sniffing +
		// libjpeg/stb from-memory decode).
		std::vector<unsigned char> readAllBytes(const char* path)
		{
			std::ifstream f(path, std::ios::binary);
			return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
											   std::istreambuf_iterator<char>());
		}

#ifdef LUX_HAVE_LIBJPEG
		// libjpeg errors return via setjmp instead of the default exit().
		struct JpegErrMgr { jpeg_error_mgr pub; std::jmp_buf jmp; };
		void jpegErrorExit(j_common_ptr cinfo)
		{
			std::longjmp(reinterpret_cast<JpegErrMgr*>(cinfo->err)->jmp, 1);
		}

		// Decode a JPEG with libjpeg-turbo into a STBI_MALLOC buffer (grayscale JPEG->1ch,
		// color->3ch RGB, matching stbi_load(...,0)'s native-channel behavior).
		// Key settings = OpenCV cv::imread defaults (JDCT_ISLOW exact IDCT + fancy upsampling
		// + JCS_RGB) -> bit-identical to cv::imread (measured 0 diff). Returns nullptr on failure.
		// The buffer is STBI_MALLOC'd so the existing stbi_image_free in the destructor frees
		// it correctly (same malloc/free family).
		unsigned char* decodeJpegTurbo(const unsigned char* buf, size_t size,
									   int* out_w, int* out_h, int* out_ch)
		{
			jpeg_decompress_struct cinfo{};
			JpegErrMgr jerr{};
			cinfo.err = jpeg_std_error(&jerr.pub);
			jerr.pub.error_exit = jpegErrorExit;
			if (setjmp(jerr.jmp)) { jpeg_destroy_decompress(&cinfo); return nullptr; }

			jpeg_create_decompress(&cinfo);
			jpeg_mem_src(&cinfo, buf, static_cast<unsigned long>(size));
			jpeg_read_header(&cinfo, TRUE);

			// Explicitly align with OpenCV defaults (also libjpeg's defaults; pinned here in
			// case libjpeg changes its defaults):
			cinfo.dct_method          = JDCT_ISLOW;   // exact integer IDCT
			cinfo.do_fancy_upsampling = TRUE;         // high-quality chroma upsampling
			const bool gray = (cinfo.jpeg_color_space == JCS_GRAYSCALE);
			cinfo.out_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;

			jpeg_start_decompress(&cinfo);
			const int width  = static_cast<int>(cinfo.output_width);
			const int height = static_cast<int>(cinfo.output_height);
			const int comps  = cinfo.output_components;   // 1 (gray) or 3 (RGB)
			auto* out = static_cast<unsigned char*>(
				STBI_MALLOC(static_cast<size_t>(width) * height * comps));
			if (!out) { jpeg_destroy_decompress(&cinfo); return nullptr; }

			const int stride = width * comps;
			while (cinfo.output_scanline < cinfo.output_height)
			{
				unsigned char* row = out + static_cast<size_t>(cinfo.output_scanline) * stride;
				jpeg_read_scanlines(&cinfo, &row, 1);
			}
			jpeg_finish_decompress(&cinfo);
			jpeg_destroy_decompress(&cinfo);

			*out_w = width; *out_h = height; *out_ch = comps;
			return out;   // RGB (or grayscale), STBI_MALLOC'd
		}

		inline bool isJpeg(const std::vector<unsigned char>& b)
		{
			return b.size() >= 2 && b[0] == 0xFF && b[1] == 0xD8;   // SOI marker
		}
#endif // LUX_HAVE_LIBJPEG
	} // namespace

	ImageS::ImageS(){}

	ImageS::ImageS(const char* path)
	{
		load(path);
	}

	ImageS::ImageS(int width, int height, int channels, const void* data)
	{
		_width = width;
		_height = height;
		_channels = channels;
		_element_size = 1;
		size_t image_size = (size_t)_width * _height * _channels * _element_size;

		_data = STBI_MALLOC(image_size);
		memcpy(_data, data, image_size);
	}

	ImageS::ImageS(int width, int height, int channels, int element_size, const void* data)
	{
		_width = width;
		_height = height;
		_channels = channels;
		_element_size = element_size;
		size_t image_size = (size_t)_width * _height * _channels * _element_size;

		_data = STBI_MALLOC(image_size);
		memcpy(_data, data, image_size);
	}

	ImageS::ImageS(const ImageS& other)
	{
		_width = other._width;
		_height = other._height;
		_channels = other._channels;
		_element_size = other._element_size;
		size_t image_size = (size_t)_width * _height * _channels * _element_size;

		_data = STBI_MALLOC(image_size);
		memcpy(_data, other.data(), image_size);
	}

	ImageS& ImageS::operator=(const ImageS& other)
	{
		size_t image_size = (size_t)other._width * other._height * other._channels * other._element_size;
		if (_width == other._width && _height == other._height && _channels == other._channels && _element_size == other._element_size)
		{
			if (!_data)
			{
				_data = STBI_MALLOC(image_size);
			}
			memcpy(_data, other.data(), image_size);
			return *this;
		}

		_width = other._width;
		_height = other._height;
		_channels = other._channels;
		_element_size = other._element_size;

		if (_data)
		{
			STBI_FREE(_data);
		}
		_data = STBI_MALLOC(image_size);

		memcpy(_data, other.data(), image_size);
		return *this;
	}

	ImageS::ImageS(ImageS&& other) noexcept
	{
		_width = other._width;
		_height = other._height;
		_channels = other._channels;
		_element_size = other._element_size;
		_data = other._data;

		other._width = 0;
		other._height = 0;
		other._channels = 0;
		other._element_size = 1;
		other._data = nullptr;
	}

	ImageS& ImageS::operator=(ImageS&& other) noexcept
	{
		if (_data)
		{
			STBI_FREE(_data);
		}

		_width = other._width;
		_height = other._height;
		_channels = other._channels;
		_element_size = other._element_size;
		_data = other._data;

		other._width = 0;
		other._height = 0;
		other._channels = 0;
		other._element_size = 1;
		other._data = nullptr;

		return *this;
	}

	bool ImageS::load(const char* path)
	{
		// already has data
		if (_data) {
			stbi_image_free(_data);
			_data = nullptr;
		}

		// Read once. When libjpeg is available, FF D8 -> libjpeg-turbo (bit-identical to
		// cv::imread); otherwise everything (incl. JPEG) goes through stb.
		std::vector<unsigned char> file = readAllBytes(path);
#ifdef LUX_HAVE_LIBJPEG
		if (isJpeg(file))
		{
			_data = decodeJpegTurbo(file.data(), file.size(), &_width, &_height, &_channels);
			if (_data) { _element_size = 1; return true; }
			// JPEG decode failed -> fall through to the stb fallback below.
		}
#endif

		_data = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
									  &_width, &_height, &_channels, 0);
		if (!_data)
		{
			_width = 0;
			_height = 0;
			_channels = 0;
			return false;
		}

		_element_size = 1;
		return true;
	}

	bool ImageS::loadNative(const char* path)
	{
		// already has data
		if (_data) {
			stbi_image_free(_data);
			_data = nullptr;
		}

		std::vector<unsigned char> file = readAllBytes(path);
#ifdef LUX_HAVE_LIBJPEG
		// JPEG is always 8-bit -> libjpeg-turbo (bit-identical to cv::imread) when available.
		if (isJpeg(file))
		{
			_data = decodeJpegTurbo(file.data(), file.size(), &_width, &_height, &_channels);
			if (_data) { _element_size = 1; return true; }
			// JPEG decode failed -> fall through to the stb path below.
		}
#endif

		// Preserve native bit depth: 16-bit PNGs stay 16-bit (byte-identical to old stbi_load_16).
		if (stbi_is_16_bit_from_memory(file.data(), static_cast<int>(file.size())))
		{
			_data = stbi_load_16_from_memory(file.data(), static_cast<int>(file.size()),
											 &_width, &_height, &_channels, 0);
			_element_size = 2;
		}
		else
		{
			_data = stbi_load_from_memory(file.data(), static_cast<int>(file.size()),
										  &_width, &_height, &_channels, 0);
			_element_size = 1;
		}

		if (!_data)
		{
			_width = 0;
			_height = 0;
			_channels = 0;
			_element_size = 1;
			return false;
		}

		return true;
	}

	ImageS::~ImageS()
	{
		if (_data) {
			stbi_image_free(_data);
		}
	}

	bool ImageS::isLoaded() const
	{
		return _data;
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

	int ImageS::elementSize() const
	{
		return _element_size;
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

namespace lux::communication::builtin_msgs
{
	namespace
	{
		using PBImage = builtin_msgs::sensor_msgs::Image;
		using STImage = builtin_msgs::sensor_msgs::ImageS;
	}

	template<> LUX_COMMUNICATION_PUBLIC void pb_st_converter::pb2st(const PBImage& in, STImage& out)
	{
		int es = in.element_size() > 0 ? in.element_size() : 1;
		size_t image_size = (size_t)in.height() * in.width() * in.channels() * es;
		if (out.width() == in.width() && out.height() == in.height() && out.channels() == in.channels() && out.elementSize() == es)
		{
			if (!out._data)
			{
				out._data = STBI_MALLOC(image_size);
			}
			memcpy(out._data, in.data().data(), image_size);
			return;
		}
		out._width = in.width();
		out._height = in.height();
		out._channels = in.channels();
		out._element_size = es;

		if (out._data)
		{
			STBI_FREE(out._data);
		}
		out._data = STBI_MALLOC(image_size);

		memcpy(out._data, in.data().data(), image_size);
	}

	template<> LUX_COMMUNICATION_PUBLIC  void pb_st_converter::st2pb(const STImage& in, PBImage& out)
	{
		size_t image_size = (size_t)in.height() * in.width() * in.channels() * in.elementSize();
		if (out.width() == in.width() && out.height() == in.height() && out.channels() == in.channels() && out.element_size() == in.elementSize())
		{
			memcpy(out.mutable_data()->data(), in._data, image_size);
			return;
		}
		out.set_width(in._width);
		out.set_height(in._height);
		out.set_channels(in._channels);
		out.set_element_size(in._element_size);
		out.mutable_data()->resize(image_size);

		memcpy(out.mutable_data()->data(), in._data, image_size);
	}
}
