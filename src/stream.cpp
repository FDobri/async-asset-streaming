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

void pack(const std::filesystem::path& path, std::span<const Bytes> blobs) {
    if (blobs.empty() || blobs.size() > entry_limit) throw std::invalid_argument("invalid pack entry count");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create pack");
    out.write(signature.data(), signature.size());
    write(out, blobs.size());
    for (std::size_t i = 0; i < blobs.size() * 3; ++i) write(out, 0);
    std::vector<Entry> index;
    Bytes buffer;
    for (const auto& blob : blobs) {
        if (blob.empty() || blob.size() > byte_limit) throw std::invalid_argument("invalid asset size");
        buffer.resize(ZSTD_compressBound(blob.size()));
        const auto compressed = ZSTD_compress(buffer.data(), buffer.size(), blob.data(), blob.size(), 3);
        if (ZSTD_isError(compressed)) throw std::runtime_error(ZSTD_getErrorName(compressed));
        index.push_back({static_cast<std::uint64_t>(out.tellp()), blob.size(), compressed});
        out.write(reinterpret_cast<const char*>(buffer.data()), compressed);
    }
    out.seekp(12);
    for (const auto& e : index) { write(out, e.offset); write(out, e.size); write(out, e.compressed); }
    out.close();
    if (!out) throw std::runtime_error("could not write pack");
}
void make_tiles(const std::filesystem::path& path, std::size_t count) {
    if (!count || count > 512) throw std::invalid_argument("tile count must be 1–512");
    std::vector<Bytes> tiles(count, Bytes(tile_bytes));
    for (std::size_t id = 0; id < count; ++id)
        for (std::size_t y = 0; y < tile_edge; ++y)
            for (std::size_t x = 0; x < tile_edge; ++x) {
                auto* pixel = &tiles[id][(y * tile_edge + x) * 3];
                const auto grain = (x * x + y * x + id * 13) % 37;
                const auto contour = ((x + y + id * 5) / 14) % 2;
                pixel[0] = static_cast<std::uint8_t>(35 + id * 19 % 140 + grain);
                pixel[1] = static_cast<std::uint8_t>(50 + id * 7 % 120 + grain + contour * 25);
                pixel[2] = static_cast<std::uint8_t>(65 + id * 23 % 110 + grain);
            }
    pack(path, tiles);
}

}
