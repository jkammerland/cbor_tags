# CBOR Range-Based Decoding Design

## Overview

This document describes the new range-based decoding approach for the cbor_tags library, which enables lazy evaluation, filtering, and composition with standard C++ ranges.

## Motivation

Traditional CBOR decoding requires knowing the exact type to decode upfront:
```cpp
std::vector<uint8_t> buffer = /* CBOR data */;
auto dec = make_decoder(buffer);
int value;
dec(value);  // Must know it's an int
```

This approach has limitations:
- Cannot handle heterogeneous data streams
- Requires full decoding even if only some items are needed
- Difficult to filter or transform CBOR data
- No integration with std::ranges algorithms

## Solution: Range-Based Decoding

### Basic Concept

The new approach treats CBOR buffers as ranges of items that can be lazily decoded:

```cpp
// Decode a stream of integers
auto integers = cbor_buffer | decode<int64_t>();
for (const auto& n : integers) {
    fmt::print("{}\n", n);
}
```

### Key Features

1. **Lazy Evaluation**: Items are decoded only when accessed
2. **Type Filtering**: Automatically skip items that don't match the requested type
3. **Variant Support**: Decode heterogeneous data using std::variant
4. **Ranges Integration**: Full compatibility with std::ranges algorithms

## API Design

### Single Type Decoding

```cpp
// Decode only integers from a mixed CBOR stream
auto integers = buffer | decode<int64_t>();
```

### Variant-Based Decoding

```cpp
// Decode multiple types, skip others
using my_types = std::variant<int64_t, double, std::string>;
auto items = buffer | decode<my_types>();

for (const auto& item : items) {
    std::visit([](const auto& val) {
        fmt::print("Got: {}\n", val);
    }, item);
}
```

### Decode Any CBOR Item

```cpp
// Decode all CBOR items as a variant of all possible types
auto all_items = buffer | decode_any();
```

### Integration with Ranges

```cpp
// Chain with standard range algorithms
auto result = buffer 
    | decode<int64_t>()
    | std::views::filter([](int64_t n) { return n > 0; })
    | std::views::transform([](int64_t n) { return n * 2; })
    | std::views::take(10);
```

## Use Cases

### 1. Heterogeneous Data Processing

```cpp
// Process sensor data with mixed types
struct Temperature { double value; };
struct Humidity { double value; };
struct Alert { std::string message; };

using sensor_data = std::variant<Temperature, Humidity, Alert>;
auto readings = buffer | decode<sensor_data>();

for (const auto& reading : readings) {
    std::visit(overloaded{
        [](const Temperature& t) { log_temperature(t.value); },
        [](const Humidity& h) { log_humidity(h.value); },
        [](const Alert& a) { send_alert(a.message); }
    }, reading);
}
```

### 2. Data Filtering

```cpp
// Extract only tagged items with specific tag
struct MyData {
    static constexpr uint64_t cbor_tag = 100;
    int64_t value;
};

auto my_items = buffer | decode<MyData>();
// Automatically skips all non-MyData items
```

### 3. Streaming Processing

```cpp
// Process large CBOR streams without loading all into memory
auto process_stream(std::istream& input) {
    std::vector<uint8_t> chunk(4096);
    
    while (input.read(chunk.data(), chunk.size())) {
        auto items = chunk | decode<my_type>();
        for (const auto& item : items | std::views::take(10)) {
            // Process only first 10 items per chunk
            process(item);
        }
    }
}
```

### 4. CBOR Data Analysis

```cpp
// Count occurrences of each type
auto analyze_cbor(const std::vector<uint8_t>& buffer) {
    size_t integers = 0, floats = 0, strings = 0;
    
    for (const auto& item : buffer | decode_any()) {
        std::visit(overloaded{
            [&](uint64_t) { integers++; },
            [&](int64_t) { integers++; },
            [&](double) { floats++; },
            [&](std::string_view) { strings++; },
            [](auto&&) { /* other types */ }
        }, item);
    }
    
    return std::tuple{integers, floats, strings};
}
```

## DOM-Style Access

Building on ranges, we can create a DOM-like interface:

```cpp
auto dom = buffer | parse_as_dom;

// Access nested data
auto username = dom["users"][0]["name"].as_string();

// Iterate over arrays
for (const auto& user : dom["users"]) {
    fmt::print("User: {}\n", user["name"].as_string());
}

// Check types
if (dom["config"]["timeout"].is_integer()) {
    auto timeout = dom["config"]["timeout"].as_int();
}
```

### Lazy DOM Evaluation

The DOM structure is built lazily:
- Indexes are created on first access
- Values are decoded only when requested
- Memory efficient for large documents

## Simplified CBOR Printing

The range approach greatly simplifies CBOR data inspection:

```cpp
// Generic CBOR printer
void print_cbor(const std::vector<uint8_t>& buffer, size_t max_items = 100) {
    size_t count = 0;
    for (const auto& item : buffer | decode_any() | std::views::take(max_items)) {
        fmt::print("[{}] ", count++);
        std::visit([](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::span<const std::byte>>) {
                fmt::print("bytes[{}]", val.size());
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                fmt::print("null");
            } else {
                fmt::print("{}", val);
            }
        }, item);
        fmt::print("\n");
    }
}
```