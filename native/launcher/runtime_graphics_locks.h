#pragma once
#include <sakura/sakura.h>

namespace entis::launcher {
// Window.Lock is a script operation, so cancellation can bypass its matching
// Unlock. Restore the caller's depth before the VM worker exits; otherwise
// the host can never acquire the graphics lock to finish destroying sprites.
class RuntimeGraphicsLocks {
    atomic_int_t initialDepth_;
public:
    RuntimeGraphicsLocks() : initialDepth_(SSystem::TestLocked()) {}
    ~RuntimeGraphicsLocks() {
        if (SSystem::TestLocked()) SSystem::UnlockAll();
        SSystem::Relock(initialDepth_);
    }
    RuntimeGraphicsLocks(const RuntimeGraphicsLocks&) = delete;
    RuntimeGraphicsLocks& operator=(const RuntimeGraphicsLocks&) = delete;
};
}
