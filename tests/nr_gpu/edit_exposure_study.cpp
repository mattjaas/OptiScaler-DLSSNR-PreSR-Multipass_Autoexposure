#include "pch.h"
#include "edit_signal_probe_fixture.h"
#include "superior_input_fixture.h"
// Separate the live and frozen Superior constant names in this standalone fixture.
namespace Live
{
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
}
#include "../../OptiScaler/dlssnr/DlssNr_PrivateExposure.h"
#include <iomanip>

// Offline research: run the committed production PSOs on captured inputs.
// No live game, NGX inference, production shader edits or performance claims.
namespace PE = DlssNr::PrivateExposure;
using PrivateConstants = DlssNr::Superior::DlssNrConstants;
constexpr auto SrvState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto UavState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
struct ExposureStudy
{
    Runner r;
    ComPtr<ID3D12PipelineState> privatePso, automaticPso;
    explicit ExposureStudy(bool hardware, bool privateOnly) : r(hardware)
    {
        privatePso = r.pipeline("OptiScaler/shaders/dlssnr/superior/precompile", "private_exposure");
        if (!privateOnly)
            automaticPso = r.pipeline("OptiScaler/shaders/dlssnr/precompile", "DlssNr");
    }
    ComPtr<ID3D12Resource> texture(UINT w, UINT h, DXGI_FORMAT format)
    {
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = format;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return r.resource(d, D3D12_HEAP_TYPE_DEFAULT, UavState);
    }
    ComPtr<ID3D12Resource> input(const EditImage& image, int channel)
    {
        auto t = texture(image.width, image.height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        const auto d = t->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
        UINT64 bytes;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        char* mapped;
        check(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
        for (UINT y = 0; y < image.height; ++y)
            for (UINT x = 0; x < image.width; ++x)
            {
                auto* pixel = reinterpret_cast<uint16_t*>(mapped + y * fp.Footprint.RowPitch + x * 8);
                for (unsigned c = 0; c < 3; ++c)
                {
                    const float edit = image.rgba[(size_t(y) * image.width + x) * 4 + (channel < 0 ? c : channel)];
                    if (!std::isfinite(edit))
                        throw std::runtime_error("Nonfinite captured edit");
                    pixel[c] = InputFixture::halfBits(std::clamp(.5f + edit, 0.f, 65504.f));
                }
                pixel[3] = InputFixture::halfBits(1.f);
            }
        upload->Unmap(0, nullptr);
        r.begin();
        r.barrier(t.Get(), UavState, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        dst.pResource = t.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        r.barrier(t.Get(), D3D12_RESOURCE_STATE_COPY_DEST, SrvState);
        r.finish();
        return t;
    }
    template <class Constants>
    void dispatch(ID3D12PipelineState* ps, const Constants& c, ID3D12Resource* source, ID3D12Resource* target, UINT gx,
                  UINT gy)
    {
        auto cpu = r.heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = source->GetDesc().Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        for (UINT i = 0; i < 6; ++i)
            r.device->CreateShaderResourceView(source, &srv, { cpu.ptr + SIZE_T(i) * r.stride });
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
        uav.Format = target->GetDesc().Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        for (UINT i = 6; i < 8; ++i)
            r.device->CreateUnorderedAccessView(target, nullptr, &uav, { cpu.ptr + SIZE_T(i) * r.stride });
        auto cb = r.buffer(sizeof(Constants), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data;
        check(cb->Map(0, nullptr, &data));
        std::memcpy(data, &c, sizeof(c));
        cb->Unmap(0, nullptr);
        r.begin();
        r.list->SetComputeRootSignature(r.root.Get());
        ID3D12DescriptorHeap* heaps[] { r.heap.Get() };
        r.list->SetDescriptorHeaps(1, heaps);
        r.list->SetComputeRootDescriptorTable(0, r.heap->GetGPUDescriptorHandleForHeapStart());
        r.list->SetComputeRootConstantBufferView(1, cb->GetGPUVirtualAddress());
        r.list->SetPipelineState(ps);
        r.list->Dispatch(gx, gy, 1);
        r.finish();
    }
    float read(ID3D12Resource* output)
    {
        const auto d = output->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp {};
        UINT64 bytes;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto readback = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        r.begin();
        r.barrier(output, UavState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = output;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        r.finish();
        void* p;
        check(readback->Map(0, nullptr, &p));
        const float value = *static_cast<float*>(p);
        readback->Unmap(0, nullptr);
        if (!std::isfinite(value) || value <= 0)
            throw std::runtime_error("No finite positive exposure reading");
        return value;
    }
    float privateExposure(ID3D12Resource* source, UINT w, UINT h, UINT method)
    {
        const UINT sw = PE::SampleExtent(w, w, method), sh = PE::SampleExtent(h, h, method);
        const UINT bx = (sw + 31) / 32, by = (sh + 31) / 32;
        auto sums = texture(bx, by, DXGI_FORMAT_R32_FLOAT), output = texture(1, 1, DXGI_FORMAT_R32G32_FLOAT);
        PrivateConstants c {};
        c.Mode = 40;
        c.Width = sw;
        c.Height = sh;
        c.ExposureMode = method;
        c.PreExposure = 1;
        c.ExposureSourceWidth = w;
        c.ExposureSourceHeight = h;
        dispatch(privatePso.Get(), c, source, sums.Get(), bx, by);
        r.begin();
        r.barrier(sums.Get(), UavState, SrvState);
        r.finish();
        c.Mode = 41;
        c.Width = bx;
        c.Height = by;
        c.DebugView = 1;
        c.ApplyModel = 1; // fresh measured frame
        dispatch(privatePso.Get(), c, sums.Get(), output.Get(), 1, 1);
        return read(output.Get());
    }
    ComPtr<ID3D12Resource> tiles(ID3D12Resource* source)
    {
        auto tiles = texture(64, 64, DXGI_FORMAT_R32_FLOAT);
        Live::DlssNrConstants c {};
        c.Mode = Live::DlssNrMode_Meter;
        c.Width = c.Height = 64;
        dispatch(automaticPso.Get(), c, source, tiles.Get(), 64, 64);
        r.begin();
        r.barrier(tiles.Get(), UavState, SrvState);
        r.finish();
        return tiles;
    }
    float automatic(ID3D12Resource* tiles, UINT w, UINT h, float protection, int low = -1, int high = -1)
    {
        auto output = texture(1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
        Live::DlssNrConstants c {};
        c.Mode = Live::DlssNrMode_AutoExposure;
        c.Width = c.Height = 1;
        c.PreExposure = 1;
        c.ExposureSourceWidth = w;
        c.ExposureSourceHeight = h;
        c.AutoExposureShadowProtection = protection;
        c.ExposureTrimAnchorCount = low < 0 ? 0 : 1;
        c.ExposureTrimAnchorExposure0 = float(low);
        c.ExposureTrimAnchorTrim0 = float(high);
        dispatch(automaticPso.Get(), c, tiles, output.Get(), 1, 1);
        return read(output.Get());
    }
};
int main(int argc, char** argv)
{
    try
    {
        bool hardware = true, privateOnly = false;
        std::string csvPath;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--warp")
                hardware = false;
            else if (arg == "--private-only")
                privateOnly = true;
            else if (arg == "--csv" && i + 1 < argc)
                csvPath = argv[++i];
            else
                paths.push_back(arg);
        }
        if (csvPath.empty() || paths.empty())
            throw std::runtime_error(
                "Usage: edit_exposure_study --csv result.csv [--warp --private-only] raw-edit.dds ...");
        if (!hardware && !privateOnly)
            throw std::runtime_error("Use --private-only on WARP: live NR percentile shader has a known WARP failure");
        ExposureStudy f(hardware, privateOnly);
        std::ofstream out(csvPath);
        if (!out)
            throw std::runtime_error("Cannot create CSV");
        out << std::setprecision(9) << "file,view,legacy_cnn,legacy_k,transformer2";
        if (!privateOnly)
            out << ",average_0,average_50,average_100,percentile_0_100,percentile_10_90,percentile_25_75";
        out << '\n';
        for (const auto& path : paths)
        {
            const auto image = Load(path);
            for (int channel = -1; channel < 3; ++channel)
            {
                auto input = f.input(image, channel);
                out << image.name << ',' << (channel < 0 ? "rgb" : channel == 0 ? "r" : channel == 1 ? "g" : "b");
                for (UINT mode : { PE::LegacyCNN, PE::LegacyK, PE::Transformer2 })
                    out << ',' << f.privateExposure(input.Get(), image.width, image.height, mode);
                if (!privateOnly)
                {
                    auto tiles = f.tiles(input.Get());
                    for (float protection : { 0.f, 50.f, 100.f })
                        out << ',' << f.automatic(tiles.Get(), image.width, image.height, protection);
                    for (auto window : { std::pair<int, int> { 0, 100 }, { 10, 90 }, { 25, 75 } })
                        out << ','
                            << f.automatic(tiles.Get(), image.width, image.height, 0, window.first, window.second);
                }
                out << '\n';
            }
            std::cout << image.name << " production exposure PSOs complete\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
