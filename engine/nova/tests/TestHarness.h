// Nova Engine — minimal test framework.
//
// Deliberately dependency-free: the engine ships to Android where every extra
// library is build weight. Supports suites, typed assertions, and a summary
// that CI can parse.
//
// The assertion macros are variadic and delegate to function templates so that
// braced initializers containing commas — CHECK_NEAR(Vec2{1,2}.length(), 1.0, 1e-6) —
// do not confuse the preprocessor.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nova {
namespace test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

/// Global registry + running counters.
class Registry {
public:
    static Registry& instance();
    void add(TestCase tc) { cases_.push_back(std::move(tc)); }
    void beginCase(const std::string& name);
    void fail(const char* file, int line, const std::string& message);
    void notePass() { ++checksRun_; }
    /// Runs all registered cases (optionally filtered by [filter] substring).
    int run(const std::string& filter);

private:
    std::vector<TestCase> cases_;
    std::string current_;
    int caseFailures_ = 0;
    int checksRun_ = 0;
    int totalFail_ = 0;
    int totalRun_ = 0;
    int suiteCount_ = 0;
};

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        Registry::instance().add(TestCase{suite, name, std::move(fn)});
    }
};

// --- stringification helpers (used by CHECK_MSG) ---------------------------

template <typename T>
inline std::string toText(const T& v) {
    if constexpr (std::is_convertible_v<T, std::string>) {
        return std::string(v);
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(v);
    } else {
        std::ostringstream ss;
        ss << v;
        return ss.str();
    }
}

inline std::string detailOf() { return {}; }
template <typename T, typename... Rest>
inline std::string detailOf(T&& first, Rest&&... rest) {
    return toText(first) + detailOf(std::forward<Rest>(rest)...);
}

// --- assertion implementations (called by the macros) ----------------------

inline void report(bool ok, const char* file, int line, const std::string& expr,
                   const std::string& detail) {
    Registry::instance().notePass();
    if (ok) return;
    std::string msg = expr;
    if (!detail.empty()) msg += " \u2014 " + detail;
    Registry::instance().fail(file, line, msg);
}

template <typename A, typename B, typename E>
inline void checkNearImpl(const char* file, int line, const char* expr, const A& a,
                          const B& b, const E& eps) {
    const double va = static_cast<double>(a);
    const double vb = static_cast<double>(b);
    const bool ok = std::fabs(va - vb) <= static_cast<double>(eps);
    char detail[192] = {0};
    if (!ok) {
        std::snprintf(detail, sizeof(detail), "got %.9g, want %.9g (eps %.3g)", va, vb,
                      static_cast<double>(eps));
    }
    report(ok, file, line, expr, detail);
}

}  // namespace test
}  // namespace nova

#define NOVA_TEST(suite_name, test_name)                                       \
    static void nova_test_##suite_name##_##test_name();                        \
    static ::nova::test::Registrar nova_reg_##suite_name##_##test_name(        \
        #suite_name, #test_name, nova_test_##suite_name##_##test_name);         \
    static void nova_test_##suite_name##_##test_name()

#define CHECK(...)                                                             \
    ::nova::test::report(static_cast<bool>(__VA_ARGS__), __FILE__, __LINE__,    \
                         #__VA_ARGS__, std::string())

#define CHECK_MSG(cond, ...)                                                   \
    ::nova::test::report(static_cast<bool>(cond), __FILE__, __LINE__, #cond,    \
                         ::nova::test::detailOf(__VA_ARGS__))

#define CHECK_NEAR(...)                                                        \
    ::nova::test::checkNearImpl(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__)

/// Component compare for vector-like types. Locals are prefixed so they do
/// not collide with the locals inside checkNearImpl.
#define CHECK_NEAR_VEC3(a, b, eps)                                             \
    do {                                                                       \
        const auto novaA_ = (a);                                               \
        const auto novaB_ = (b);                                               \
        CHECK_NEAR(novaA_.x, novaB_.x, eps);                                   \
        CHECK_NEAR(novaA_.y, novaB_.y, eps);                                   \
        CHECK_NEAR(novaA_.z, novaB_.z, eps);                                   \
    } while (0)
