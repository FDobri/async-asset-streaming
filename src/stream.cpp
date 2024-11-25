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

struct Loader::State {
    struct Request { int priority; std::uint64_t ticket; };
    struct Cached { std::shared_ptr<const Bytes> data; std::uint64_t access; };
    explicit State(const std::filesystem::path& path, std::size_t budget)
        : input(path, std::ios::binary), budget(budget) {
        std::array<char, 4> marker{};
        if (!input.read(marker.data(), marker.size()) || marker != signature)
            throw std::runtime_error("invalid pack signature");
        const auto length = std::filesystem::file_size(path);
        const auto count = read(input);
        if (!count || count > entry_limit || 12 + count * 24 > length)
            throw std::runtime_error("invalid pack entry count");
        auto minimum = 12 + count * 24;
        for (std::size_t i = 0; i < count; ++i) {
            Entry e{read(input), read(input), read(input)};
            if (!e.size || e.size > byte_limit || e.offset < minimum || e.offset > length ||
                !e.compressed || e.compressed > length - e.offset ||
                e.compressed > ZSTD_compressBound(e.size))
                throw std::runtime_error("invalid pack entry");
            minimum = e.offset + e.compressed;
            entries.push_back(e);
        }
        if (!budget) throw std::invalid_argument("budget must be positive");
        worker = std::thread([this] { run(); });
    }
    ~State() {
        { std::lock_guard guard(mutex); stopping = true; }
        wake.notify_all();
        worker.join();
    }
    std::ifstream input;
    std::vector<Entry> entries;
    const std::size_t budget;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<std::size_t, Request> pending;
    std::unordered_map<std::size_t, Cached> cache;
    std::unordered_map<std::size_t, bool> failed;
    std::thread worker;
    std::uint64_t counter{};
    std::size_t loading = entry_limit, resident_bytes{}, reserved{}, failures{};
    bool stopping{};

    bool admit(std::size_t bytes) {
        while (bytes > budget - resident_bytes) {
            auto victim = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (it->second.data.use_count() == 1 &&
                    (victim == cache.end() || it->second.access < victim->second.access))
                    victim = it;
            if (victim == cache.end()) return false;
            resident_bytes -= victim->second.data->size();
            cache.erase(victim);
        }
        return true;
    }
    std::shared_ptr<const Bytes> load(Entry e) {
#ifdef STREAM_TRACY
        ZoneScopedN("Read and decompress asset");
#endif
        Bytes compressed(static_cast<std::size_t>(e.compressed));
        auto output = std::make_shared<Bytes>(static_cast<std::size_t>(e.size));
        input.clear();
        input.seekg(static_cast<std::streamoff>(e.offset));
        if (!input.read(reinterpret_cast<char*>(compressed.data()), compressed.size())) return {};
        auto decoded = ZSTD_decompress(output->data(), output->size(), compressed.data(), compressed.size());
        return decoded == output->size() ? output : nullptr;
    }
    void run() {
        std::unique_lock lock(mutex);
        while (!stopping) {
            if (pending.empty()) {
                wake.wait(lock, [&] { return stopping || !pending.empty(); });
                continue;
            }
            const auto best = std::max_element(pending.begin(), pending.end(), [](const auto& a, const auto& b) {
                if (a.second.priority != b.second.priority) return a.second.priority < b.second.priority;
                return a.second.ticket > b.second.ticket;
            });
            const auto id = best->first;
            const auto ticket = best->second.ticket;
            const Entry e = entries[id];
            if (!admit(static_cast<std::size_t>(e.size))) {
                // Views can be released without a call into the loader.
                wake.wait_for(lock, std::chrono::milliseconds(5));
                continue;
            }
            loading = id;
            reserved = static_cast<std::size_t>(e.size);
            lock.unlock();
            std::shared_ptr<const Bytes> output;
            try { output = load(e); } catch (const std::bad_alloc&) {}
            lock.lock();
            loading = entry_limit;
            reserved = 0;
            const auto current = pending.find(id);
            if (current == pending.end() || current->second.ticket != ticket) continue;
            pending.erase(current);
            if (!output) { failed[id] = true; ++failures; continue; }
            cache.emplace(id, Cached{std::move(output), ++counter});
            resident_bytes += static_cast<std::size_t>(e.size);
        }
    }
};

Loader::Loader(const std::filesystem::path& path, std::size_t budget)
    : state_(std::make_unique<State>(path, budget)) {}
Loader::~Loader() = default;
std::size_t Loader::count() const noexcept { return state_->entries.size(); }
void Loader::request(std::size_t id, int priority) {
    auto& s = *state_;
    if (id >= count()) throw std::out_of_range("invalid asset ID");
    if (s.entries[id].size > s.budget) throw std::invalid_argument("asset exceeds budget");
    {
        std::lock_guard guard(s.mutex);
        if (s.failed.contains(id)) return;
        if (auto cached = s.cache.find(id); cached != s.cache.end()) {
            cached->second.access = ++s.counter;
            return;
        }
        auto [it, inserted] = s.pending.try_emplace(id, State::Request{priority, ++s.counter});
        if (!inserted) it->second.priority = priority;
    }
    s.wake.notify_one();
}
void Loader::cancel(std::size_t id) {
    { std::lock_guard guard(state_->mutex); state_->pending.erase(id); }
    state_->wake.notify_one();
}
std::shared_ptr<const Bytes> Loader::view(std::size_t id) {
    auto& s = *state_;
    std::lock_guard guard(s.mutex);
    auto it = s.cache.find(id);
    if (it == s.cache.end()) return {};
    it->second.access = ++s.counter;
    return it->second.data;
}
Stats Loader::stats() const {
    auto& s = *state_;
    std::lock_guard guard(s.mutex);
    return {s.resident_bytes + s.reserved, s.budget, s.pending.size(), s.cache.size(), s.failures};
}
}
