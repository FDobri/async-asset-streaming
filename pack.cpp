#include "stream.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) try {
    if (argc < 3) throw std::invalid_argument("usage: asset_pack OUTPUT --tiles COUNT | FILE...");
    if (argc == 4 && std::string_view(argv[2]) == "--tiles") {
        std::string_view text(argv[3]);
        std::size_t count{};
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), count);
        if (ec != std::errc{} || ptr != text.data() + text.size()) throw std::invalid_argument("invalid count");
        asset::make_tiles(argv[1], count);
    } else {
        std::vector<asset::Bytes> blobs;
        for (int i = 2; i < argc; ++i) {
            std::ifstream in(argv[i], std::ios::binary);
            const auto size = std::filesystem::file_size(argv[i]);
            if (!in || !size || size > 64 * 1024 * 1024) throw std::runtime_error("invalid input file size");
            auto& blob = blobs.emplace_back(static_cast<std::size_t>(size));
            if (!in.read(reinterpret_cast<char*>(blob.data()), blob.size())) throw std::runtime_error("input read failed");
        }
        asset::pack(argv[1], blobs);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
