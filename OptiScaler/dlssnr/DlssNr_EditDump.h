#pragma once
#include "DlssNr_Profiles.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace DlssNr::EditDump
{
// A single filename under the DLL's capture directory, never an arbitrary path.
inline std::optional<std::string> Filename(std::string name)
{
    const auto clean = DlssNrProfiles::SanitiseName(name);
    if (!clean)
        return std::nullopt;
    name = *clean;
    if (name.size() >= 4 && DlssNrProfiles::EqualNoCase(std::string_view(name).substr(name.size() - 4), ".dds"))
        name.resize(name.size() - 4);
    const auto base = DlssNrProfiles::SanitiseName(name);
    return base ? std::optional<std::string>(*base + ".dds") : std::nullopt;
}
// DDS + DX10 header, R32G32B32A32_FLOAT, single uncompressed top-down 2D image.
// Separate from row writing so the header and readback padding contract are CPU-testable.
inline std::array<uint32_t, 37> Header(uint32_t width, uint32_t height)
{
    std::array<uint32_t, 37> h {};
    h[0] = 0x20534444u;
    h[1] = 124u;
    h[2] = 0x100fu;
    h[3] = height;
    h[4] = width;
    h[5] = width * 16u;
    h[19] = 32u;
    h[20] = 4u;
    h[21] = 0x30315844u; // DDPF_FOURCC / DX10
    h[27] = 0x1000u;     // DDSCAPS_TEXTURE
    h[32] = 2u;
    h[33] = 3u;
    h[35] = 1u; // DXGI_FORMAT / TEXTURE2D / arraySize
    return h;
}
template <class Write> bool WriteRows(uint32_t width, uint32_t height, const void* data, size_t rowPitch, Write write)
{
    if (!width || !height || width > 16384 || height > 16384 || !data || rowPitch < size_t(width) * 16)
        return false;
    const auto header = Header(width, height);
    if (!write(header.data(), sizeof(header)))
        return false;
    for (uint32_t y = 0; y < height; ++y)
        if (!write(static_cast<const char*>(data) + size_t(y) * rowPitch, size_t(width) * 16))
            return false;
    return true;
}
} // namespace DlssNr::EditDump
