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

Use the Release subdirectory for multi-config builds. The demo requires desktop OpenGL 2.1 compatibility. Configure with `-DSTREAM_DEMO=OFF` for a headless packer and loader or `-DSTREAM_TRACY=ON` to capture read/decompression zones with Tracy 0.11.1.

Use the camera slider or arrow keys, or let auto-scroll move the camera. The overlay shows the decoded CPU budget, GPU texture payload, request status and frame time. `stream_scene sample.pack --smoke` exits after uploading a tile. The packer also accepts a list of ordinary files, using their argument positions as asset IDs.

## Data and scheduling

The little-endian pack begins with an ASP1 marker, an entry count and fixed-size offset/length records. A worker reads only the selected compressed entry and publishes an immutable decoded buffer. Priority sorts first and request age breaks ties. Requests can be reprioritized. Cancellation discards an obsolete in-flight result even when Zstandard has already started decompression.

The decoded budget includes cache-owned buffers, pinned views and an in-flight reservation. A shared view pins an object until all callers release it. The worker evicts the least recently used unpinned buffer to fulfill the next request. Temporary compressed bytes, directory data and GPU resources are measured separately from the decoded budget. The test fills a four-byte cache, pins its only block, cancels a pending load and verifies eviction and recovery.
