// Included once by DlssNr_Dx12.cpp. Original v59 blobs use their own constant ABI.
namespace DlssNr::Superior::Bytecode
{
#include "precompile/dlssnr_interpass_cross5_Shader.h"
#include "precompile/dlssnr_interpass_fast_Shader.h"
#include "precompile/dlssnr_interpass_low36_cached_Shader.h"
#include "precompile/dlssnr_interpass_low36_Shader.h"
#include "precompile/DlssNr_Shader.h"
#include "precompile/dlssnr_superior_area_Shader.h"
#include "precompile/dlssnr_superior_carrier_Shader.h"
#include "precompile/dlssnr_superior_cross5_Shader.h"
#include "precompile/dlssnr_superior_encode_Shader.h"
#include "precompile/dlssnr_superior_exposure_Shader.h"
#include "precompile/dlssnr_superior_hdr0_Shader.h"
#include "precompile/dlssnr_superior_hdr2_Shader.h"
#include "precompile/dlssnr_superior_hdr4_Shader.h"
#include "precompile/dlssnr_superior_low_Shader.h"
#include "precompile/dlssnr_superior_meter_Shader.h"
#include "precompile/dlssnr_superior_normal_Shader.h"
#include "precompile/dlssnr_superior_resolve_Shader.h"
#include "precompile/dlssnr_superior_sdr_Shader.h"
#include "precompile/private_carrier_Shader.h"
#include "precompile/private_transport_bounds_Shader.h"
#include "precompile/private_transport_reduce_Shader.h"
#include "precompile/private_affine_base_Shader.h"
#include "precompile/private_affine_ghost_Shader.h"
#include "precompile/private_affine_guided_Shader.h"
#include "precompile/private_affine_temporal_Shader.h"
#include "precompile/private_affine_simple_Shader.h"
#include "precompile/private_affine_fast_Shader.h"
#include "precompile/private_affine_fast_fade_Shader.h"
#include "precompile/private_affine_coarse_Shader.h"
#include "precompile/private_affine_contrast_temporal_Shader.h"
#include "precompile/private_affine_contrast_simple_Shader.h"
#include "precompile/private_affine_contrast_fast_Shader.h"
#include "precompile/private_affine_contrast_coarse_Shader.h"
#include "precompile/private_carrier_ghost_Shader.h"
#include "precompile/private_carrier_guided_ghost_Shader.h"
#include "precompile/private_carrier_temporal_ghost_Shader.h"
#include "precompile/private_carrier_compact_simple_Shader.h"
#include "precompile/private_carrier_compact_simple_answer_Shader.h"
#include "precompile/private_carrier_compact_fast_Shader.h"
#include "precompile/private_carrier_compact_fast_answer_Shader.h"
#include "precompile/private_carrier_compact_fast_fade_Shader.h"
#include "precompile/private_ghost_compact_1_Shader.h"
#include "precompile/private_ghost_compact_wave_Shader.h"
#include "precompile/private_ghost_coarse_1_Shader.h"
#include "precompile/private_ghost_coarse_2_Shader.h"
#include "precompile/private_ghost_coarse_dilate_1_Shader.h"
#include "precompile/private_ghost_coarse_dilate_2_Shader.h"
#include "precompile/private_carrier_coarse_Shader.h"
#include "precompile/private_carrier_coarse_answer_Shader.h"
#include "precompile/private_ghost_compact_2_Shader.h"
#include "precompile/private_ghost_clear_Shader.h"
#include "precompile/private_ghost_feather_horizontal_Shader.h"
#include "precompile/private_ghost_feather_apply_Shader.h"
#include "precompile/private_ghost_feather_inplace_Shader.h"
#include "precompile/private_ghost_feather_rings_Shader.h"
#include "precompile/private_ghost_feather_rings_apply_Shader.h"
#include "precompile/private_ghost_feather_rings_inplace_Shader.h"
#include "precompile/private_ghost_scatter_Shader.h"
#include "precompile/private_ghost_history_Shader.h"
#include "precompile/private_ghost_contrast_history_Shader.h"
#include "precompile/private_ghost_contrast_compact_Shader.h"
#include "precompile/private_ghost_contrast_coarse_Shader.h"
#include "precompile/private_ghost_contrast_dilate_1_Shader.h"
#include "precompile/private_ghost_contrast_dilate_2_Shader.h"
#include "precompile/private_carrier_contrast_temporal_0_Shader.h"
#include "precompile/private_carrier_contrast_temporal_1_Shader.h"
#include "precompile/private_carrier_contrast_simple_0_Shader.h"
#include "precompile/private_carrier_contrast_simple_1_Shader.h"
#include "precompile/private_carrier_contrast_fast_0_Shader.h"
#include "precompile/private_carrier_contrast_fast_1_Shader.h"
#include "precompile/private_carrier_contrast_coarse_0_Shader.h"
#include "precompile/private_carrier_contrast_coarse_1_Shader.h"
#include "precompile/private_exposure_Shader.h"
} // namespace DlssNr::Superior::Bytecode

// Optional superset root: the original table and its descriptor ring stay unchanged. b1 is a GPU-written 256B buffer.
bool DlssNr_Dx12::EnsureSuperiorTransportRoot()
{
    if (_superiorTransportRootAttempted)
        return _superiorTransportRoot != nullptr;
    _superiorTransportRootAttempted = true;
    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSrvCount, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kUavCount, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    CD3DX12_ROOT_PARAMETER1 params[2];
    params[0].InitAsDescriptorTable(3, ranges);
    params[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC desc;
    desc.Init_1_1(2, params, 1, &sampler);
    Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
    return SUCCEEDED(D3D12SerializeVersionedRootSignature(&desc, &blob, &error)) &&
           SUCCEEDED(_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&_superiorTransportRoot)));
}

bool DlssNr_Dx12::EnsureSuperiorRing()
{
    if (_superiorRingAttempted)
        return _superiorRingReady;
    _superiorRingAttempted = true;
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNr::Superior::DlssNrConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    for (auto& buffer : _superiorConstantBuffers)
        if (FAILED(_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))))
        {
            LOG_ERROR("[{0}] Superior Scaling: constant buffers could not be created", _name);
            return false;
        }
    _superiorHeaps = std::make_unique<FrameDescriptorHeap[]>(DLSSNR_SUPERIOR_NUM_OF_HEAPS);
    _superiorRingReady = InitHeaps(_device, _superiorHeaps.get(), DLSSNR_SUPERIOR_NUM_OF_HEAPS);
    return _superiorRingReady;
}

ID3D12PipelineState* DlssNr_Dx12::SuperiorPipeline(const DlssNr::Superior::DlssNrConstants& constants,
                                                   ID3D12Resource* source)
{
    using namespace DlssNr::Superior;
    using namespace DlssNr::Superior::Bytecode;
    const bool carrier = constants.Mode == DlssNr::Superior::DlssNrMode_SuperiorCarrierEncode ||
                         constants.Mode == DlssNr::Superior::DlssNrMode_SuperiorCarrierDecode ||
                         constants.Mode == DlssNr::Superior::DlssNrMode_SuperiorCarrierMotion || constants.Mode == 43u;
    if (!carrier && (constants.DirectResolveFlags & DlssNr::SuperiorScaling::ShaderFlag) == 0u)
        return nullptr;
    const auto d = (constants.Mode == DlssNr::Superior::DlssNrMode_Resolve ||
                    constants.Mode == DlssNr::Superior::DlssNrMode_Encode ||
                    constants.Mode == DlssNr::Superior::DlssNrMode_GuidedResidualLow ||
                    constants.Mode == DlssNr::Superior::DlssNrMode_Downsample ||
                    constants.Mode == DlssNr::Superior::DlssNrMode_Meter ||
                    constants.Mode == DlssNr::Superior::DlssNrMode_AutoExposure) &&
                           source
                       ? source->GetDesc()
                       : D3D12_RESOURCE_DESC {};
    const bool inputEncode = DlssNr::SuperiorScaling::InputEncode(constants, d.Width, d.Height);
    const bool lowResidual = DlssNr::SuperiorScaling::LowResidual(constants, d.Width, d.Height);
    if (constants.Mode == 18u && constants.Transfer == 0u && constants.DirectResolveFlags == 65536u && d.Width &&
        d.Height && constants.Width == (d.Width + 7u) / 8u && constants.Height == (d.Height + 7u) / 8u)
        return SuperiorRawLowPipeline();
    const bool areaDownsample = DlssNr::SuperiorScaling::AreaDownsample(constants, d.Width, d.Height);
    const auto exposureStage = DlssNr::SuperiorScaling::ExposureStage(constants, d.Width, d.Height);
    // Fixed input/exposure, Area, final low and radius-one stages; other uses retain the general fallback.
    if (!carrier && !inputEncode && !lowResidual && !areaDownsample && exposureStage == 0u &&
        constants.Mode != DlssNr::Superior::DlssNrMode_InterPassFastCross5 &&
        (constants.Mode != DlssNr::Superior::DlssNrMode_Resolve || constants.ResidualHistoryValid != 1u))
        return nullptr;
    const bool normalResolve = DlssNr::SuperiorScaling::NormalResolve(constants, d.Width, d.Height);
    const unsigned index = inputEncode           ? 7u
                           : lowResidual         ? 8u
                           : areaDownsample      ? 9u
                           : exposureStage != 0u ? 9u + exposureStage
                           : carrier             ? 0u
                           : normalResolve       ? 3u + DlssNr::SuperiorScaling::NormalColorProfile(constants)
                           : constants.Mode == DlssNr::Superior::DlssNrMode_InterPassFastCross5 ? 2u
                                                                                                : 1u;
    const void* const blobs[] { private_carrier_cso, dlssnr_superior_resolve_cso, dlssnr_superior_cross5_cso,
                                dlssnr_superior_normal_cso,  dlssnr_superior_sdr_cso,     dlssnr_superior_hdr2_cso,
                                dlssnr_superior_hdr4_cso,    dlssnr_superior_encode_cso,  dlssnr_superior_low_cso,
                                dlssnr_superior_area_cso,    dlssnr_superior_meter_cso,   dlssnr_superior_exposure_cso,
                                dlssnr_superior_hdr0_cso };
    const size_t sizes[] { sizeof(private_carrier_cso), sizeof(dlssnr_superior_resolve_cso),
                           sizeof(dlssnr_superior_cross5_cso),  sizeof(dlssnr_superior_normal_cso),
                           sizeof(dlssnr_superior_sdr_cso),     sizeof(dlssnr_superior_hdr2_cso),
                           sizeof(dlssnr_superior_hdr4_cso),    sizeof(dlssnr_superior_encode_cso),
                           sizeof(dlssnr_superior_low_cso),     sizeof(dlssnr_superior_area_cso),
                           sizeof(dlssnr_superior_meter_cso),   sizeof(dlssnr_superior_exposure_cso),
                           sizeof(dlssnr_superior_hdr0_cso) };
    const auto get = [&](unsigned selected) -> ID3D12PipelineState*
    {
        if (!_superiorAttempted[selected])
        {
            _superiorAttempted[selected] = true;
            if (!CreateComputePipeline(_device, &_superiorPipeline[selected], blobs[selected], sizes[selected],
                                       nullptr))
                LOG_WARN("Superior Scaling PSO {} creation failed", selected);
        }
        return _superiorPipeline[selected];
    };
    // Try literal HDR soft knee only for the normal signed-residual path. A failed
    // specialization still tries the unchanged normal PSO before the main shader.
    if (normalResolve && DlssNr::SuperiorScaling::SoftKneeResolve(constants))
        if (auto* pipeline = get(12u))
            return pipeline;
    if (auto* pipeline = get(index))
        return pipeline;
    // Input, exposure, Area and low failures retain their own mode in the main shader, never Mode 1.
    return normalResolve && index > 3u ? get(3u) : nullptr;
}

bool DlssNr_Dx12::DispatchSuperiorPass(ID3D12GraphicsCommandList* InCmdList,
                                       const DlssNr::Superior::DlssNrConstants& InConstants, ID3D12Resource* InSource,
                                       ID3D12Resource* InModel, ID3D12Resource* InOriginal, ID3D12Resource* InMotion,
                                       ID3D12Resource* InPrevEdit, ID3D12Resource* InAux2, ID3D12Resource* OutTarget,
                                       ID3D12Resource* OutKeep, uint32_t* immutableSlot,
                                       ID3D12Resource* transportCoefficients)
{
    using namespace DlssNr::Superior::Bytecode;
    const bool affine = DlssNr::Superior::Fields::CarrierTransport(InConstants) != 0;
    if (affine && (!transportCoefficients || !EnsureSuperiorTransportRoot()))
        return false;
    const bool ghostRejection = DlssNr::Superior::Fields::CarrierGhostRejection(InConstants);
    ID3D12PipelineState* pipeline = affine || ghostRejection || (InConstants.Mode >= 49u && InConstants.Mode <= 61u)
                                        ? nullptr
                                        : SuperiorPipeline(InConstants, InSource);
    const auto get = [&](unsigned index, const void* blob, size_t size)
    {
        if (index >= kSuperiorPipelineCount)
            return static_cast<ID3D12PipelineState*>(nullptr);
        if (!_superiorAttempted[index])
        {
            _superiorAttempted[index] = true;
            if (index >= 58u)
                CreateComputeShader(_device, _superiorTransportRoot.Get(), &_superiorPipeline[index], nullptr,
                                    CD3DX12_SHADER_BYTECODE(blob, size));
            else
                CreateComputePipeline(_device, &_superiorPipeline[index], blob, size, nullptr);
        }
        return _superiorPipeline[index];
    };
    const bool contrastGhost = DlssNr::Superior::Fields::GhostEditContrast(InConstants);
    if (!affine)
    {
        if (ghostRejection && !contrastGhost && !DlssNr::Superior::Fields::CompactGhostRejection(InConstants))
            pipeline = DlssNr::Superior::Fields::TemporalGhostRejection(InConstants)
                           ? get(22u, private_carrier_temporal_ghost_cso, sizeof(private_carrier_temporal_ghost_cso))
                       : DlssNr::Superior::Fields::GuidedGhostRejection(InConstants)
                           ? get(18u, private_carrier_guided_ghost_cso, sizeof(private_carrier_guided_ghost_cso))
                           : get(17u, private_carrier_ghost_cso, sizeof(private_carrier_ghost_cso));
        if (!contrastGhost && DlssNr::Superior::Fields::CompactGhostRejection(InConstants))
        {
            if (InConstants.GuideHeight == 5u)
                pipeline = InConstants.DirectDetailMode != 0u
                               ? get(36u, private_carrier_coarse_answer_cso, sizeof(private_carrier_coarse_answer_cso))
                               : get(35u, private_carrier_coarse_cso, sizeof(private_carrier_coarse_cso));
            else if (InConstants.GuideHeight == 4u)
                pipeline =
                    InConstants.DirectDetailMode != 0u ? get(28u, private_carrier_compact_fast_answer_cso,
                                                             sizeof(private_carrier_compact_fast_answer_cso))
                    : DlssNr::Superior::Fields::PositiveUnfilteredEditWeight(InConstants)
                        ? get(30u, private_carrier_compact_fast_fade_cso, sizeof(private_carrier_compact_fast_fade_cso))
                        : get(27u, private_carrier_compact_fast_cso, sizeof(private_carrier_compact_fast_cso));
            else
                pipeline = InConstants.DirectDetailMode != 0u ? get(26u, private_carrier_compact_simple_answer_cso,
                                                                    sizeof(private_carrier_compact_simple_answer_cso))
                                                              : get(25u, private_carrier_compact_simple_cso,
                                                                    sizeof(private_carrier_compact_simple_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode == 49u)
        {
            pipeline = InConstants.DebugView == 2u
                           ? get(34u, private_ghost_coarse_dilate_2_cso, sizeof(private_ghost_coarse_dilate_2_cso))
                           : get(33u, private_ghost_coarse_dilate_1_cso, sizeof(private_ghost_coarse_dilate_1_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode == 50u || InConstants.Mode == 51u)
        {
            pipeline = InConstants.Mode == 50u
                           ? get(31u, private_ghost_coarse_1_cso, sizeof(private_ghost_coarse_1_cso))
                           : get(32u, private_ghost_coarse_2_cso, sizeof(private_ghost_coarse_2_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode == 47u || InConstants.Mode == 48u)
        {
            pipeline = nullptr;
            if (DlssNr::Superior::Fields::FastCompactScatter(InConstants))
                pipeline = get(29u, private_ghost_compact_wave_cso, sizeof(private_ghost_compact_wave_cso));
            // Devices that cannot create the wave shader retain the same algorithm via the ordinary scatter.
            if (!pipeline)
                pipeline = InConstants.Mode == 47u
                               ? get(23u, private_ghost_compact_1_cso, sizeof(private_ghost_compact_1_cso))
                               : get(24u, private_ghost_compact_2_cso, sizeof(private_ghost_compact_2_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode >= 44u && InConstants.Mode <= 46u)
        {
            const void* blobs[] = { private_ghost_clear_cso, private_ghost_scatter_cso, private_ghost_history_cso };
            const size_t sizes[] = { sizeof(private_ghost_clear_cso), sizeof(private_ghost_scatter_cso),
                                     sizeof(private_ghost_history_cso) };
            const unsigned stage = InConstants.Mode - 44u;
            pipeline = get(19u + stage, blobs[stage], sizes[stage]);
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode == 52u)
        {
            pipeline = InConstants.CompareMode == 5u
                           ? get(39u, private_ghost_contrast_coarse_cso, sizeof(private_ghost_contrast_coarse_cso))
                       : InConstants.CompareMode >= 3u
                           ? get(38u, private_ghost_contrast_compact_cso, sizeof(private_ghost_contrast_compact_cso))
                           : get(37u, private_ghost_contrast_history_cso, sizeof(private_ghost_contrast_history_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode == 53u)
        {
            pipeline = InConstants.DebugView == 2u
                           ? get(41u, private_ghost_contrast_dilate_2_cso, sizeof(private_ghost_contrast_dilate_2_cso))
                           : get(40u, private_ghost_contrast_dilate_1_cso, sizeof(private_ghost_contrast_dilate_1_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode >= 54u && InConstants.Mode <= 56u)
        {
            pipeline = InConstants.Mode == 54u ? get(50u, private_ghost_feather_horizontal_cso,
                                                     sizeof(private_ghost_feather_horizontal_cso))
                       : InConstants.Mode == 56u
                           ? get(52u, private_ghost_feather_inplace_cso, sizeof(private_ghost_feather_inplace_cso))
                           : get(51u, private_ghost_feather_apply_cso, sizeof(private_ghost_feather_apply_cso));
            if (!pipeline)
                return false;
        }
        if (InConstants.Mode >= 57u && InConstants.Mode <= 59u)
        {
            pipeline = InConstants.Mode == 57u
                           ? get(53u, private_ghost_feather_rings_cso, sizeof(private_ghost_feather_rings_cso))
                       : InConstants.Mode == 58u ? get(54u, private_ghost_feather_rings_apply_cso,
                                                       sizeof(private_ghost_feather_rings_apply_cso))
                                                 : get(55u, private_ghost_feather_rings_inplace_cso,
                                                       sizeof(private_ghost_feather_rings_inplace_cso));
            if (!pipeline)
                return false;
        }
        if (contrastGhost)
        {
            const void* blobs[] = { private_carrier_contrast_temporal_0_cso, private_carrier_contrast_temporal_1_cso,
                                    private_carrier_contrast_simple_0_cso,   private_carrier_contrast_simple_1_cso,
                                    private_carrier_contrast_fast_0_cso,     private_carrier_contrast_fast_1_cso,
                                    private_carrier_contrast_coarse_0_cso,   private_carrier_contrast_coarse_1_cso };
            const size_t sizes[] = {
                sizeof(private_carrier_contrast_temporal_0_cso), sizeof(private_carrier_contrast_temporal_1_cso),
                sizeof(private_carrier_contrast_simple_0_cso),   sizeof(private_carrier_contrast_simple_1_cso),
                sizeof(private_carrier_contrast_fast_0_cso),     sizeof(private_carrier_contrast_fast_1_cso),
                sizeof(private_carrier_contrast_coarse_0_cso),   sizeof(private_carrier_contrast_coarse_1_cso)
            };
            const unsigned selected = 2u * (InConstants.GuideHeight - 2u) + (InConstants.DirectDetailMode != 0u);
            pipeline = get(42u + selected, blobs[selected], sizes[selected]);
            if (!pipeline)
                return false;
        }
    }
    else
    {
        unsigned selected = 58;
        if (ghostRejection)
        {
            if (contrastGhost)
                selected = 66 + InConstants.GuideHeight - 2;
            else if (InConstants.GuideHeight == 0)
                selected = 59;
            else if (InConstants.GuideHeight == 1)
                selected = 60;
            else if (InConstants.GuideHeight == 2)
                selected = 61;
            else if (InConstants.GuideHeight == 3)
                selected = 62;
            else if (InConstants.GuideHeight == 4)
                selected = DlssNr::Superior::Fields::PositiveUnfilteredEditWeight(InConstants) ? 64 : 63;
            else
                selected = 65;
        }
        const void* blobs[] = { private_affine_base_cso,
                                private_affine_ghost_cso,
                                private_affine_guided_cso,
                                private_affine_temporal_cso,
                                private_affine_simple_cso,
                                private_affine_fast_cso,
                                private_affine_fast_fade_cso,
                                private_affine_coarse_cso,
                                private_affine_contrast_temporal_cso,
                                private_affine_contrast_simple_cso,
                                private_affine_contrast_fast_cso,
                                private_affine_contrast_coarse_cso };
        const size_t sizes[] = { sizeof(private_affine_base_cso),
                                 sizeof(private_affine_ghost_cso),
                                 sizeof(private_affine_guided_cso),
                                 sizeof(private_affine_temporal_cso),
                                 sizeof(private_affine_simple_cso),
                                 sizeof(private_affine_fast_cso),
                                 sizeof(private_affine_fast_fade_cso),
                                 sizeof(private_affine_coarse_cso),
                                 sizeof(private_affine_contrast_temporal_cso),
                                 sizeof(private_affine_contrast_simple_cso),
                                 sizeof(private_affine_contrast_fast_cso),
                                 sizeof(private_affine_contrast_coarse_cso) };
        pipeline = get(selected, blobs[selected - 58], sizes[selected - 58]);
    }
    if (InConstants.Mode == 60u)
        pipeline = get(56u, private_transport_bounds_cso, sizeof(private_transport_bounds_cso));
    if (InConstants.Mode == 61u)
        pipeline = get(57u, private_transport_reduce_cso, sizeof(private_transport_reduce_cso));
    if ((InConstants.Mode == 60u || InConstants.Mode == 61u) && !pipeline)
        return false;
    if (!pipeline && (InConstants.Mode == 37u || InConstants.Mode == 38u || InConstants.Mode == 39u || InConstants.Mode == 43u))
        return false;
    const bool privateExposure = InConstants.Mode >= 40u && InConstants.Mode <= 42u;
    if (privateExposure)
    {
        pipeline = get(16u, private_exposure_cso, sizeof(private_exposure_cso));
        if (!pipeline) return false; // never silently use a different algorithm
    }
    if (!pipeline && InConstants.Mode == 34u)
    {
        if (InConstants.DirectResolveFlags & 32768u)
            pipeline = get(14u, dlssnr_interpass_low36_cached_cso, sizeof(dlssnr_interpass_low36_cached_cso));
        if (!pipeline)
            pipeline = get(15u, dlssnr_interpass_low36_cso, sizeof(dlssnr_interpass_low36_cso));
    }
    if (!pipeline)
        pipeline = get(13u, DlssNr::Superior::Bytecode::DlssNr_cso, sizeof(DlssNr::Superior::Bytecode::DlssNr_cso));
    if (!_init || !pipeline || !InCmdList || !_device || !InSource || !OutTarget || !EnsureSuperiorRing())
        return false;

    const bool reuse = immutableSlot && *immutableSlot != UINT32_MAX;
    const uint32_t slot = reuse ? *immutableSlot : _superiorHeapIndex;
    if (!reuse)
        _superiorHeapIndex = (_superiorHeapIndex + 1) % DLSSNR_SUPERIOR_NUM_OF_HEAPS;

    FrameDescriptorHeap& currentHeap = _superiorHeaps[slot];
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
            CreateShaderResourceView(_device, srvs[i],
                                     currentHeap.GetSrvCPU(i)); // janblade already handles typed depth SRVs
        }

        ID3D12Resource* const uavs[kUavCount] = {
            OutTarget,
            OutKeep != nullptr ? OutKeep : OutTarget,
        };

        for (uint32_t i = 0; i < kUavCount; ++i)
            if (InConstants.Mode == 61u)
            {
                D3D12_UNORDERED_ACCESS_VIEW_DESC view {};
                view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                view.Format = DXGI_FORMAT_R32_TYPELESS;
                view.Buffer.NumElements = 64;
                view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                _device->CreateUnorderedAccessView(uavs[i], nullptr, &view, currentHeap.GetUavCPU(i));
            }
            else
                CreateUnorderedAccessView(_device, uavs[i], currentHeap.GetUavCPU(i), 0);

        if (!CreateConstantsBuffer(_device, _superiorConstantBuffers[slot], InConstants, currentHeap.GetCbvCPU(0)))
        {
            LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
            return false;
        }

        if (immutableSlot)
            *immutableSlot = slot;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(affine ? _superiorTransportRoot.Get() : _rootSignature);
    if (affine)
        InCmdList->SetComputeRootConstantBufferView(1, transportCoefficients->GetGPUVirtualAddress());
    InCmdList->SetPipelineState(pipeline);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Sized from the constants rather than from a resource, because the pass that shrinks the proxy
    // writes fewer pixels than its source has.
    const UINT dispatchWidth = InConstants.Mode == 60u   ? (InConstants.Width + 31) / 32
                               : InConstants.Mode == 61u ? 1
                               : privateExposure ? (InConstants.Mode == 40u ? (InConstants.Width + 31u) / 32u : 1u)
                               : InConstants.Mode == DlssNr::Superior::DlssNrMode_Meter
                                   ? InConstants.Width
                                   : (InConstants.Width + _numThreadsX - 1) / _numThreadsX;
    // Final-low PSO uses 8x16, the generic fallback 8x8; dispatch for the selected PSO.
    const UINT threadsY = pipeline == _superiorPipeline[8] || pipeline == _superiorRawLow
                              ? DlssNr::SuperiorScaling::LowGroupHeight
                              : _numThreadsY;
    const UINT dispatchHeight = InConstants.Mode == 60u   ? (InConstants.Height + 31) / 32
                                : InConstants.Mode == 61u ? 1
                                : privateExposure ? (InConstants.Mode == 40u ? (InConstants.Height + 31u) / 32u : 1u)
                                : InConstants.Mode == DlssNr::Superior::DlssNrMode_Meter
                                    ? InConstants.Height
                                    : (InConstants.Height + threadsY - 1) / threadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

namespace DlssNr::Superior::HostBytecode
{
#include "precompile/host_resolve_hdr_Shader.h"
#include "precompile/host_resolve_sdr_Shader.h"
#include "precompile/host_resolve_raw_Shader.h"
#include "precompile/host_low_raw_Shader.h"
#include "precompile/host_resolve_packed_hdr_Shader.h"
#include "precompile/host_resolve_packed_sdr_Shader.h"
#include "precompile/host_ghost_hdr_Shader.h"
#include "precompile/host_ghost_sdr_Shader.h"
#include "precompile/host_ghost_packed_hdr_Shader.h"
#include "precompile/host_ghost_packed_sdr_Shader.h"
#include "precompile/host_normal_sdr_Shader.h"
#include "precompile/host_normal_hdr0_Shader.h"
#include "precompile/host_normal_hdr2_Shader.h"
#include "precompile/host_normal_hdr4_Shader.h"
#include "precompile/host_normal_hdr6_Shader.h"
#include "precompile/host_normal_packed_sdr_Shader.h"
#include "precompile/host_normal_packed_hdr0_Shader.h"
#include "precompile/host_normal_packed_hdr2_Shader.h"
#include "precompile/host_normal_packed_hdr4_Shader.h"
#include "precompile/host_normal_packed_hdr6_Shader.h"
#include "precompile/host_cached_packed_sdr_Shader.h"
#include "precompile/host_cached_packed_hdr0_Shader.h"
#include "precompile/host_cached_packed_hdr2_Shader.h"
#include "precompile/host_cached_packed_hdr4_Shader.h"
#include "precompile/host_cached_packed_hdr6_Shader.h"
#include "precompile/host_warp_cache_Shader.h"
} // namespace DlssNr::Superior::HostBytecode

ID3D12PipelineState* DlssNr_Dx12::SuperiorResolvePipeline(const DlssNrConstants& params, bool rawAnswer, bool packed,
                                                          bool cachedWarp)
{
    using namespace DlssNr::Superior::HostBytecode;
    // Every packed PSO fixes gGuideHeight to 0 (precompile/dlssnr.hlsl, DLSSNR_SUPERIOR_HOST_RESOLVE): it adds the
    // signed edit to the proxy. A packed generation always supplies a signed edit, either extracted directly or
    // decoded from the private DLSS output. A raw NR answer here would be added as if it were an edit.
    if (packed && rawAnswer)
    {
        static std::once_flag once;
        std::call_once(once, [] { LOG_ERROR("Superior Scaling: packed resolve asked for a raw NR answer; skipped"); });
        return nullptr;
    }
    const bool ghostPreview = !rawAnswer && DlssNr::Superior::Fields::GhostMaskPreview(params);
    int normal = ghostPreview ? -1 :
        DlssNr::SuperiorScaling::NormalResolveVariant(rawAnswer, packed, params.Passthrough, params.ReversibleMode,
                                                      params.DebugView, params.CompareMode, params.ApplyModel);
    if (normal >= 5 && cachedWarp)
        normal += 5;
    if (normal >= 0)
    {
        auto*& pso = _superiorNormalResolve[normal];
        if (!_superiorNormalResolveAttempted[normal])
        {
            _superiorNormalResolveAttempted[normal] = true;
            const void* blobs[] {
                host_normal_sdr_cso,         host_normal_hdr0_cso,        host_normal_hdr2_cso,
                host_normal_hdr4_cso,        host_normal_hdr6_cso,        host_normal_packed_sdr_cso,
                host_normal_packed_hdr0_cso, host_normal_packed_hdr2_cso, host_normal_packed_hdr4_cso,
                host_normal_packed_hdr6_cso, host_cached_packed_sdr_cso,  host_cached_packed_hdr0_cso,
                host_cached_packed_hdr2_cso, host_cached_packed_hdr4_cso, host_cached_packed_hdr6_cso
            };
            const size_t sizes[] { sizeof(host_normal_sdr_cso),         sizeof(host_normal_hdr0_cso),
                                   sizeof(host_normal_hdr2_cso),        sizeof(host_normal_hdr4_cso),
                                   sizeof(host_normal_hdr6_cso),        sizeof(host_normal_packed_sdr_cso),
                                   sizeof(host_normal_packed_hdr0_cso), sizeof(host_normal_packed_hdr2_cso),
                                   sizeof(host_normal_packed_hdr4_cso), sizeof(host_normal_packed_hdr6_cso),
                                   sizeof(host_cached_packed_sdr_cso),  sizeof(host_cached_packed_hdr0_cso),
                                   sizeof(host_cached_packed_hdr2_cso), sizeof(host_cached_packed_hdr4_cso),
                                   sizeof(host_cached_packed_hdr6_cso) };
            CreateComputePipeline(_device, &pso, blobs[normal], sizes[normal], nullptr);
        }
        if (pso)
            return pso;
    }
    const bool sdr = params.Passthrough != 0;
    // The raw (non-temporal) resolve compiles to the same bytecode for SDR and HDR: one blob, one PSO (slot 2).
    const auto index = ghostPreview ? 6u + (packed ? 2u : 0u) + (sdr ? 1u : 0u)
                                   : packed ? 4u + (sdr ? 1u : 0u) : rawAnswer ? 2u : (sdr ? 1u : 0u);
    auto*& pso = _superiorResolve[index];
    if (!_superiorResolveAttempted[index])
    {
        _superiorResolveAttempted[index] = true;
        const void* blobs[] { host_resolve_hdr_cso, host_resolve_sdr_cso,        host_resolve_raw_cso,
                              host_resolve_raw_cso, host_resolve_packed_hdr_cso, host_resolve_packed_sdr_cso,
                              host_ghost_hdr_cso, host_ghost_sdr_cso, host_ghost_packed_hdr_cso, host_ghost_packed_sdr_cso };
        const size_t sizes[] { sizeof(host_resolve_hdr_cso),        sizeof(host_resolve_sdr_cso),
                               sizeof(host_resolve_raw_cso),        sizeof(host_resolve_raw_cso),
                               sizeof(host_resolve_packed_hdr_cso), sizeof(host_resolve_packed_sdr_cso),
                               sizeof(host_ghost_hdr_cso), sizeof(host_ghost_sdr_cso), sizeof(host_ghost_packed_hdr_cso), sizeof(host_ghost_packed_sdr_cso) };
        static_assert(std::size(blobs) == kSuperiorResolveCount && std::size(sizes) == kSuperiorResolveCount);
        CreateComputePipeline(_device, &pso, blobs[index], sizes[index], nullptr);
    }
    return pso;
}

bool DlssNr_Dx12::BuildSuperiorWarpCache(ID3D12GraphicsCommandList* cmd, unsigned width, unsigned height,
                                         ID3D12Resource* metadata, ID3D12Resource* output)
{
    using namespace DlssNr::Superior::HostBytecode;
    if (!_superiorWarpCacheAttempted)
    {
        _superiorWarpCacheAttempted = true;
        CreateComputePipeline(_device, &_superiorWarpCache, host_warp_cache_cso, sizeof(host_warp_cache_cso), nullptr);
    }
    if (!_superiorWarpCache || !metadata || !output || !cmd || !_init || !EnsureSuperiorRing())
        return false;
    DlssNr::Superior::DlssNrConstants params {};
    params.Width = width;
    params.Height = height;
    const uint32_t slot = _superiorHeapIndex;
    _superiorHeapIndex = (_superiorHeapIndex + 1) % DLSSNR_SUPERIOR_NUM_OF_HEAPS;
    auto& heap = _superiorHeaps[slot];
    for (uint32_t i = 0; i < kSrvCount; ++i)
        CreateShaderResourceView(_device, metadata, heap.GetSrvCPU(i));
    for (uint32_t i = 0; i < kUavCount; ++i)
        CreateUnorderedAccessView(_device, output, heap.GetUavCPU(i), 0);
    if (!CreateConstantsBuffer(_device, _superiorConstantBuffers[slot], params, heap.GetCbvCPU(0)))
        return false;
    ID3D12DescriptorHeap* heaps[] = { heap.GetHeapCSU() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootSignature(_rootSignature);
    cmd->SetPipelineState(_superiorWarpCache);
    cmd->SetComputeRootDescriptorTable(0, heap.GetTableGPUStart());
    cmd->Dispatch((std::max(11u, std::max(width, height)) + 7u) / 8u, 1, 1);
    return true;
}

ID3D12PipelineState* DlssNr_Dx12::SuperiorRawLowPipeline()
{
    using namespace DlssNr::Superior::HostBytecode;
    if (!_superiorRawLowAttempted)
    {
        _superiorRawLowAttempted = true;
        CreateComputePipeline(_device, &_superiorRawLow, host_low_raw_cso, sizeof(host_low_raw_cso), nullptr);
    }
    return _superiorRawLow;
}
