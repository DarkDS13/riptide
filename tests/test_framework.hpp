#pragma once

// A minimal self-contained test framework, so the project needs no test dependency.
//
//   RIPTIDE_TEST(suite, name) { CHECK(...); CHECK_EQ(a, b); CHECK_THROWS(expr, Type); }
//
// The runner executes every test whose "suite.name" contains the first command-line
// argument; CTest registers one entry per suite.

#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace riptide::test {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();

bool register_test(std::string name, std::function<void()> body);

/// Records a failed check; the current test continues so all failures are shown.
void report_failure(const char* file, int line, const std::string& message);

template <typename T>
std::string describe(const T& value) {
    if constexpr (requires(std::ostream& out) { out << value; }) {
        std::ostringstream out;
        out << value;
        return out.str();
    } else {
        return "<value>";
    }
}

}  // namespace riptide::test

#define RIPTIDE_TEST_CONCAT_INNER(a, b) a##b
#define RIPTIDE_TEST_CONCAT(a, b) RIPTIDE_TEST_CONCAT_INNER(a, b)

#define RIPTIDE_TEST(suite, name)                                                         \
    static void RIPTIDE_TEST_CONCAT(test_##suite##_, name)();                             \
    [[maybe_unused]] static const bool RIPTIDE_TEST_CONCAT(registered_##suite##_, name) = \
        ::riptide::test::register_test(#suite "." #name,                                  \
                                       RIPTIDE_TEST_CONCAT(test_##suite##_, name));       \
    static void RIPTIDE_TEST_CONCAT(test_##suite##_, name)()

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            ::riptide::test::report_failure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
        }                                                                                 \
    } while (false)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        const auto& riptide_actual = (actual);                                           \
        const auto& riptide_expected = (expected);                                       \
        if (!(riptide_actual == riptide_expected)) {                                     \
            ::riptide::test::report_failure(                                             \
                __FILE__, __LINE__,                                                      \
                "CHECK_EQ(" #actual ", " #expected "): " +                               \
                    ::riptide::test::describe(riptide_actual) + " != " +                 \
                    ::riptide::test::describe(riptide_expected));                        \
        }                                                                                \
    } while (false)

#define CHECK_THROWS(expression, exception_type)                                          \
    do {                                                                                  \
        bool riptide_thrown = false;                                                      \
        try {                                                                             \
            static_cast<void>(expression);                                                \
        } catch (const exception_type&) {                                                 \
            riptide_thrown = true;                                                        \
        } catch (...) {                                                                   \
        }                                                                                 \
        if (!riptide_thrown) {                                                            \
            ::riptide::test::report_failure(__FILE__, __LINE__,                           \
                                            #expression " did not throw " #exception_type); \
        }                                                                                 \
    } while (false)
