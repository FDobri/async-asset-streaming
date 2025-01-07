# Asynchronous Asset Streaming

A C++20 packer and background Zstandard loader with an SDL3/OpenGL scene. The scene scrolls across a strip of independently compressed textures.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
./build/asset_pack sample.pack --tiles 64
./build/stream_scene sample.pack
```
