#include "stream.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;

int main() try {
    auto path = std::filesystem::temp_directory_path() /
        ("stream-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove(path, e); } } cleanup{path};
    const std::vector<asset::Bytes> data{{1, 2, 3, 4}, {4, 5, 6, 7}, {8, 9, 0, 1}};
    asset::pack(path, data);
    asset::Loader loader(path, 4);
    auto wait = [&](std::size_t id) {
        for (int i = 0; i < 1000; ++i) {
            if (auto result = loader.view(id)) return result;
            std::this_thread::sleep_for(2ms);
        }
        throw std::runtime_error("asset did not become resident");
    };
    loader.request(0, 1);
    auto pinned = wait(0);
    if (*pinned != data[0]) return 1;
    loader.request(1, 1);
    loader.request(2, 4);
    loader.cancel(1);
    std::this_thread::sleep_for(20ms);
    if (loader.stats().queued != 1 || loader.view(2)) return 1;
    pinned.reset();
    auto result = wait(2);
    if (*result != data[2] || loader.view(0) || loader.view(1) || loader.stats().used != 4) return 1;
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
