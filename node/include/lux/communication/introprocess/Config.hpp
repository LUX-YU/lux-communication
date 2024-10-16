#pragma once
#include <memory>

namespace lux::communication::introprocess {
#ifdef SHARED_MESSAGE_MODE
#	define make_message(type, ...) std::make_shared<type>(__VA_ARGS__)
	template<typename T> using message_t = std::shared_ptr<T>;
	template<T>
	static inline message_t<T> msg_copy_or_reference(message_t<T>& msg) {
		return msg;
	}
#else
#	define make_message(type, ...) std::make_unique<type>(__VA_ARGS__)
	template<typename T> using message_t = std::unique_ptr<T>;
	template<typename T>
	static inline message_t<T> msg_copy_or_reference(message_t<T>& msg) {
		return std::make_unique<T>(*msg);
	}
#endif
}