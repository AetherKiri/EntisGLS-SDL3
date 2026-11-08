// Run with adb shell: no JVM or SDLActivity is available in this process.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
        std::exit(1);
    }
}
int wake(void*) {
    SDL_Delay(10);
    SDL_Event event{};
    event.type = SDL_EVENT_USER;
    return SDL_PushEvent(&event) ? 0 : 1;
}
}

int main() {
    SDL_SetMainReady();
    for (int session = 0; session < 2; ++session) {
        require(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen"), "select offscreen video");
        require(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "openslES"), "select native audio");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS), "initialize without Java");
        SDL_PathInfo info{};
        require(SDL_GetPathInfo(".", &info) && info.type == SDL_PATHTYPE_DIRECTORY, "relative filesystem access");
        require(!SDL_IOFromFile("aether-missing-native-host-fixture", "rb"), "missing file without Java asset fallback");
        require(!SDL_EnumerateDirectory("aether-missing-native-host-fixture",
            [](void*, const char*, const char*) { return SDL_ENUM_CONTINUE; }, nullptr),
            "missing directory without Java asset fallback");
        require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES), "GLES profile");
        require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3), "GLES version");
        auto* window = SDL_CreateWindow("native host", 32, 32, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window, "offscreen window");
        const auto context = SDL_GL_CreateContext(window);
        require(context, "offscreen GLES context");
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]{};
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        require(glGetError() == GL_NO_ERROR && pixel[0] == 255 && pixel[1] == 0, "presented pixels");

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {}
        auto* thread = SDL_CreateThread(wake, "native wake", nullptr);
        require(thread, "thread without Java");
        require(SDL_WaitEventTimeout(&event, 1000) && event.type == SDL_EVENT_USER, "portable event loop");
        int status = -1;
        SDL_WaitThread(thread, &status);
        require(status == 0, "event delivery");

        SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
        auto* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        require(audio, "native audio stream");
        short silence[960]{};
        require(SDL_PutAudioStreamData(audio, silence, sizeof(silence)), "queue audio");
        require(SDL_ResumeAudioStreamDevice(audio), "audio thread without Java");
        SDL_Delay(30);
        SDL_DestroyAudioStream(audio);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    std::puts("PASS: native Android initialization, GLES, events, audio and restart without Java");
}
