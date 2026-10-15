#include <libadmin/exposition.hpp>

#include <gtest/gtest.h>

#include <cstdint>

TEST(Exposition, FamilyAndSamples) {
    admin::exposition out;
    out.family("x_total", "counter", "Things.");
    out.sample("x_total", "", std::uint64_t{ 3 });
    out.sample("x_total", R"(kind="a")", std::uint64_t{ 4 });
    out.sample("x_ratio", "", 0.5);

    EXPECT_EQ(std::move(out).str(), "# HELP x_total Things.\n"
                                    "# TYPE x_total counter\n"
                                    "x_total 3\n"
                                    "x_total{kind=\"a\"} 4\n"
                                    "x_ratio 0.5\n");
}
