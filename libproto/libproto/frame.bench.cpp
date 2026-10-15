#include <libproto/frame.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>

static void BM_EncodeDecode(benchmark::State& state) {
    std::array<std::byte, proto::header_size> buf{};
    auto h = proto::header{ .len = 200 };

    for (auto _ : state) {
        ++h.seq;
        proto::encode(h, buf);
        benchmark::DoNotOptimize(buf);
        auto res = proto::decode(buf);
        benchmark::DoNotOptimize(res);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_EncodeDecode);
