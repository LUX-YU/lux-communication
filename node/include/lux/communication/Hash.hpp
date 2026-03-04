#pragma once
/// Lightweight hash utilities shared across the library.
/// Delegates to lux::cxx::algorithm::fnv1a from the lux-cxx algorithm component.

#include <lux/cxx/algorithm/hash.hpp>
#include <string>
#include <string_view>

namespace lux::communication {

/// FNV-1a 64-bit hash.
inline uint64_t fnv1a_64(std::string_view sv) {
    return lux::cxx::algorithm::fnv1a(sv);
}

inline uint64_t fnv1a_64(const char* data, size_t len) {
    return lux::cxx::algorithm::fnv1a(std::string_view(data, len));
}

inline uint64_t fnv1a_64(const std::string& str) {
    return lux::cxx::algorithm::fnv1a(std::string_view(str));
}

} // namespace lux::communication
