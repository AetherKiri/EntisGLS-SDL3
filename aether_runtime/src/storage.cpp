#include "storage.h"
#include "platform/sdl/system.h"
#include <SDL3/SDL.h>
#include <stdexcept>

namespace aether::entisgls {
void ConfigureGameStorage(const std::filesystem::path& gameRoot,
                          const std::filesystem::path& cacheRoot) {
    std::filesystem::create_directories(gameRoot / "savedata");
    std::filesystem::create_directories(cacheRoot);
    study::platform::sdl::SystemPaths paths;
    paths.assetsRoot = paths.storageRoot = cacheRoot.u8string();
    paths.localRoot = paths.gameRoot = gameRoot.u8string();
    if (!study::platform::sdl::ConfigureSystemPaths(paths))
        throw std::runtime_error(SDL_GetError());
}
}
