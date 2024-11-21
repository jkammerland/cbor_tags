```cpp
// Note c++23

#include <ranges>
#include <span>
#include <array>
#include <deque>
#include <concepts>

namespace rv = std::ranges::views;

// Concept for any contiguous buffer of byte-like data
template<typename T>
concept ByteBuffer = std::ranges::contiguous_range<T> && 
    (sizeof(std::ranges::range_value_t<T>) == 1);

// Generic parser function
template<ByteBuffer Buffer>
auto parse_data(Buffer&& buf) {
    // Create a span view of the data
    auto bytes = std::span(std::forward<Buffer>(buf));
    
    // Example: find a pattern
    auto find_pattern = [](auto view) {
        return view 
            | rv::slide(2) // Look at pairs of bytes
            | rv::filter([](auto pair) { 
                return pair[0] == 0xFF && pair[1] == 0xAA; 
            });
    };

    // Example: extract chunks of 4 bytes
    auto get_chunks = [](auto view) {
        return view | rv::chunk(4);
    };

    // Example: convert to integers
    auto to_integers = [](auto view) {
        return view 
            | rv::transform([](auto chunk) {
                uint32_t result = 0;
                for(size_t i = 0; i < chunk.size(); ++i) {
                    result |= static_cast<uint32_t>(chunk[i]) << (i * 8);
                }
                return result;
            });
    };

    return bytes | get_chunks | to_integers;
}

// Usage example
int main() {
    std::array<uint8_t, 8> arr_buf{0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    std::deque<char> deque_buf{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    
    // Works with both containers
    auto parsed_arr = parse_data(arr_buf);
    auto parsed_deque = parse_data(deque_buf);

    // For more complex parsing, you can create a parser class
    class Parser {
        std::span<const uint8_t> data;
        size_t pos = 0;

    public:
        template<ByteBuffer Buffer>
        Parser(Buffer&& buf) : data(std::as_bytes(std::span(buf))) {}

        auto get_next_chunk(size_t n) {
            auto chunk = data.subspan(pos, n);
            pos += n;
            return chunk;
        }

        template<typename T>
        T read() {
            T value;
            auto bytes = get_next_chunk(sizeof(T));
            std::memcpy(&value, bytes.data(), sizeof(T));
            return value;
        }
    };
}
```