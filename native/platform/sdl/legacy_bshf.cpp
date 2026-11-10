#include "platform/sdl/legacy_bshf.h"
#include <algorithm>
#include <array>
#include <vector>

namespace study::platform::sdl {
namespace {
bool ReadExact(SSystem::SInputStream& source, void* output, size_t size) {
    auto* bytes = static_cast<uint8_t*>(output);
    while (size) {
        const auto count = source.Read(bytes, size);
        if (!count || count > size) return false;
        bytes += count; size -= count;
    }
    return true;
}

class DecodedBshf final : public SakuraCL::CRC32InputStream {
    std::vector<uint8_t> bytes_;
    SSystem::SMemoryReferenceFile memory_;
public:
    explicit DecodedBshf(std::vector<uint8_t> bytes)
        : CRC32InputStream(nullptr), bytes_(std::move(bytes)) {
        memory_.AttachMemory(bytes_.data(), bytes_.size());
        m_pStream = &memory_;
    }
};
}

SakuraCL::CRC32InputStream* CreateLegacyBshfDecoder(
        SSystem::SInputStream* source, uint64_t decodedBytes, const wchar_t* password) {
    if (!source || decodedBytes > 0x40000000) return nullptr;
    // The legacy context expands short byte passwords before rotating the
    // starting byte once per block. Preserve 8-bit overflow in that expansion.
    const auto utf8 = SSystem::SString(password ? password : L"").ToUTF8();
    std::vector<uint8_t> key(utf8.GetConstArray(), utf8.GetConstArray() + utf8.GetLength());
    if (key.empty()) key.push_back(' ');
    if (key.size() < 32) {
        key.push_back(0x1b);
        const auto length = key.size();
        while (key.size() < 32) key.push_back(uint8_t(key[key.size() % length] + key.back()));
    }
    std::vector<uint8_t> output(static_cast<size_t>(decodedBytes));
    std::array<uint8_t, 4> checksum{};
    size_t passwordOffset = 0;
    for (size_t offset = 0; offset < output.size(); offset += 32) {
        std::array<uint8_t, 32> encoded{}, decoded{}, used{};
        if (!ReadExact(*source, encoded.data(), encoded.size())) return nullptr;
        size_t position = passwordOffset;
        passwordOffset = (passwordOffset + 1) % key.size();
        unsigned bit = 0;
        for (unsigned i = 0; i < 256; ++i) {
            bit = (bit + key[position]) & 255;
            position = (position + 1) % key.size();
            unsigned byte = bit >> 3, mask = 0x80 >> (bit & 7);
            while (used[byte] == 255) { bit = (bit + 8) & 255; byte = bit >> 3; }
            while (used[byte] & mask) {
                ++bit; mask >>= 1;
                if (!mask) { bit = (bit + 8) & 255; byte = bit >> 3; mask = 0x80; }
            }
            used[byte] |= uint8_t(mask);
            if (encoded[i >> 3] & (0x80 >> (i & 7))) decoded[byte] |= uint8_t(mask);
        }
        const auto count = std::min(size_t(32), output.size() - offset);
        std::copy_n(decoded.begin(), count, output.begin() + offset);
        for (size_t i = 0; i < count; ++i) checksum[(offset + i) & 3] ^= decoded[i];
    }
    std::array<uint8_t, 4> expected{};
    if (!ReadExact(*source, expected.data(), expected.size()) || expected != checksum) return nullptr;
    return new DecodedBshf(std::move(output));
}
}
