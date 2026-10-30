#pragma once
#include "AetherGameHooks.h"
#include "launcher/game_config.h"

namespace aether::entisgls {
// Lua is evaluated before the VM starts; the environment only sees an immutable
// file opener. The opener retains the session cache until all users release it.
void ApplyGameHooks(const std::filesystem::path &root, const std::filesystem::path &cache,
                    const std::shared_ptr<hooks::GameHooks> &hooks,
                    entis::launcher::GameLaunchConfig &config);
}
