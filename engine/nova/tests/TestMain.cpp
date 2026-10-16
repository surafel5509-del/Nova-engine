#include "tests/TestHarness.h"

#include <chrono>

namespace nova {
namespace test {

Registry& Registry::instance() {
    static Registry r;
    return r;
}

void Registry::beginCase(const std::string& name) {
    current_ = name;
    caseFailures_ = 0;
}

void Registry::fail(const char* file, int line, const std::string& message) {
    ++caseFailures_;
    ++totalFail_;
    const char* base = std::strrchr(file, '/');
    std::printf("  \033[31mFAIL\033[0m %s\n        %s:%d: %s\n", current_.c_str(),
                base ? base + 1 : file, line, message.c_str());
}

int Registry::run(const std::string& filter) {
    const auto start = std::chrono::steady_clock::now();
    std::string lastSuite;
    bool suiteFailed = false;

    for (const TestCase& tc : cases_) {
        if (!filter.empty() && tc.suite.find(filter) == std::string::npos &&
            tc.name.find(filter) == std::string::npos) {
            continue;
        }
        if (tc.suite != lastSuite) {
            if (!lastSuite.empty()) {
                std::printf("  %s %s\n", suiteFailed ? "\033[31m✗\033[0m" : "\033[32m✓\033[0m",
                            lastSuite.c_str());
            }
            lastSuite = tc.suite;
            suiteFailed = false;
            ++suiteCount_;
            std::printf("\033[1m%s\033[0m\n", tc.suite.c_str());
        }
        beginCase(tc.name);
        const int before = caseFailures_;
        try {
            tc.fn();
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            fail(__FILE__, __LINE__, "uncaught non-standard exception");
        }
        if (caseFailures_ == before) std::printf("  \033[32m✓\033[0m %s\n", tc.name.c_str());
        suiteFailed = suiteFailed || caseFailures_ > before;
        ++totalRun_;
    }
    if (!lastSuite.empty()) {
        std::printf("  %s %s\n", suiteFailed ? "\033[31m✗\033[0m" : "\033[32m✓\033[0m",
                    lastSuite.c_str());
    }

    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start).count();
    std::printf("\n\033[1m────────────────────────────────────────\033[0m\n");
    if (totalFail_ == 0) {
        std::printf("\033[32m\033[1m ALL PASS \033[0m  %d tests, %d suites, %d checks, %.1f ms\n",
                    totalRun_, suiteCount_, checksRun_, ms);
    } else {
        std::printf("\033[31m\033[1m FAILED \033[0m   %d/%d tests failed, %d checks, %.1f ms\n",
                    totalFail_, totalRun_, checksRun_, ms);
    }
    return totalFail_ == 0 ? 0 : 1;
}

}  // namespace test
}  // namespace nova

int main(int argc, char** argv) {
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::printf("usage: nova_tests [--filter <substring>]\n");
            return 0;
        }
        if (a == "--filter" && i + 1 < argc) filter = argv[++i];
    }
    return nova::test::Registry::instance().run(filter);
}
