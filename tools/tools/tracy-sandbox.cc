#include <libdsp/profiler.hh>
#include <tools/tracy_alloc.hh>

#include <nova/log.hh>

#include <fmt/core.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

class shell {
public:
    using command = std::function<void()>;

    shell() {
        add("q", [this]() { m_alive = false; });
    }

    void add(const std::string& name, command cmd) {
        m_commands.insert({ name, std::move(cmd) });
    }

    void run() {
        fmt::println("Usage: Issue `do` command for triggering allocation. Issue `q` for exiting the shell.");

        std::string line;

        while (m_alive) {
            fmt::print("sandbox> ");
            if (not std::getline(std::cin, line)) {
                break;
            }

            if (const auto it = m_commands.find(line); it != std::end(m_commands)) {
                const auto& [name, cmd] = *it;
                std::invoke(cmd);
            } else {
                fmt::println("Invalid command: {}", line);
            }
        }
    }

private:
    std::unordered_map<std::string, command> m_commands;
    bool m_alive = true;

};

int main() {
    nova::log::init();
    dsp::start_profiler();

    // auto a = alloc{ };
    // for (int i = 0; i < 32; ++i) {
        // a();
    // }

    fmt::println("--==[ MALLOC TEST BEGIN ]==--");

    fmt::println("      MALLOC");
    auto* ptr = std::malloc(4);
    std::free(ptr);

    fmt::println("      REALLOC");
    auto* ptr2a = std::malloc(4);
    auto* ptr2b = std::realloc(ptr2a, 8);
    auto* ptr2c = std::realloc(ptr2b, 64);
    std::free(ptr2c);

    fmt::println("      CALLOC");
    auto* ptr3a = std::calloc(1, 1);
    auto* ptr3b = std::calloc(1, 4);
    auto* ptr3c = std::calloc(2, 4);

    std::free(ptr3a);
    std::free(ptr3b);
    std::free(ptr3c);

    fmt::println("--==[ MALLOC TEST END ]==--");

    auto sh = shell{ };
    sh.add("alloc", alloc{ });
    sh.add("calloc", []{ std::ignore = std::calloc(1, 1); } );
    sh.run();

    dsp::stop_profiler();
    return EXIT_SUCCESS;
}
