#include "pch.h"
#include <dlssnr/DlssNr_StreamlinePicture.h>
#include "DlssNr_Dx12_State.h"
#include <atomic>
#include <list>
#include "precompile/DlssNr_Shader.h"
#include "precompile/dlssnr_tiled_v16_wide16_Shader.h"
#include "precompile/dlssnr_tiled_v16_pair16_Shader.h"
#include "precompile/dlssnr_tiled_v16_both16_Shader.h"
#include "precompile/dlssnr_tiled_v16_wide20_Shader.h"
#include "precompile/dlssnr_tiled_v16_pair20_Shader.h"
#include "precompile/dlssnr_tiled_v16_both20_Shader.h"
#include "precompile/dlssnr_v16_low_Shader.h"

#include "precompile/dlssnr_tiled_Shader.h"
#include "precompile/dlssnr_tiled_strided_Shader.h"
#include "precompile/dlssnr_tiled_compact_Shader.h"
#include "precompile/dlssnr_tiled_linear16_Shader.h"
#include "precompile/dlssnr_tiled_v9_linear20_Shader.h"
#include "precompile/dlssnr_tiled_v9_linear16_Shader.h"
#include "precompile/dlssnr_tiled_v9_strided20_Shader.h"
#include "precompile/dlssnr_tiled_v9_strided16_Shader.h"
#include "precompile/dlssnr_classic_cache_Shader.h"
#include "precompile/dlssnr_tiled_v10_weights_Shader.h"
#include "precompile/dlssnr_tiled_v10_cache_Shader.h"
#include "precompile/dlssnr_tiled_v10_both_Shader.h"
#include "precompile/dlssnr_tiled_v11_spatial_Shader.h"
#include "precompile/dlssnr_tiled_v11_quadfill_Shader.h"
#include "precompile/dlssnr_tiled_v11_both_Shader.h"
#include "precompile/dlssnr_tiled_v12_interior_Shader.h"
#include "precompile/dlssnr_tiled_v12_axes_Shader.h"
#include "precompile/dlssnr_tiled_v12_both_Shader.h"
#include "precompile/dlssnr_tiled_v13_area_Shader.h"
#include "precompile/dlssnr_tiled_v13_mode28_Shader.h"
#include "precompile/dlssnr_tiled_v13_both_Shader.h"
#include "precompile/dlssnr_tiled_v14_rgb16_Shader.h"
#include "precompile/dlssnr_tiled_v14_guide16_Shader.h"
#include "precompile/dlssnr_tiled_v14_both16_Shader.h"
#include "precompile/dlssnr_tiled_v14_weights20_Shader.h"
#include "precompile/dlssnr_tiled_v14_rgb20_Shader.h"
#include "precompile/dlssnr_tiled_v14_guide20_Shader.h"
#include "precompile/dlssnr_tiled_v14_both20_Shader.h"
#include "precompile/dlssnr_classic_v10_cache_Shader.h"
#include "precompile/dlssnr_residual_Shader.h"
#include "precompile/dlssnr_finished_color_Shader.h"
#include "precompile/dlssnr_spatial_Shader.h"
#include "precompile/dlssnr_spatial_guides_Shader.h"

namespace
{
std::recursive_mutex nrOwnersMutex;
std::vector<DlssNr_Dx12*> nrOwners;
std::atomic_uint nrCaptureOutstanding { 0 };
// Only unresolved work at process teardown is intentionally retained. During the session
// retired owners stay registered for submission/reset callbacks until they can be reclaimed.
auto& RetiredNrOwners()
{
    static auto* owners = new std::list<std::unique_ptr<DlssNr_Dx12>>;
    return *owners;
}
unsigned nrNotificationDepth = 0;
void CollectRetiredNrOwners()
{
    static bool collecting = false;
    if (collecting || nrNotificationDepth)
        return;
    collecting = true;
    auto& owners = RetiredNrOwners();
    for (auto it = owners.begin(); it != owners.end();)
    {
        if (!(*it)->ReadyToDestroy())
        {
            ++it;
            continue;
        }
        auto finished = std::move(*it);
        it = owners.erase(it);
        finished.reset(); // May enqueue a child codec; list iterators remain valid.
        LOG_INFO("DLSS-NR: reclaimed retired GPU owner; {} waiting", owners.size());
    }
    collecting = false;
}
struct NrNotificationScope
{
    NrNotificationScope() { ++nrNotificationDepth; }
    ~NrNotificationScope()
    {
        --nrNotificationDepth;
        CollectRetiredNrOwners();
    }
};
DlssNr_Dx12* activeNrOwner = nullptr;
void ActivateNrOwner(DlssNr_Dx12* owner)
{
    if (std::find(nrOwners.begin(), nrOwners.end(), owner) == nrOwners.end())
        nrOwners.push_back(owner);
    activeNrOwner = owner;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// The pass itself. Everything above is what it is made of; everything below is the shape the rest
// of OptiScaler sees.
// ---------------------------------------------------------------------------------------------

DlssNr_Dx12::DlssNr_Dx12(std::string InName, ID3D12Device* InDevice)
    : Shader_Dx12(InName, InDevice), _state(std::make_unique<State>(*this))
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Five inputs, two outputs, one constant buffer, and a clamped linear sampler.
    //
    // The sampler exists because the model may be run below full resolution, in which case its answer
    // has to be read back at a different size from the frame it is being transferred onto.
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    if (!SetupRootSignature(InDevice, kSrvCount, kUavCount, 1, 0, 0, 1, &sampler))
    {
        LOG_ERROR("[{0}] Failed to setup root signature", _name);
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    for (uint32_t i = 0; i < DLSSNR_NUM_OF_HEAPS; ++i)
    {
        auto result = InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&_constantBuffers[i]));

        if (result != S_OK)
        {
            LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
            return;
        }
    }

    // Precompiled, with no source fallback. The shader used to be compiled at runtime from a string,
    // which would have meant no shader at all for anyone leaving UsePrecompiledShaders at its
    // default.
    if (!CreateComputePipeline(InDevice, &_pipelineState, DlssNr_cso, sizeof(DlssNr_cso), nullptr))
    {
        LOG_ERROR("[{0}] Failed to create the compute pipeline", _name);
        return;
    }

    // Second PSO for the ResidualAcrossRR v2 accumulator (its own blob, same root signature).
    // A failure here is not fatal to the class -- only that experimental mode goes unavailable.
    if (!CreateComputePipeline(InDevice, &_residualPipelineState, dlssnr_residual_cso, sizeof(dlssnr_residual_cso),
                               nullptr))
    {
        _residualPipelineState = nullptr;
        LOG_WARN("[{0}] ResidualAcrossRR compute pipeline unavailable", _name);
    }

    _init = InitHeaps(InDevice, _frameHeaps, DLSSNR_NUM_OF_HEAPS);
    if (_init)
        ResTrack_Dx12::HookLateNrQueue(InDevice); // Observe feature-creation submissions too, before the first Run.
    // Codec-only instances never call Dispatch/ProcessSeam, but still record GPU work.
    std::lock_guard lock(nrOwnersMutex);
    nrOwners.push_back(this);
}

bool DlssNr_Dx12::DispatchPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                               ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                               ID3D12Resource* InMotion, ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                               ID3D12Resource* OutKeep, uint32_t* immutableSlot)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    ID3D12PipelineState* pipeline = _pipelineState;
    if (InConstants.Mode == DlssNrMode_GuidedResidualLow && InConstants.Transfer == 0u &&
        (InConstants.DirectResolveFlags & 134217728u) != 0u)
    {
        if (!_v16LowPipelineAttempted)
        {
            _v16LowPipelineAttempted = true;
            if (!CreateComputePipeline(_device, &_v16LowPipelineState,
                                       dlssnr_v16_low_cso, sizeof(dlssnr_v16_low_cso), nullptr))
                LOG_WARN("[{0}] v16 Mode18 PSO unavailable; retaining reference", _name);
        }
        if (_v16LowPipelineState)
            pipeline = _v16LowPipelineState;
    }
    return DispatchCompute(InCmdList, InConstants, pipeline, InSource, InModel, InOriginal, InMotion, InPrevEdit,
                           nullptr, OutTarget, OutKeep, immutableSlot);
}

bool DlssNr_Dx12::DispatchPassAux2(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                                   ID3D12Resource* InSource, ID3D12Resource* InModel,
                                   ID3D12Resource* InOriginal, ID3D12Resource* InMotion,
                                   ID3D12Resource* InPrevEdit, ID3D12Resource* InAux2,
                                   ID3D12Resource* OutTarget, ID3D12Resource* OutKeep, uint32_t* immutableSlot)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    // Lazy, isolated PSOs. The standard DXIL and its fast dispatch are unchanged.
    ID3D12PipelineState* pipeline = _pipelineState;
    uint32_t groupWidth = 8;
    const auto& flags = InConstants.DirectResolveFlags;
    if (InConstants.Mode == DlssNrMode_InterPassGuidedP100 &&
        (flags & (1024u | 32768u)) == (1024u | 32768u))
    {
        if (!_classicV10CachePipelineAttempted)
        {
            _classicV10CachePipelineAttempted = true;
            if (!CreateComputePipeline(_device, &_classicV10CachePipelineState,
                                       dlssnr_classic_v10_cache_cso, sizeof(dlssnr_classic_v10_cache_cso), nullptr))
                LOG_WARN("[{0}] v10 Classic source/model cache PSO unavailable", _name);
        }
        if (_classicV10CachePipelineState)
            pipeline = _classicV10CachePipelineState;
        // No silent fallback to an old cached shader with different LDS behavior.
        // If v10 PSO is unavailable, reference v6 stencil is used with unchanged input.
    }
    if (InConstants.Mode == DlssNrMode_InterPassGuidedWorking &&
        InConstants.Transfer == 0u && (flags & 2048u) != 0u)
    {
        const bool compact = (flags & 8192u) != 0u;
        const bool strided = (flags & 4096u) != 0u;
        const bool v9 = (flags & (16384u | 32768u)) != 0u;
        // v14 variants preserve v10 reconstruction and always compile Mode28.
        // GuideOne is eligible only for exactly 1; never approximate user settings.
        const bool rgb14 = (flags & 4194304u) != 0u;
        const bool guide14 = (flags & 16777216u) != 0u &&
                             InConstants.ResidualConfidenceSensitivity == 1.0f;
        const bool linear20 = !compact && (flags & 8388608u) != 0u;
        const bool v14 = !strided && (flags & 16384u) != 0u &&
                         (flags & (32768u | 65536u | 131072u | 262144u | 524288u | 1048576u)) == 0u &&
                         (compact ? (rgb14 || guide14) : linear20);
        const bool wide16 = (flags & 33554432u) != 0u;
        const bool pair16 = (flags & 67108864u) != 0u;
        if (v14 && rgb14 && !guide14 && (wide16 || pair16))
        {
            const uint32_t index = (compact ? 0u : 3u) +
                                   (wide16 ? 1u : 0u) + (pair16 ? 2u : 0u) - 1u;
            const void* const blobs[] = { dlssnr_tiled_v16_wide16_cso,
                                          dlssnr_tiled_v16_pair16_cso,
                                          dlssnr_tiled_v16_both16_cso,
                                          dlssnr_tiled_v16_wide20_cso,
                                          dlssnr_tiled_v16_pair20_cso,
                                          dlssnr_tiled_v16_both20_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v16_wide16_cso),
                                     sizeof(dlssnr_tiled_v16_pair16_cso),
                                     sizeof(dlssnr_tiled_v16_both16_cso),
                                     sizeof(dlssnr_tiled_v16_wide20_cso),
                                     sizeof(dlssnr_tiled_v16_pair20_cso),
                                     sizeof(dlssnr_tiled_v16_both20_cso) };
            if (!_tiledV16PipelineAttempted[index])
            {
                _tiledV16PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV16PipelineState[index], blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v16 PSO {1} unavailable; retaining v14", _name, index);
                else
                    LOG_INFO("[{0}] v16 PSO {1} created (wide={2}, pair={3})", _name, index, wide16, pair16);
            }
            if (_tiledV16PipelineState[index])
            {
                pipeline = _tiledV16PipelineState[index];
                groupWidth = wide16 ? 16u : 8u;
            }
        }
        if (v14 && pipeline == _pipelineState)
        {
            const uint32_t combination = (rgb14 ? 1u : 0u) + (guide14 ? 2u : 0u);
            const uint32_t index = compact ? combination - 1u : 3u + combination;
            const void* const blobs[] = { dlssnr_tiled_v14_rgb16_cso, dlssnr_tiled_v14_guide16_cso,
                                          dlssnr_tiled_v14_both16_cso, dlssnr_tiled_v14_weights20_cso,
                                          dlssnr_tiled_v14_rgb20_cso, dlssnr_tiled_v14_guide20_cso,
                                          dlssnr_tiled_v14_both20_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v14_rgb16_cso), sizeof(dlssnr_tiled_v14_guide16_cso),
                                     sizeof(dlssnr_tiled_v14_both16_cso), sizeof(dlssnr_tiled_v14_weights20_cso),
                                     sizeof(dlssnr_tiled_v14_rgb20_cso), sizeof(dlssnr_tiled_v14_guide20_cso),
                                     sizeof(dlssnr_tiled_v14_both20_cso) };
            if (!_tiledV14PipelineAttempted[index])
            {
                _tiledV14PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV14PipelineState[index],
                                           blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v14 PSO {1} unavailable; retaining previous path", _name, index);
                else
                    LOG_INFO("[{0}] v14 PSO {1} created (pitch={2}, RGB={3}, guideOne={4})",
                             _name, index, compact ? 16 : 20, rgb14, guide14);
            }
            if (_tiledV14PipelineState[index])
                pipeline = _tiledV14PipelineState[index];
        }
        // v13: independent Area loops and constant Mode28 compilation.
        // Require older experiments OFF so this benchmark isolates one factor.
        const bool v13 = compact && !strided && (flags & 16384u) != 0u &&
                             (flags & (32768u | 65536u | 131072u | 262144u | 524288u)) == 0u &&
                             (flags & (1048576u | 2097152u)) != 0u;
        if (v13 && pipeline == _pipelineState)
        {
            const uint32_t index = ((flags & 1048576u) != 0u ? 1u : 0u) +
                                   ((flags & 2097152u) != 0u ? 2u : 0u) - 1u;
            const void* const blobs[] = { dlssnr_tiled_v13_area_cso,
                                          dlssnr_tiled_v13_mode28_cso,
                                          dlssnr_tiled_v13_both_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v13_area_cso),
                                     sizeof(dlssnr_tiled_v13_mode28_cso),
                                     sizeof(dlssnr_tiled_v13_both_cso) };
            if (!_tiledV13PipelineAttempted[index])
            {
                _tiledV13PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV13PipelineState[index],
                                           blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v13 optimized PSO {1} unavailable; retaining v10", _name, index);
            }
            if (_tiledV13PipelineState[index])
                pipeline = _tiledV13PipelineState[index];
        }
        // v12 experiments are independent of both v10 and v11. v12 takes
        // priority if the user intentionally enables flags from both versions.
        // Eligibility is enforced using real tile geometry and v10 weights.
        const bool v12 = compact && !strided && (flags & 16384u) != 0u &&
                         (flags & 32768u) == 0u &&
                         (flags & (262144u | 524288u)) != 0u;
        if (v12)
        {
            const uint32_t index = ((flags & 262144u) != 0u ? 1u : 0u) +
                                   ((flags & 524288u) != 0u ? 2u : 0u) - 1u;
            const void* const blobs[] = { dlssnr_tiled_v12_interior_cso,
                                          dlssnr_tiled_v12_axes_cso,
                                          dlssnr_tiled_v12_both_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v12_interior_cso),
                                     sizeof(dlssnr_tiled_v12_axes_cso),
                                     sizeof(dlssnr_tiled_v12_both_cso) };
            if (!_tiledV12PipelineAttempted[index])
            {
                _tiledV12PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV12PipelineState[index],
                                           blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v12 interior/axes PSO {1} unavailable; retaining v10", _name, index);
            }
            if (_tiledV12PipelineState[index])
                pipeline = _tiledV12PipelineState[index];
        }
        // v11 only for the verified P65 Linear16+Weights configuration.
        // All other routes, including P50 and cache, remain exactly v10.
        const bool v11 = compact && !strided && (flags & 16384u) != 0u &&
                         (flags & 32768u) == 0u &&
                         (flags & (65536u | 131072u)) != 0u;
        if (v11 && pipeline == _pipelineState)
        {
            const uint32_t index = ((flags & 65536u) != 0u ? 1u : 0u) +
                                   ((flags & 131072u) != 0u ? 2u : 0u) - 1u;
            const void* const blobs[] = { dlssnr_tiled_v11_spatial_cso,
                                          dlssnr_tiled_v11_quadfill_cso,
                                          dlssnr_tiled_v11_both_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v11_spatial_cso),
                                     sizeof(dlssnr_tiled_v11_quadfill_cso),
                                     sizeof(dlssnr_tiled_v11_both_cso) };
            if (!_tiledV11PipelineAttempted[index])
            {
                _tiledV11PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV11PipelineState[index],
                                           blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v11 optimized PSO {1} unavailable; retaining v10", _name, index);
            }
            if (_tiledV11PipelineState[index])
                pipeline = _tiledV11PipelineState[index];
        }
        if (v9 && pipeline == _pipelineState && compact && !strided)
        {
            // Fully compile-time specialized: NO dormant source LDS in weights-only DXIL.
            const uint32_t index = ((flags & 16384u) != 0u ? 1u : 0u) +
                                   ((flags & 32768u) != 0u ? 2u : 0u) - 1u;
            const void* const blobs[] = { dlssnr_tiled_v10_weights_cso,
                                          dlssnr_tiled_v10_cache_cso,
                                          dlssnr_tiled_v10_both_cso };
            const size_t sizes[] = { sizeof(dlssnr_tiled_v10_weights_cso),
                                     sizeof(dlssnr_tiled_v10_cache_cso),
                                     sizeof(dlssnr_tiled_v10_both_cso) };
            if (!_tiledV10PipelineAttempted[index])
            {
                _tiledV10PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV10PipelineState[index],
                                           blobs[index], sizes[index], nullptr))
                    LOG_WARN("[{0}] v10 isolated PSO {1} unavailable", _name, index);
            }
            if (_tiledV10PipelineState[index])
                pipeline = _tiledV10PipelineState[index];
        }
        if (v9 && pipeline == _pipelineState)
        {
            // (compact, strided) -> [linear20, linear16, strided20, strided16].
            const uint32_t index = (compact ? 1u : 0u) + (strided ? 2u : 0u);
            const void* const blobs[] = { dlssnr_tiled_v9_linear20_cso, dlssnr_tiled_v9_linear16_cso,
                                          dlssnr_tiled_v9_strided20_cso, dlssnr_tiled_v9_strided16_cso };
            const size_t lengths[] = { sizeof(dlssnr_tiled_v9_linear20_cso),
                                       sizeof(dlssnr_tiled_v9_linear16_cso),
                                       sizeof(dlssnr_tiled_v9_strided20_cso),
                                       sizeof(dlssnr_tiled_v9_strided16_cso) };
            if (!_tiledV9PipelineAttempted[index])
            {
                _tiledV9PipelineAttempted[index] = true;
                if (!CreateComputePipeline(_device, &_tiledV9PipelineState[index],
                                           blobs[index], lengths[index], nullptr))
                    LOG_WARN("[{0}] v9 tiled PSO variant {1} unavailable; falling back to v8", _name, index);
            }
            if (_tiledV9PipelineState[index])
                pipeline = _tiledV9PipelineState[index];
        }
        if (pipeline == _pipelineState && compact && strided)
        {
            if (!_tiledCompactPipelineAttempted)
            {
                _tiledCompactPipelineAttempted = true;
                if (!CreateComputePipeline(_device, &_tiledCompactPipelineState, dlssnr_tiled_compact_cso,
                                           sizeof(dlssnr_tiled_compact_cso), nullptr))
                    LOG_WARN("[{0}] v8 compact tiled PSO unavailable", _name);
            }
            if (_tiledCompactPipelineState)
                pipeline = _tiledCompactPipelineState;
        }
        if (pipeline == _pipelineState && compact && !strided)
        {
            if (!_tiledCompactLinearPipelineAttempted)
            {
                _tiledCompactLinearPipelineAttempted = true;
                if (!CreateComputePipeline(_device, &_tiledCompactLinearPipelineState,
                                           dlssnr_tiled_linear16_cso, sizeof(dlssnr_tiled_linear16_cso), nullptr))
                    LOG_WARN("[{0}] v9 linear16 tiled PSO unavailable", _name);
            }
            if (_tiledCompactLinearPipelineState)
                pipeline = _tiledCompactLinearPipelineState;
        }
        if (pipeline == _pipelineState && strided)
        {
            if (!_tiledStridedPipelineAttempted)
            {
                _tiledStridedPipelineAttempted = true;
                if (!CreateComputePipeline(_device, &_tiledStridedPipelineState, dlssnr_tiled_strided_cso,
                                           sizeof(dlssnr_tiled_strided_cso), nullptr))
                    LOG_WARN("[{0}] v8 strided tiled PSO unavailable", _name);
            }
            if (_tiledStridedPipelineState)
                pipeline = _tiledStridedPipelineState;
        }
        if (pipeline == _pipelineState)
        {
            if (!_tiledFusedPipelineAttempted)
            {
                _tiledFusedPipelineAttempted = true;
                if (!CreateComputePipeline(_device, &_tiledFusedPipelineState, dlssnr_tiled_cso,
                                           sizeof(dlssnr_tiled_cso), nullptr))
                    LOG_WARN("[{0}] v7 tiled PSO unavailable; using untiled reference", _name);
            }
            if (_tiledFusedPipelineState)
                pipeline = _tiledFusedPipelineState;
        }
    }
    return DispatchCompute(InCmdList, InConstants, pipeline, InSource, InModel, InOriginal, InMotion,
                           InPrevEdit, InAux2, OutTarget, OutKeep, immutableSlot, groupWidth);
}

bool DlssNr_Dx12::DispatchCompute(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                                  ID3D12PipelineState* pipeline, ID3D12Resource* InSource, ID3D12Resource* InModel,
                                  ID3D12Resource* InOriginal, ID3D12Resource* InMotion, ID3D12Resource* InPrevEdit,
                                  ID3D12Resource* InAux2, ID3D12Resource* OutTarget, ID3D12Resource* OutKeep,
                                  uint32_t* immutableSlot, uint32_t groupWidth)
{
    _state->lifetime.Record(InCmdList);
    if (!_init || !pipeline || !InCmdList || !_device || !InSource || !OutTarget)
        return false;

    const bool reuse = immutableSlot && *immutableSlot != UINT32_MAX;
    const uint32_t slot = reuse ? *immutableSlot : _heapIndex;
    if (!reuse)
        _heapIndex = (_heapIndex + 1) % DLSSNR_NUM_OF_HEAPS;

    FrameDescriptorHeap& currentHeap = _frameHeaps[slot];
    if (!reuse)
    {

        // Every slot in the table gets a view, whether the mode reads it or not. An unbound descriptor is
        // not an empty read; it is a read from nothing, and the source stands in wherever a mode has
        // nothing of its own to put there.
        ID3D12Resource* const srvs[kSrvCount] = {
            InSource,
            InModel != nullptr ? InModel : InSource,
            InOriginal != nullptr ? InOriginal : InSource,
            InMotion != nullptr ? InMotion : InSource,
            InPrevEdit != nullptr ? InPrevEdit : InSource,
            InAux2 != nullptr ? InAux2 : InSource,
        };

        for (uint32_t i = 0; i < kSrvCount; ++i)
        {
            // Copied depth guides already use an SRV format; the upstream translator maps it back to a DSV.
            const bool translate = srvs[i]->GetDesc().Format != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            CreateShaderResourceView(_device, srvs[i], currentHeap.GetSrvCPU(i), DXGI_FORMAT_UNKNOWN, translate);
        }

        ID3D12Resource* const uavs[kUavCount] = {
            OutTarget,
            OutKeep != nullptr ? OutKeep : OutTarget,
        };

        for (uint32_t i = 0; i < kUavCount; ++i)
            CreateUnorderedAccessView(_device, uavs[i], currentHeap.GetUavCPU(i), 0);

        if (!CreateConstantsBuffer(_device, _constantBuffers[slot], InConstants, currentHeap.GetCbvCPU(0)))
        {
            LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
            return false;
        }

        if (immutableSlot)
            *immutableSlot = slot;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(pipeline);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Sized from the constants rather than from a resource, because the pass that shrinks the proxy
    // writes fewer pixels than its source has.
    const UINT dispatchWidth = InConstants.Mode == DlssNrMode_Meter
                                   ? InConstants.Width
                                   : (InConstants.Width + groupWidth - 1) / groupWidth;
    const UINT dispatchHeight = InConstants.Mode == DlssNrMode_Meter
                                    ? InConstants.Height
                                    : (InConstants.Height + _numThreadsY - 1) / _numThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

void DlssNr_Dx12::Retire(std::unique_ptr<DlssNr_Dx12> owner)
{
    if (!owner)
        return;
    if (::State::Instance().isShuttingDown)
    {
        owner.release(); // No locks, GPU calls or destructors under the loader lock.
        return;
    }
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner == owner.get())
        activeNrOwner = nullptr;
    DlssNr::ClearStatus(owner.get());
    {
        std::lock_guard stateLock(owner->_state->mutex);
        owner->_state->late.Cancel();
    }
    RetiredNrOwners().push_back(std::move(owner));
    LOG_INFO("DLSS-NR: retaining retired GPU owner until recordings finish; {} waiting", RetiredNrOwners().size());
}

bool DlssNr_Dx12::ReadyToDestroy()
{
    std::lock_guard lock(_state->mutex);
    _state->CollectEnlargers();
    if (_state->collectingEnlargers)
        return false;
    if (!_state->retiredEnlargers.empty() || (_state->enlarger && !_state->enlarger->lifetime.Idle()))
        return false;
    if (!_state->lifetime.Idle() || !_state->deferredSr.lifetime.Idle())
        return false;
    for (auto& model : _state->nr.models)
        if (!model.Idle())
            return false;
    for (auto& model : _state->styleAnalysisCapture.models)
        if (!model.Idle())
            return false;
    for (auto& model : _state->styleAnalysisCapture.nativeModels)
        if (!model.Idle())
            return false;
    for (const auto& slot : _state->late.slots)
        if (slot.submitted && !_state->late.Finished(slot))
            return false;
    return _state->late.dx11.Idle();
}

void DlssNr_Dx12::FinishSubmitted()
{
    std::lock_guard lock(_state->mutex);
    _state->lifetime.FinishSubmitted();
    _state->deferredSr.lifetime.FinishSubmitted();
    _state->captureFrames.FinishSubmitted();
    if (_state->enlarger)
        _state->enlarger->lifetime.FinishSubmitted();
    for (auto& old : _state->retiredEnlargers)
        old->lifetime.FinishSubmitted();
    for (auto& model : _state->nr.models)
        model.FinishSubmitted();
    for (auto& model : _state->styleAnalysisCapture.models)
        model.FinishSubmitted();
    for (auto& model : _state->styleAnalysisCapture.nativeModels)
        model.FinishSubmitted();
}

DlssNr_Dx12::~DlssNr_Dx12()
{
    if (::State::Instance().isShuttingDown)
    {
        _state.release();
        for (auto& heap : _frameHeaps)
            heap.Abandon();
        GpuTime.release();
        return;
    }
    std::lock_guard lock(nrOwnersMutex);
    std::erase(nrOwners, this);
    if (activeNrOwner == this)
        activeNrOwner = nullptr;
    DlssNr::ClearStatus(this);
    const bool finished = _state->WaitForFinishedPicture();
    if (!finished || !_state->lifetime.Idle())
    {
        LOG_WARN("DLSS-NR: abandoning GPU ownership with unresolved command recordings at teardown");
        _state.release();
        for (auto& heap : _frameHeaps)
            heap.Abandon();
        _rootSignature = nullptr;
        _pipelineState = nullptr;
        _constantBuffer = nullptr;
        GpuTime.release();
        return;
    }
    _state.reset();
    for (auto& heap : _frameHeaps)
        heap.ReleaseHeaps();
    if (_finishedColorPipelineState)
        _finishedColorPipelineState->Release();
    for (auto& buffer : _constantBuffers)
    {
        if (buffer != nullptr)
        {
            buffer->Release();
            buffer = nullptr;
        }
    }

    if (_residualPipelineState != nullptr)
    {
        _residualPipelineState->Release();
        _residualPipelineState = nullptr;
    }
    if (_tiledFusedPipelineState != nullptr)
    {
        _tiledFusedPipelineState->Release();
        _tiledFusedPipelineState = nullptr;
    }
    if (_tiledStridedPipelineState != nullptr)
    {
        _tiledStridedPipelineState->Release();
        _tiledStridedPipelineState = nullptr;
    }
    if (_tiledCompactPipelineState != nullptr)
    {
        _tiledCompactPipelineState->Release();
        _tiledCompactPipelineState = nullptr;
    }
    if (_tiledCompactLinearPipelineState)
    {
        _tiledCompactLinearPipelineState->Release();
        _tiledCompactLinearPipelineState = nullptr;
    }
    for (auto*& state : _tiledV12PipelineState)
    {
        if (state)
        {
            state->Release();
            state = nullptr;
        }
    }
    if (_v16LowPipelineState)
        _v16LowPipelineState->Release();
    for (auto*& state : _tiledV16PipelineState)
    {
        if (state)
            state->Release();
    }
    for (auto*& state : _tiledV14PipelineState)
    {
        if (state)
            state->Release();
        state = nullptr;
    }
    for (auto*& state : _tiledV13PipelineState)
    {
        if (state)
        {
            state->Release();
            state = nullptr;
        }
    }
    for (auto*& state : _tiledV11PipelineState)
    {
        if (state)
        {
            state->Release();
            state = nullptr;
        }
    }
    for (auto*& state : _tiledV10PipelineState)
    {
        if (state)
        {
            state->Release();
            state = nullptr;
        }
    }
    for (auto*& state : _tiledV9PipelineState)
    {
        if (state)
        {
            state->Release();
            state = nullptr;
        }
    }
    if (_classicV10CachePipelineState)
    {
        _classicV10CachePipelineState->Release();
        _classicV10CachePipelineState = nullptr;
    }
    if (_classicCachePipelineState)
    {
        _classicCachePipelineState->Release();
        _classicCachePipelineState = nullptr;
    }
    if (_spatialPipelineState != nullptr)
    {
        _spatialPipelineState->Release();
        _spatialPipelineState = nullptr;
    }
    if (_spatialGuidesPipelineState != nullptr)
    {
        _spatialGuidesPipelineState->Release();
        _spatialGuidesPipelineState = nullptr;
    }
}

bool DlssNr_Dx12::SpatialReady()
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    if (!_init)
        return false;
    if (!_spatialPipelineState)
        CreateComputePipeline(_device, &_spatialPipelineState, dlssnr_spatial_cso, sizeof(dlssnr_spatial_cso), nullptr);
    if (!_spatialGuidesPipelineState)
        CreateComputePipeline(_device, &_spatialGuidesPipelineState, dlssnr_spatial_guides_cso,
                              sizeof(dlssnr_spatial_guides_cso), nullptr);
    return _spatialPipelineState != nullptr && _spatialGuidesPipelineState != nullptr;
}

bool DlssNr_Dx12::DispatchSpatial(ID3D12GraphicsCommandList* cmd, const DlssNr::Spatial::Constants& constants,
                                  ID3D12Resource* source, ID3D12Resource* depthOrAnswer, ID3D12Resource* motion,
                                  ID3D12Resource* target, ID3D12Resource* secondary)
{
    static_assert(sizeof(DlssNr::Spatial::Constants) == sizeof(DlssNrConstants));
    DlssNrConstants bytes {};
    std::memcpy(&bytes, &constants, sizeof(bytes));
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    auto* pipeline = constants.mode == 101 ? _spatialGuidesPipelineState : _spatialPipelineState;
    return DispatchCompute(cmd, bytes, pipeline, source, depthOrAnswer, motion, nullptr, nullptr, nullptr, target,
                           secondary, nullptr);
}

bool DlssNr_Dx12::DispatchResidualPass(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                                       ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                                       ID3D12Resource* InMotion, ID3D12Resource* OutTarget, bool finishedColor)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    if (finishedColor && !_finishedColorPipelineState && _init)
        CreateComputePipeline(_device, &_finishedColorPipelineState, dlssnr_finished_color_cso,
                              sizeof(dlssnr_finished_color_cso), nullptr);
    auto* pipeline = finishedColor ? _finishedColorPipelineState : _residualPipelineState;
    return DispatchCompute(InCmdList, InConstants, pipeline, InSource, InModel, InOriginal, InMotion, nullptr,
                           nullptr, OutTarget, nullptr, nullptr);
}

bool DlssNr_Dx12::CreateBufferResource(ID3D12Device* device, ID3D12Resource* source, D3D12_RESOURCE_STATES state)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    if (device == nullptr || source == nullptr)
        return false;
    auto desc = source->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1)
        return false;
    desc.MipLevels = 1;
    desc.Alignment = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = (desc.Flags | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) & ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    if (_state->buffer != nullptr)
    {
        const auto previous = _state->buffer->GetDesc();
        if (previous.Width == desc.Width && previous.Height == desc.Height && previous.Format == desc.Format &&
            previous.Flags == desc.Flags)
            return true;
        _state->ParkNrResource(_state->buffer);
    }
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                               IID_PPV_ARGS(&_state->buffer))))
        return false;
    _state->bufferState = state;
    return true;
}

void DlssNr_Dx12::SetBufferState(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    _state->lifetime.Record(cmdList);
    Shader_Dx12::SetBufferState(cmdList, state, _state->buffer, &_state->bufferState);
}

ID3D12Resource* DlssNr_Dx12::Buffer() { return _state->buffer; }
bool DlssNr_Dx12::CanRender() const { return _init && _state->buffer != nullptr; }

bool DlssNr_Dx12::State::RunOrdinaryAsync(ID3D12GraphicsCommandList* gameCommands, ID3D12Resource* colour,
                                                   ID3D12Resource* depth, ID3D12Resource* motion,
                                                   ID3D12Resource* output, const DlssNrFrameInfo& frame,
                                                   ID3D12CommandQueue* queue)
{
    const Config& cfg = *Config::Instance();
    const uint32_t transfer = cfg.DlssNrTransfer.value_or_default();
    const bool upscaledResidual = transfer == 6;
    const uint32_t execution =
        std::min(upscaledResidual ? cfg.DlssNrUpscaledResidualReferenceExecutionMode.value_or_default()
                                  : cfg.DlssNrDirectDetailReferenceExecutionMode.value_or_default(),
                 2u);
    const uint32_t detailReference =
        std::min(upscaledResidual ? cfg.DlssNrUpscaledResidualReferenceUpscaler.value_or_default()
                                  : cfg.DlssNrDirectDetailReferenceUpscaler.value_or_default(),
                 10u);
    const bool needsP50Reference =
        upscaledResidual || (transfer == 5 && cfg.DlssNrDirectDetailRecovery.value_or_default() != 0);
    // Auto never submits a caller-owned native DX12 list early: the app may enqueue a queue-level Wait only
    // after recording finishes. Explicit Async compute is the opt-in experimental override for that native path.
    if (execution == 1 || (execution == 0 && !frame.IndependentCommands) || frame.BeforeUpscale ||
        frame.FinishedPicture || !gameCommands || !queue || !output || !depth || !motion ||
        (transfer != 5 && transfer != 6) || !needsP50Reference || detailReference >= 10 ||
        cfg.DlssNrHoldFrame.value_or_default() || cfg.DlssNrDebugView.value_or_default() != 0 ||
        cfg.DlssNrCompare.value_or_default() != 0 || cfg.DlssNrShowSkinMask.value_or_default() ||
        captureFrames.isActive() || ::State::Instance().isShuttingDown)
        return false;

    float workScale = cfg.DlssNrWorkingScale.value_or_default();
    if (!std::isfinite(workScale))
        workScale = 1.0f;
    workScale = std::clamp(workScale, 0.25f, 2.0f);
    if (workScale >= 0.999f)
        return false;

    if (gameCommands->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return false;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
    if (FAILED(output->GetDevice(IID_PPV_ARGS(&device))) ||
        FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || device.Get() != queueDevice.Get())
        return false;

    const auto outDesc = output->GetDesc();
    const auto spatialSettings = DlssNr::Spatial::ReadSettings(cfg);
    if (DlssNr::Spatial::Build(spatialSettings, (unsigned) outDesc.Width, outDesc.Height, workScale).active)
        return false;

    // Resetting a caller-owned list is only allowed when we can at least put its tracked root signature back.
    // Extended state snapshots (descriptor heaps/root arguments/PSO) are restored too when they were enabled.
    if (!D3D12Hooks::CanRestoreRootSignature(gameCommands))
    {
        static bool warnedNoState = false;
        if (execution == 2 && !warnedNoState)
        {
            warnedNoState = true;
            LOG_WARN("DLSS-NR ordinary async detail: caller command-list state is not tracked; using Serial.");
        }
        return false;
    }

    LateContext::Slot* slot = nullptr;
    for (size_t offset = 0; offset < ordinaryAsyncSlots.size(); ++offset)
    {
        const size_t index = (ordinaryAsyncNext + offset) % ordinaryAsyncSlots.size();
        auto& candidate = ordinaryAsyncSlots[index];
        if (candidate.fence && candidate.done != 0)
        {
            const auto completed = candidate.fence->GetCompletedValue();
            if (completed == UINT64_MAX || completed < candidate.done)
                continue;
        }
        slot = &candidate;
        ordinaryAsyncNext = (index + 1) % ordinaryAsyncSlots.size();
        break;
    }
    if (!slot)
        return false;

    if (!slot->fence && FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&slot->fence))))
        return false;

    if (!slot->allocator)
    {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot->allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot->allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&slot->commands))))
        {
            slot->allocator.Reset();
            slot->commands.Reset();
            return false;
        }
    }
    else if (FAILED(slot->allocator->Reset()) || FAILED(slot->commands->Reset(slot->allocator.Get(), nullptr)))
    {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> continuationAllocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&continuationAllocator))))
    {
        slot->commands->Close();
        return false;
    }

    // Everything recorded so far is exactly the old Serial prefix, including the adapter's guide barriers.
    // Submit that prefix before any private list can read the DLSS output/P50 source.
    if (FAILED(gameCommands->Close()))
    {
        slot->commands->Close();
        nr.failed = true;
        nr.reason = "the game command list could not be split for ordinary async detail";
        return true;
    }
    ID3D12CommandList* prefix[] = { gameCommands };
    queue->ExecuteCommandLists(1, prefix);

    // Continue the game's recording on the very same command-list object, but on our allocator. The allocator
    // stays alive until the caller submits and later resets this continuation.
    if (FAILED(gameCommands->Reset(continuationAllocator.Get(), nullptr)))
    {
        nr.failed = true;
        nr.reason = "the game command list could not resume after ordinary async detail";
        slot->commands->Close();
        return true;
    }
    D3D12Hooks::RestoreTrackedStateAfterReset(gameCommands);
    ordinaryContinuationLifetime.BeginGeneration();
    ordinaryContinuationLifetime.Record(gameCommands);
    auto keepAllocator = continuationAllocator;
    ordinaryContinuationLifetime.Retire([keepAllocator]() mutable { keepAllocator.Reset(); });

    DlssNrFrameInfo privateFrame = frame;
    privateFrame.IndependentCommands = true;
    privateFrame.PipelineManagedStates = true;
    privateFrame.PrivateColorCopy = true;

    const auto before = nr.successfulDispatches;
    Run(slot->commands.Get(), colour, depth, motion, output, privateFrame, queue, slot, nullptr, false);

    if (FAILED(slot->commands->Close()))
    {
        nr.failed = true;
        nr.reason = "the ordinary async resolve list could not be closed";
        // The successful path deliberately has no pre-resolve direct-queue signal. If the final
        // resolve cannot be submitted, fence the already queued private async work only on this
        // failure path so its slot resources cannot be recycled while the GPU still uses them.
        slot->done = std::max(slot->done, slot->ready) + 1;
        if (SUCCEEDED(queue->Signal(slot->fence.Get(), slot->done)))
            slot->asyncProvisional = true;
        else
            slot->done = UINT64_MAX - 1;
        return true;
    }

    ID3D12CommandList* resolve[] = { slot->commands.Get() };
    queue->ExecuteCommandLists(1, resolve);
    slot->done = std::max(slot->done, slot->ready) + 1;
    if (FAILED(queue->Signal(slot->fence.Get(), slot->done)))
    {
        nr.failed = true;
        nr.reason = "the graphics queue could not protect the ordinary async resolve";
        slot->done = UINT64_MAX - 1;
    }
    slot->asyncProvisional = false;

    static bool loggedIndependent = false;
    static bool loggedForcedNative = false;
    if (frame.IndependentCommands && !loggedIndependent)
    {
        loggedIndependent = true;
        LOG_INFO("DLSS-NR ordinary post-SR: async P50 detail reference is using an owned/independent command split.");
    }
    else if (!frame.IndependentCommands && !loggedForcedNative)
    {
        loggedForcedNative = true;
        LOG_WARN("DLSS-NR ordinary native DX12: forced Async compute is splitting and submitting the caller command "
                 "list early. This is experimental; use Serial if the game has queue-ordering issues.");
    }
    return true;
}

bool DlssNr_Dx12::Dispatch(ID3D12GraphicsCommandList* cmd, ID3D12Resource* colour, ID3D12Resource* depth,
                           ID3D12Resource* motion, ID3D12Resource* output, const DlssNrFrameInfo& frame,
                           ID3D12CommandQueue* queue)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    ActivateNrOwner(this);
    std::lock_guard stateLock(_state->mutex);
    _state->ConsumeControls();
    struct Publish
    {
        State& s;
        ~Publish() { s.Publish(); }
    } publish { *_state };
    if (!_init || !cmd || !colour || !depth || !motion || !output)
        return false;
    auto info = frame;
    info.PipelineManagedStates = true;
    info.PrivateColorCopy = true;
    if (!info.RenderSubrectWidth)
        info.RenderSubrectWidth = info.Width;
    if (!info.RenderSubrectHeight)
        info.RenderSubrectHeight = info.Height;
    if (colour != output)
    {
        const auto source = colour->GetDesc(), target = output->GetDesc();
        if (source.Width != target.Width || source.Height != target.Height || source.Format != target.Format)
            return false;
        _state->Barrier(cmd, colour, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        _state->Barrier(cmd, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        DlssNr::CopyActiveColor(cmd, output, colour, { (unsigned) target.Width, target.Height });
        _state->Barrier(cmd, colour, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _state->Barrier(cmd, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    const auto before = _state->nr.successfulDispatches;
    if (!_state->RunOrdinaryAsync(cmd, output, depth, motion, output, info, queue))
        _state->Run(cmd, output, depth, motion, output, info, queue);
    return _state->nr.successfulDispatches != before;
}

void DlssNr_Dx12::BeginInputHold(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                                 const D3D12_RESOURCE_STATES* inputStates)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard lock(_state->mutex);
    _state->BeginInputHold(cmd, params, inputStates);
}

void DlssNr_Dx12::EndInputHold(NVSDK_NGX_Parameter* params)
{
    std::lock_guard lock(_state->mutex);
    _state->inputHold.parameters.Restore(params);
}

bool DlssNr_Dx12::ProcessSeam(ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params, bool beforeUpscale,
                              ID3D12CommandQueue* queue, bool rayReconstruction, unsigned long long submissionEpoch,
                              bool interop, uint32_t featureFlags)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    ActivateNrOwner(this);
    std::lock_guard stateLock(_state->mutex);
    _state->ConsumeControls();
    _state->featureFlags = featureFlags;
    const auto& cfg = *Config::Instance();
    // Both seams reach this scheduler; ordinary passes remain in the shared shader pipeline.
    const auto placement = DlssNr::ResolvePlacement(
        cfg.DlssNrRunBeforeSr.value_or_default(), cfg.DlssNrDeferredDlss.value_or_default(),
        cfg.DlssNrResidualAcrossRr.value_or_default(), cfg.DlssNrFinishedPicture.value_or_default());
    const bool special = placement.finished || placement.deferred;
    if (special)
        _state->EvaluateInternal(cmd, params, beforeUpscale, queue, rayReconstruction, submissionEpoch, interop);
    else
    {
        if (_state->lastFinishedMode != 0)
        {
            _state->lastFinishedMode = 0;
            _state->nr.reset = true;
            if (_state->gpuTime)
                _state->gpuTime->ClearLast();
            if (_state->ngxTime)
                _state->ngxTime->ClearLast();
            _state->lastGpuTime.reset();
            _state->lastNgxTime.reset();
        }
        _state->late.Cancel();
        _state->deferredSr.Cancel();
    }
    _state->Publish();
    return special;
}
void DlssNr_Dx12::DiagnosePipeline(unsigned stage, ID3D12GraphicsCommandList* cmd, NVSDK_NGX_Parameter* params,
                                   ID3D12Resource* color, uint32_t flags, bool rr, bool success)
{
    std::lock_guard ownersLock(nrOwnersMutex);
    std::lock_guard stateLock(_state->mutex);
    auto& state = *_state;
    if (stage == 0)
    {
        state.lifetime.Collect();
        static bool previousGameplay = false, previousPhoto = false;
        static unsigned runs = 0;
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        const bool control = foregroundProcess == GetCurrentProcessId() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool gameplay = control && (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const bool photo = control && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        const char* label = gameplay && !previousGameplay ? "gameplay"
                            : photo && !previousPhoto     ? "photomode"
                                                          : nullptr;
        previousGameplay = gameplay;
        previousPhoto = photo;
        if (label && !state.pipelineCaptureRemaining && !nrCaptureOutstanding)
        {
            if (runs >= 2)
                LOG_WARN("NR pipeline capture: two-run limit reached; restart to capture again");
            else
            {
                ++runs;
                SYSTEMTIME time {};
                GetLocalTime(&time);
                char folder[100];
                std::snprintf(folder, sizeof(folder), "%04u%02u%02u-%02u%02u%02u-%03u-%s-%u", time.wYear, time.wMonth,
                              time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, label, runs);
                state.pipelineCaptureDirectory = Util::DllPath().parent_path() / "nr-pipeline-captures" / folder;
                state.pipelineCaptureRemaining = 4;
                LOG_INFO("NR pipeline capture armed: {} (four frames)", state.pipelineCaptureDirectory.string());
            }
        }
        if (!state.pipelineCaptureRemaining || state.pipelineCapture)
            return;
        auto job = std::make_unique<DlssNr::PipelineCaptureFrame>();
        if (!job->Init(_device))
        {
            state.pipelineCaptureRemaining = 0;
            LOG_ERROR("NR pipeline capture allocation failed");
            return;
        }
        job->directory = state.pipelineCaptureDirectory / std::to_string(4 - state.pipelineCaptureRemaining);
        job->metadata << "stage_semantics before_nr=scene_linear_input after_nr=NR_composed_RR_input "
                         "after_rr=upscaler_output_before_postprocessing\n"
                      << "game_frame " << ::State::Instance().frameCount << " command_list " << cmd << " parameters "
                      << params << " rr " << rr << " feature_flags " << flags << '\n';
        const auto& cfg = *Config::Instance();
        job->metadata << "nr_passes " << cfg.DlssNrPasses.value_or_default() << " working_scale "
                      << cfg.DlssNrWorkingScale.value_or_default() << " nr_history_reset " << state.nr.reset << " hold "
                      << cfg.DlssNrHoldFrame.value_or_default() << '\n';
        for (const char* key :
             { NVSDK_NGX_Parameter_Reset, NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
               NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, NVSDK_NGX_Parameter_OutWidth,
               NVSDK_NGX_Parameter_OutHeight, NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,
               NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
               NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y })
        {
            unsigned value = 0;
            auto result = params->Get(key, &value);
            job->metadata << key << ' ' << value << " get_result " << unsigned(result) << '\n';
        }
        for (const char* key :
             { NVSDK_NGX_Parameter_MV_Scale_X, NVSDK_NGX_Parameter_MV_Scale_Y, NVSDK_NGX_Parameter_Jitter_Offset_X,
               NVSDK_NGX_Parameter_Jitter_Offset_Y, NVSDK_NGX_Parameter_DLSS_Pre_Exposure,
               NVSDK_NGX_Parameter_DLSS_Exposure_Scale, NVSDK_NGX_Parameter_FrameTimeDeltaInMsec })
        {
            float value = 0;
            auto result = params->Get(key, &value);
            job->metadata << key << ' ' << value << " get_result " << unsigned(result) << '\n';
        }
        // Record descriptors of optional RR inputs without assuming their presence.
        for (const char* key : { "DLSS.Input.DiffuseAlbedo", "DLSS.Input.SpecularAlbedo", "GBuffer.Normals",
                                 "GBuffer.Roughness", "MotionVectorsReflection", "DLSSD.SpecularHitDistance",
                                 "DLSS.Input.ColorBeforeParticles", "DLSSD.DiffuseHitDistance" })
        {
            auto* resource = state.GetResource(params, key, key);
            job->metadata << key << " resource " << resource;
            if (resource)
            {
                auto desc = resource->GetDesc();
                job->metadata << " width " << desc.Width << " height " << desc.Height << " format " << desc.Format;
            }
            job->metadata << '\n';
        }
        state.pipelineCapture = job.release();
        ++nrCaptureOutstanding;
        state.lifetime.Record(cmd);
        const auto inputs = DlssNr::ResolveInputStates_Dx12(false);
        state.pipelineCapture->Copy(cmd, _device, "before_nr", color, inputs.color);
        state.pipelineCapture->Copy(cmd, _device, "motion",
                                    state.GetResource(params, NVSDK_NGX_Parameter_MotionVectors, "DLSSD.MotionVectors"),
                                    inputs.motion);
        state.pipelineCapture->Copy(cmd, _device, "depth",
                                    state.GetResource(params, NVSDK_NGX_Parameter_Depth, "DLSSD.Depth"), inputs.depth);
        state.pipelineCapture->Copy(
            cmd, _device, "exposure",
            state.GetResource(params, NVSDK_NGX_Parameter_ExposureTexture, "DLSSD.ExposureTexture"), inputs.exposure);
        return;
    }
    auto* job = state.pipelineCapture;
    if (!job)
        return;
    job->metadata << "stage " << stage << " success " << success << '\n';
    if (stage == 1)
    {
        job->metadata << "nr_model_evaluated " << state.modelRunning << '\n';
        job->Copy(cmd, _device, "after_nr", color, DlssNr::ResolveInputStates_Dx12(false).color);
        return;
    }
    if (success)
        job->Copy(cmd, _device, "after_rr", color, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    job->End(cmd);
    state.pipelineCapture = nullptr;
    --state.pipelineCaptureRemaining;
    state.lifetime.Retire(
        [job]
        {
            if (job->Write())
                LOG_INFO("NR pipeline capture saved: {}", job->directory.string());
            else
                LOG_WARN("NR pipeline capture discarded or write failed: {}", job->directory.string());
            delete job;
            --nrCaptureOutstanding;
        });
}

void DlssNr_Dx12::ResetFinishedCommands(ID3D12CommandList* cmd) { _state->FinishedPictureResetCommandList(cmd); }
void DlssNr_Dx12::SubmitFinishedCommands(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    _state->FinishedPictureSubmitted(queue, count, lists);
}
bool DlssNr_Dx12::WaitFinished() { return _state->WaitForFinishedPicture(); }
void DlssNr_Dx12::ApplyFinished(ID3D12Resource* picture, ID3D12CommandQueue* queue, DXGI_COLOR_SPACE_TYPE space,
                                bool gameFrameHandoff)
{
    std::lock_guard lock(_state->mutex);
    if (!Config::Instance()->DlssNrFinishedPicture.value_or_default() ||
        !Config::Instance()->DlssNrEnabled.value_or_default())
        _state->late.Cancel();
    else if (picture && queue)
        _state->ApplyFinishedColor(picture, queue, space, gameFrameHandoff);
    _state->Publish();
}
void DlssNr_Dx12::ApplyFinishedDx11(IDXGISwapChain* swapchain)
{
    _state->ApplyToFinishedPictureDx11(swapchain);
    _state->Publish();
}
std::string DlssNr_Dx12::FinishedStatus() { return _state->FinishedPictureStatus(); }
std::string DlssNr_Dx12::DeferredStatus() { return _state->DeferredDlssStatus(); }
namespace DlssNr
{
void FinishedPictureResetCommandList(ID3D12CommandList* cmd)
{
    if (::State::Instance().isShuttingDown)
        return;
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        owner->ResetFinishedCommands(cmd);
}
void FinishedPictureSubmitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    if (::State::Instance().isShuttingDown)
        return;
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        owner->SubmitFinishedCommands(queue, count, lists);
}
bool WaitForFinishedPicture()
{
    if (::State::Instance().isShuttingDown)
        return false;
    std::lock_guard lock(nrOwnersMutex);
    NrNotificationScope notification;
    bool ready = true;
    const auto owners = nrOwners;
    for (auto* owner : owners)
        ready = owner->WaitFinished() && ready;
    return ready;
}
static DXGI_COLOR_SPACE_TYPE ReadFinishedSpace(IDXGISwapChain* swapchain, ID3D12Resource* picture)
{
    auto space = picture->GetDesc().Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                                                                             : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT size = sizeof(space);
    swapchain->GetPrivateData(FinishedColorSpaceKey, &size, &space);
    return space;
}
void ApplyToFinishedPicture(IDXGISwapChain* swapchain, ID3D12CommandQueue* queue)
{
    if (::State::Instance().isShuttingDown)
        return;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> chain;
    Microsoft::WRL::ComPtr<ID3D12Resource> picture;
    auto space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    const auto& config = *Config::Instance();
    // Swapchain calls must precede NR locks: FG Present can submit commands while holding its own lock.
    if (swapchain && queue && config.DlssNrEnabled.value_or_default() &&
        config.DlssNrFinishedPicture.value_or_default())
    {
        if (StreamlinePicture::RenderQueue(swapchain) || FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&chain))) ||
            FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&picture))))
            return;
        space = ReadFinishedSpace(swapchain, picture.Get());
    }
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyFinished(picture.Get(), queue, space);
}
void ApplyToStreamlinePicture(IDXGISwapChain* swapchain, ID3D12Resource* picture, ID3D12CommandQueue* queue)
{
    if (::State::Instance().isShuttingDown)
        return;
    if (!swapchain || !picture || !queue)
        return;
    const auto space = ReadFinishedSpace(swapchain, picture);
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyFinished(picture, queue, space, true);
}
void ApplyToFinishedPictureDx11(IDXGISwapChain* swapchain)
{
    if (::State::Instance().isShuttingDown)
        return;
    std::lock_guard lock(nrOwnersMutex);
    if (activeNrOwner)
        activeNrOwner->ApplyFinishedDx11(swapchain);
}
void FinishedPictureColorSpace(IDXGISwapChain* swapchain, DXGI_COLOR_SPACE_TYPE colorSpace)
{
    if (swapchain)
        swapchain->SetPrivateData(FinishedColorSpaceKey, sizeof(colorSpace), &colorSpace);
}
std::string FinishedPictureStatus()
{
    std::lock_guard lock(nrOwnersMutex);
    return activeNrOwner ? activeNrOwner->FinishedStatus() : "Waiting for a finished picture.";
}
std::string DeferredDlssStatus()
{
    std::lock_guard lock(nrOwnersMutex);
    return activeNrOwner ? activeNrOwner->DeferredStatus() : "not started";
}
bool Shutdown()
{
    if (::State::Instance().isShuttingDown)
        return false;
    const auto deadline = GetTickCount64() + 1000;
    do
    {
        {
            std::lock_guard lock(nrOwnersMutex);
            // Callbacks may retire a child codec. Defer owner destruction until traversal ends.
            {
                NrNotificationScope notification;
                for (auto& owner : RetiredNrOwners())
                    owner->FinishSubmitted();
            }
            if (nrOwners.empty())
                return true;
        }
        // Submission/reset hooks must be able to make progress while we drain.
        Sleep(1);
    } while (GetTickCount64() < deadline);
    LOG_WARN("NR shutdown deferred: owners or GPU recordings remain; keeping the NGX runtime alive");
    return false;
}
} // namespace DlssNr
