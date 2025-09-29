#include <gmock/gmock.h>

#include <libdsp/crypto.hpp>
#include <libnova/data.hpp>

using namespace nova::literals;

TEST(Crypto, ph) {
    const auto key = "4ArycO6S1s5UpPkpCkX6Gz17u9kpqW3s"_data;
    const auto secret = "aVerySensitiveSecret"_data;

    const auto encrypted = dsp::encrypt(secret, key);
    const auto decrypted = dsp::decrypt(encrypted, key);

    EXPECT_EQ(
        nova::data_view{ decrypted }.as_hex_string(),
        secret.as_hex_string()
    );
}
