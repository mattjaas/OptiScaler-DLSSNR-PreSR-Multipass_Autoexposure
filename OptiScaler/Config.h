#pragma once

#include "SysUtils.h"
#include "State.h"

#include <optional>
#include <filesystem>

enum HasDefaultValue
{
    WithDefault,
    NoDefault,
    SoftDefault // Change always gets saved to the config
};

template <class T, HasDefaultValue defaultState = WithDefault> class CustomOptional : public std::optional<T>
{
  private:
    T _defaultValue;
    std::optional<T> _configIni;
    bool _volatile;

  public:
    CustomOptional(T defaultValue)
        requires(defaultState != NoDefault)
        : std::optional<T>(), _defaultValue(std::move(defaultValue)), _configIni(std::nullopt), _volatile(false)
    {
    }

    CustomOptional()
        requires(defaultState == NoDefault)
        : std::optional<T>(), _defaultValue(T {}), _configIni(std::nullopt), _volatile(false)
    {
    }

    // Prevents a change from being saved to ini
    constexpr void set_volatile_value(const T& value)
    {
        if (!_volatile)
        { // make sure the previously set value is saved
            if (this->has_value())
                _configIni = this->value();
            else
                _configIni = std::nullopt;
        }
        _volatile = true;
        std::optional<T>::operator=(value);
    }

    // Use this when first setting a CustomOptional
    constexpr void set_from_config(const std::optional<T>& opt)
    {
        if (!this->has_value())
        {
            _configIni = opt;
            std::optional<T>::operator=(opt);
        }
    }

    constexpr CustomOptional& operator=(const T& value)
    {
        _volatile = false;
        std::optional<T>::operator=(value);
        return *this;
    }

    constexpr CustomOptional& operator=(T&& value)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(value));
        return *this;
    }

    constexpr CustomOptional& operator=(const std::optional<T>& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(opt);
        return *this;
    }

    constexpr CustomOptional& operator=(std::optional<T>&& opt)
    {
        _volatile = false;
        std::optional<T>::operator=(std::move(opt));
        return *this;
    }

    // Needed for string literals for some reason
    constexpr CustomOptional& operator=(const char* value)
        requires std::same_as<T, std::string>
    {
        _volatile = false;
        std::optional<T>::operator=(T(value));
        return *this;
    }

    constexpr T value_or_default() const&
        requires(defaultState != NoDefault)
    {
        return this->has_value() ? this->value() : _defaultValue;
    }

    constexpr T value_or_default() &&
        requires(defaultState != NoDefault) {
            return this->has_value() ? std::move(this->value()) : std::move(_defaultValue);
        }

        constexpr std::optional<T> value_for_config()
            requires(defaultState == WithDefault)
    {
        if (_volatile)
        {
            if (_configIni != _defaultValue)
                return _configIni;

            return std::nullopt;
        }

        if (!this->has_value() || *this == _defaultValue)
            return std::nullopt;

        return this->value();
    }

    constexpr std::optional<T> value_for_config()
        requires(defaultState != WithDefault)
    {
        if (_volatile)
            return _configIni;

        if (this->has_value())
            return this->value();

        return std::nullopt;
    }

    constexpr T value_for_config_or(T other)
    {
        auto option = value_for_config();

        if (option.has_value())
            return option.value();
        else
            return other;
    }
};

constexpr inline int UnboundKey = -1;
constexpr uint32_t NV_PRESET_LATEST = 0x00FFFFFF;

enum FpsOverlayPos : uint32_t
{
    FpsOverlayPos_TopLeft,
    FpsOverlayPos_TopRight,
    FpsOverlayPos_BottomLeft,
    FpsOverlayPos_BottomRight,
    FpsOverlayPos_COUNT,
};

enum FpsOverlay : uint32_t
{
    FpsOverlay_JustFPS,
    FpsOverlay_Simple,
    FpsOverlay_Detailed,
    FpsOverlay_DetailedGraph,
    FpsOverlay_Full,
    FpsOverlay_FullGraph,
    FpsOverlay_ReflexTimings,
    FpsOverlay_COUNT,
};

// Output scaling downscaler
enum class Scaler : uint32_t
{
    FSR1 = 0,
    Bicubic = 1,
    CatmullRom = 2,
    Lanczos2 = 3,
    Lanczos3 = 4,
    Kaiser2 = 5,
    Kaiser3 = 6,
    Magic = 7,
    Count
};

enum class ForceReflex : uint32_t
{
    InGame,
    ForceDisable,
    ForceEnable,
    Count
};

enum class LFXMode : uint32_t
{
    Conservative,
    Aggressive,
    ReflexIDs,
    Count
};

enum class LowLatencyInput : uint32_t
{
    None,
    Auto,
    AntiLag2,
    Reflex,
    XeLL,
    UeLowLatency,
    _
};

enum class LowLatencyMode : uint32_t
{
    None,
    Auto,
    LatencyFlex,
    AntiLag2,
    XeLL,
    AntiLagVk,
    Reflex
};

class Config
{
  public:
    Config();

    // Init flags
    CustomOptional<bool, NoDefault> DepthInverted;
    CustomOptional<bool, NoDefault> AutoExposure;
    CustomOptional<bool, NoDefault> HDR;
    CustomOptional<bool, NoDefault> JitterCancellation;
    CustomOptional<bool, NoDefault> DisplayResolution;
    CustomOptional<bool, NoDefault> DisableReactiveMask;
    CustomOptional<float> DlssReactiveMaskBias { 0.45f };

    // Logging
    CustomOptional<bool> LogToFile { false };
    CustomOptional<bool> LogToConsole { false };
    CustomOptional<bool> LogToDebug { false };
    CustomOptional<bool> LogToNGX { false };
    CustomOptional<bool> OpenConsole { false };
    CustomOptional<bool> DebugWait { false }; // not in ini
    CustomOptional<int> LogLevel { 0 };
    CustomOptional<std::wstring> LogFileName { L"OptiScaler.log" };
    CustomOptional<bool> LogSingleFile { true };
    CustomOptional<bool> LogAsync { false };
    CustomOptional<int> LogAsyncThreads { 4 };

    // XeSS
    CustomOptional<bool> BuildPipelines { true };
    CustomOptional<int32_t> NetworkModel { 0 };
    CustomOptional<bool> CreateHeaps { true };

    // DLSS Neural Rendering
    // NR is opt-in. Placement defaults to the upscaler output.
    CustomOptional<bool> DlssNrEnabled { false };
    CustomOptional<bool> DlssNrRunBeforeSr { false };
    CustomOptional<bool> DlssNrFinishedPicture { false };
    // Fit the scene-to-finished HDR response for early residuals.
    CustomOptional<bool> DlssNrHdrTransfer { false };
    // Generate before SR, privately upscale the edit, then compose after SR.
    CustomOptional<bool> DlssNrDeferredDlss { false };
    // Private carrier: 0 DLSS, 1 FSR 2.2, 2 FidelityFX runtime, 3 XeSS.
    CustomOptional<int> DlssNrPrivateUpscaler { 0 };
    // Legacy alias for the deferred path when RunBeforeSR is enabled.
    CustomOptional<bool> DlssNrResidualAcrossRr { false };
    // RR history blend before private upscaling, clamped to 0.01..1.
    CustomOptional<float> DlssNrResidualAcrossRrBlend { 0.08f };
    CustomOptional<int> DlssNrToggleKey { UnboundKey };
    CustomOptional<uint32_t> DlssNrPreset { 0 };
    CustomOptional<float> DlssNrIntensity { 1.0f };
    // 0 Standard, 1 Natural, 2 Cinematic.
    CustomOptional<uint32_t> DlssNrStyle { 0 };
    CustomOptional<float> DlssNrLocalStructure { 1.0f };
    CustomOptional<float> DlssNrLocalTone { 1.0f };
    // -1 follows the model's LocalStructure setting.
    CustomOptional<float> DlssNrSkinStructure { -1.0f };
    CustomOptional<bool> DlssNrAutoMask { true };
    // Optional final-composition filter, independent of the model's semantic mask.
    CustomOptional<bool> DlssNrSkinProtection { false };
    CustomOptional<float> DlssNrSkinDetail { 1.0f };
    CustomOptional<float> DlssNrSkinColour { 1.0f };
    CustomOptional<float> DlssNrEnvironmentDetail { 1.0f };
    CustomOptional<float> DlssNrEnvironmentColour { 1.0f };
    CustomOptional<bool> DlssNrShowSkinMask { false };
    CustomOptional<bool> DlssNrUnlockPasses { false };
    // Below-native WorkingScale: resample depth and motion to the model's working size, so the
    // guides and the colour it reprojects agree pixel for pixel. Off hands the model full-size
    // guides for a smaller colour, which is what flickered at every scale below 100%.
    CustomOptional<bool> DlssNrMatchGuides { true };
    // The model's motion-vector scale converts the game's units to pixels of the motion texture the
    // model is handed (the matched working-size resample, or the game's own region), measured
    // against the size the vectors come in: the render size for low-resolution vectors, the output
    // size otherwise (the DLSS-enlargement path's formula). Off restores the old conversion,
    // working size / frame size, which halved every vector below 100% in a game upscaling 2x with
    // low-resolution vectors.
    CustomOptional<bool> DlssNrRenderMotionScale { true };
    // Passes 2..30 inherit pass 1, except LocalTone defaults to zero.
    struct NrPassOverrides
    {
        CustomOptional<uint32_t, NoDefault> preset, style;
        CustomOptional<float, NoDefault> intensity, structure, tone, skin;
        CustomOptional<bool, NoDefault> autoMask;
    };
    NrPassOverrides DlssNrPassOverrides[29]; // Existing Pass2..Pass30 INI keys remain unchanged.

    // Composition strengths are separate from model creation settings.
    CustomOptional<float> DlssNrTransferStrength { 1.0f };
    CustomOptional<float> DlssNrColourStrength { 1.0f };

    // 0 soft knee; 1/2 Neutwo compose/replace; 3/4 hybrid compose/replace.
    CustomOptional<uint32_t> DlssNrReversibleMode { 0 };

    // Hide the edit while leaving NR running for held-frame comparisons.
    CustomOptional<bool> DlssNrApplyModel { true };

    // Freeze NR input for tuning. See dlssnr/design/frame-hold.md.
    CustomOptional<bool> DlssNrHoldFrame { false };

    // Maximum pixel brightening ratio.
    CustomOptional<float> DlssNrMaxRatio { 2.0f };

    // Maximum luminance reduction allowed from the NR result, in percent.
    // 100 means darkening is uncapped; 0 prevents any darkening.
    CustomOptional<float> DlssNrMaxDarkening { 100.0f };

    // Final global colour matching. Compare the untouched pre-NR frame with the fully composed post-NR
    // frame in perceptual OKLab. Zero keeps the legacy output bit-for-bit on this branch.
    CustomOptional<float> DlssNrFinalChromaticityRecovery { 0.0f }; // 0..100%, global temperature/tint/hue cast
    // Optional strict mode: rotate/translate chromaticity direction but restore each pixel's pre-correction OKLab chroma.
    CustomOptional<bool> DlssNrFinalChromaticityPreserveSaturation { false };
    CustomOptional<float> DlssNrFinalSaturationRecovery { 0.0f };   // 0..100%, global perceptual chroma match
    CustomOptional<uint32_t> DlssNrFinalSaturationMode { 0 };       // 0 Saturation, 1 Vibrance
    CustomOptional<float> DlssNrFinalHighSaturationProtection { 0.0f }; // 0..100%, positive-gain protection
    CustomOptional<float> DlssNrFinalColourSmoothingMs { 250.0f };  // time constant; 0 = immediate

    // Reduced output: 0 classic, 1/2 matched residual spatial/DLSS, 3/4 lighting + colour spatial/DLSS,
    // 5 direct NR output enlarged and composed like a native-resolution model answer,
    // 6 upscale P50 and NR50 separately, create their residual at P100, then apply it to untouched P100,
    // 7 P100-guided residual: jointly upscale NR50-P50 while native P100 guides edge-aware positive weights,
    // 8 temporal DLAA NR50: stabilize NR50 at the working resolution with 1:1 DLSS DLAA, then P100-guide it,
    // 9 temporal DLAA residual: stabilize a signed E50 carrier at the working resolution,
    // 10 image-anchored temporal DLAA residual: stabilize Proxy50 + K*(NR50-Proxy50), then recover E50.
    CustomOptional<uint32_t> DlssNrTransfer { 1 };
    // NGX render preset for private DLSS SR used by NR enlargement: 0 default, 1..15 A..O,
    // NV_PRESET_LATEST for the latest model supported by the loaded DLSS DLL.
    CustomOptional<int> DlssNrScalingDlssPreset { 1 };
    // Independent NGX render presets for the 1:1 temporal DLAA experiments.
    CustomOptional<int> DlssNrTemporalDlaaNrPreset { 1 };
    CustomOptional<int> DlssNrTemporalDlaaResidualPreset { 1 };
    CustomOptional<int> DlssNrTemporalDlaaAnchoredPreset { 1 };
    // Auto Exposure is intentionally independent per temporal experiment so switching modes does not
    // silently change another mode's input interpretation.
    CustomOptional<bool> DlssNrTemporalDlaaNrAutoExposure { false };
    CustomOptional<bool> DlssNrTemporalDlaaResidualAutoExposure { false };
    CustomOptional<bool> DlssNrTemporalDlaaAnchoredAutoExposure { false };

    // Temporal residual carrier experiments.
    // Residual encoding: 0 reversible nonlinear E/(1+abs(E)), 1 linear 0.5+K*E.
    CustomOptional<uint32_t> DlssNrTemporalResidualEncoding { 0 };
    // Gain mode: 0 Manual, 1 Auto Strict (no intentional clipping), 2 Auto Robust
    // (ignores one most restrictive sample per ~32x32 source tile).
    CustomOptional<uint32_t> DlssNrTemporalResidualGainMode { 1 };
    CustomOptional<float> DlssNrTemporalResidualManualGain { 4.0f };
    CustomOptional<uint32_t> DlssNrTemporalAnchoredGainMode { 1 };
    CustomOptional<float> DlssNrTemporalAnchoredManualGain { 4.0f };
    // Compress Proxy around the carrier midpoint before adding K*E. At the legacy [0,1] range,
    // strength 0.5 maps the clean image to 0.25..0.75; custom ranges move the midpoint but keep
    // the clean-anchor contrast excursion unchanged.
    CustomOptional<float> DlssNrTemporalAnchoredBaseStrength { 0.50f };
    // Keep an explicit Custom selection sticky even when its numeric value exactly matches a preset.
    CustomOptional<bool> DlssNrTemporalAnchoredBaseStrengthCustom { false };
    // Reference experiment: run a second independent 1:1 DLAA history on the compressed clean anchor
    // and decode (DLAA(anchor+K*E)-DLAA(anchor))/K.
    CustomOptional<bool> DlssNrTemporalAnchoredPairedBaseline { false };
    // Manual carrier domain for the linear/image-anchored experiments. Neutral is always the exact
    // midpoint (Min+Max)/2. The storage texture is FP16, whose largest finite magnitude is 65504.
    // This numeric storage limit does not imply that private DLAA preserves such extreme values.
    CustomOptional<float> DlssNrTemporalCarrierRangeMin { 0.0f };
    CustomOptional<float> DlssNrTemporalCarrierRangeMax { 1.0f };
    // Independently advertise the private 1:1 DLAA input as HDR to NGX. This is intentionally
    // separate from the carrier numeric range so either behaviour can be A/B tested on its own.
    CustomOptional<bool> DlssNrTemporalDlaaIsHdr { false };

    // Auto Exposure for the other private DLSS roles.
    CustomOptional<bool> DlssNrScalingDlssAutoExposure { false };
    CustomOptional<bool> DlssNrDetailReferenceDlssAutoExposure { false };
    CustomOptional<bool> DlssNrPrivateUpscalerAutoExposure { false };
    // Carrier headroom from the active carrier limits for automatic gain. Auto ceiling prevents
    // effectively-infinite K on nearly-zero residuals; typed finite values remain user-adjustable.
    CustomOptional<float> DlssNrTemporalCarrierMargin { 0.01f };
    CustomOptional<float> DlssNrTemporalCarrierAutoMaxGain { 32.0f };
    // Auto K drops immediately when the current frame requires it, but grows at this rate
    // to avoid changing the DLAA carrier amplitude too aggressively between frames.
    CustomOptional<float> DlssNrTemporalCarrierAutoRiseStopsPerSecond { 4.0f };
    // Ignore the two known NVIDIA diagnostic watermark footprints when measuring Auto-K limits:
    // the game/SR watermark scales with the working image; the NR watermark is approximately
    // fixed-size in pixels. The actual carrier still includes those pixels unchanged.
    CustomOptional<bool> DlssNrTemporalCarrierIgnoreNvidiaWatermarks { true };
    // Extra conservative extent from the bottom-left watermark anchor. X absorbs rightward shifts /
    // longer text; Y absorbs upward shifts / perspective-correction differences between games.
    CustomOptional<float> DlssNrTemporalCarrierWatermarkMarginX { 128.0f };
    CustomOptional<float> DlssNrTemporalCarrierWatermarkMarginY { 64.0f };

    // Direct native-output upscalers. 0 bilinear, 1 bicubic, 2 Catmull-Rom, 3 Lanczos2,
    // 4 Lanczos3, 5 Kaiser2, 6 Kaiser3, 7 Area, 8 MAGIC, 9 FSR1, 10 DLSS.
    // Output defaults to DLSS to preserve the previous Direct-DLSS path.
    CustomOptional<uint32_t> DlssNrDirectOutputUpscaler { 10 };
    // Upscaled NR residual has independent filters for all three resize legs.
    // Downscale uses the proxy-filter selector (0..11); both upscalers use the Direct selector (0..10).
    CustomOptional<uint32_t> DlssNrUpscaledResidualDownscaleFilter { 0 };
    CustomOptional<uint32_t> DlssNrUpscaledResidualReferenceUpscaler { 9 };
    CustomOptional<uint32_t> DlssNrUpscaledResidualUpscaler { 10 };
    // P50 -> P100 reference execution: 0 Auto, 1 Serial, 2 Async compute.
    CustomOptional<uint32_t> DlssNrUpscaledResidualReferenceExecutionMode { 0 };

    // P100-guided residual family (Transfer=7..10). Transfer 7 directly upsamples NR50-P50.
    // Transfer 8 applies 1:1 DLAA to NR50; 9 applies DLAA to a signed residual carrier; 10 uses
    // a compressed image anchor with optional paired clean-anchor DLAA.
    // The final resolve then uses positive joint-bilateral weights guided by untouched native P100.
    CustomOptional<uint32_t> DlssNrGuidedResidualRadius { 1 };       // P50 texels; 1 => 3x3, 2 => 5x5
    CustomOptional<float> DlssNrGuidedResidualRangeSigma { 0.015f }; // capture-derived encoded proxy-domain RGB distance
    CustomOptional<float> DlssNrGuidedResidualSpatialSigma { 1.20f };
    CustomOptional<float> DlssNrGuidedResidualGuideStrength { 0.75f }; // 0 bilinear residual, 1 fully guided
    // Residual frequency shaping: 0 off, 1 Auto by final-pass style, 2 Manual.
    // Auto for the capture-calibrated plain guided path (Transfer 7):
    // Standard high/low 0.421/0.609, Natural 0.454/0.741, Cinematic 0.370/0.780.
    // Temporal DLAA variants intentionally keep Auto neutral until separately capture-calibrated.
    CustomOptional<uint32_t> DlssNrGuidedResidualShaping { 1 };
    // Manual values are calibrated at 50% working scale and one effective pass.
    // Legacy High/Low keys remain the Natural values for INI compatibility.
    CustomOptional<float> DlssNrGuidedResidualStandardHighGain { 0.421f };
    CustomOptional<float> DlssNrGuidedResidualStandardLowGain { 0.609f };
    CustomOptional<float> DlssNrGuidedResidualHighGain { 0.454f }; // Natural
    CustomOptional<float> DlssNrGuidedResidualLowGain { 0.741f };  // Natural
    CustomOptional<float> DlssNrGuidedResidualCinematicHighGain { 0.370f };
    CustomOptional<float> DlssNrGuidedResidualCinematicLowGain { 0.780f };
    // When enabled, the resolution-corrected high/mid gain is compounded once per effective NR pass.
    // Low-frequency gain is never pass-compounded.
    CustomOptional<bool> DlssNrGuidedResidualCompoundPasses { true };
    // Optional capture-derived deep-shadow confidence; experimental until validated on more scenes.
    CustomOptional<bool> DlssNrGuidedResidualShadowGate { false };
    CustomOptional<float> DlssNrGuidedResidualShadowFloor { 0.15f };
    CustomOptional<float> DlssNrGuidedResidualShadowLow { 0.02f };
    CustomOptional<float> DlssNrGuidedResidualShadowHigh { 0.08f };
    // Reference used to reconstruct P50 to P100 for lost-detail detection.
    // FSR1 is the default reconstruction filter; Area remains the default P100 -> P50 reduction.
    CustomOptional<uint32_t> DlssNrDirectDetailReferenceUpscaler { 9 };
    // 0 Auto, 1 Serial, 2 Async compute. Async is only used for non-DLSS P50 references on
    // an owned DX12 finished-picture path; unsupported cases fall back to Serial.
    CustomOptional<uint32_t> DlssNrDirectDetailReferenceExecutionMode { 0 };

    // Direct-DLSS native detail recovery: 0 off, 1 full lost detail, 2 NR-gated.
    CustomOptional<uint32_t> DlssNrDirectDetailRecovery { 0 };
    // 0 = do not suppress restored detail, 100 = full NR-derived suppression mask.
    CustomOptional<float> DlssNrDirectDetailMaskStrength { 100.0f };

    // Experimental reduced-resolution detail-quality laboratory. All defaults are neutral/off.
    // Numeric slider ranges are UI conveniences only; manually typed finite values are intentionally unbounded.
    // Input prefilter: 0 off, 1 uniform 5-tap, 2 edge-selective 5-tap, 3 edge-normal 3-tap,
    // 4 thin-edge core attenuation. Core attenuation has its own neutral sub-selector so selecting the
    // experiment itself never changes the image until Luma-only or RGB is chosen.
    CustomOptional<uint32_t> DlssNrExperimentInputFilter { 0 };
    CustomOptional<float> DlssNrExperimentInputRadius { 0.75f };       // P50/P60 pixels
    CustomOptional<float> DlssNrExperimentInputStrength { 1.0f };     // slider 0..1; typed value unbounded
    CustomOptional<float> DlssNrExperimentEdgeThreshold { 0.04f };    // model-domain luma range
    CustomOptional<uint32_t> DlssNrExperimentCoreAttenuation { 0 };   // 0 off, 1 luma-only, 2 RGB
    CustomOptional<float> DlssNrExperimentCoreAttenuationStrength { 0.75f };
    CustomOptional<float> DlssNrExperimentCoreDetectionThreshold { 0.04f };
    CustomOptional<float> DlssNrExperimentCoreWidth { 1.0f };         // reduced-resolution pixels
    CustomOptional<float> DlssNrExperimentCoreHaloProtection { 1.0f };
    // Reference source for P50->P100 reconstruction: 0 = the actual (possibly softened) NR input,
    // 1 = the original sharp reduced proxy. This intentionally exposes both hypotheses.
    CustomOptional<uint32_t> DlssNrExperimentReferenceSource { 0 };
    // Rebase the processed model response from prepared B back onto sharp S: S + (N - B).
    // Default off preserves exact comparability with the previous detail-quality experiments.
    CustomOptional<bool> DlssNrExperimentCancelInputPreparationFootprint { false };
    // NR50 post-filter: 0 off, 1 uniform, 2 edge-selective isotropic, 3 edge-normal, 4 excess-only edge-normal.
    CustomOptional<uint32_t> DlssNrExperimentNrEdgeFilter { 0 };
    CustomOptional<float> DlssNrExperimentNrEdgeRadius { 0.75f };
    CustomOptional<float> DlssNrExperimentNrEdgeStrength { 1.0f };
    // Blur-direction ghost guard. It compares S=sharp P50, B=prepared P50 and N=NR50 in the existing
    // post-NR small-raster dispatch. 0 off, 1 basic, 2 edge-only, 3 edge-only directional.
    CustomOptional<uint32_t> DlssNrExperimentGhostGuard { 0 };
    CustomOptional<float> DlssNrExperimentGhostGuardStrength { 1.0f };
    CustomOptional<float> DlssNrExperimentGhostDetectionThreshold { 0.01f };
    CustomOptional<float> DlssNrExperimentGhostEdgeThreshold { 0.04f };
    CustomOptional<float> DlssNrExperimentGhostBandRadius { 2.0f };
    CustomOptional<float> DlssNrExperimentGhostMaxSuppression { 1.0f };
    // Low/mid-frequency ghost suppression: 0 off, 1 one-band, 2 two-band.
    CustomOptional<uint32_t> DlssNrExperimentGhostBandSuppression { 0 };
    CustomOptional<float> DlssNrExperimentBandSuppressionStrength { 1.0f };
    CustomOptional<float> DlssNrExperimentLowBandRadius { 3.0f };
    CustomOptional<float> DlssNrExperimentMidBandRadius { 1.5f };
    // Scale-aware model structure compensation for Standard/Natural only. The factor is reached at P50
    // and interpolates back to 1.0 at P100.
    CustomOptional<bool> DlssNrExperimentScaleAwareStructure { false };
    CustomOptional<float> DlssNrExperimentStructureP50Factor { 0.60f };
    // Native-P100 resulting-edge gain limiter: 0 off, 1 fine band, 2 fine + mid bands.
    // Strength blends toward the capped result; MaxGain=1 prevents NR from increasing band energy
    // above untouched P100, while values >1 allow proportional amplification.
    CustomOptional<uint32_t> DlssNrExperimentP100EdgeLimiter { 0 };
    CustomOptional<float> DlssNrExperimentP100EdgeLimiterStrength { 1.0f };
    CustomOptional<float> DlssNrExperimentP100EdgeLimiterMaxGain { 1.0f };
    CustomOptional<bool> DlssNrExperimentP100EdgeLimiterDebug { false };
    // Structure-gain transfer: 0 off, 1 one-band (radius 1), 2 two-band (radii 1+2).
    // It keeps model low-frequency edits but derives high-frequency geometry from untouched P100.
    CustomOptional<uint32_t> DlssNrExperimentStructureTransfer { 0 };
    CustomOptional<float> DlssNrExperimentStructureTransferStrength { 1.0f };
    // Optional safeguards for ringing / shadow sparkles. Defaults preserve A/B availability.
    CustomOptional<bool> DlssNrExperimentStructurePolarityGuard { false };
    // 0 off, 1 hard local P100 envelope, 2 soft-knee local P100 envelope.
    CustomOptional<uint32_t> DlssNrExperimentStructureEnvelope { 0 };
    CustomOptional<float> DlssNrExperimentStructureEnvelopeMargin { 0.0f }; // percent of local P100 RGB span
    CustomOptional<float> DlssNrExperimentStructureMaxGain { 4.0f };
    CustomOptional<float> DlssNrExperimentStructureConfidenceThreshold { 0.020f };
    CustomOptional<bool> DlssNrExperimentStructureShadowProtection { false };
    CustomOptional<float> DlssNrExperimentStructureShadowThreshold { 0.08f };
    CustomOptional<float> DlssNrExperimentStructureShadowStrength { 1.0f };

    // GPU-time display smoothing window in milliseconds. 0 keeps the latest raw sample.
    // Four decimal digits are exposed in the UI: 0..9999 ms.
    CustomOptional<uint32_t> DlssNrGpuTimeAverageWindowMs { 0 };

    // 0 normal, 1 model input, 2 model answer, 3 amplified edit.
    CustomOptional<uint32_t> DlssNrDebugView { 0 };

    // 0 off, 1 side by side, 2 wipe.
    CustomOptional<uint32_t> DlssNrCompare { 0 };
    CustomOptional<float> DlssNrCompareSplit { 0.5f };

    // Side by side: 1 fit, 2 fill/crop.
    CustomOptional<float> DlssNrCompareZoom { 1.0f };

    CustomOptional<bool> DlssNrCompareSwap { false };

    CustomOptional<bool> DlssNrCompareTags { false };
    CustomOptional<float> DlssNrTagScale { 1.5f };

    // Model width/height scale; composition remains at the input size.
    CustomOptional<float> DlssNrWorkingScale { 1.0f };

    // Filter used to reduce the ordinary full-resolution proxy before NR below 100%.
    // 0 area, 1 bilinear, 2 Catmull-Rom, 3 Lanczos2, 4 point, 5 FSR1, 6 bicubic,
    // 7 Lanczos3, 8 Kaiser2, 9 Kaiser3, 10 MAGIC, 11 SSIM Sharp (experimental).
    CustomOptional<uint32_t> DlssNrProxyDownscaleFilter { 0 };

    CustomOptional<bool> DlssNrSpatialCompression { false };
    CustomOptional<float> DlssNrSpatialCenterX { 80.0f };
    CustomOptional<float> DlssNrSpatialCenterY { 80.0f };
    CustomOptional<float> DlssNrSpatialWorkX { 90.0f };
    CustomOptional<float> DlssNrSpatialWorkY { 90.0f };
    CustomOptional<float> DlssNrSpatialOffsetX { 0.0f };
    CustomOptional<float> DlssNrSpatialOffsetY { 0.0f };
    CustomOptional<float> DlssNrSpatialShiftX { 0.0f };
    CustomOptional<float> DlssNrSpatialShiftY { 0.0f };
    CustomOptional<bool> DlssNrSpatialShowCenter { false };
    CustomOptional<bool> DlssNrSpatialShowWork { false };

    // Independent downsampling filter for NR model scales above 100%.
    CustomOptional<Scaler> DlssNrScalingDownscaler { Scaler::Lanczos3 };

    // Sequential layers with independent histories; up to 30 when unlocked.
    CustomOptional<uint32_t> DlssNrPasses { 1 };
    // Between-pass reconstruction only changes the input to the next NR feature. Final transfer stays independent.
    // 0 Off, 1 P100-guided -> P100 -> working-res, 2 P100-guided fused -> working-res.
    CustomOptional<uint32_t> DlssNrInterPassReconstruction { 0 };
    // Optional faster shader implementation of the SAME P100-guided inter-pass reconstruction.
    // False = original reference; true = optimized shared-tap fused P50 Area path and cheaper weights.
    CustomOptional<bool> DlssNrInterPassExactOptimized { false };
    // v2 P50/Area/radius=1 exact-stencil specialization; ON by default for optimized inter-pass.
    // Preserving the v1 path as a controllable A/B reference for GPU timing and image inspection.
    CustomOptional<bool> DlssNrInterPassSharedBilinear { true };
    // Extend fused Area guided/bilinear reuse to any working scale (including P66).
    // Cached geometry scalars update only when actual native/model dimensions change.
    // OFF retains v2 as the A/B performance and quality reference.
    CustomOptional<bool> DlssNrInterPassDynamicSharedTaps { true };

    // Manual white-point divisor for the HDR-to-model encode.
    CustomOptional<float> DlssNrWhitePointScale { 1.0f };
    CustomOptional<float> DlssNrReplaceDetailStrength { 0.0f };
    CustomOptional<float> DlssNrResidualConfidenceSensitivity { 0.0f };
    CustomOptional<uint32_t> DlssNrWhitePointSource { 0u };
    CustomOptional<float> DlssNrWhitePointTrim { 1.0f };
    CustomOptional<float> DlssNrAutoExposureTrim { 5.0f };
    CustomOptional<float> DlssNrAutoExposureHighlightProtection { 0.0f };
    CustomOptional<std::string> DlssNrExposureTrimAnchors { "" };
    CustomOptional<std::string> DlssNrAutoExposureTrimAnchors { "" };

    // --- end DLSS 5 Neural Rendering -------------------------------------------------------------

    // DLSS
    CustomOptional<bool> DLSSEnabled { true };
    CustomOptional<bool> RenderPresetOverride { false };
    CustomOptional<uint32_t> RenderPresetForAll { 0 };
    CustomOptional<uint32_t> RenderPresetDLAA { 0 };
    CustomOptional<uint32_t> RenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> RenderPresetQuality { 0 };
    CustomOptional<uint32_t> RenderPresetBalanced { 0 };
    CustomOptional<uint32_t> RenderPresetPerformance { 0 };
    CustomOptional<uint32_t> RenderPresetUltraPerformance { 0 };

    // DLSSD
    CustomOptional<bool> DLSSDRenderPresetOverride { false };
    CustomOptional<uint32_t> DLSSDRenderPresetForAll { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetDLAA { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetQuality { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetBalanced { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetPerformance { 0 };
    CustomOptional<uint32_t> DLSSDRenderPresetUltraPerformance { 0 };

    // Nukems
    CustomOptional<bool> NvngxFGMakeDepthCopy { false };

    // Libraries
    CustomOptional<std::wstring, NoDefault> MainDllPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12Path;
    CustomOptional<std::wstring, NoDefault> FfxDx12SRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12FGPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RRPath;
    CustomOptional<std::wstring, NoDefault> FfxDx12RCPath;
    CustomOptional<std::wstring, NoDefault> FfxVkPath;
    CustomOptional<std::wstring, NoDefault> XeSSLibrary;
    CustomOptional<std::wstring, NoDefault> XeFGLibrary;
    CustomOptional<std::wstring, NoDefault> XeLLLibrary;
    CustomOptional<std::wstring, NoDefault> XeSSDx11Library;
    CustomOptional<std::wstring, NoDefault> NvngxPath;
    CustomOptional<std::wstring, NoDefault> NVNGX_DLSS_Library;
    CustomOptional<std::wstring, NoDefault> DLSSFeaturePath;
    CustomOptional<std::wstring, NoDefault> NvapiDllPath;

    // Sharpness
    CustomOptional<SharpenShader> SharpnessShader { SharpenShader::RCAS };
    CustomOptional<bool> OverrideSharpness { false };
    CustomOptional<float> Sharpness { 0.4f };

    // RCAS
    CustomOptional<bool> RcasEnabled { false };
    CustomOptional<bool> ContrastEnabled { false };
    CustomOptional<float> Contrast { -0.3f };

    // DA Sharpening
    CustomOptional<float, NoDefault> DADepthScale;
    CustomOptional<float, NoDefault> DADepthBias;
    CustomOptional<bool, NoDefault> DAClampOutput;

    // MAS
    CustomOptional<bool> MotionSharpnessEnabled { false };
    CustomOptional<bool> MotionSharpnessDebug { false };
    CustomOptional<float> MotionSharpness { 0.2f };
    CustomOptional<float> MotionThreshold { 0.0f };
    CustomOptional<float> MotionScaleLimit { 10.0f };

    // Magnifier
    CustomOptional<bool> MagnifierEnabled { false };
    CustomOptional<float> MagnifierSize { 15.f }; // % of screen Height
    CustomOptional<int> MagnifierZoomFactor { 4 };
    CustomOptional<float> MagnifierBorderSize { 0.3f };   // % of screen Height
    CustomOptional<float> MagnifierCursorOffsetX { 0.f }; // Pixels
    CustomOptional<float> MagnifierCursorOffsetY { 0.f }; // Pixels
    CustomOptional<float, NoDefault> MagnifierStaticPosX; // % of screen Width, static pos enabled if both are defined
    CustomOptional<float, NoDefault> MagnifierStaticPosY; // % of screen Height

    // Menu
    CustomOptional<float, NoDefault> MenuScale;
    CustomOptional<bool> OverlayMenu { true };
    // Let controller input continue to the game while the OptiScaler menu owns keyboard/mouse input.
    CustomOptional<bool> MenuGamepadPassthrough { false };
    CustomOptional<bool> ShortcutKeyRequireCtrl { false };
    CustomOptional<bool> ShortcutKeyRequireAlt { false };
    CustomOptional<int> ShortcutKey { VK_INSERT };
    CustomOptional<bool> ExtendedLimits { false };
    CustomOptional<bool> ShowFps { false };
    /// 0 Top Left, 1 Top Right, 2 Bottom Left, 3 Bottom Right
    CustomOptional<FpsOverlayPos> FpsOverlayPosition { FpsOverlayPos_TopLeft };
    /// 0 Only FPS, 1 +Avg FPS & Upscaler info 2 +Frame Time,
    /// 3 +Upscaler Time, 4 +Frame Time Graph, 5 +Upscaler Time Graph
    /// 6 +Reflex timings
    CustomOptional<FpsOverlay> FpsOverlayType { FpsOverlay_JustFPS };
    CustomOptional<int> FpsShortcutKey { VK_PRIOR };
    CustomOptional<int> FpsCycleShortcutKey { VK_NEXT };
    CustomOptional<bool> FpsOverlayHorizontal { false };
    CustomOptional<float> FpsOverlayAlpha { 0.4f };
    CustomOptional<float, NoDefault> FpsScale; // No value means same as MenuScale
    CustomOptional<bool> UseHQFont { true };
    CustomOptional<bool> DisableSplash { false };
    CustomOptional<float> FontSize { 14.0f };
    CustomOptional<std::wstring, NoDefault> TTFFontPath;
    CustomOptional<int> FGShortcutKey { VK_END };
    CustomOptional<bool> LightTheme { false };
    CustomOptional<bool> OverlaysUseTheme { false };
    CustomOptional<float> MenuAccentColorR { 0.00f };
    CustomOptional<float> MenuAccentColorG { 0.40f };
    CustomOptional<float> MenuAccentColorB { 0.77f };
    CustomOptional<float> MenuBGColorR { 0.0f };
    CustomOptional<float> MenuBGColorG { 0.0f };
    CustomOptional<float> MenuBGColorB { 0.0f };
    CustomOptional<float> MenuBGColorA { 0.99f };

    // Hooks
    CustomOptional<bool> HookOriginalNvngxOnly { false };
    CustomOptional<bool> EarlyHooking { false };
    CustomOptional<bool> UseNtdllHooks { true };

    // Upscale Ratio Override
    CustomOptional<bool> UpscaleRatioOverrideEnabled { false };
    CustomOptional<float> UpscaleRatioOverrideValue { 1.3f };

    // DRS
    CustomOptional<bool> DrsMinOverrideEnabled { false };
    CustomOptional<bool> DrsMaxOverrideEnabled { false };

    // Quality Overrides
    CustomOptional<bool> QualityRatioOverrideEnabled { false };
    CustomOptional<float> QualityRatio_DLAA { 1.0f };
    CustomOptional<float> QualityRatio_UltraQuality { 1.3f };
    CustomOptional<float> QualityRatio_Quality { 1.5f };
    CustomOptional<float> QualityRatio_Balanced { 1.7f };
    CustomOptional<float> QualityRatio_Performance { 2.0f };
    CustomOptional<float> QualityRatio_UltraPerformance { 3.0f };

    // ProcessFilter
    CustomOptional<std::wstring, NoDefault> TargetProcess;
    CustomOptional<std::wstring> ProcessExclusionList = {
        L"crashpad_handler.exe|crashreport.exe|crashreporter.exe|crs-handler.exe|crs-uploader.exe|crs-video.exe|"
        L"unitycrashhandler64.exe|idtechlauncher.exe|cefviewwing.exe|ace-setup64.exe|ace-service64.exe|"
        L"qtwebengineprocess.exe|platformprocess.exe|bugsplathd64.exe|bssndrpt64.exe|pspcsdkappmgr.exe|pspcsdkcore.exe|"
        L"pspcsdkstttts.exe|pspcsdktelemetry.exe|pspcsdkui.exe|pspcsdkupdatechecker.exe|pspcsdkvoicechat.exe|"
        L"pspcsdkwebview.exe|windhawk.exe|vscodium.exe|crash_reporter.exe|steamerrorreporter64.exe|crashreportclient."
        L"exe|edcefcrashpadprocess.exe|edcefrenderprocess.exe"
    };

    // Hotfixes
    CustomOptional<bool> CheckForUpdate { true };
    CustomOptional<bool, SoftDefault> DisableOverlays { false };

    CustomOptional<bool> SimulateWaitableObject { false };

    CustomOptional<float, NoDefault> MipmapBiasOverride; // disabled by default
    CustomOptional<bool> MipmapBiasFixedOverride { false };
    CustomOptional<bool> MipmapBiasScaleOverride { false };
    CustomOptional<bool> MipmapBiasOverrideAll { false };

    CustomOptional<int, NoDefault> AnisotropyOverride; // disabled by default
    CustomOptional<bool> OverrideShaderSampler { true };
    CustomOptional<bool> AnisotropyModifyComp { true };
    CustomOptional<bool> AnisotropyModifyMinMax { true };
    CustomOptional<bool> AnisotropySkipPointFilter { true };

    CustomOptional<int, NoDefault> RoundInternalResolution; // disabled by default

    CustomOptional<int, NoDefault> SkipFirstFrames; // disabled by default
    CustomOptional<bool> RestoreComputeSignature { false };
    CustomOptional<bool> RestoreGraphicSignature { false };
    CustomOptional<bool> ExtendedStateRestore { false };

    CustomOptional<bool> UsePrecompiledShaders { true };

    CustomOptional<bool> UseGenericAppIdWithDlss { false };
    CustomOptional<bool> PreferDedicatedGpu { true };
    CustomOptional<bool> PreferFirstDedicatedGpu { false };

    CustomOptional<int32_t, NoDefault> ColorResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> MVResourceBarrier;       // disabled by default
    CustomOptional<int32_t, NoDefault> DepthResourceBarrier;    // disabled by default
    CustomOptional<int32_t, NoDefault> ExposureResourceBarrier; // disabled by default
    CustomOptional<int32_t, NoDefault> MaskResourceBarrier;     // disabled by default
    CustomOptional<int32_t, NoDefault> OutputResourceBarrier;   // disabled by default

    CustomOptional<bool> CreateD3D12DeviceForLuma { false };

    // Upscalers
    CustomOptional<Upscaler, SoftDefault> Dx11Upscaler { Upscaler::FSR22 };
    CustomOptional<Upscaler, SoftDefault> Dx12Upscaler { Upscaler::XeSS };
    CustomOptional<Upscaler, SoftDefault> VulkanUpscaler { Upscaler::FSR22 };

    // Output Scaling
    CustomOptional<bool> OutputScalingEnabled { false };
    CustomOptional<float> OutputScalingMultiplier { 1.5f };
    CustomOptional<Scaler> OutputScalingDownscaler { Scaler::FSR1 };

    // FSR
    CustomOptional<bool> FsrDebugView { false };
    CustomOptional<int> FfxUpscalerIndex { 0 };
    CustomOptional<int> FfxFGIndex { 0 };
    CustomOptional<bool> FsrUseMaskForTransparency { true };
    CustomOptional<bool> FsrNonLinearColorSpace { false };
    CustomOptional<bool> FsrNonLinearSRGB { false };
    CustomOptional<bool> FsrNonLinearPQ { false };
    CustomOptional<bool> FsrAgilitySDKUpgrade { false };

    // These default values will be overwritten at upscaler init time with optimized values
    CustomOptional<float> FsrVelocity { 1.0f };
    CustomOptional<float> FsrReactiveScale { 1.0f };
    CustomOptional<float> FsrShadingScale { 1.0f };
    CustomOptional<float> FsrAccAddPerFrame { 0.333f };
    CustomOptional<float> FsrMinDisOccAcc { -0.333f };

    // FSR4
    CustomOptional<FSR4Support> Fsr4ForceModel { FSR4Support::None };
    CustomOptional<uint32_t, NoDefault> Fsr4Preset;
    CustomOptional<bool> Fsr4EnableWatermark { false };
    CustomOptional<bool> Fsr4DoNotLoadAmdxc64 { false };

    // FSR Common
    CustomOptional<float> FsrVerticalFov { 60.0f };
    CustomOptional<float> FsrHorizontalFov { 0.0f }; // off by default
    CustomOptional<float> FsrCameraNear { 0.1f };
    CustomOptional<float> FsrCameraFar { 100000.0f };
    CustomOptional<bool> FsrUseFsrInputValues { true };

    // dx11wdx12
    CustomOptional<bool> Dx11DelayedInit { false };
    CustomOptional<bool> DontUseNTShared { true };

    // vulkanwdx12
    CustomOptional<bool> VulkanUseCopyForInputs { false };
    CustomOptional<bool> VulkanUseCopyForOutput { false };

    // NVAPI Override
    CustomOptional<bool> DisableFlipMetering { false };

    // Spoofing
    CustomOptional<bool, SoftDefault> DxgiSpoofing { true };
    CustomOptional<bool> DxgiFactoryWrapping { false };
    CustomOptional<bool> StreamlineSpoofing { true };
    CustomOptional<std::string, NoDefault> DxgiBlacklist; // disabled by default
    CustomOptional<int, NoDefault> DxgiVRAM;              // disabled by default
    CustomOptional<bool> VulkanSpoofing { false };
    CustomOptional<bool> VulkanExtensionSpoofing { false };
    CustomOptional<int, NoDefault> VulkanVRAM; // disabled by default
    CustomOptional<bool> SpoofHAGS { false };
    CustomOptional<bool> SpoofFeatureLevel { false };
    CustomOptional<uint32_t> SpoofedVendorId { VendorId::Nvidia };
    CustomOptional<uint32_t> SpoofedDeviceId { 0x2684 };
    CustomOptional<uint32_t, NoDefault> TargetVendorId;
    CustomOptional<uint32_t, NoDefault> TargetDeviceId;
    CustomOptional<std::wstring> SpoofedGPUName { L"NVIDIA GeForce RTX 4090" };
    CustomOptional<bool> UESpoofIntelAtomics64 { false };
    CustomOptional<bool> SpoofRegistry { false };
    CustomOptional<bool> SpoofUser32 { false };
    CustomOptional<std::wstring> SpoofedDriver { L"32.0.15.9155" };

    // Plugins
    CustomOptional<std::wstring, NoDefault> PluginPath;
    CustomOptional<bool> LoadSpecialK { false };
    CustomOptional<bool> LoadReShade { false };
    CustomOptional<bool> LoadCustomAmdxc64OnRdna2 { false };
    CustomOptional<bool> LoadAsiPlugins { false };
    CustomOptional<int> LateAsiPluginsDelay { 30 };

    // Frame Generation
    CustomOptional<FGInput> FGInput { FGInput::NoFG };
    CustomOptional<FGOutput> FGOutput { FGOutput::NoFG };
    CustomOptional<FGNvngxReplacement> FGNvngxReplacement { FGNvngxReplacement::Nukems };
    CustomOptional<bool> FGDrawUIOverFG { false };
    CustomOptional<bool> FGUIPremultipliedAlpha { true };
    CustomOptional<bool> FGDisableHudless { false };
    CustomOptional<bool> FGDisableUI { false };
    CustomOptional<bool> FGSkipReset { false };
    CustomOptional<int> FGAllowedFrameAhead { 1 };
    CustomOptional<bool> FGDepthValidNow { false };
    CustomOptional<bool> FGVelocityValidNow { false };
    CustomOptional<bool> FGHudlessValidNow { false };
    CustomOptional<bool> FGOnlyAcceptFirstHudless { false };
    CustomOptional<bool> FGPreserveSwapChain { true };
    CustomOptional<bool> FGSkipResizeBuffers { false };
    CustomOptional<bool> FGModifyBufferState { false };
    CustomOptional<bool> FGModifySCIndex { false };
    CustomOptional<float> FGHudCutoff { 0.0f };
    CustomOptional<FrameTimeSource> FTInput { FrameTimeSource::Input };

    // OptiFG
    CustomOptional<bool> FGEnabled { false };
    CustomOptional<bool> FGUseMutexForSwapchain { true };
    CustomOptional<bool> FGMakeMVCopy { true };
    CustomOptional<bool> FGMakeDepthCopy { true };
    CustomOptional<bool> FGResourceFlip { false };
    CustomOptional<bool> FGResourceFlipOffset { false };
    CustomOptional<bool> FGAlwaysCaptureFSRFGSwapchain { false };

    CustomOptional<int, NoDefault> FGRectLeft;
    CustomOptional<int, NoDefault> FGRectTop;
    CustomOptional<int, NoDefault> FGRectWidth;
    CustomOptional<int, NoDefault> FGRectHeight;

    // OptiFG - Hudfix
    CustomOptional<bool> FGDisableHUDFix { false };
    CustomOptional<bool> FGHUDFix { false };
    CustomOptional<int> FGHUDLimit { 1 };
    CustomOptional<bool> FGHUDFixExtended { false };
    CustomOptional<bool> FGImmediateCapture { false };
    CustomOptional<bool> FGHudfixPersistentBindings { true };
    CustomOptional<bool> FGDontUseSwapchainBuffers { false };
    CustomOptional<bool> FGRelaxedResolutionCheck { false };
    CustomOptional<bool> FGHudfixDisableRTV { false };
    CustomOptional<bool> FGHudfixDisableSRV { false };
    CustomOptional<bool> FGHudfixDisableUAV { false };
    CustomOptional<bool> FGHudfixDisableOM { false };
    CustomOptional<bool> FGHudfixDisableDispatch { false };
    CustomOptional<bool> FGHudfixDisableDI { false };
    CustomOptional<bool> FGHudfixDisableDII { false };
    CustomOptional<bool> FGHudfixDisableSCR { true };
    CustomOptional<bool> FGHudfixDisableSGR { true };

    // OptiFG - Resource Tracking
    CustomOptional<bool> FGAlwaysTrackHeaps { false };
    CustomOptional<bool> FGResourceBlocking { false };
    CustomOptional<bool> FGUseShards { false };

    // OptiFG - DLSS-D Depth scale
    CustomOptional<bool> FGEnableDepthScale { false };
    CustomOptional<float> FGDepthScaleMax { 10000.0f };

    // FSR-FG
    CustomOptional<bool> FGDebugView { false };
    CustomOptional<bool> FGDebugResetLines { false };
    CustomOptional<bool> FGDebugTearLines { false };
    CustomOptional<bool> FGDebugPacingLines { false };
    CustomOptional<bool> FGAsync { false };
    CustomOptional<bool> FGFramePacingTuning { true };
    CustomOptional<float> FGFPTSafetyMarginInMs { 0.01f };
    CustomOptional<float> FGFPTVarianceFactor { 0.3f };
    CustomOptional<bool> FGFPTAllowHybridSpin { false };
    CustomOptional<int> FGFPTHybridSpinTime { 2 };
    CustomOptional<bool> FGFPTAllowWaitForSingleObjectOnFence { false };

    CustomOptional<bool> FSRFGSkipConfigForHudless { false };
    CustomOptional<bool> FSRFGSkipDispatchForHudless { false };
    CustomOptional<bool> FSRFGEnableWatermark { false };

    // XeFG
    CustomOptional<bool> FGXeFGIgnoreInitChecks { false };
    CustomOptional<int> FGXeFGInterpolationCount { 1 };
    CustomOptional<bool> FGXeFGUIComposition { false };
    CustomOptional<bool> FGXeFGDepthInverted { true };
    CustomOptional<bool> FGXeFGJitteredMV { false };
    CustomOptional<bool> FGXeFGHighResMV { false };
    CustomOptional<bool> FGXeFGDebugView { false };
    CustomOptional<bool> FGXeFGForceBorderless { false };

    // DLSSG
#if defined(OPTISCALER_RTX40_MFG)
    CustomOptional<bool> FGDLSSGAdaMfgUnlock { false };           // RTX 40 only; restart required
    CustomOptional<std::string, NoDefault> FGDLSSGAdaTemporalFix; // Auto / Retarget / Ptx
    CustomOptional<bool> FGDLSSGAdaFlipMeteringPatch { false };   // pin sl.dlss_g to software frame pacing
#endif
    CustomOptional<int> FGDLSSGInterpolationCount { 1 }; // For Opti's own SL instance
    CustomOptional<bool> FGDLSSGUseGamesReflexMarkers { true };
    CustomOptional<int, NoDefault>
        FGDLSSGOverrideInterpolationCount; // For overriding game's value sent to SL, could be Nvngx FG, could be noFG
                                           // but someone just uses real DLSSG
    CustomOptional<bool> FGDLSSGOverrideForceDMFG { false };   // Overrides game's DLSSG mode to Dynamic
    CustomOptional<bool> FGDLSSGForceDMFG { false };           // Overrides Opti's DLSSG mode to Dynamic
    CustomOptional<float> FGDLSSGFramerateTargetDMFG { 0.0f }; // 0.0 means auto-detects the display refresh rate

    // As per
    // https://github.com/artur-graniszewski/dlss-enabler-main/blob/a92464d468eb0d91ae17befa66c6bf6229f20b9f/Utils/DlssgProxy.cpp#L1033
    CustomOptional<uint32_t> NvngxFGDispatchFlags { 0x10000000 }; // IGNORE_UI_TEXTURE
    CustomOptional<bool> NvngxFGShowDebug { false };
    CustomOptional<bool> NvngxFGDisableHudless { false };

    // fakenvapi
    CustomOptional<bool> UseFakenvapi { true };
    CustomOptional<bool> ForceXeLL { false };
    CustomOptional<bool> FN_ForceLatencyFlex { false };
    CustomOptional<LFXMode> FN_LatencyFlexMode { LFXMode::Conservative };
    CustomOptional<ForceReflex> FN_ForceReflex { ForceReflex::InGame };
    CustomOptional<LowLatencyInput> LowLatencyInput { LowLatencyInput::Auto }; // TODO: no reading/saving to config
    CustomOptional<LowLatencyMode> LowLatencyOutput { LowLatencyMode::Auto };

    // Inputs
    CustomOptional<bool> EnableDlssInputs { true };
    CustomOptional<bool> EnableXeSSInputs { true };
    CustomOptional<bool> UseFsr2Inputs { true };
    CustomOptional<bool> UseFsr2Dx11Inputs { false };
    CustomOptional<bool> UseFsr2VulkanInputs { false };
    CustomOptional<bool> Fsr2Pattern { false };
    CustomOptional<bool> UseFsr3Inputs { true };
    CustomOptional<bool> Fsr3Pattern { false };
    CustomOptional<bool> UseFfxInputs { true };
    CustomOptional<bool> EnableHotSwapping { false };
    CustomOptional<bool> EnableFsr2Inputs { true };
    CustomOptional<bool> EnableFsr3Inputs { true };
    CustomOptional<bool> EnableFfxInputs { true };

    // Framerate
    CustomOptional<float> FramerateLimit { 0.0f };

    // HDR
    CustomOptional<bool> ForceHDR { false };
    CustomOptional<bool> UseHDR10 { false };
    CustomOptional<bool> SkipColorSpace { false };

    // V-Sync
    CustomOptional<bool> OverrideVsync { false };
    CustomOptional<bool, NoDefault> ForceVsync;
    CustomOptional<UINT> VsyncInterval { 0 };

    // Old configs for compat reasons
    CustomOptional<bool, NoDefault> _DONTUSE_Fsr4ForceEnableInt8;

    bool LoadFromPath(const wchar_t* InPath);
    bool SaveIni(std::filesystem::path destination = {});
    bool SaveProfile(const std::wstring& name);
    bool LoadProfile(const std::wstring& name);
    std::vector<std::string> ListProfiles();
    bool SaveXeFG();

    void CheckUpscalerFiles();

    std::vector<std::string> GetConfigLog();

    static Config* Instance();

  private:
    inline static Config* _config;
    inline static std::vector<std::string> _log;

    std::filesystem::path absoluteFileName;
    std::wstring fileName = L"OptiScaler.ini";

    bool Reload(std::filesystem::path iniPath);

    std::optional<std::string> readString(std::string section, std::string key, bool lowercase = false);
    std::optional<std::wstring> readWString(std::string section, std::string key, bool lowercase = false);
    std::optional<float> readFloat(std::string section, std::string key);
    std::optional<int> readInt(std::string section, std::string key);
    std::optional<uint32_t> readUInt(std::string section, std::string key);
    std::optional<bool> readBool(std::string section, std::string key);

    template <typename Enum> std::optional<Enum> readEnum(std::string section, std::string key);
};
