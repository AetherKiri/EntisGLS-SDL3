#pragma once
#include <sakura/sakura.h>

namespace study::platform::sdl {
inline atomic_int_t UnlockAllMutex(SSystem::SMutex* mutex) {
    // UnlockAll is nonvirtual in the SDK. UI windows store the shared global
    // lock as SMutex*, so calling the base method leaves a stale owner table
    // while another thread can already acquire the underlying mutex.
    if (auto* shared = ESLTypeCast<SSystem::SSharableMutex>(mutex))
        return shared->UnlockAll();
    return mutex ? mutex->UnlockAll() : 0;
}
}
