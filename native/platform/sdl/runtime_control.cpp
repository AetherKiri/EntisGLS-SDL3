#include "platform/sdl/runtime_control.h"
#include <sakura/sakura.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace study::platform::sdl {
namespace {
std::mutex mutex;
std::condition_variable changed;
std::atomic<bool> paused{false};
std::uint64_t offset=0, stopped=0;
}
void ResetRuntimeControl() {
    std::lock_guard<std::mutex> lock(mutex);
    paused=false;offset=stopped=0;changed.notify_all();
}
void SetRuntimePaused(bool value) {
    std::lock_guard<std::mutex> lock(mutex);
    if(value==paused.load())return;
    if(value)stopped=SDL_GetTicks();else offset+=SDL_GetTicks()-stopped;
    paused=value;changed.notify_all();
}
void ResumeRuntimeForShutdown() {SetRuntimePaused(false);}
std::uint64_t RuntimeMilliseconds() {
    std::lock_guard<std::mutex> lock(mutex);
    return (paused.load()?stopped:SDL_GetTicks())-offset;
}
void WaitForRuntimeResume() {
    if(!paused.load(std::memory_order_acquire)||SDL_IsMainThread())return;
    // A native method can enter a nested context while holding the SDK UI
    // lock. Let the host drain rendering/destruction tasks during suspension.
    const auto held=SSystem::UnlockAll();
    {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[]{return !paused.load();});}
    SSystem::Relock(held);
}
}
