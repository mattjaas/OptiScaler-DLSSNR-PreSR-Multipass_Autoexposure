#pragma once
#include <cstdint>

namespace DlssNr::InterPass
{
enum class Path : uint32_t
{
    Off, ClassicReference, FusedReference, ClassicOptimized, FusedOptimized, Rgb20, Rgb16
};
inline const char* Name(Path path)
{
    switch (path)
    {
    case Path::Off: return "Off";
    case Path::ClassicReference: return "Classic reference";
    case Path::FusedReference: return "Fused reference";
    case Path::ClassicOptimized: return "Classic optimized";
    case Path::FusedOptimized: return "Fused optimized P50/general";
    case Path::Rgb20: return "Fused RGB20";
    case Path::Rgb16: return "Fused RGB16";
    }
    return "Unknown";
}
inline bool Fused(Path path)
{
    return path == Path::FusedReference || path == Path::FusedOptimized ||
           path == Path::Rgb20 || path == Path::Rgb16;
}
inline bool Optimized(Path path)
{
    return path >= Path::ClassicOptimized;
}
// Select using actual texture geometry, not rounded slider labels. Measurements:
// RTX 4090, TLOU Part I, Area/radius-one. Other settings retain exact classical filters.
inline Path Select(uint32_t mode, uint32_t nativeW, uint32_t nativeH, uint32_t workW,
                   uint32_t workH, uint32_t filter, uint32_t radius, float confidence)
{
    if (mode == 0) return Path::Off;
    if (mode == 1) return Path::ClassicReference;
    if (mode == 2) return Path::FusedReference;
    if (filter == 0 && radius == 1 && confidence > 0 && nativeW && nativeH && workW && workH)
    {
        if (uint64_t(workW) * 2 == nativeW && uint64_t(workH) * 2 == nativeH)
            return Path::FusedOptimized;
        if (nativeW > workW && nativeH > workH &&
            uint64_t(workW) * 1000 > uint64_t(nativeW) * 505 &&
            uint64_t(workH) * 1000 > uint64_t(nativeH) * 505 &&
            uint64_t(workW) * 10 <= uint64_t(nativeW) * 9 &&
            uint64_t(workH) * 10 <= uint64_t(nativeH) * 9)
        {
            const bool compact = uint64_t(nativeW) * 3 <= uint64_t(workW) * 5 &&
                                 uint64_t(nativeH) * 3 <= uint64_t(workH) * 5;
            return compact ? Path::Rgb16 : Path::Rgb20;
        }
    }
    return Path::ClassicOptimized;
}
} // namespace DlssNr::InterPass
