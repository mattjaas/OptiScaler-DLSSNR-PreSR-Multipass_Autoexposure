#include "pch.h"
#include "interpass_fixture.h"
#include <iomanip>

// Synthetic resident copy stages only. Excludes NR/mode-43 producer, NGX,
// CPU allocation/submission/file I/O and queue separation in the game.
int main()
{
    try
    {
        Runner r(true);
        std::ofstream out("artifacts/edit-dump-guides-timing.csv");
        out << std::setprecision(9) << "width,height,variant,block,quartet,position,gpu_ms\n";
        for (auto size : { std::pair<UINT, UINT> { 2240, 1264 }, { 2496, 1404 } })
        {
            const UINT w = size.first, h = size.second;
            std::array<ComPtr<ID3D12Resource>, 3> source, snapshot, readback;
            std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 3> fp;
            const DXGI_FORMAT formats[] { DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT,
                                          DXGI_FORMAT_R32G32_FLOAT };
            for (unsigned i = 0; i < 3; ++i)
            {
                D3D12_RESOURCE_DESC d {};
                d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                d.Width = w;
                d.Height = h;
                d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
                d.Format = formats[i];
                source[i] = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
                snapshot[i] = r.resource(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_SOURCE);
                UINT64 bytes;
                r.device->GetCopyableFootprints(&d, 0, 1, 0, &fp[i], nullptr, nullptr, &bytes);
                readback[i] = r.buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
                auto upload = r.buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
                unsigned char* data;
                check(upload->Map(0, nullptr, reinterpret_cast<void**>(&data)));
                std::mt19937 random(108 + i);
                for (UINT64 b = 0; b < bytes; ++b)
                    data[b] = static_cast<unsigned char>(random() & 255);
                upload->Unmap(0, nullptr);
                r.begin();
                D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
                src.pResource = upload.Get();
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint = fp[i];
                dst.pResource = source[i].Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                r.barrier(source[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                r.finish();
            }
            auto imageCopy = [&](unsigned i)
            {
                r.barrier(source[i].Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                          D3D12_RESOURCE_STATE_COPY_SOURCE);
                r.barrier(snapshot[i].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
                r.list->CopyResource(snapshot[i].Get(), source[i].Get());
                r.barrier(snapshot[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
                r.barrier(source[i].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            };
            r.begin();
            imageCopy(0);
            r.finish(); // the shared raw-edit producer is outside all windows
            auto record = [&](bool guides)
            {
                if (guides)
                {
                    imageCopy(1);
                    imageCopy(2);
                }
                for (unsigned i = 0; i < (guides ? 3u : 1u); ++i)
                {
                    D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
                    src.pResource = snapshot[i].Get();
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst.pResource = readback[i].Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint = fp[i];
                    r.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }
            };
            for (bool guides : { false, true })
                for (unsigned warmup = 0; warmup < 8; ++warmup)
                {
                    r.begin();
                    record(guides);
                    r.finish();
                }
            ComPtr<ID3D12QueryHeap> queries;
            D3D12_QUERY_HEAP_DESC q {};
            q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            q.Count = 8;
            check(r.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
            auto times = r.buffer(64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            UINT64 frequency;
            check(r.queue->GetTimestampFrequency(&frequency));
            for (unsigned block = 0; block < 4; ++block)
                for (unsigned quartet = 0; quartet < 8; ++quartet)
                {
                    r.begin();
                    for (unsigned pos = 0; pos < 4; ++pos)
                    {
                        r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pos * 2);
                        record(pos == 1 || pos == 2);
                        r.list->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, pos * 2 + 1);
                    }
                    r.list->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 8, times.Get(), 0);
                    r.finish();
                    UINT64* stamps;
                    check(times->Map(0, nullptr, reinterpret_cast<void**>(&stamps)));
                    for (unsigned pos = 0; pos < 4; ++pos)
                        out << w << ',' << h << ',' << (pos == 1 || pos == 2 ? "edit_and_guides" : "edit_only") << ','
                            << block << ',' << quartet << ',' << pos << ','
                            << double(stamps[pos * 2 + 1] - stamps[pos * 2]) * 1000 / frequency << '\n';
                    times->Unmap(0, nullptr);
                }
            std::cout << w << 'x' << h << " copy timing complete\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
