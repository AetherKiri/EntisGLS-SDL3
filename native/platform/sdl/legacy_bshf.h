#pragma once
#include <sakura/sakura.h>
#include <sakuracl/erisa/sgl_erisa_crc32_context.h>

namespace study::platform::sdl {
// GLS3 NOA encoding 0x40000000: 32-byte BSHF blocks followed by XOR32.
// The caller supplies the archive's declared decoded length and password.
SakuraCL::CRC32InputStream* CreateLegacyBshfDecoder(
    SSystem::SInputStream* source, uint64_t decodedBytes, const wchar_t* password);
}
