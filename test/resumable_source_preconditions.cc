#include <cbor_tags/cbor_resumable_source.h>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <span>
#include <string_view>
#include <vector>

#ifdef NDEBUG
#error "The source precondition test must be compiled with assertions enabled"
#endif

namespace ct = cbor::tags;

namespace {

template <typename Action> int expect_assertion(Action action) {
    // An uncaught exception must not masquerade as the expected assertion.
    std::set_terminate([] { std::_Exit(43); });
    if (std::signal(SIGABRT, [](int) { std::_Exit(42); }) == SIG_ERR)
        return 44;

    action();
    return 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;

    const std::string_view selected{argv[1]};
    std::vector<std::byte> bytes{std::byte{1}, std::byte{2}};

    if (selected == "descriptor-removal") {
        std::vector<std::span<const std::byte>> storage{std::span<const std::byte>{bytes}};
        auto                                    source = ct::make_input_source(storage);

        storage.clear();
        return expect_assertion([&] { source.refresh(); });
    }
    if (selected == "tail-shrink") {
        auto source = ct::make_input_source(bytes);

        bytes.pop_back();
        return expect_assertion([&] { source.refresh(); });
    }
    if (selected == "closed-tail-growth") {
        auto source = ct::make_input_source(bytes);
        source.close();

        bytes.push_back(std::byte{3});
        return expect_assertion([&] { source.refresh(); });
    }
    if (selected == "closed-descriptor-growth") {
        std::vector<std::span<const std::byte>> storage{std::span<const std::byte>{bytes}};
        auto                                    source = ct::make_input_source(storage);
        source.close();

        // Even an empty descriptor is an unsupported append after closure.
        storage.emplace_back();
        return expect_assertion([&] { source.refresh(); });
    }
    return 2;
}
