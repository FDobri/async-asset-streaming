#include "stream.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl2.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv) try {
    if (argc < 2) throw std::runtime_error("usage: stream_scene PACK [--budget-kib N] [--smoke]");
    bool smoke = false;
    std::size_t budget_kib = 480;
    for (int i = 2; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        if (flag == "--smoke") {
            smoke = true;
        } else if (flag == "--budget-kib" && i + 1 < argc) {
            const std::string_view text(argv[++i]);
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), budget_kib);
            if (error != std::errc{} || end != text.data() + text.size() ||
                budget_kib < asset::tile_bytes / 1024 || budget_kib > 65536)
                throw std::invalid_argument("budget must be between 48 and 65536 KiB");
        } else {
            throw std::invalid_argument("usage: stream_scene PACK [--budget-kib N] [--smoke]");
        }
    }
    asset::Loader loader(argv[1], budget_kib * 1024);
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    auto* window = SDL_CreateWindow("Streaming landscape", 1100, 660,
                                    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
    const auto context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error(SDL_GetError()); }
    SDL_GL_SetSwapInterval(1);
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL2_Init();
    std::map<int, GLuint> textures;
    float camera = 0;
    bool running = true, automatic = true, loaded = false;
    int previous_lo = 0, previous_hi = -1;
    const int last = static_cast<int>(loader.count()) - 1;
    int frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
        }
        if (!running) break;
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto& io = ImGui::GetIO();
        if (automatic) camera = std::min(static_cast<float>(last), camera + io.DeltaTime * 1.1f);
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (!io.WantCaptureKeyboard)
            camera += (static_cast<int>(keys[SDL_SCANCODE_RIGHT]) - static_cast<int>(keys[SDL_SCANCODE_LEFT])) *
                io.DeltaTime * 4.0f;
        camera = std::clamp(camera, 0.0f, static_cast<float>(last));
        ImGui::Begin("Streaming");
        ImGui::Checkbox("Auto-scroll", &automatic);
        ImGui::SliderFloat("Camera", &camera, 0, static_cast<float>(last));
        const int center = static_cast<int>(camera);
        const int lo = std::max(0, center - 3), hi = std::min(last, center + 4);
        for (int i = previous_lo; i <= previous_hi; ++i) if (i < lo || i > hi) loader.cancel(i);
        previous_lo = lo;
        previous_hi = hi;
        for (auto it = textures.begin(); it != textures.end();) {
            if (it->first < lo || it->first > hi) {
                glDeleteTextures(1, &it->second);
                it = textures.erase(it);
            } else ++it;
        }
        for (int i = lo; i <= hi; ++i) {
            if (textures.contains(i)) continue;
            loader.request(i, 20 - std::abs(i - center));
            auto data = loader.view(i);
            if (!data || data->size() != asset::tile_bytes) continue;
            GLuint texture{};
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, asset::tile_edge, asset::tile_edge,
                         0, GL_RGB, GL_UNSIGNED_BYTE, data->data());
            textures.emplace(i, texture);
            loaded = true;
        }
        const auto stats = loader.stats();
        ImGui::Text("CPU decoded memory: %zu / %zu KiB", stats.used / 1024, stats.budget / 1024);
        ImGui::Text("GPU texture payload: %zu KiB", textures.size() * asset::tile_bytes / 1024);
        ImGui::Text("Pending: %zu   Resident: %zu   Failed: %zu", stats.queued, stats.resident, stats.failures);
        ImGui::Text("Frame: %.2f ms", io.DeltaTime * 1000);
        ImGui::End();
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.055f, 0.08f, 0.12f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-3.5, 3.5, -1, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        for (int i = lo; i <= hi; ++i) {
            auto it = textures.find(i);
            if (it != textures.end()) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, it->second);
                glColor3f(1, 1, 1);
            } else {
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.18f, 0.23f, 0.3f);
            }
            const auto x = i - camera - 0.5f;
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(x, -0.45f);
            glTexCoord2f(1, 0); glVertex2f(x + 0.98f, -0.45f);
            glTexCoord2f(1, 1); glVertex2f(x + 0.98f, 0.32f);
            glTexCoord2f(0, 1); glVertex2f(x, 0.32f);
            glEnd();
        }
        glDisable(GL_TEXTURE_2D);
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (smoke && (++frames >= 120 || loaded)) running = false;
    }
    for (const auto& [id, texture] : textures) glDeleteTextures(1, &texture);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return smoke && !loaded ? 1 : 0;
} catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
