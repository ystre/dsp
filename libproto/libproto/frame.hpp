/**
 * Benchmark wire protocol: frame header (Contract, see doc/protocol.adoc).
 *
 * Phase 1: header layout, encode and decode only. The real codec (in-place
 * frame iteration over receive buffers, batching) is Phase 2.
 */

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace proto {

inline constexpr std::uint16_t magic  = 0xD5F0;
inline constexpr std::uint8_t version = 1;

enum class msg_type : std::uint8_t {
    data = 1,           ///< Sink: the server consumes it, no reply.
    echo_req = 2,       ///< The server replies with `echo_rsp` carrying the same payload.
    echo_rsp = 3,
    req      = 4,       ///< The server replies with `rsp` of `aux` payload bytes.
    rsp      = 5,
};

/**
 * @brief   Fixed 32-byte header, little-endian on the wire.
 *
 * `len` is the payload length (excluding the header). `seq` is a per-connection
 * sequence number starting at 0, `ts_ns` the sender's CLOCK_MONOTONIC send
 * time (echoed back unchanged in replies).
 */
struct header {
    std::uint16_t magic  = proto::magic;
    std::uint8_t version = proto::version;
    msg_type type        = msg_type::data;
    std::uint32_t len    = 0;
    std::uint64_t seq    = 0;
    std::uint64_t ts_ns  = 0;
    std::uint32_t aux    = 0;
    std::uint32_t flags  = 0;

    friend auto operator==(const header&, const header&) -> bool = default;
};

inline constexpr std::size_t header_size = 32;
static_assert(sizeof(header) == header_size);

/// Upper bound accepted by decoders (protects receive buffers).
inline constexpr std::uint32_t max_payload = 16U * 1024U * 1024U;

/**
 * @brief   Write `h` into the first `header_size` bytes of `out`.
 *
 * The in-memory layout equals the wire layout on little-endian targets, which
 * is the only kind supported.
 */
inline void encode(
        const header& h,
        std::span<std::byte, header_size> out
) noexcept {
    static_assert(std::endian::native == std::endian::little);
    std::memcpy(out.data(), &h, header_size);
}

enum class decode_error : std::uint8_t {
    short_buffer,
    bad_magic,
    bad_version,
    too_large,
};

struct decode_result {
    std::optional<header> value;
    decode_error error = decode_error::short_buffer;
};

/**
 * @brief   Decode a header from the beginning of `in`.
 */
[[nodiscard]] inline auto decode(std::span<const std::byte> in) noexcept -> decode_result {
    if (in.size() < header_size) {
        return { std::nullopt, decode_error::short_buffer };
    }

    header h;
    std::memcpy(&h, in.data(), header_size);

    if (h.magic != proto::magic) {
        return { std::nullopt, decode_error::bad_magic };
    }

    if (h.version != proto::version) {
        return { std::nullopt, decode_error::bad_version };
    }

    if (h.len > max_payload) {
        return { std::nullopt, decode_error::too_large };
    }

    return { h, decode_error::short_buffer };
}

} // namespace proto
