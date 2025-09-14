#include <libdsp/cache.hpp>
#include <libdsp/router.hpp>

#include <gmock/gmock.h>

using namespace testing;

TEST(Dsp, Router_Allow) {
    const auto msg = dsp::message{
        .key = { },
        .subject = { },
        .properties = { },
        .payload = { },
    };

    auto router = dsp::router{ };
    const auto xs = router.route(msg);
    ASSERT_GE(xs.size(), 1);
    EXPECT_EQ(xs.size(), 1);

    EXPECT_EQ(xs[0].subject, "dev-test");
}
