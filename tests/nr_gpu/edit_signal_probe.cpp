#include "pch.h"
#include "interpass_fixture.h"
#include <cmath>
#include <iomanip>
#include <set>

// Research probe: independent DDS/CPU reference and resident-stage GPU timing.
// No NGX inference, production dispatch or claim of temporal validation here.
struct EditImage
{
    std::string name;
    UINT width, height;
    std::vector<float> rgba;
};
EditImage Load(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    std::array<uint32_t, 37> h {};
    file.read(reinterpret_cast<char*>(h.data()), sizeof(h));
    if (!file || h[0] != 0x20534444 || h[1] != 124 || h[19] != 32 || h[21] != 0x30315844 || h[32] != 2 || h[33] != 3 ||
        h[34] || h[35] != 1 || h[36] || !h[3] || !h[4] || h[5] != h[4] * 16 || UINT64(h[3]) * h[4] > 32000000)
        throw std::runtime_error("Expected tight 2D FP32 edit DDS: " + path);
    EditImage image { path.substr(path.find_last_of("/\\") + 1), h[4], h[3], {} };
    image.rgba.resize(size_t(image.width) * image.height * 4);
    file.read(reinterpret_cast<char*>(image.rgba.data()), image.rgba.size() * 4);
    if (!file || file.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("DDS payload size");
    return image;
}
struct Parameters
{
    UINT width, height, phaseX = 0, phaseY = 0, gridX = 64, gridY = 36;
    float threshold = .14f;
    UINT quantilePermille = 900;
};
static_assert(sizeof(Parameters) == 32, "Probe cbuffer ABI");
struct ProbeFixture
{
    Runner& r;
    Parameters constants;
    ComPtr<ID3D12Resource> source, output, cb, readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> times;
    UINT64 frequency;
    ProbeFixture(Runner& runner, const EditImage& image) : r(runner), constants { image.width, image.height }
    {
        D3D12_RESOURCE_DESC d {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = image.width;
        d.Height = image.height;
        d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        source = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        UINT64 bytes;
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        auto upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        char* data;
        check(upload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
        for (UINT y = 0; y < image.height; ++y)
            std::memcpy(data + y * footprint.Footprint.RowPitch, image.rgba.data() + size_t(y) * image.width * 4,
                        image.width * 16);
        upload->Unmap(0, nullptr);
        r.begin();
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;
        dst.pResource = source.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        r.barrier(source.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        r.finish();
        d.Width = 65;
        d.Height = 37;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        output = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        r.device->GetCopyableFootprints(&d, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        readback = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        const auto cpu = r.heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv {};
        srv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        for (UINT i = 0; i < 6; ++i)
            r.device->CreateShaderResourceView(source.Get(), &srv, { cpu.ptr + SIZE_T(i) * r.stride });
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav {};
        uav.Format = d.Format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        for (UINT i = 6; i < 8; ++i)
            r.device->CreateUnorderedAccessView(output.Get(), nullptr, &uav, { cpu.ptr + SIZE_T(i) * r.stride });
        cb = r.buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        update(0, 0);
        D3D12_QUERY_HEAP_DESC q {};
        q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        q.Count = 8;
        check(r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
        times = r.buffer(64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        check(r.queue->GetTimestampFrequency(&frequency));
    }
    void update(UINT x, UINT y)
    {
        constants.phaseX = x;
        constants.phaseY = y;
        void* data;
        check(cb->Map(0, nullptr, &data));
        std::memcpy(data, &constants, sizeof(constants));
        cb->Unmap(0, nullptr);
    }
    void roots()
    {
        r.list->SetComputeRootSignature(r.root.Get());
        ID3D12DescriptorHeap* heaps[] { r.heap.Get() };
        r.list->SetDescriptorHeaps(1, heaps);
        r.list->SetComputeRootDescriptorTable(0, r.heap->GetGPUDescriptorHandleForHeapStart());
        r.list->SetComputeRootConstantBufferView(1, cb->GetGPUVirtualAddress());
    }
    void order()
    {
        D3D12_RESOURCE_BARRIER b {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        b.UAV.pResource = output.Get();
        r.list->ResourceBarrier(1, &b);
    }
    void record(ID3D12PipelineState* pso, ID3D12PipelineState* reduce)
    {
        r.list->SetPipelineState(pso);
        r.list->Dispatch(reduce ? 36 : 1, 1, 1);
        order();
        if (reduce)
        {
            r.list->SetPipelineState(reduce);
            r.list->Dispatch(1, 1, 1);
            order();
        }
    }
    std::array<float, 4> run(ID3D12PipelineState* pso, ID3D12PipelineState* reduce)
    {
        r.begin();
        roots();
        record(pso, reduce);
        r.barrier(output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = output.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        r.barrier(output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        r.finish();
        void* data;
        check(readback->Map(0, nullptr, &data));
        std::array<float, 4> result {};
        std::memcpy(result.data(), data, sizeof(result));
        readback->Unmap(0, nullptr);
        return result;
    }
    void timing(std::ofstream& stream, const std::string& scene, const std::string& method, ID3D12PipelineState* pso,
                ID3D12PipelineState* reduce)
    {
        update(3, 3);
        // Separate warmups for A(no meter) and B; no setup/readback in GPU windows.
        for (unsigned variant = 0; variant < 2; ++variant)
        {
            r.begin();
            roots();
            for (unsigned i = 0; i < 64; ++i)
                if (variant)
                    record(pso, reduce);
            r.finish();
        }
        for (unsigned block = 0; block < 4; ++block)
            for (unsigned quartet = 0; quartet < 16; ++quartet)
            {
                r.begin();
                roots();
                for (unsigned position = 0; position < 4; ++position)
                {
                    r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, position * 2);
                    for (unsigned repeat = 0; repeat < 8; ++repeat)
                        if (position == 1 || position == 2)
                            record(pso, reduce);
                    r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, position * 2 + 1);
                }
                r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 8, times.Get(), 0);
                r.finish();
                UINT64* data;
                check(times->Map(0, nullptr, reinterpret_cast<void**>(&data)));
                for (unsigned position = 0; position < 4; ++position)
                    stream << scene << ',' << constants.width << ',' << constants.height << ','
                           << constants.quantilePermille << ',' << method << ',' << block << ',' << quartet << ','
                           << position << ',' << (position == 1 || position == 2 ? "probe" : "none") << ','
                           << double(data[position * 2 + 1] - data[position * 2]) * 1000 / frequency / 8 << '\n';
                times->Unmap(0, nullptr);
            }
    }
};
std::pair<UINT, float> Reference(const EditImage& image, const Parameters& c)
{
    std::vector<float> values;
    UINT count = 0;
    for (UINT y = 0; y < c.gridY; ++y)
        for (UINT x = 0; x < c.gridX; ++x)
        {
            const UINT px = UINT(UINT64(x * 16 + c.phaseX * 2 + 1) * image.width / (c.gridX * 16));
            const UINT py = UINT(UINT64(y * 16 + c.phaseY * 2 + 1) * image.height / (c.gridY * 16));
            float a = 0;
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const float v = image.rgba[(size_t(py) * image.width + px) * 4 + channel];
                if (std::isfinite(v))
                    a = std::max(a, v);
            }
            count += a > c.threshold;
            values.push_back(a);
        }
    const UINT quantile = c.quantilePermille >= 1 && c.quantilePermille <= 1000 ? c.quantilePermille : 900;
    const size_t rank = (values.size() * quantile + 999) / 1000 - 1;
    std::nth_element(values.begin(), values.begin() + rank, values.end());
    return { count, values[rank] };
}
EditImage Synthetic(unsigned kind)
{
    EditImage image { "synthetic" + std::to_string(kind), 37, 29, {} };
    image.rgba.resize(size_t(image.width) * image.height * 4);
    for (size_t i = 0; i < image.rgba.size(); ++i)
    {
        const float v = float(int((i * 37) % 103) - 40) / 256;
        image.rgba[i] = kind == 0    ? 0
                        : kind == 1  ? v
                        : kind == 2  ? v * 100000
                        : kind == 3  ? v * .0001f
                        : i % 5 == 0 ? NAN
                        : i % 7 == 0 ? INFINITY
                                     : v;
    }
    return image;
}
int main(int argc, char** argv)
{
    try
    {
        bool hardware = true;
        std::string timingPath;
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--warp")
                hardware = false;
            else if (std::string(argv[i]) == "--timing" && i + 1 < argc)
                timingPath = argv[++i];
            else
                paths.push_back(argv[i]);
        Runner r(hardware);
        const std::string dir = "artifacts/edit-analysis";
        std::array<std::string, 5> names { "count64", "count256", "hist256", "tiled64", "tiledhist64" };
        std::array<ComPtr<ID3D12PipelineState>, 5> pipelines;
        for (unsigned i = 0; i < 5; ++i)
            pipelines[i] = r.pipeline(dir, names[i]);
        auto reduce = r.pipeline(dir, "reduce");
        auto reducehist = r.pipeline(dir, "reducehist");
        auto reduction = [&](unsigned method)
        {
            return method == 4 ? reducehist.Get() : method == 3 ? reduce.Get() : nullptr;
        };
        std::ofstream timing;
        if (!timingPath.empty())
        {
            timing.open(timingPath);
            if (!timing)
                throw std::runtime_error("Cannot create timing CSV");
            timing << std::setprecision(9)
                   << "scene,width,height,quantile_permille,method,block,quartet,position,variant,gpu_ms\n";
        }
        unsigned cases = 0;
        std::set<std::pair<UINT, UINT>> timedSizes;
        for (size_t n = 0; n < paths.size() + 5; ++n)
        {
            auto image = n < 5 ? Synthetic(unsigned(n)) : Load(paths[n - 5]);
            ProbeFixture f(r, image);
            for (UINT quantile : { 500u, 900u })
            {
                f.constants.quantilePermille = quantile;
                f.constants.threshold = quantile == 500 ? .021f : .14f;
                for (UINT py = 0; py < 8; ++py)
                    for (UINT px = 0; px < 8; ++px)
                    {
                        f.update(px, py);
                        const auto expected = Reference(image, f.constants);
                        for (unsigned method = 0; method < 5; ++method)
                        {
                            auto got = f.run(pipelines[method].Get(), reduction(method));
                            if (got[0] != expected.first || got[3] != 2304 ||
                                ((method == 2 || method == 4) &&
                                 !(got[1] <= expected.second && expected.second < got[2])))
                                throw std::runtime_error("Count/quantile assertion: " + image.name + "/" +
                                                         names[method]);
                            ++cases;
                        }
                    }
            }
            std::cout << image.name << ": 2 quantiles x 64 phases x 5 probe methods PASS\n" << std::flush;
            if (n == 1)
                for (UINT invalid : { 0u, 1001u, 0xffffffffu })
                {
                    f.constants.quantilePermille = invalid;
                    f.update(3, 3);
                    const auto expected = Reference(image, f.constants);
                    for (unsigned method = 0; method < 5; ++method)
                    {
                        auto got = f.run(pipelines[method].Get(), reduction(method));
                        if (got[0] != expected.first || got[3] != 2304 ||
                            ((method == 2 || method == 4) && !(got[1] <= expected.second && expected.second < got[2])))
                            throw std::runtime_error("Invalid quantile did not fall back to P90");
                        ++cases;
                    }
                }
            if (timing && n >= 5 && timedSizes.insert({ image.width, image.height }).second)
                for (UINT quantile : { 500u, 900u })
                {
                    f.constants.quantilePermille = quantile;
                    f.constants.threshold = quantile == 500 ? .021f : .14f;
                    for (unsigned method = 0; method < 5; ++method)
                        f.timing(timing, image.name, names[method], pipelines[method].Get(), reduction(method));
                }
        }
        std::cout << "Edit signal probe: " << cases << " independent CPU/GPU assertions PASS\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
