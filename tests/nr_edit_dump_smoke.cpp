#include <cassert>
#include <cstring>
#include <vector>
#include "../OptiScaler/dlssnr/DlssNr_EditDump.h"

int main()
{
    using namespace DlssNr::EditDump;
    assert(Filename("nr-edit") == "nr-edit.dds");
    assert(Filename(" test.DDS  ") == "test.dds");
    for (auto name : {"", ".dds", "../test", "a/b", "a\\b", "C:test", "CON", "NUL.dds", "COM1.dds", "a*", ".hidden"})
        assert(!Filename(name));
    // Two odd-width rows with padding. Inspect an independent fixed DDS/DX10 layout,
    // and ensure negative/HDR data are unmodified and row padding never reaches the file.
    constexpr uint32_t w = 3, h = 2;
    float data[2][16] {};
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w * 4; ++x) data[y][x] = float(int(x + y * 12) - 7) * .5f;
    for (auto& row : data) for (unsigned x = 12; x < 16; ++x) row[x] = 12345;
    std::vector<char> bytes;
    assert(WriteRows(w, h, data, sizeof(data[0]), [&](const void* p, size_t n)
    {
        bytes.insert(bytes.end(), static_cast<const char*>(p), static_cast<const char*>(p) + n); return true;
    }));
    assert(bytes.size() == 148 + w * h * 16);
    auto word = [&](size_t offset) { uint32_t v; std::memcpy(&v, bytes.data() + offset, 4); return v; };
    assert(word(0) == 0x20534444 && word(4) == 124 && word(12) == h && word(16) == w && word(20) == w * 16);
    assert(word(76) == 32 && word(80) == 4 && word(84) == 0x30315844);
    assert(word(108) == 0x1000 && word(128) == 2 && word(132) == 3 && word(140) == 1);
    for (unsigned y = 0; y < h; ++y) assert(std::memcmp(bytes.data() + 148 + y * w * 16, data[y], w * 16) == 0);
    const auto fail = [](const void*, size_t) { return false; };
    assert(!WriteRows(w, h, data, sizeof(data[0]), fail));
    assert(!WriteRows(0, h, data, sizeof(data[0]), fail));
    assert(!WriteRows(w, h, data, 4, fail));
    assert(!WriteRows(w, h, nullptr, sizeof(data[0]), fail));
}
