# Private DLSS exposure and positive NR edit transport

Experimental branch based on janblade `release/0.1.35`, `789f882072254776c80c0ba2a02433ca73c22c6d`, fetched on 2026-10-27. The existing local v13 checkout was left intact. These controls apply to **Superior Scaling's private DLAA**, on D3D12 and D3D12 bridges. They do not change the game's DLSS configuration, NR proxy exposure, ordinary NVIDIA residual mode, or native Vulkan. Superior remains disabled for the upstream multi-view route; its new history belongs to its private temporal generation, not to a game-wide static latch.

## Controls

Select **Superior Scaling** under **Upscale Mode**. Below **DLSS to temporally stabilize NR**:

- **Positive NR edit transport multiplier**: logarithmic slider 1..64. **Exact transport multiplier** accepts 1..1024. Default 1.
- **Disable DLSS IsHDR flag**: the existing independent IsHDR experiment.
- **Disable private DLSS AutoExposure**: unchecked by default. When checked, clears only the private feature's AutoExposure creation bit, independently of IsHDR.
- **Private DLSS exposure method**: **Off (constant 1.0)**, **Manual**, **Legacy CNN E/F**, **Legacy Transformer K**, **Transformer 2 L/M**. Every method is selectable with either private model preference; there is no preset-dependent gate.
- **Private DLSS manual exposure**: logarithmic slider 0.001..64; **Exact private exposure** accepts 0.000001..65504. This is an exposure factor, not EV. Enabled only for Manual with private AutoExposure disabled.

Controls are disabled on native Vulkan or when Superior reports itself unavailable. The source selector is disabled while native private AutoExposure is enabled. An inactive/non-temporal Superior route does not dispatch these experiments.

The AutoExposure checkbox tooltip explicitly says: “Some L/M runtimes may ignore external exposure even with this flag disabled.” The installed NGX model can ignore the external texture. This branch exposes the experiment; it does not patch NVIDIA's private kernels or promise that selecting an external method overrides L/M's own metering. The fallback from an unavailable private preset to the runtime default still applies.

All four new settings participate in INI read/write, NR profile save/load/reset and the profile key filter:

```ini
[DlssNr]
SuperiorPositiveEditGain=1.0
SuperiorScalingDisableAutoExposure=false
; 1=unit, 2=manual, 3=Legacy CNN, 4=Legacy K, 5=Transformer 2
SuperiorPrivateExposureMode=1
SuperiorPrivateExposureValue=1.0
```

## Positive transport

For each RGB channel, with `d = NR - proxy` and multiplier `m`:

```text
carrier = clamp(0.5 + min(d, 0) + m * max(d, 0), 0, 65504)
filteredEdit = privateDLSS(carrier) - 0.5
decodedEdit = min(filteredEdit, 0) + max(filteredEdit, 0) / m
```

The neutral offset is added after subtraction and multiplication: unchanged input stays exactly 0.5 for every multiplier. Negative changes are independent of the multiplier. The old -1..2 clip is removed from both live preparation routes (separate carrier+motion and fused guides+carrier). The remaining limits are zero, because DLSS cannot represent negative carrier values, and 65504, the maximum finite value of the existing FP16 carrier. Darkening beyond -0.5 cannot be transported through a nonnegative carrier. As requested, positive gain is undone after DLSS; temporal filtering can still change signs and detail. Large gains may change ghosting and stability; no in-game improvement is claimed.

The frozen v59 source/blobs remain an independent reference, and their hashes remain unchanged. Production carrier modes 37/38/39 select `private_carrier`; the fused route uses the same `PositiveCarrier.hlsli` equations. No extra image pass or gain texture is added for multiplication. Changing the gain resets private history and invalidates measured exposure history without releasing in-flight resources.

## Exposure implementation

The implementation follows the supplied specification and the referenced [d4r exposure kernel](https://github.com/countervolts/d4r/blob/linux/engine/exposure_k.comp). Numerical agreement with every NGX/DLL version is not established.

The meter reads **the finished FP16 carrier immediately before private DLSS evaluation**, after the multiplier and nonnegative storage limits. Its input rectangle is the full working carrier at origin (0,0), which is exactly the rectangle passed to private DLAA. It never meters the game image, an earlier proxy or the upscaled output. The shader also supports independent origins; numeric tests exercise them. Packed/compressed routes meter the packed carrier actually passed to DLSS.

Private DLAA input and output sizes are both `workWidth × workHeight`; each sampling axis is therefore floor(work/2). The common policy retains K's output/2 rule and the CNN/T2 Ultra Performance fallback independently per axis, tested with render/output ratios above two. The private carrier is already binary16, so K does not need another canonical input conversion. Private PreExposure and ExposureScale remain 1: this difference carrier has no scene pre-exposure to undo. The Legacy shader normalizes by PreExposure before taking the logarithm; tests vary it. T2 ignores it in the measurement.

Two optional dispatches implement measurement: 32×32 sample blocks, 64 threads per group, sixteen samples per thread in the reference's 4×4 assignment; sequential summation of 64 partials, then the reference's strided final reduction and sequential summation. Sampling is endpoint-aligned nearest neighbor. RGB weights are 0.25/0.5/0.25; Legacy uses nested multiply-add and the report's exact LN2/negative-LOG2E bit constants. T2 retains its arithmetic expression and clamp [0,10000]. The final exposure is explicitly truncated to binary16, then stored as FP32 in a 1×1 R32G32 texture; the first channel is supplied to NGX as ExposureTexture. Invalid Legacy luminance contributes zero with the full denominator retained. T2 treats NaN as zero; infinities follow the clamp. Invalid/nonpositive quantized exposure becomes 1.

Legacy CNN and K keep independent history and measure on evaluations 0,2,4,... and on reset. On skipped measurements a single reduce dispatch retains current and sets previous=current. T2 measures every evaluation and stores previous=0 on reset. Each method owns two ping-ponged current/previous surfaces to avoid SRV/UAV aliasing. The shader exports separate `CalculateLegacyDLSSAutoExposure` and `CalculateTransformer2DLSSAutoExposure` functions. There is no histogram, percentile meter or EMA.

The external NGX API receives current exposure; NVIDIA owns reconstruction's internal history/ratio. The stored previous value verifies the requested algorithm but cannot directly overwrite internal K/M parameter blocks. Accordingly this is a report-based exposure provider, not a replacement implementation of the proprietary reconstruction stages or a claim of bitwise NGX equivalence.

Native/unit add no per-frame exposure dispatches. Manual updates the existing 1×1 texture only when the value changes. Custom resources are lazy and reused, with no per-frame allocation, readback, CPU/GPU wait or telemetry. Switching the exposure source or either creation flag retires/recreates the private generation and its NGX history using the upstream GPU lifetime tracker and submission gate. Shutdown drops all state. Resource/PSO failures preserve the game frame instead of silently substituting a different measurement algorithm.

## Validation

Local RTX 4090 and WARP numeric tests: 97 cases against an independent CPU reference (double natural-log/exp arithmetic and binary search over binary16 values), including the report's uniform values, black, very low light, >10000 HDR, 90/10 dark/bright mixtures, negative/NaN/Inf samples, independent origins, grids smaller/larger than input, changing PreExposure, dark/bright transitions, odd-evaluation reset, held exposure, FP16 interval boundaries, manual entry and positive-only carrier inversion. Assertions allow one half interval for FP32 reduction/transcendental differences; the dedicated truncation cases require exact values.

`private_guides_quality`: 384 cases, 129377760 compared words, separate vs fused preparation at gains 1/16/1024; independent origins, odd/partial groups, reversed/quantized depth, invalid colour/guides and Hold. This is route parity, supplemented by the independent carrier/exposure numeric test. The upstream Superior policy/guide/surface tests and `nr_profiles_smoke OptiScaler/Config.cpp` pass. All 54 Superior blobs rebuild; the frozen v59 identity/constant ABI and embedded-bytecode checks pass.

Reproduce:

```powershell
./tools/compile_nr_interpass.ps1 -Check
python tests/test_nr_superior_scaling.py
./tools/test_superior_port.ps1 -GpuPrivateGuides
# Compile with cl /std:c++17 /EHsc /O2, link d3d12.lib dxgi.lib:
./artifacts/private_exposure_quality.exe
./artifacts/private_exposure_quality.exe --warp
./artifacts/private_exposure_quality.exe --timing
```

### Synthetic GPU timing (not a game benchmark)

RTX 4090, procedural stationary carrier field, FP16 input/output. A is upstream carrier encode+decode, B is experimental gain=16 encode+decode with the selected optional exposure measurement. NGX/NR, final composition and unrelated renderer work are excluded. Separate 64-iteration warmups; four blocks of sixteen ABBA quartets; 128 samples per variant, D3D12 GPU timestamps. No allocations/uploads/fences inside measurement windows. Legacy rows measure update evaluations; skipped-measurement frames are cheaper and are not represented by these rows.

| Carrier size | B exposure | A median ms (P10–P90) | B median ms (P10–P90) | B−A ms |
|---|---|---|---|---|
| 1920×1080 | Native (no extra meter) | 0.034816 (0.034816–0.035840) | 0.034816 (0.034816–0.035840) | +0.000000 |
| 1920×1080 | Legacy CNN | 0.034816 (0.034816–0.034816) | 0.044032 (0.044032–0.045056) | +0.009216 |
| 1920×1080 | Transformer 2 | 0.034816 (0.034816–0.035840) | 0.044032 (0.044032–0.045056) | +0.009216 |
| 3840×2160 | Native (no extra meter) | 0.172032 (0.164864–0.179200) | 0.182272 (0.173056–0.192512) | +0.010240 |
| 3840×2160 | Legacy CNN | 0.166912 (0.158720–0.182272) | 0.202752 (0.196608–0.215040) | +0.035840 |
| 3840×2160 | Transformer 2 | 0.167936 (0.158720–0.183296) | 0.202240 (0.195584–0.215040) | +0.034304 |

Raw windows: [private-exposure-timing.csv](validation/private-exposure-timing.csv). These are measurements of changed stages, not estimates of full-frame performance. Full NR/private-DLSS GPU times and temporal quality still require game testing: fixed scene/camera, GPU, render/output sizes, NR/private presets, HDR/IsHDR flags, model scale, gain and exposure method recorded; separate warmup then ABBA, recording medians and spread in ms. Include neutral areas, darkening, bright edits, motion, disocclusion, DRS and history resets. Compare native AutoExposure, constant 1, manual and each measured method with IsHDR both on and off. Do not infer an in-game winner from the synthetic timing.
