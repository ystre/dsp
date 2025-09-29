#include <benchmark/benchmark.h>

#include <libdsp/crypto.hpp>
#include <libnova/data.hpp>

constexpr auto ByteArrayLength = 100'000'000;

using namespace nova::literals;

static void encrypt(benchmark::State& state) {
    const auto key = "4ArycO6S1s5UpPkpCkX6Gz17u9kpqW3s"_data;
    const auto secret = "aVerySensitiveSecret"_data;

    for (auto _ : state) {
        auto data = dsp::encrypt(secret, key);
        benchmark::DoNotOptimize(data);
    }
}

static void encrypt_n_decrypt(benchmark::State& state) {
    const auto key = "4ArycO6S1s5UpPkpCkX6Gz17u9kpqW3s"_data;
    const auto secret = "aVerySensitiveSecret"_data;

    for (auto _ : state) {
        auto enc = dsp::encrypt(secret, key);
        auto data = dsp::decrypt(enc, key);
        benchmark::DoNotOptimize(data);
    }
}


BENCHMARK(encrypt);
BENCHMARK(encrypt_n_decrypt);

BENCHMARK_MAIN();
