#pragma once
#include <cstdint>
namespace study::platform::sdl {
void ResetRuntimeControl();
void SetRuntimePaused(bool paused);
void ResumeRuntimeForShutdown();
void WaitForRuntimeResume();
std::uint64_t RuntimeMilliseconds();
}
