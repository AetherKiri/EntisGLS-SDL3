#include "entisgls/provider.h"
#include "input.h"
#include "font_discovery.h"
#include "game_hooks.h"
#include "storage.h"
#include "compatibility/sdk/legacy/gls.h"
#include "launcher/game_config.h"
#include "launcher/runtime_session.h"
#include "extensions/emote/psb/psb_key_resolver.h"
#include "extensions/emote/psb/psb_key_runtime.h"
#include "platform/sdl/system.h"
#include "platform/sdl/runtime_control.h"
#include "platform/sdl/window.h"
#include "platform/sdl/sdl_sound_player.h"
#include "platform/sdl/sdk_image_codec.h"
#include "platform/sdl/sdl_pcm_stream.h"
#include "runtime/cotopha_port/legacy_window_draw.h"
#include "runtime/cotopha_port/legacy_sprite.h"
#include "runtime/cotopha_port/legacy_sprite_projection.h"
#include <sakuraglx/sprite/sglx_sprite_message.h>
#include <SDL3/SDL_main.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#endif

SakuraGL::SGLError sglStaticInitialize() { return SakuraGL::sglErrSuccess; }
SakuraGL::SGLError sglStaticFinalize() { return SakuraGL::sglErrSuccess; }

namespace aether::entisgls {
namespace {
namespace fs = std::filesystem;
namespace platform = study::platform::sdl;
std::atomic<void*> activeSession{nullptr};
// SDL owns an offscreen context; Godot may own a different OpenGL context on
// the same thread. Clear SDL's TLS binding before restoring the host context.
struct HostGraphicsScope {
#if defined(__APPLE__)
    CGLContextObj previous = CGLGetCurrentContext();
    ~HostGraphicsScope() {
        if (CGLGetCurrentContext() != previous) {
            if (SDL_GL_GetCurrentContext()) SDL_GL_MakeCurrent(SDL_GL_GetCurrentWindow(), nullptr);
            CGLSetCurrentContext(previous);
        }
    }
#endif
};

struct Player {
    engine_runtime_host_v1_t host{};
    entis::launcher::GameLaunchConfig config;
    std::shared_ptr<hooks::GameHooks> gameHooks;
    fs::path cacheBase, cache, root;
    std::string error, workerError, explicitConfig, debugSnapshot;
    std::thread worker;
    std::atomic<bool> finished{false};
    int exitCode = 0;
    bool opened = false, started = false, sdk = false, video = false, paused = false;
    bool stopping = false, ownsSession = false;
    bool exitNotified = false;
    uint32_t width = 640, height = 480;
    uint64_t serial = 0, delivered = 0, inputs = 0;
    std::vector<uint8_t> pixels;
    float gain = 1;

    void log(uint32_t level, const std::string& message) const {
        if (host.log) host.log(host.user_data, level, "entisgls", message.c_str());
    }
    static void capture(SDL_Window* window, void* opaque) {
        auto& p = *static_cast<Player*>(opaque);
        int w = 0, h = 0;
        if (!SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0 || w > 8192 || h > 8192)
            throw std::runtime_error("EntisGLS invalid frame dimensions");
        std::vector<uint8_t> next(size_t(w) * size_t(h) * 4);
        GLint framebuffer = 0, alignment = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, next.data());
        const auto glError = glGetError();
        glPixelStorei(GL_PACK_ALIGNMENT, alignment); glBindFramebuffer(GL_FRAMEBUFFER, GLuint(framebuffer));
        if (glError) throw std::runtime_error("EntisGLS frame readback failed: " + std::to_string(glError));
        const auto stride = size_t(w) * 4;
        for (int y = 0; y < h / 2; ++y)
            std::swap_ranges(next.begin() + y * stride, next.begin() + (y + 1) * stride,
                             next.begin() + (h - 1 - y) * stride);
        p.width = uint32_t(w); p.height = uint32_t(h); p.pixels.swap(next); ++p.serial;
    }
    void start() {
        void* expected = nullptr;
        if (!activeSession.compare_exchange_strong(expected, this))
            throw std::runtime_error("An EntisGLS runtime is already active");
        ownsSession = true;
        SDL_SetMainReady();
        if (!SDL_IsMainThread()) throw std::runtime_error("EntisGLS presentation must start on the host main thread");
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) throw std::runtime_error(SDL_GetError());
        video = true;
        platform::ResetRuntimeControl();
        platform::SetRuntimePaused(paused);
        studysteady::platform::SetSdlRuntimeAudioPaused(paused);
        ConfigureGameStorage(root, cache);
        entis::launcher::SetGamePsbKeyResolver(std::make_shared<entis::launcher::PsbKeyResolver>(root, cache, config.psbKey));
        platform::SetEmbeddedPresentation(true);
        platform::SetEmbeddedPlatformRequest(host.platform_request, host.user_data);
        platform::SetWindowDrawHandler(&StudySteadyDrawWindow);
        platform::SetFrameObserver(capture, this);
        SakuraGL::Initialize(); sdk = true;
        SakuraGL::RegisterSDLSoundPlayer(); SakuraGL::RegisterSDLImageDecoder();
        studysteady::platform::SetSdlMasterVolume(gain);
        worker = std::thread([this] {
            try { exitCode = RunLegacyGame(L"", &config); workerError = LegacyGameLastError(); }
            catch (const std::exception& e) { exitCode = -1; workerError = e.what(); }
            catch (...) { exitCode = -1; workerError = "Unknown EntisGLS interpreter exception"; }
            SSystem::SThread::ReleaseLocalStorage();
            finished.store(true, std::memory_order_release);
        });
        started = true;
        log(ENGINE_RUNTIME_LOG_INFO, "Running " + config.entryScript + " from " + config.configSource);
    }
    void pump() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // The embedded window receives only host input. SDL still pumps
            // main-thread work, but cannot steal focus/input from Godot.
            if (event.type == SDL_EVENT_QUIT && stopping) platform::ProcessWindowEvent(event);
        }
        platform::DrawWindows();
    }
    void stop() {
        if (!ownsSession) return;
        HostGraphicsScope graphics;
        stopping = true;
        platform::SetFrameObserver(nullptr, nullptr);
        platform::ResumeRuntimeForShutdown();
        if (worker.joinable()) {
            SDL_Event event{}; event.type = SDL_EVENT_QUIT;
            platform::ProcessWindowEvent(event);
            if (!finished.load(std::memory_order_acquire)) AbortLegacyGame();
            while (!finished.load(std::memory_order_acquire)) {
                try { pump(); } catch (const std::exception& e) { log(ENGINE_RUNTIME_LOG_ERROR, e.what()); }
                SDL_Delay(1);
            }
            worker.join();
        }
        platform::SetFrameObserver(nullptr, nullptr); platform::SetWindowDrawHandler(nullptr);
        platform::SetEmbeddedPlatformRequest(nullptr, nullptr);
        entis::launcher::SetGamePsbKeyResolver(nullptr);
        size_t remainingSprites = 0;
        VisitLegacySprites([](const SakuraGL::SGLSprite&, void* user) { ++*static_cast<size_t*>(user); }, &remainingSprites);
        log(remainingSprites ? ENGINE_RUNTIME_LOG_WARNING : ENGINE_RUNTIME_LOG_DEBUG,
            "Sprites remaining after VM shutdown: " + std::to_string(remainingSprites));
        if (sdk) { SakuraGL::Finalize(); sdk = false; }
        platform::CollectRetiredWindows(); platform::SetEmbeddedPresentation(false);
        studysteady::platform::SetSdlRuntimeAudioPaused(false);
        if (video) { SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO); video = false; }
        activeSession.store(nullptr); ownsSession = false;
    }
    ~Player() { try { stop(); } catch (...) { log(ENGINE_RUNTIME_LOG_ERROR, "EntisGLS shutdown failed"); } }
};
Player& cast(void* opaque) { return *static_cast<Player*>(opaque); }
template<class F> engine_result_t guard(void* opaque, F work) {
    if (!opaque) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& p = cast(opaque);
    HostGraphicsScope graphics;
    try { work(p); return ENGINE_RESULT_OK; }
    catch (const std::exception& e) { p.error = e.what(); p.log(ENGINE_RUNTIME_LOG_ERROR, p.error); return ENGINE_RESULT_INTERNAL_ERROR; }
    catch (...) { p.error = "Unknown EntisGLS failure"; return ENGINE_RESULT_INTERNAL_ERROR; }
}
void requireOpen(const Player& p) { if (!p.opened) throw std::runtime_error("EntisGLS game is not open"); }
int32_t probe(void*, const char* path) {
    if (!path || !*path) return 0;
    try { const auto config = entis::launcher::DiscoverGame(fs::u8path(path)); return config.entryScript.empty() ? 0 : 97; }
    catch (...) { return 0; }
}
engine_result_t create(void*, const engine_runtime_host_v1_t* host, const engine_create_desc_t* desc, void** out) {
    if (!out) return ENGINE_RESULT_INVALID_ARGUMENT;
    *out = nullptr;
    if (!host || !desc || host->struct_size < sizeof(*host) || desc->struct_size < sizeof(*desc)) return ENGINE_RESULT_INVALID_ARGUMENT;
    if (host->api_version != ENGINE_RUNTIME_PROVIDER_API_VERSION || desc->api_version != ENGINE_API_VERSION) return ENGINE_RESULT_NOT_SUPPORTED;
    if (!desc->writable_path_utf8 || !*desc->writable_path_utf8 || !desc->cache_path_utf8 || !*desc->cache_path_utf8) return ENGINE_RESULT_INVALID_ARGUMENT;
    try {
        auto p = std::make_unique<Player>(); p->host = *host;
        p->cacheBase = fs::absolute(fs::u8path(desc->cache_path_utf8)) / "entisgls";
        *out = p.release(); return ENGINE_RESULT_OK;
    } catch (...) { return ENGINE_RESULT_INTERNAL_ERROR; }
}
engine_result_t open(void* opaque, const char* path, const char* startup) {
    if (!path || !*path) return ENGINE_RESULT_INVALID_ARGUMENT;
    return guard(opaque, [&](Player& p) {
        if (p.opened) throw std::runtime_error("Recreate the EntisGLS provider before opening another game");
        p.config = {};
        p.cache = p.cacheBase;
        p.root = fs::canonical(fs::u8path(path));
        p.gameHooks = std::make_shared<hooks::GameHooks>();
        p.gameHooks->Load(p.root, "entisgls");
        const auto selection = !p.explicitConfig.empty() ? p.explicitConfig : startup && *startup ? startup : "";
        p.config = entis::launcher::DiscoverGame(p.root, fs::u8path(selection));
        for (const auto& font : DiscoverBundledFonts(p.root))
            p.config.bundledFonts.push_back("storage://game/" + font);
        p.cache /= p.config.gameId;
        ApplyGameHooks(p.root, p.cache, p.gameHooks, p.config);
        for (const auto &message : p.gameHooks->Messages()) p.log(ENGINE_RUNTIME_LOG_INFO, message);
        // The host can request a frame before the asynchronous VM creates its
        // first window. Supply an opaque black frame until capture replaces it.
        p.pixels.assign(size_t(p.width) * p.height * 4, 0);
        for (size_t i = 3; i < p.pixels.size(); i += 4) p.pixels[i] = 255;
        p.opened = true;
        for (const auto& warning : p.config.warnings) p.log(ENGINE_RUNTIME_LOG_WARNING, warning);
    });
}
engine_result_t tick(void* opaque, uint32_t) {
    return guard(opaque, [](Player& p) {
        requireOpen(p);
        if (!p.started) p.start();
        p.pump();
        if (p.finished.load(std::memory_order_acquire) && p.exitCode && !p.stopping)
            throw std::runtime_error(p.workerError.empty() ? "EntisGLS script terminated with error" : p.workerError);
        if (p.finished.load(std::memory_order_acquire) && !p.exitNotified && !p.stopping) {
            p.exitNotified = true;
            if (p.host.platform_request) p.host.platform_request(p.host.user_data, "runtime_end", "");
        }
        if (entis::launcher::HasGamePsbKeyError()) throw std::runtime_error(entis::launcher::GamePsbKeyError());
    });
}
engine_result_t input(void* opaque, const engine_input_event_t* event) {
    if (!event || event->struct_size < sizeof(*event)) return ENGINE_RESULT_INVALID_ARGUMENT;
    SDL_Event translated{}; std::string text;
    if (!TranslateInput(*event, translated, text)) return ENGINE_RESULT_NOT_SUPPORTED;
    return guard(opaque, [&](Player& p) {
        requireOpen(p);
        if (p.started && !p.paused && platform::DispatchHostEvent(translated)) ++p.inputs;
        if (p.started && !p.paused && event->type == ENGINE_INPUT_EVENT_BACK) {
            translated.type = SDL_EVENT_KEY_UP; translated.key.down = false; platform::DispatchHostEvent(translated);
        }
    });
}
engine_result_t pause(void* opaque, bool paused) {
    return guard(opaque, [&](Player& p) {
        requireOpen(p);
        if (p.paused == paused) return;
        p.paused = paused;
        if (p.started) {
            SDL_Event event{}; event.type = paused ? SDL_EVENT_WILL_ENTER_BACKGROUND : SDL_EVENT_DID_ENTER_FOREGROUND;
            platform::ProcessWindowEvent(event);
            platform::SetRuntimePaused(paused);
            if (!studysteady::platform::SetSdlRuntimeAudioPaused(paused)) throw std::runtime_error(SDL_GetError());
        }
    });
}
engine_result_t option(void* opaque, const engine_option_t* value) {
    if (!opaque || !value || !value->key_utf8 || !value->value_utf8) return ENGINE_RESULT_INVALID_ARGUMENT;
    const std::string key(value->key_utf8);
    if (key != "entisgls_config" && key != "master_volume") return ENGINE_RESULT_NOT_SUPPORTED;
    return guard(opaque, [&](Player& p) {
        if (key == "entisgls_config") {
            if (p.opened) throw std::runtime_error("Set EntisGLS configuration before opening the game");
            p.explicitConfig = value->value_utf8;
        } else {
            size_t used = 0; const auto gain = std::stof(value->value_utf8, &used);
            if (used != std::strlen(value->value_utf8) || !std::isfinite(gain) || gain < 0 || gain > 1)
                throw std::runtime_error("EntisGLS volume must be between 0 and 1");
            p.gain = gain;
            if (p.started && !studysteady::platform::SetSdlMasterVolume(gain)) throw std::runtime_error(SDL_GetError());
        }
    });
}
engine_result_t frame(void* opaque, engine_frame_desc_t* out) {
    if (!opaque || !out || out->struct_size < sizeof(*out)) return ENGINE_RESULT_INVALID_ARGUMENT;
    const auto& p = cast(opaque);
    *out = {}; out->struct_size = sizeof(*out); out->width = p.width; out->height = p.height;
    out->stride_bytes = p.width * 4; out->pixel_format = ENGINE_PIXEL_FORMAT_RGBA8888; out->frame_serial = p.serial;
    return ENGINE_RESULT_OK;
}
engine_result_t read(void* opaque, void* output, size_t size) {
    if (!opaque || !output) return ENGINE_RESULT_INVALID_ARGUMENT;
    auto& p = cast(opaque);
    if (p.pixels.empty()) return ENGINE_RESULT_INVALID_STATE;
    if (size < p.pixels.size()) return ENGINE_RESULT_INVALID_ARGUMENT;
    std::memcpy(output, p.pixels.data(), p.pixels.size()); p.delivered = p.serial; return ENGINE_RESULT_OK;
}
engine_result_t debug(void* opaque, char* output, uint32_t size, uint32_t* written) {
    if (!opaque || !written) return ENGINE_RESULT_INVALID_ARGUMENT;
    return guard(opaque, [&](Player& p) {
        if (!output || p.debugSnapshot.empty()) {
        nlohmann::json data = {{"runtime", "entisgls"}, {"title", p.config.title},
            {"entry", p.config.entryScript}, {"opened", p.opened}, {"started", p.started},
            {"finished", p.finished.load(std::memory_order_acquire)}, {"paused", p.paused},
            {"frame_serial", p.serial}, {"width", p.width}, {"height", p.height},
            {"input_events", p.inputs}, {"error", p.error}, {"visible_message_characters", 0}};
        data["save_directory"]=(p.root/"savedata").u8string();
        if(p.sdk&&!p.finished.load()) {
            auto nodes=nlohmann::json::array();
            VisitLegacySprites([](const SakuraGL::SGLSprite& sprite,void* user) {
                auto& nodes=*static_cast<nlohmann::json*>(user);if(nodes.size()>=256)return;
                const auto& param=sprite.GetParameter();double focal=0;bool fixed=false;
                const bool projection=GetLegacySpriteZScale(&sprite,focal,fixed);
                SakuraGL::SGLPaintParam paint;SakuraGL::SGLAffine affine;
                const bool paintable=sprite.GetPaintParam(paint,affine);
                const auto size=sprite.GetImageSize();LegacySpriteCamera camera;GetLegacySpriteCamera(&sprite,camera);
                nodes.push_back({{"address",uintptr_t(&sprite)},{"parent",uintptr_t(sprite.GetParent())},
                    {"image",{size.w,size.h}},{"children",sprite.GetChildCount()},
                    {"position",{param.vDst.x,param.vDst.y,param.vDst.z}},{"zoom",{param.vZoom.x,param.vZoom.y}},
                    {"transparency",param.nTransparency},{"visible",sprite.IsVisible()},
                    {"projection",projection},{"focal",focal},{"paintable",paintable},
                    {"affine",{affine.a11,affine.a22,affine.a13,affine.a23}},
                    {"camera",{camera.enabled,camera.position.x,camera.position.y,camera.position.z}}});
                if(auto* message=ESLTypeCast<SakuraGL::SGLSpriteMessage>(const_cast<SakuraGL::SGLSprite*>(&sprite))) {
                    bool visible=true;
                    for(auto* node=&sprite;node;node=node->GetParent())
                        if(!node->IsVisible()||node->GetTransparency()>=256)visible=false;
                    nodes.back()["message_characters"]=message->GetMessageCharacterCount();
                    nodes.back()["message_visible"]=visible;
                }
            },&nodes);data["sprites"]=std::move(nodes);
            size_t characters=0;
            for(const auto& node:data["sprites"])if(node.value("message_visible",false))characters+=node.value("message_characters",size_t(0));
            data["visible_message_characters"]=characters;
        }
        p.debugSnapshot = data.dump();
        }
        *written = uint32_t(p.debugSnapshot.size() + 1);
        if (output && size >= *written) {
            std::memcpy(output, p.debugSnapshot.c_str(), *written);
            p.debugSnapshot.clear();
        }
        else if (output) throw std::runtime_error("EntisGLS diagnostic buffer too small");
    });
}
}
}

extern "C" const engine_runtime_provider_v1_t* AetherEntisGLSProvider() {
    using namespace aether::entisgls;
    static const auto descriptor = [] {
        engine_runtime_provider_v1_t p{}; p.struct_size = sizeof(p); p.api_version = ENGINE_RUNTIME_PROVIDER_API_VERSION;
        p.runtime_id_utf8 = "entisgls"; p.display_name_utf8 = "EntisGLS / Cotopha"; p.priority = 97;
        p.probe = probe; p.create = create; p.destroy = [](void* p) { delete static_cast<Player*>(p); };
        p.open_game = open; p.tick = tick; p.pause = [](void* p) { return pause(p, true); };
        p.resume = [](void* p) { return pause(p, false); }; p.set_option = option;
        p.get_frame_desc = frame; p.read_frame_rgba = read; p.send_input = input;
        p.set_surface_size = [](void* p, uint32_t w, uint32_t h) { return p && w && h ? ENGINE_RESULT_OK : ENGINE_RESULT_INVALID_ARGUMENT; };
        p.get_frame_rendered_flag = [](void* p, uint32_t* out) {
            if (!p || !out) return ENGINE_RESULT_INVALID_ARGUMENT;
            *out = cast(p).serial != cast(p).delivered; return ENGINE_RESULT_OK;
        };
        p.get_last_error = [](void* p) { return p ? cast(p).error.c_str() : "Invalid EntisGLS runtime"; };
        p.get_plugin_debug_info = debug;
        p.get_renderer_info = [](void* p, char* out, uint32_t size) {
            constexpr char info[] = "runtime=entisgls renderer=embedded-opengl-rgba";
            if (!p || !out || size < sizeof(info)) return ENGINE_RESULT_INVALID_ARGUMENT;
            std::memcpy(out, info, sizeof(info)); return ENGINE_RESULT_OK;
        };
        p.get_text_input_state = [](void* p, uint32_t* out) {
            if (!p || !out) return ENGINE_RESULT_INVALID_ARGUMENT;
            *out = cast(p).started && platform::HostTextInputActive() ? ENGINE_TEXT_INPUT_STATE_ACTIVE : 0;
            return ENGINE_RESULT_OK;
        };
        return p;
    }();
    return &descriptor;
}
