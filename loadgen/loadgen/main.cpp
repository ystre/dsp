/**
 * Hello-world load generator (Phase 1).
 *
 * One thread, one TCP connection, closed loop: sends `--count` frames (or for
 * `--duration` seconds) in batches and, for echo/req, reads the replies of each
 * batch before sending the next one, checking their sequence numbers. Prints
 * the run summary in the contract format (contract/loadgen-summary.schema.json)
 * and optionally serves the Phase 1 subset of `loadgen_*` metrics.
 *
 * The real generator (thread-per-core, open loop, latency histograms, UDP/WS,
 * churn, fan-out, self-check; doc/loadgen.adoc) is Phase 2.
 */

#include <libadmin/exposition.hpp>
#include <libadmin/http.hpp>
#include <libproto/frame.hpp>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

std::atomic_bool g_stop{ false };

void on_signal(int /*signo*/) {
    g_stop.store(true);
}

struct options {
    std::string target         = "127.0.0.1:7200";
    std::string transport      = "tcp";
    std::string mode           = "closed-loop";
    std::string type           = "data";
    std::uint32_t connections  = 1;
    std::uint32_t threads      = 1;
    double rate                = 0;
    std::uint64_t count        = 1'000'000;
    double duration_s          = 0;
    double warmup_s            = 0;
    std::uint32_t size         = 200;
    std::uint32_t aux          = 64;
    std::uint32_t batch        = 64;
    std::uint16_t metrics_port = 0;
    double connect_timeout     = 10;
    double linger_s            = 0;
    std::uint32_t instance     = 0;
    std::string scenario;
    std::string output;
};

struct stats {
    std::atomic<std::uint64_t> connections{ 0 };
    std::atomic<std::uint64_t> connect_errors{ 0 };
    std::atomic<std::uint64_t> io_errors{ 0 };
    std::atomic<std::uint64_t> tx_messages{ 0 };
    std::atomic<std::uint64_t> tx_bytes{ 0 };
    std::atomic<std::uint64_t> rx_messages{ 0 };
    std::atomic<std::uint64_t> rx_bytes{ 0 };
    std::atomic<std::uint64_t> missing{ 0 };
    std::atomic<std::uint64_t> duplicate{ 0 };
    std::atomic<std::uint64_t> out_of_order{ 0 };
    std::atomic<std::uint64_t> corrupt{ 0 };
};

void add(std::atomic<std::uint64_t>& c, std::uint64_t n = 1) {
    c.store(c.load(std::memory_order_relaxed) + n,
            std::memory_order_relaxed); // Single writer.
}

auto get(const std::atomic<std::uint64_t>& c) -> std::uint64_t {
    return c.load(std::memory_order_relaxed);
}

auto usage() -> std::string {
    return std::format(
        "Usage: {}-loadgen [options]\n"
        "Hello-world load generator (Phase 1): one connection, closed loop.\n\n"
        "The full CLI is a contract (doc/loadgen.adoc); values this "
        "implementation\n"
        "does not support are rejected (see --capabilities).\n\n"
        "  --target HOST:PORT     server address [127.0.0.1:7200]\n"
        "  --transport tcp        transport [tcp]\n"
        "  --mode closed-loop     load model [closed-loop]\n"
        "  --type data|echo|req   message type [data]\n"
        "  --connections 1        connections in total [1]\n"
        "  --threads 1            generator threads [1]\n"
        "  --rate 0               messages/s in total, 0 = closed loop [0]\n"
        "  --count N              messages per connection, 0 = unlimited "
        "[1000000]\n"
        "  --duration S           stop after S seconds, 0 = unlimited [0]\n"
        "  --warmup 0             seconds excluded from the summary [0]\n"
        "  --payload fixed:N      payload size distribution [fixed:200]\n"
        "  --aux N                response payload bytes for --type req [64]\n"
        "  --batch N              messages per write (echo/req: pipeline "
        "depth) "
        "[64]\n"
        "  --metrics-port N       serve loadgen_* metrics, 0 = off [0]\n"
        "  --linger S             keep serving metrics S seconds after the run "
        "[0]\n"
        "  --connect-timeout S    retry connecting for S seconds [10]\n"
        "  --instance N           instance index, recorded in the summary [0]\n"
        "  --scenario NAME        scenario name, recorded in the summary\n"
        "  --output FILE          write the JSON summary to FILE instead of "
        "stdout\n"
        "  --capabilities         print supported features as JSON and exit\n"
        "  --version\n",
        PROJ_NAME);
}

/// What this implementation supports; tools/bench skips scenarios needing more.
auto capabilities() -> std::string {
    return R"({"schema":"loadgen-capabilities/1","transports":["tcp"],"modes":["closed-loop"],)"
           R"("types":["data","echo","req"],"payload":["fixed"],"max_connections":1,"max_threads":1,)"
           R"("latency":false,"rate":false,"warmup":false,"metrics":true,"self_check":false})";
}

auto parse(int argc, char** argv) -> options {
    options opts;
    const auto args =
        std::span{ argv, static_cast<std::size_t>(argc) }.subspan(1);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto arg = std::string_view{ args[i] };
        auto next      = [&]() -> std::string {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(
                    std::format("missing value for {}", arg));
            }
            return args[++i];
        };
        auto u32 = [&]() {
            return static_cast<std::uint32_t>(std::stoul(next()));
        };

        if (arg == "--target") {
            opts.target = next();
        } else if (arg == "--transport") {
            opts.transport = next();
        } else if (arg == "--mode") {
            opts.mode = next();
        } else if (arg == "--type") {
            opts.type = next();
        } else if (arg == "--connections") {
            opts.connections = u32();
        } else if (arg == "--threads") {
            opts.threads = u32();
        } else if (arg == "--rate") {
            opts.rate = std::stod(next());
        } else if (arg == "--count") {
            opts.count = std::stoull(next());
        } else if (arg == "--duration") {
            opts.duration_s = std::stod(next());
        } else if (arg == "--warmup") {
            opts.warmup_s = std::stod(next());
        } else if (arg == "--payload") {
            const auto v = next();
            if (not v.starts_with("fixed:")) {
                throw std::invalid_argument(std::format(
                    "unsupported --payload (Phase 1: fixed:N only): {}", v));
            }
            opts.size = static_cast<std::uint32_t>(std::stoul(v.substr(6)));
        } else if (arg == "--aux") {
            opts.aux = u32();
        } else if (arg == "--batch") {
            opts.batch = std::max(1U, u32());
        } else if (arg == "--metrics-port") {
            opts.metrics_port = static_cast<std::uint16_t>(u32());
        } else if (arg == "--linger") {
            opts.linger_s = std::stod(next());
        } else if (arg == "--connect-timeout") {
            opts.connect_timeout = std::stod(next());
        } else if (arg == "--instance") {
            opts.instance = u32();
        } else if (arg == "--scenario") {
            opts.scenario = next();
        } else if (arg == "--output") {
            opts.output = next();
        } else if (arg == "--capabilities") {
            std::cout << capabilities() << '\n';
            std::exit(0);
        } else if (arg == "--version") {
            std::cout << std::format("{}-loadgen {} ({})\n", PROJ_NAME,
                                     PROJ_VERSION, PROJ_GIT_HASH);
            std::exit(0);
        } else if (arg == "--help" or arg == "-h") {
            std::cout << usage();
            std::exit(0);
        } else {
            throw std::invalid_argument(
                std::format("unknown argument: {}", arg));
        }
    }
    if (opts.type != "data" and opts.type != "echo" and opts.type != "req") {
        throw std::invalid_argument(
            std::format("unsupported --type: {}", opts.type));
    }
    if (opts.transport != "tcp" or opts.mode != "closed-loop"
        or opts.connections != 1 or opts.threads != 1 or opts.rate != 0
        or opts.warmup_s != 0) {
        throw std::invalid_argument(
            "unsupported by the Phase 1 load generator: only --transport tcp "
            "--mode closed-loop "
            "--connections 1 --threads 1 --rate 0 --warmup 0");
    }
    if (opts.size > proto::max_payload or opts.aux > proto::max_payload) {
        throw std::invalid_argument(
            "--payload/--aux above the protocol maximum");
    }
    return opts;
}

auto connect_once(const std::string& target) -> int {
    const auto colon = target.rfind(':');

    if (colon == std::string::npos) {
        throw std::invalid_argument(
            std::format("--target must be HOST:PORT: {}", target));
    }

    const auto host = target.substr(0, colon);
    const auto port = target.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res     = nullptr;

    if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;

    for (auto* ai = res; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC,
                      ai->ai_protocol);
        if (fd >= 0 and ::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    ::freeaddrinfo(res);

    if (fd >= 0) {
        const int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }

    return fd;
}

auto connect_retry(const options& opts, stats& st) -> int {
    const auto deadline = clock_type::now() + std::chrono::duration<double>(opts.connect_timeout);

    while (not g_stop.load()) {
        const int fd = connect_once(opts.target);

        if (fd >= 0) {
            return fd;
        }

        add(st.connect_errors);

        if (clock_type::now() >= deadline) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return -1;
}

auto send_all(int fd, std::span<const std::byte> data) -> bool {
    while (not data.empty()) {
        const auto n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);

        if (n < 0 and errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return false;
        }

        data = data.subspan(static_cast<std::size_t>(n));
    }

    return true;
}

auto recv_exact(int fd, std::span<std::byte> out) -> bool {
    while (not out.empty()) {
        const auto n = ::recv(fd, out.data(), out.size(), 0);
        if (n < 0 and errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            return false;
        }

        out = out.subspan(static_cast<std::size_t>(n));
    }

    return true;
}

/// Sequence check of one reply (shared rules with the Phase 2 verifier,
/// doc/loadgen.adoc).
void check_seq(std::uint64_t seq, std::uint64_t& expected, stats& st) {
    if (seq == expected) {
        ++expected;
    } else if (seq > expected) {
        add(st.missing, seq - expected);
        expected = seq + 1;
    } else {
        add(st.out_of_order);
    }
}

auto now_ns() -> std::uint64_t {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            clock_type::now().time_since_epoch()).count()
    );
}

struct run_result {
    double start_unix_s = 0;
    double elapsed_s    = 0;
};

auto run(const options& opts, stats& st) -> run_result {
    const auto type      = opts.type == "echo"  ? proto::msg_type::echo_req
                           : opts.type == "req" ? proto::msg_type::req
                                                : proto::msg_type::data;
    const bool replies   = type != proto::msg_type::data;
    const auto frame_len = proto::header_size + opts.size;
    const auto reply_len =
        proto::header_size
        + (type == proto::msg_type::req ? opts.aux : opts.size);

    run_result result;
    const int fd = connect_retry(opts, st);
    if (fd < 0) {
        return result;
    }
    add(st.connections);

    std::vector<std::byte> out(static_cast<std::size_t>(opts.batch) * frame_len,
                               std::byte{ 0xA5 });
    std::vector<std::byte> in(reply_len);

    result.start_unix_s =
        std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    const auto start = clock_type::now();
    const auto deadline =
        opts.duration_s > 0
            ? start + std::chrono::duration<double>(opts.duration_s)
            : clock_type::time_point::max();

    std::uint64_t seq      = 0;
    std::uint64_t expected = 0;
    while (not g_stop.load() and (opts.count == 0 or seq < opts.count)
           and clock_type::now() < deadline) {
        const auto n =
            opts.count == 0
                ? std::uint64_t{ opts.batch }
                : std::min<std::uint64_t>(opts.batch, opts.count - seq);
        const auto ts = now_ns();
        for (std::uint64_t i = 0; i < n; ++i) {
            const auto h = proto::header{ .type  = type,
                                          .len   = opts.size,
                                          .seq   = seq + i,
                                          .ts_ns = ts,
                                          .aux   = opts.aux };
            proto::encode(h, std::span{ out }
                                 .subspan(i * frame_len)
                                 .first<proto::header_size>());
        }
        if (not send_all(fd, std::span{ out }.first(n * frame_len))) {
            add(st.io_errors);
            break;
        }
        seq += n;
        add(st.tx_messages, n);
        add(st.tx_bytes, n * frame_len);

        if (replies) {
            for (std::uint64_t i = 0; i < n; ++i) {
                if (not recv_exact(fd, in)) {
                    add(st.io_errors);
                    goto done;
                }
                const auto res  = proto::decode(in);
                const auto want = type == proto::msg_type::req
                                      ? proto::msg_type::rsp
                                      : proto::msg_type::echo_rsp;
                if (not res.value or res.value->type != want
                    or proto::header_size + res.value->len != reply_len) {
                    add(st.corrupt);
                    goto done; // Framing is lost; nothing after this can be
                               // trusted.
                }
                check_seq(res.value->seq, expected, st);
                add(st.rx_messages);
                add(st.rx_bytes, reply_len);
            }
        }
    }
done:
    result.elapsed_s =
        std::chrono::duration<double>(clock_type::now() - start).count();
    ::close(fd);
    st.connections.store(0);
    return result;
}

auto metrics(const options& opts, const stats& st) -> std::string {
    admin::exposition out;
    out.family("loadgen_build_info", "gauge", "Always 1.");
    out.sample("loadgen_build_info",
               std::format(R"(version="{}",revision="{}")", PROJ_VERSION,
                           PROJ_GIT_HASH),
               std::uint64_t{ 1 });
    out.family("loadgen_connections", "gauge", "Open connections.");
    out.sample("loadgen_connections", "", get(st.connections));
    out.family("loadgen_connect_errors_total", "counter",
               "Failed connection attempts.");
    out.sample("loadgen_connect_errors_total", "", get(st.connect_errors));
    out.family("loadgen_tx_messages_total", "counter", "Frames sent.");
    out.sample("loadgen_tx_messages_total",
               std::format(R"(type="{}")",
                           opts.type == "echo" ? "echo_req" : opts.type),
               get(st.tx_messages));
    out.family("loadgen_tx_bytes_total", "counter", "Bytes sent.");
    out.sample("loadgen_tx_bytes_total", "", get(st.tx_bytes));
    out.family("loadgen_rx_messages_total", "counter",
               "Reply frames received.");
    out.sample("loadgen_rx_messages_total", "", get(st.rx_messages));
    out.family("loadgen_rx_bytes_total", "counter", "Bytes received.");
    out.sample("loadgen_rx_bytes_total", "", get(st.rx_bytes));
    out.family("loadgen_integrity_errors_total", "counter",
               "Reply sequence violations.");
    out.sample("loadgen_integrity_errors_total", R"(kind="missing")",
               get(st.missing));
    out.sample("loadgen_integrity_errors_total", R"(kind="duplicate")",
               get(st.duplicate));
    out.sample("loadgen_integrity_errors_total", R"(kind="out_of_order")",
               get(st.out_of_order));
    out.sample("loadgen_integrity_errors_total", R"(kind="corrupt")",
               get(st.corrupt));
    return std::move(out).str();
}

auto json_escape(std::string_view s) -> std::string {
    std::string out;
    for (const char ch : s) {
        if (ch == '"' or ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

auto summary(const options& opts, const stats& st, const run_result& r)
    -> std::string {
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
    auto secs = [](timeval tv) {
        return static_cast<double>(tv.tv_sec)
               + static_cast<double>(tv.tv_usec) / 1e6;
    };
    const auto elapsed = r.elapsed_s > 0 ? r.elapsed_s : 1.0;

    return std::format(
        R"({{"schema":"loadgen-summary/1",)"
        R"("tool":{{"name":"{}-loadgen","version":"{}","revision":"{}"}},)"
        R"("scenario":"{}","instance":{},)"
        R"("config":{{"target":"{}","transport":"tcp","mode":"closed-loop","type":"{}","connections":1,"threads":1,)"
        R"("payload":"fixed:{}","batch":{},"count":{},"duration_s":{},"rate":0}},)"
        R"("valid":true,"invalid_reasons":[],"self_check":"not-implemented",)"
        R"("start_unix_s":{:.3f},"elapsed_s":{:.6f},)"
        R"("sent":{{"messages":{},"bytes":{}}},"received":{{"messages":{},"bytes":{}}},)"
        R"("throughput":{{"messages_per_s":{:.1f},"bytes_per_s":{:.1f}}},)"
        R"("latency_ns":null,)"
        R"("integrity":{{"missing":{},"duplicate":{},"out_of_order":{},"corrupt":{}}},)"
        R"("errors":{{"connect":{},"io":{}}},)"
        R"("resources":{{"cpu_user_s":{:.3f},"cpu_sys_s":{:.3f},"max_rss_kib":{}}}}})",
        PROJ_NAME, PROJ_VERSION, PROJ_GIT_HASH, json_escape(opts.scenario),
        opts.instance, json_escape(opts.target), opts.type, opts.size,
        opts.batch, opts.count, opts.duration_s, r.start_unix_s, r.elapsed_s,
        get(st.tx_messages), get(st.tx_bytes), get(st.rx_messages),
        get(st.rx_bytes), static_cast<double>(get(st.tx_messages)) / elapsed,
        static_cast<double>(get(st.tx_bytes)) / elapsed, get(st.missing),
        get(st.duplicate), get(st.out_of_order), get(st.corrupt),
        get(st.connect_errors), get(st.io_errors), secs(ru.ru_utime),
        secs(ru.ru_stime), ru.ru_maxrss);
}

} // namespace

auto main(int argc, char** argv) -> int {
    struct sigaction sa{};
    sa.sa_handler = on_signal;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    try {
        const auto opts = parse(argc, argv);
        stats st;

        std::optional<admin::http_server> admin_http;
        std::jthread admin_thread;
        if (opts.metrics_port != 0) {
            admin_http.emplace(opts.metrics_port);
            admin_thread = std::jthread([&](std::stop_token stop) {
                admin_http->run(
                    [&opts, &st](std::string_view path) -> admin::response {
                        if (path == "/metrics") {
                            return {
                                .content_type =
                                    "text/plain; version=0.0.4; charset=utf-8",
                                .body = metrics(opts, st)
                            };
                        }
                        if (path == "/healthz" or path == "/readyz") {
                            return admin::probe(true);
                        }
                        return { .status = 404, .body = "not found\n" };
                    },
                    stop);
            });
        }

        const auto result = run(opts, st);
        const auto json   = summary(opts, st, result);
        if (opts.output.empty()) {
            std::cout << json << '\n';
        } else {
            std::ofstream(opts.output) << json << '\n';
        }

        // Let Prometheus scrape the final counters (k8s Jobs).
        const auto linger_end =
            clock_type::now() + std::chrono::duration<double>(opts.linger_s);
        while (not g_stop.load() and clock_type::now() < linger_end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Exit code 2: the run happened but is not clean (nothing sent, I/O or
        // integrity errors).
        const bool failed = get(st.tx_messages) == 0 or get(st.io_errors) != 0
                            or get(st.corrupt) != 0 or get(st.missing) != 0
                            or get(st.duplicate) != 0
                            or get(st.out_of_order) != 0;
        return failed ? 2 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n' << usage();
        return 1;
    }
}
