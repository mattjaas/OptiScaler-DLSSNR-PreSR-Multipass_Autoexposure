#pragma once
#include "DlssNr_Readback.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace DlssNr
{
namespace AnalysisCapture
{
inline float HalfToFloat(uint16_t h)
{
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1fu;
    uint32_t mant = h & 0x03ffu;
    uint32_t bits = 0;
    if (exp == 0)
    {
        if (mant == 0)
            bits = sign;
        else
        {
            int e = -14;
            while ((mant & 0x0400u) == 0)
            {
                mant <<= 1;
                --e;
            }
            mant &= 0x03ffu;
            bits = sign | (uint32_t(e + 127) << 23) | (mant << 13);
        }
    }
    else if (exp == 31)
        bits = sign | 0x7f800000u | (mant << 13);
    else
        bits = sign | ((exp - 15u + 127u) << 23) | (mant << 13);
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

inline float DecodeUnsignedFloat(uint32_t value, unsigned mantissaBits)
{
    const uint32_t mantissaMask = (1u << mantissaBits) - 1u;
    const uint32_t mantissa = value & mantissaMask;
    const uint32_t exponent = (value >> mantissaBits) & 0x1fu;
    if (exponent == 0u)
        return mantissa == 0u ? 0.0f : std::ldexp(float(mantissa), 1 - 15 - int(mantissaBits));
    if (exponent == 31u)
        return mantissa == 0u ? std::numeric_limits<float>::infinity()
                              : std::numeric_limits<float>::quiet_NaN();
    return std::ldexp(1.0f + float(mantissa) / float(1u << mantissaBits), int(exponent) - 15);
}

inline uint32_t Crc32(const uint8_t* data, size_t bytes)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < bytes; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320u & uint32_t(0u - (crc & 1u)));
    }
    return ~crc;
}

inline uint32_t Adler32(const uint8_t* data, size_t bytes)
{
    constexpr uint32_t mod = 65521u;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < bytes; ++i)
    {
        a += data[i];
        if (a >= mod)
            a -= mod;
        b += a;
        b %= mod;
    }
    return (b << 16) | a;
}

inline void PutBe32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(uint8_t(value >> 24));
    out.push_back(uint8_t(value >> 16));
    out.push_back(uint8_t(value >> 8));
    out.push_back(uint8_t(value));
}

inline void AppendChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data)
{
    PutBe32(png, uint32_t(data.size()));
    const size_t crcStart = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    PutBe32(png, Crc32(png.data() + crcStart, 4 + data.size()));
}

inline bool WritePng16Rgb(const std::filesystem::path& path, unsigned width, unsigned height,
                          const std::vector<float>& rgb, float minimum, float maximum)
{
    if (!width || !height || rgb.size() != size_t(width) * height * 3)
        return false;
    if (!(maximum > minimum))
        maximum = minimum + 1.0f;
    const double inv = 65535.0 / double(maximum - minimum);

    std::vector<uint8_t> scan;
    scan.reserve(size_t(height) * (1 + size_t(width) * 6));
    for (unsigned y = 0; y < height; ++y)
    {
        scan.push_back(0); // PNG filter: None.
        for (unsigned x = 0; x < width; ++x)
        {
            const size_t p = (size_t(y) * width + x) * 3;
            for (unsigned c = 0; c < 3; ++c)
            {
                const float v = std::isfinite(rgb[p + c]) ? rgb[p + c] : 0.0f;
                const double qf = std::clamp((double(v) - minimum) * inv, 0.0, 65535.0);
                const uint16_t q = uint16_t(std::llround(qf));
                scan.push_back(uint8_t(q >> 8));
                scan.push_back(uint8_t(q));
            }
        }
    }

    // Minimal zlib stream using uncompressed DEFLATE blocks. PNG readers support this universally,
    // and avoiding a new compression dependency keeps this one-shot diagnostic self-contained.
    std::vector<uint8_t> z;
    z.reserve(scan.size() + scan.size() / 65535 * 5 + 16);
    z.push_back(0x78);
    z.push_back(0x01);
    size_t offset = 0;
    while (offset < scan.size())
    {
        const size_t block = std::min<size_t>(65535, scan.size() - offset);
        const bool final = offset + block == scan.size();
        z.push_back(final ? 0x01 : 0x00);
        const uint16_t len = uint16_t(block);
        const uint16_t nlen = uint16_t(~len);
        z.push_back(uint8_t(len));
        z.push_back(uint8_t(len >> 8));
        z.push_back(uint8_t(nlen));
        z.push_back(uint8_t(nlen >> 8));
        z.insert(z.end(), scan.begin() + offset, scan.begin() + offset + block);
        offset += block;
    }
    PutBe32(z, Adler32(scan.data(), scan.size()));

    std::vector<uint8_t> png { 137, 80, 78, 71, 13, 10, 26, 10 };
    std::vector<uint8_t> ihdr;
    PutBe32(ihdr, width);
    PutBe32(ihdr, height);
    ihdr.insert(ihdr.end(), { 16, 2, 0, 0, 0 }); // 16-bit, truecolour RGB.
    AppendChunk(png, "IHDR", ihdr);
    AppendChunk(png, "IDAT", z);
    AppendChunk(png, "IEND", {});

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    file.close();
    return !file.fail();
}

struct Image
{
    std::string name;
    ReadbackImage readback;
    std::vector<float> rgb;

    bool Decode()
    {
        const auto& f = readback.layout.Footprint;
        if (!f.Width || !f.Height || !readback.readback)
            return false;

        void* mapped = nullptr;
        D3D12_RANGE range { 0, SIZE_T(readback.bytes) }, empty {};
        if (FAILED(readback.readback->Map(0, &range, &mapped)))
            return false;

        rgb.assign(size_t(f.Width) * f.Height * 3, 0.0f);
        const auto* base = static_cast<const uint8_t*>(mapped);
        bool supported = true;
        for (unsigned y = 0; y < f.Height && supported; ++y)
        {
            const auto* row = base + size_t(y) * f.RowPitch;
            for (unsigned x = 0; x < f.Width; ++x)
            {
                float r = 0, g = 0, b = 0;
                switch (f.Format)
                {
                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                {
                    const auto* p = reinterpret_cast<const uint16_t*>(row + size_t(x) * 8);
                    r = HalfToFloat(p[0]); g = HalfToFloat(p[1]); b = HalfToFloat(p[2]);
                    break;
                }
                case DXGI_FORMAT_R32G32B32A32_FLOAT:
                {
                    const auto* p = reinterpret_cast<const float*>(row + size_t(x) * 16);
                    r = p[0]; g = p[1]; b = p[2];
                    break;
                }
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                {
                    const auto* p = row + size_t(x) * 4;
                    r = p[0] / 255.0f; g = p[1] / 255.0f; b = p[2] / 255.0f;
                    break;
                }
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                {
                    const auto* p = row + size_t(x) * 4;
                    r = p[2] / 255.0f; g = p[1] / 255.0f; b = p[0] / 255.0f;
                    break;
                }
                case DXGI_FORMAT_R10G10B10A2_UNORM:
                {
                    uint32_t packed = 0;
                    std::memcpy(&packed, row + size_t(x) * 4, sizeof(packed));
                    r = float(packed & 0x3ffu) / 1023.0f;
                    g = float((packed >> 10) & 0x3ffu) / 1023.0f;
                    b = float((packed >> 20) & 0x3ffu) / 1023.0f;
                    break;
                }
                case DXGI_FORMAT_R11G11B10_FLOAT:
                {
                    uint32_t packed = 0;
                    std::memcpy(&packed, row + size_t(x) * 4, sizeof(packed));
                    r = DecodeUnsignedFloat(packed & 0x7ffu, 6);
                    g = DecodeUnsignedFloat((packed >> 11) & 0x7ffu, 6);
                    b = DecodeUnsignedFloat((packed >> 22) & 0x3ffu, 5);
                    break;
                }
                default:
                    supported = false;
                    break;
                }
                if (!supported)
                    break;
                const size_t d = (size_t(y) * f.Width + x) * 3;
                rgb[d] = r; rgb[d + 1] = g; rgb[d + 2] = b;
            }
        }
        readback.readback->Unmap(0, &empty);
        return supported;
    }
};

struct Set
{
    std::vector<Image> images;
    std::filesystem::path directory;
    std::ostringstream metadata;
    std::function<bool()> complete;
    bool recorded = false;

    void Reset()
    {
        images.clear();
        directory.clear();
        metadata.str(std::string {});
        metadata.clear();
        complete = {};
        recorded = false;
    }

    bool Add(ID3D12GraphicsCommandList* cmd, ID3D12Device* device, const char* name,
             ID3D12Resource* resource, D3D12_RESOURCE_STATES state)
    {
        if (!cmd || !device || !resource)
            return false;
        Image image;
        image.name = name;
        if (!image.readback.Allocate(device, resource->GetDesc()))
            return false;
        image.readback.Copy(cmd, resource, state);
        images.push_back(std::move(image));
        return true;
    }

    bool Ready() const { return recorded && complete && complete(); }

    bool Write()
    {
        if (!Ready() || images.empty())
            return false;

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
            return false;

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        for (auto& image : images)
        {
            if (!image.Decode())
                return false;
            for (float v : image.rgb)
                if (std::isfinite(v))
                {
                    minimum = std::min(minimum, v);
                    maximum = std::max(maximum, v);
                }
        }
        if (!std::isfinite(minimum) || !std::isfinite(maximum))
            return false;
        if (minimum >= 0.0f)
            minimum = 0.0f;
        if (!(maximum > minimum))
            maximum = minimum + 1.0f;

        bool success = true;
        for (const auto& image : images)
        {
            success &= image.readback.Write(directory / (image.name + ".raw"));
            success &= WritePng16Rgb(directory / (image.name + ".png"),
                                     image.readback.layout.Footprint.Width,
                                     image.readback.layout.Footprint.Height,
                                     image.rgb, minimum, maximum);
        }

        std::ofstream manifest(directory / "manifest.txt");
        manifest << metadata.str();
        manifest << "png_encoding shared_linear_affine_rgb16\n"
                 << "png_min " << minimum << "\n"
                 << "png_max " << maximum << "\n"
                 << "png_decode float = uint16 / 65535 * (png_max - png_min) + png_min\n"
                 << "raw_files preserve the exact GPU readback bytes including row pitch; PNGs use one shared mapping "
                    "for direct cross-style comparison.\n";
        for (const auto& image : images)
        {
            const auto& f = image.readback.layout.Footprint;
            manifest << image.name << " width " << f.Width << " height " << f.Height << " format " << int(f.Format)
                     << " rowPitch " << f.RowPitch << "\n";
        }
        manifest.close();
        success &= !manifest.fail();
        return success;
    }
};
} // namespace AnalysisCapture
} // namespace DlssNr
