#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace asset {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::size_t tile_edge = 128;
inline constexpr std::size_t tile_bytes = tile_edge * tile_edge * 3;
void pack(const std::filesystem::path& path, std::span<const Bytes> entries);
void make_tiles(const std::filesystem::path& path, std::size_t count);
}
