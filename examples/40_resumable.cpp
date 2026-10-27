#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

struct record {
    int number{};
    std::string text;
    std::vector<int> values;

    bool operator==(const record &) const = default;
};

// This small owner starts immediately and keeps a suspended consumer alive.
class consumer {
  public:
    struct promise_type {
        consumer get_return_object() noexcept {
            return consumer{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() const noexcept {
            return {};
        }
        std::suspend_always final_suspend() const noexcept {
            return {};
        }
        void return_void() const noexcept {
        }
        void unhandled_exception() const noexcept {
            std::terminate();
        }
    };

    explicit consumer(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {
    }
    consumer(const consumer &) = delete;
    consumer &operator=(const consumer &) = delete;
    consumer(consumer &&other) noexcept : handle_(std::exchange(other.handle_, {})) {
    }
    ~consumer() {
        if (handle_)
            handle_.destroy();
    }

    bool done() const noexcept {
        return handle_.done();
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

template <typename Generator>
consumer collect(Generator &source, std::vector<int> &values,
    std::optional<expected<std::size_t, status_code>> &result) {
    while (co_await source.next()) {
        values.push_back(source.value());
    }
    result.emplace(source.result());
}

} // namespace

int main() {
    // Values and caller-owned byte buffers survive the complete operation.
    const record original{42, "split input", {1, -2, 300}};
    const bool enabled = true;
    std::array<std::byte, 2> first{};
    std::list<std::byte> middle(3);
    std::array<std::byte, 128> last{};

    // A variadic call encodes consecutive values into ordered output chunks.
    auto encoder = make_resumable_encoder()(original, enabled);
    const auto written = encoder(first, middle, last);
    if (!encoder.done() || !encoder.result()) {
        return 1;
    }

    std::vector<std::byte> bytes;
    for (const auto prefix : written.produced()) {
        for (const auto byte : prefix) {
            bytes.push_back(byte);
        }
    }

    // A feed can mix individual chunks with a runtime batch of chunks.
    record decoded;
    bool decoded_enabled{};
    auto decoder = make_resumable_decoder()(decoded, decoded_enabled);
    const std::span<const std::byte> input{bytes};
    const std::array batch{input.subspan(1, input.size() - 2)};
    const auto read = decoder(input.first(1), std::span{batch}, input.last(1), end_of_input);
    if (!decoder.done() || !decoder.result() || read.bytes != bytes.size() || decoded != original ||
        decoded_enabled != enabled) {
        return 2;
    }

    // Prepare an independent sequence of two complete CBOR integer items.
    std::array<std::byte, 16> sequence_buffer{};
    auto sequence_encoder = make_resumable_encoder()(7, 42);
    const auto sequence_written = sequence_encoder(sequence_buffer);
    if (!sequence_encoder.done() || !sequence_encoder.result()) {
        return 3;
    }
    const auto sequence = std::span{sequence_buffer}.first(sequence_written.bytes);

    // The application starts the consumer before supplying input; no scheduler is installed.
    auto items = make_resumable_decoder().items<int>();
    std::vector<int> values;
    std::optional<expected<std::size_t, status_code>> result;
    auto reader = collect(items, values, result);
    if (reader.done()) {
        return 4;
    }

    // Each feed resumes the waiting consumer inline, which requests its next item.
    const auto prefix = items(sequence.first(1));
    if (prefix.error != status_code::success || values != std::vector<int>{7} || reader.done()) {
        return 5;
    }
    const auto suffix = items(sequence.subspan(1), end_of_input);
    if (suffix.error != status_code::success || !reader.done() ||
        values != std::vector<int>{7, 42} || !result || !*result || **result != sequence.size()) {
        return 6;
    }
    return 0;
}
