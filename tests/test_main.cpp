#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include "test_framework.hpp"

namespace riptide::test {
namespace {

int failures_in_current_test = 0;

}  // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

bool register_test(std::string name, std::function<void()> body) {
    registry().push_back({std::move(name), std::move(body)});
    return true;
}

void report_failure(const char* file, int line, const std::string& message) {
    ++failures_in_current_test;
    std::fprintf(stderr, "    %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace riptide::test

int main(int argc, char** argv) {
    using riptide::test::registry;
    const std::string filter = argc > 1 ? argv[1] : "";

    int failed = 0;
    int run = 0;
    for (const auto& test : registry()) {
        if (test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++run;
        riptide::test::failures_in_current_test = 0;
        const auto start = std::chrono::steady_clock::now();
        try {
            test.body();
        } catch (const std::exception& error) {
            riptide::test::report_failure(__FILE__, __LINE__,
                                          std::string("unexpected exception: ") + error.what());
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool ok = riptide::test::failures_in_current_test == 0;
        failed += ok ? 0 : 1;
        std::printf("[%s] %s (%.1f ms)\n", ok ? " OK " : "FAIL", test.name.c_str(), ms);
    }
    std::printf("%d of %d tests passed\n", run - failed, run);
    return failed == 0 && run > 0 ? 0 : 1;
}
