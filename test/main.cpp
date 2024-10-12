#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

using namespace doctest;

struct TestCaseLogger : public IReporter {
    // caching pointers/references to objects of these types - safe to do
    std::ostream         &stdout_stream;
    const ContextOptions &opt;
    const TestCaseData   *tc{};
    std::mutex            mutex;

    // constructor has to accept the ContextOptions by ref as a single argument
    TestCaseLogger(const ContextOptions &in) : stdout_stream(*in.cout), opt(in) {}

    void test_run_start() override {}

    void test_run_end(const TestRunStats & /*in*/) override {}

    void report_query(const QueryData & /*in*/) override {}

    void test_case_start(const TestCaseData &in) override {
        tc = &in;
        stdout_stream << "\033[35mBEGIN: " << tc->m_name << "\033[0m" << std::endl;
    }

    void test_case_reenter(const TestCaseData & /*in*/) override {}

    void test_case_end(const CurrentTestCaseStats & /*in*/) override { stdout_stream << "\033[35mEND:   " << tc->m_name << "\033[0m\n\n"; }

    void test_case_exception(const TestCaseException & /*in*/) override {}

    void subcase_start(const SubcaseSignature & /*in*/) override { std::lock_guard<std::mutex> lock(mutex); }

    void subcase_end() override { std::lock_guard<std::mutex> lock(mutex); }

    void log_assert(const AssertData &in) override {
        // don't include successful asserts by default - this is done here
        // instead of in the framework itself because doctest doesn't know
        // if/when a reporter/listener cares about successful results
        if (!in.m_failed && !opt.success)
            return;

        // make sure there are no races - this is done here instead of in the
        // framework itself because doctest doesn't know if reporters/listeners
        // care about successful asserts and thus doesn't lock a mutex unnecessarily
        std::lock_guard<std::mutex> lock(mutex);

        // ...
    }

    void log_message(const MessageData & /*in*/) override {
        // messages too can be used in a multi-threaded context - like asserts
        std::lock_guard<std::mutex> lock(mutex);

        // ...
    }

    void test_case_skipped(const TestCaseData & /*in*/) override {}
};

// registering the same class as a reporter and as a listener is nonsense but it's possible
REGISTER_LISTENER("TestCaseLogger", 1, TestCaseLogger);