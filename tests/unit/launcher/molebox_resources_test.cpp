#include "launcher/molebox_resources.h"
#include "io/game_files.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace io = entis::io;
using Bytes = std::vector<uint8_t>;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F operation) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Malformed package was accepted");
}
std::string Text(const Bytes& bytes) { return {bytes.begin(), bytes.end()}; }

int main(int argc, char** argv) {
    try {
        Require(argc == 3, "Expected fixture and work directories");
        const auto valid = io::ReadFile(fs::path(argv[1]) / "valid.bin", 4096);
        const auto files = entis::launcher::ReadMoleboxResources(valid);
        Require(files.size() == 2, "Internal package enumeration");
        Require(Text(*files.at("patch.noa")) == "packaged archive", "Encrypted compressed block");
        Require(Text(*files.at(u8"漢化.csx")) == u8"中文对白", "UTF-16 package filename");
        Require(entis::launcher::ReadMoleboxResources(Bytes{1, 2, 3}).empty(), "Ordinary file is not a package");
        auto ordinary = valid; ordinary.resize(768);
        Require(entis::launcher::ReadMoleboxResources(ordinary).empty(), "Ordinary PE has no resources");
        for (const char* name : {"traversal.bin", "cycle.bin", "block-crc.bin", "oversize.bin"})
            Reject([&] { entis::launcher::ReadMoleboxResources(io::ReadFile(fs::path(argv[1]) / name, 4096)); });
        auto corrupt = valid; corrupt.back() ^= 1;
        Reject([&] { entis::launcher::ReadMoleboxResources(corrupt); });
        auto truncated = valid; truncated.resize(800);
        Reject([&] { entis::launcher::ReadMoleboxResources(truncated); });

        const auto root = fs::absolute(argv[2]); fs::create_directories(root);
        std::ofstream(root / "patch.noa") << "original archive";
        io::SetReadOnlyFiles(root, files);
        Require(!io::IsVirtual(root.parent_path()), "Overlay must not claim directories outside its game");
        Require(Text(io::ReadFile(root / "patch.noa", 4096)) == "packaged archive", "Packaged patch overrides original");
        Require(io::Stat(root / u8"漢化.csx").size == 12, "Packaged file absent from host storage");
        Require(!fs::exists(root / u8"漢化.csx"), "No extraction to game directory");
        Require(io::List(root).size() == 2, "Packaged files participate in enumeration without duplicates");
        auto* stream = io::Open(root / "patch.noa", "rb");
        io::SetReadOnlyFiles({}, {});
        char word[8]{};
        Require(std::fread(word, 1, 8, stream) == 8 && std::string(word, 8) == "packaged", "Open stream survives session teardown");
        std::fclose(stream);
        Require(Text(io::ReadFile(root / "patch.noa", 4096)) == "original archive", "Original remains unchanged");
        io::SetReadOnlyFiles(root, files);
        auto* save = io::Open(root / "save.dat", "wb");
        std::fputs("saved progress", save); std::fclose(save);
        io::SetReadOnlyFiles({}, {});
        Require(Text(io::ReadFile(root / "save.dat", 4096)) == "saved progress", "Saves use persistent storage");
        Reject([&] { io::SetReadOnlyFiles(root, {{"../escape", files.begin()->second}}); });
        io::SetReadOnlyFiles(root, {{"nested/translated.csx", files.begin()->second}});
        Require(io::Stat(root / "nested").kind == io::FileInfo::Kind::Directory, "Virtual parent directory");
        Require(io::List(root / "nested").at(0).name == "translated.csx", "Nested package enumeration");
        io::SetReadOnlyFiles({}, {});
        fs::remove(root / "save.dat"); fs::remove(root / "patch.noa");
        std::puts("Molebox resource overlay PASS");
        return 0;
    } catch (const std::exception& error) {
        io::SetReadOnlyFiles({}, {});
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
