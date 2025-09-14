#pragma once

#include <libnova/log.hpp>

#include <cstdlib>
#include <ranges>
#include <string_view>
#include <span>

#define DSP_MAIN_ARG_PARSE(func, parse)                                         \
    int main(int argc, char* argv[]) {                                          \
        try {                                                                   \
            const auto args = parse(argc, argv);                                \
            if (not args.has_value()) {                                         \
                return EXIT_SUCCESS;                                            \
            }                                                                   \
            return func(*args);                                                 \
        } catch (nova::exception& ex) {                                         \
            nova::log::error(                                                   \
                "Exception caught in main: {}\n{}\n",                           \
                ex.what(),                                                      \
                ex.where(),                                                     \
                ex.backtrace()                                                  \
            );                                                                  \
        } catch (std::exception& ex) {                                          \
            nova::log::error("Exception caught in main: {}", ex.what());        \
        } catch (const char* msg) {                                             \
            nova::log::error("Exception caught in main: {}", msg);              \
        } catch (...) {                                                         \
            nova::log::error("Unknown exception caught in main");               \
        }                                                                       \
        return EXIT_FAILURE;                                                    \
    }
