#pragma once
/// Serializer — compile-time dispatch to the optimal serialisation strategy.
///
///   TriviallyCopyableMsg  →  raw memcpy   (zero-copy for POD structs)
///   ProtobufMessage       →  protobuf     (SerializeToArray / ParseFromArray)
///   CustomSerializable    →  ADL free-fns (lux_serialize / lux_deserialize)

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <lux/communication/transport/FrameHeader.hpp>

namespace lux::communication::serialization
{
    // ════════════════════════════════════════════════════════════
    //  Concepts
    // ════════════════════════════════════════════════════════════
    /// POD-like message: can be transferred with a plain memcpy.
    template <typename T>
    concept TriviallyCopyableMsg =
        std::is_trivially_copyable_v<T> &&
        std::is_standard_layout_v<T> &&
        !std::is_pointer_v<T>;

    /// Protobuf message: duck-typed by ByteSizeLong / SerializeToArray / ParseFromArray.
    template <typename T>
    concept ProtobufMessage = requires(T t, const T ct) {
        { ct.ByteSizeLong() } -> std::convertible_to<size_t>;
        { ct.SerializeToArray(static_cast<void *>(nullptr), int{}) } -> std::same_as<bool>;
        { t.ParseFromArray(static_cast<const void *>(nullptr), int{}) } -> std::same_as<bool>;
    };

    /// User-defined serialisable type: provide free functions found via ADL.
    ///   size_t lux_serialize   (const T&, void* buf, size_t max);
    ///   bool   lux_deserialize (T&, const void* buf, size_t len);
    ///   size_t lux_serialized_size(const T&);
    template <typename T>
    concept CustomSerializable = requires(const T &ct, T &t,
                                          void *buf, const void *cbuf, size_t sz) {
        { lux_serialize(ct, buf, sz) } -> std::convertible_to<size_t>;
        { lux_deserialize(t, cbuf, sz) } -> std::same_as<bool>;
        { lux_serialized_size(ct) } -> std::convertible_to<size_t>;
    };

    /// Compile-time format selector.
    template <typename T>
    constexpr transport::SerializationFormat selectSerializationFormat()
    {
        using enum transport::SerializationFormat;
        if constexpr (TriviallyCopyableMsg<T>)
            return RawMemcpy;
        else if constexpr (ProtobufMessage<T>)
            return Protobuf;
        else if constexpr (CustomSerializable<T>)
            return Custom;
        else
            static_assert(sizeof(T) == 0,
                          "Type T is not serializable. Make it trivially copyable, "
                          "provide protobuf ByteSizeLong/SerializeToArray/ParseFromArray, "
                          "or supply ADL functions lux_serialize / lux_deserialize / lux_serialized_size.");
    }

    // ════════════════════════════════════════════════════════════
    //  Primary template (never instantiated — specialisations below)
    // ════════════════════════════════════════════════════════════

    template <typename T, typename = void>
    struct Serializer;

    /// Concept: true when Serializer<T> is defined (i.e. T is serialisable).
    template <typename T>
    concept HasSerializer = requires(const T &ct) {
        { Serializer<T>::serializedSize(ct) } -> std::convertible_to<size_t>;
    };

    // ════════════════════════════════════════════════════════════
    //  Specialisation: trivially copyable → raw memcpy
    // ════════════════════════════════════════════════════════════

    template <TriviallyCopyableMsg T>
    struct Serializer<T>
    {
        static constexpr auto format = transport::SerializationFormat::RawMemcpy;

        static size_t serializedSize(const T & /*msg*/)
        {
            return sizeof(T);
        }

        static size_t serialize(const T &msg, void *buffer, size_t max_len)
        {
            if (max_len < sizeof(T))
                return 0;
            std::memcpy(buffer, &msg, sizeof(T));
            return sizeof(T);
        }

        static bool deserialize(T &out, const void *buffer, size_t len)
        {
            if (len < sizeof(T))
                return false;
            std::memcpy(&out, buffer, sizeof(T));
            return true;
        }
    };

    // ════════════════════════════════════════════════════════════
    //  Specialisation: Protobuf
    // ════════════════════════════════════════════════════════════

    template <ProtobufMessage T>
    struct Serializer<T>
    {
        static constexpr auto format = transport::SerializationFormat::Protobuf;

        static size_t serializedSize(const T &msg)
        {
            return msg.ByteSizeLong();
        }

        static size_t serialize(const T &msg, void *buffer, size_t max_len)
        {
            const size_t sz = msg.ByteSizeLong();
            if (sz > max_len)
                return 0;
            msg.SerializeToArray(buffer, static_cast<int>(sz));
            return sz;
        }

        static bool deserialize(T &out, const void *buffer, size_t len)
        {
            return out.ParseFromArray(buffer, static_cast<int>(len));
        }
    };

    // ════════════════════════════════════════════════════════════
    //  Specialisation: Custom (ADL free functions)
    // ════════════════════════════════════════════════════════════

    template <CustomSerializable T>
    struct Serializer<T>
    {
        static constexpr auto format = transport::SerializationFormat::Custom;

        static size_t serializedSize(const T &msg)
        {
            return lux_serialized_size(msg);
        }

        static size_t serialize(const T &msg, void *buffer, size_t max_len)
        {
            return lux_serialize(msg, buffer, max_len);
        }

        static bool deserialize(T &out, const void *buffer, size_t len)
        {
            return lux_deserialize(out, buffer, len);
        }
    };

} // namespace lux::communication::serialization
