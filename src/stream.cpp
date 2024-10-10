#include "stream.hpp"
#include <zstd.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace asset {
namespace {
constexpr std::size_t entry_limit = 4096, byte_limit = 64 * 1024 * 1024;
constexpr std::array<char, 4> signature{'A', 'S', 'P', '1'};
struct Entry { std::uint64_t offset, size, compressed; };
void write(std::ostream& out, std::uint64_t word) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.put(static_cast<char>(word >> shift));
}
std::uint64_t read(std::istream& in) {
    std::uint64_t word = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        auto c = in.get();
        if (c == std::char_traits<char>::eof()) throw std::runtime_error("truncated pack directory");
        word |= static_cast<std::uint64_t>(c) << shift;
    }
    return word;
}
}

}
