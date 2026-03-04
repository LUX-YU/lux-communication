#pragma once
/// MessageTraits — compile-time policy for message storage and callback types.
///
/// SmallValueMsg<T>:
///   Trivially-copyable, standard-layout types with sizeof(T) <= threshold
///   are passed by value through the intra-process pipeline, completely
///   avoiding shared_ptr heap allocation and atomic refcount overhead.
///
/// For types that do NOT satisfy SmallValueMsg, the existing shared_ptr<T>
/// path is used — zero-copy multi-subscriber distribution via refcounting.

#include <concepts>
#include <type_traits>
#include <memory>
#include <functional>

namespace lux::communication {

#ifndef LUX_SMALL_VALUE_MSG_THRESHOLD
#   define LUX_SMALL_VALUE_MSG_THRESHOLD 128
#endif

/// Concept: small, trivially-copyable types that can be passed by value
/// through the pub/sub pipeline without shared_ptr overhead.
template<typename T>
concept SmallValueMsg =
    std::is_trivially_copyable_v<T> &&
    std::is_standard_layout_v<T> &&
    (sizeof(T) <= LUX_SMALL_VALUE_MSG_THRESHOLD) &&
    !std::is_pointer_v<T>;

// ── Conditional type aliases ──

/// What gets stored internally in OrderedItem / queues.
///   SmallValueMsg  →  T           (value, zero heap alloc)
///   otherwise      →  shared_ptr<T>
template<typename T>
using stored_msg_t = std::conditional_t<SmallValueMsg<T>, T, std::shared_ptr<T>>;

/// What the user callback receives.
///   SmallValueMsg  →  const T&          (pass by reference, no alloc)
///   otherwise      →  std::shared_ptr<T>
template<typename T>
using callback_arg_t = std::conditional_t<SmallValueMsg<T>, const T&, std::shared_ptr<T>>;

} // namespace lux::communication
