#include <libproto/frame.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

TEST(Frame, RoundTrip) {
    const auto h = proto::header{
        .type  = proto::msg_type::echo_req,
        .len   = 200,
        .seq   = 42,
        .ts_ns = 123456789,
        .aux   = 7,
    };

    std::array<std::byte, proto::header_size> buf{};
    proto::encode(h, buf);
    const auto res = proto::decode(buf);

    ASSERT_TRUE(res.value.has_value());
    EXPECT_EQ(*res.value, h);
}

TEST(Frame, WireLayoutIsLittleEndian) {
    std::array<std::byte, proto::header_size> buf{};
    proto::encode(proto::header{ .len = 0x01020304 }, buf);

    EXPECT_EQ(buf[0], std::byte{ 0xF0 });
    EXPECT_EQ(buf[1], std::byte{ 0xD5 });
    EXPECT_EQ(buf[4], std::byte{ 0x04 });
    EXPECT_EQ(buf[7], std::byte{ 0x01 });
}

TEST(Frame, DecodeErrors) {
    std::array<std::byte, proto::header_size> buf{};

    EXPECT_EQ(proto::decode(std::span{ buf }.first(10)).error, proto::decode_error::short_buffer);
    EXPECT_EQ(proto::decode(buf).error, proto::decode_error::bad_magic);

    proto::encode(proto::header{ .version = 9 }, buf);
    EXPECT_EQ(proto::decode(buf).error, proto::decode_error::bad_version);

    proto::encode(proto::header{ .len = proto::max_payload + 1 }, buf);
    EXPECT_EQ(proto::decode(buf).error, proto::decode_error::too_large);
}
