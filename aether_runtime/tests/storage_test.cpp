#include "storage.h"
#include "platform/sdl/system.h"
#include <sakuragl/sakuragl.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace SSystem;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::string read(const wchar_t* path) {
    std::unique_ptr<SFileInterface> file(g_defURLOpener.NewOpenFile(path, SFile::shareRead));
    require(bool(file), "open existing save through SDK");
    std::string result(static_cast<size_t>(file->GetLength()), '\0');
    require(file->Read(result.data(), result.size()) == result.size(), "read entire save");
    return result;
}
void write(const wchar_t* path, const std::string& data) {
    std::unique_ptr<SFileInterface> file(g_defURLOpener.NewOpenFile(path, SFile::modeCreate));
    require(bool(file), "create save through SDK");
    require(file->Write(data.data(), data.size()) == data.size(), "write entire save");
}
struct Session {
    Session(const fs::path& game, const fs::path& cache) {
        aether::entisgls::ConfigureGameStorage(game, cache);
        SSystem::Initialize();
    }
    ~Session() { SSystem::Finalize(); }
};
}
int main(int argc, char** argv) {
    if (argc != 2 || !SDL_Init(0)) return 2;
    int result = 0;
    try {
        const auto root = fs::absolute(argv[1]);
        const auto game = root / fs::u8path(u8"遊戯 × 中文");
        const auto other = root / "other-game";
        const auto cache = root / "host-cache";
        fs::create_directories(game / "savedata");
        std::ofstream(game / "savedata/slot.dat", std::ios::binary) << "existing save";
        {
            Session session(game, cache);
            require(read(L"storage://game/savedata/slot.dat") == "existing save", "game save is readable without migration");
            require(read(L"local://savedata/slot.dat") == "existing save", "legacy local scheme uses the same save");
            write(L"local://savedata/new.dat", "new progress");
            require(read(L"storage://game/savedata/new.dat") == "new progress", "both schemes share writes");
        }
        {
            Session session(game, cache);
            require(read(L"storage://game/savedata/new.dat") == "new progress", "save survives SDK shutdown and reopen");
            write(L"storage://game/savedata/slot.dat", "updated progress");
            require(read(L"local://savedata/slot.dat") == "updated progress", "game scheme can update an existing save");
        }
        {
            Session session(other, cache);
            require(fs::is_directory(other / "savedata"), "fresh game creates savedata");
            write(L"local://savedata/slot.dat", "other progress");
            require(read(L"storage://game/savedata/slot.dat") == "other progress", "second game uses its own directory");
        }
        {
            Session session(game, root / "another-cache");
            require(read(L"local://savedata/slot.dat") == "updated progress", "cache changes do not relocate saves");
        }
        require(fs::exists(game / "savedata/new.dat"), "new save physically resides inside game directory");
        require(!fs::exists(cache / "savedata"), "cache contains no alternate savedata tree");
        std::puts("EntisGLS game-local storage PASS");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Storage FAIL: %s\n", error.what());
        result = 1;
    }
    SDL_Quit();
    return result;
}
