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
struct Stats {
    std::size_t used, budget, queued, resident, failures;
};
class Loader {
    struct State;
public:
    Loader(const std::filesystem::path& path, std::size_t budget);
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;
    std::size_t count() const noexcept;
    void request(std::size_t id, int priority);
    void cancel(std::size_t id);
    // The shared view pins decoded bytes against eviction until released.
    std::shared_ptr<const Bytes> view(std::size_t id);
    Stats stats() const;
private:
    std::unique_ptr<State> state_;
};
}
