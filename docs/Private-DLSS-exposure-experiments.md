# Private DLSS exposure and NR edit transport

Experimental branch based on janblade `release/0.1.35`, `789f882072254776c80c0ba2a02433ca73c22c6d`, fetched on 2026-10-27. The existing local v13 checkout was left intact. These controls apply to **Superior Scaling's private DLAA**, on D3D12 and D3D12 bridges. They do not change the game's DLSS configuration, NR proxy exposure, ordinary NVIDIA residual mode, or native Vulkan. Superior remains disabled for the upstream multi-view route; its new history belongs to its private temporal generation, not to a game-wide static latch.

## Controls

Select **Superior Scaling** under **Upscale Mode**. Below **DLSS to temporally stabilize NR**:

- **Private DLSS on full NR answer**: unchecked by default. When checked, private DLSS filters the complete NR response at working resolution; its output is then compared with the current immutable proxy to extract the edit. With transport controls at identity, the sanitized NR answer goes directly to DLSS, subject only to nonnegative FP16 range limits. Available on the same D3D12 Superior route as the other controls.
- **Positive NR edit transport multiplier**: logarithmic slider 0.01..64. **Exact transport multiplier** accepts 0.01..1024. Default 1. Values below 1 attenuate the positive edit before DLSS and divide it back afterwards; 0 is disallowed because the inverse would be undefined.
- **NR edit transport saturation**: logarithmic slider 1..16. **Exact edit saturation** accepts 1..64. Default 1 disables the transform.
- **Depth-weighted positive edit multiplier**, **Add near-camera brightness before DLSS** and **Original depth and MV for private DLSS (experimental)**: see [depth brightness controls, exact formulas, capture format and native-guide limits](Private-DLSS-depth-brightness.md). The brightness modes combine with the controls below; raw guides remain an explicit compatibility experiment.
- **Depth colour transport**: off by default. **Near / far depth colours** selects Red / Cyan, Green / Magenta or Blue / Yellow. **Depth colour strength** spans 0..4; **Exact depth colour strength** accepts 0..16. **Depth colour curve** spans 0.03125..64, default 1. Depth controls are disabled when the checkbox is off.
- **Disable DLSS IsHDR flag**: the existing independent IsHDR experiment.
- **Disable private DLSS AutoExposure**: unchecked by default. When checked, clears only the private feature's AutoExposure creation bit, independently of IsHDR.
- **Private DLSS exposure method**: **Off (constant 1.0)**, **Manual**, **Legacy CNN E/F**, **Legacy Transformer K**, **Transformer 2 L/M**. Every method is selectable with either private model preference; there is no preset-dependent gate.
- **Private DLSS manual exposure**: logarithmic slider 0.001..64; **Exact private exposure** accepts 0.000001..65504. This is an exposure factor, not EV. Enabled only for Manual with private AutoExposure disabled.

Controls are disabled on native Vulkan or when Superior reports itself unavailable. The source selector is disabled while native private AutoExposure is enabled. An inactive/non-temporal Superior route does not dispatch these experiments.

The AutoExposure checkbox tooltip explicitly says: “Some L/M runtimes may ignore external exposure even with this flag disabled.” The installed NGX model can ignore the external texture. This branch exposes the experiment; it does not patch NVIDIA's private kernels or promise that selecting an external method overrides L/M's own metering. The fallback from an unavailable private preset to the runtime default still applies.

All transport and exposure settings participate in INI read/write, NR profile save/load/reset and the profile key filter:

```ini
[DlssNr]
SuperiorPositiveEditGain=1.0
SuperiorEditSaturation=1.0
SuperiorFilterNrAnswer=false
SuperiorDepthColour=false
SuperiorDepthColourStrength=1.0
SuperiorDepthColourPair=0
SuperiorDepthColourCurve=1.0
SuperiorScalingDisableAutoExposure=false
; 1=unit, 2=manual, 3=Legacy CNN, 4=Legacy K, 5=Transformer 2
SuperiorPrivateExposureMode=1
SuperiorPrivateExposureValue=1.0
```

## Positive transport

With full-answer mode off, saturation at 1 and depth colour disabled, for each RGB channel, with `d = NR - proxy` and multiplier `m`:

```text
carrier = clamp(0.5 + min(d, 0) + m * max(d, 0), 0, 65504)
filteredEdit = privateDLSS(carrier) - 0.5
decodedEdit = min(filteredEdit, 0) + max(filteredEdit, 0) / m
```

The neutral offset is added after subtraction and multiplication: unchanged input stays exactly 0.5 for every multiplier. Negative changes are independent of the multiplier. The old -1..2 clip is removed from both live preparation routes (separate carrier+motion and fused guides+carrier). The remaining limits are zero, because DLSS cannot represent negative carrier values, and 65504, the maximum finite value of the existing FP16 carrier. Darkening beyond -0.5 cannot be transported through a nonnegative carrier. As requested, positive gain is undone after DLSS; temporal filtering can still change signs and detail. Large gains may change ghosting and stability; no in-game improvement is claimed.

The frozen v59 source/blobs remain an independent reference, and their hashes remain unchanged. Production carrier modes 37/38/39 select `private_carrier`; the fused route uses the same `PositiveCarrier.hlsli` equations. No extra image pass or gain texture is added for multiplication. Changing the gain resets private history and invalidates measured exposure history without releasing in-flight resources.

## One-shot pre-DLSS edit file

Enter a filename in **Pre-DLSS edit filename**, then click **Save pre-DLSS edit**. This queues one snapshot at the next Superior private DLSS evaluation. The menu displays waiting, success (including the full path), cancellation or failure. The button is disabled while a capture is pending/in flight. Capture requires the active temporal Superior path; native Vulkan and unavailable Superior remain disabled by the existing menu gate.

The outputs are `<OptiScaler.dll folder>/dlssnr-edit/<name>.dds` and `<name>.json` settings. The `.dds` extension is added if omitted. Plain ASCII names (letters/digits/spaces/dash/underscore/dot, subject to the existing profile-name length rule) are accepted; paths, traversal, Windows reserved characters and device names are rejected. Existing files are never overwritten: `CREATE_NEW` fails and the UI asks for another name. The last typed name is kept in the running menu only, not in NR look profiles or INI.

The file contains the **signed edit after the positive multiplier**, before saturation, depth colouring, carrier offset/clipping and private DLSS:

```text
edit = sanitizedNRanswer - sanitizedCurrentProxy
savedRGB = min(edit,0) + positiveMultiplier * max(edit,0)
```

A zero edit is saved as **0**, not the carrier encoding's 0.5. Negative values and values above 1 are preserved; alpha is 1. This deliberate diagnostic is the same in edit and full-NR-answer modes. It is not a screenshot of the game, the final coloured DLSS input, or a post-DLSS edit. With screen-edge compression, it is the packed working raster actually used by this private path.

DDS uses a DX10 header with `DXGI_FORMAT_R32G32B32A32_FLOAT`, one uncompressed top-down 2D image, no mipmaps or gamma conversion. Row padding from D3D12 readback is removed; FP32 pixels are written without quantization, tonemapping or clamping. A plain PNG would lose signed/HDR values and is not used.

Mode 43 of the existing private-carrier PSO computes the requested snapshot in a temporary FP32 texture, then copies it to a readback buffer. This adds **one dispatch and one copy only when the button is pressed**. No new PSO, image allocation, copy, GPU wait or per-frame diagnostic is introduced while idle; only the request/pending checks execute on the CPU. One capture at a time, with a 512 MiB bound on the snapshot/readback pixel storage. File writing can briefly stall a frame when the one-shot result is consumed.

After the game actually submits the snapshot-producing list, its submission hook queues one capture-owned copy list on that same DIRECT/COMPUTE queue and signals a capture-specific fence. Queue order ensures the image has been produced before this private copy. The readback is written only by this one-shot private list, never by the replayable game recording. The original game list need not be Reset or destroyed for the file to become ready. This fixes the wait shown as “Snapshot recorded; waiting for command recording completion and GPU fences.”: resource retirement and readback readiness are different events. Merely relaxing the old retirement check would allow replay to overwrite a buffer while the CPU maps it, so the readback copy is moved out of the game recording instead.

`DlssNr_EditDumpReadback.h` owns the capture allocator/list, fence, queue and source/destination until completion. `CollectEditDump` maps/writes after the actual private fence completes, from the NR owner or the capture's menu status/busy calls; no frame-count guess, blocking GPU wait or command-list Reset is required. The game's original snapshot texture is separately retained by the existing Superior GPU lifetime tracker until replay is impossible, across DRS, settings changes and shutdown. The cumulative 512 MiB diagnostic budget includes retained snapshot textures and the new snapshot/readback, preventing repeated captures from growing memory indefinitely while an engine keeps recordings replayable. The completion hook does not map or write files.

A reset/destruction without an observed submission cancels the capture rather than writing uninitialized data. Command-list identities are normalized for submission matching. A lock-free identity marker prevents reuse of a list pointer being mistaken for the old recording; the destruction hook never takes the NR owner lock. Setup failures report failure; a Signal failure after copy execution retains ownership (and reports it) because completion cannot be proved. Removed devices fail the capture. A queued request is cancelled when Superior is deselected/unavailable or NR/NGX shuts down. File writing still requires an owner/menu tick, but no longer waits for closure of the game's recording. The status now distinguishes waiting for game submission from waiting for the capture fence.

### Lower positive multipliers

The host and shader both accept finite 0.01..1024 transport gains; nonfinite values fall back to 1. The CPU clamps typed/INI values to range. Negative edits remain unscaled. Encoding and decoding use the same effective gain in both input modes and both preparation paths, with a history reset on changes. As gain falls below 1, the inverse amplifies FP16 storage error and DLSS changes as well as the signal. Small edits can disappear from the carrier's representable range; this is an experimental tradeoff, not a claim of improved stability.

## DLSS on the full NR answer

`SuperiorFilterNrAnswer=true` switches the private DLSS input from the encoded edit to NR's complete working-resolution response. Without optional transport modifications:

```text
input = clamp(NRanswer, 0, 65504)
filteredAnswer = privateDLSS(input)
edit = filteredAnswer - currentProxy
```

There is no artificial neutral 0.5 offset in this mode. Where NR made no edit, DLSS sees the proxy picture itself. Subtraction happens point-for-point on the same working/packed raster after DLSS, before the existing residual enlargement, band shaping and final composition. It uses the same immutable proxy that the original residual route compares against, not an unfiltered NR answer, a reconstructed native proxy or a previous frame. The default multiplier/saturation identity path feeds sanitized NR directly without a subtract/add round trip.

All transport options remain usable independently and together. With `d=NRanswer-proxy` and `e=min(d,0)+multiplier*max(d,0)`:

```text
base = clamp(proxy + S(e,saturation), 0, 65504)
input = min(base + depthColour, 65504)
filteredAnswer = privateDLSS(input)
restored = S(filteredAnswer - depthColour - proxy, 1/saturation)
edit = min(restored,0) + max(restored,0)/multiplier
```

The exposure providers always measure the final input actually submitted to private DLSS, so they automatically meter the full answer in this mode. Preset, IsHDR and AutoExposure choices remain independent. Final composition still receives a signed edit from the decode dispatch. With an identity filter and no clipping, zero edit reconstructs zero within storage precision; real DLSS may also change proxy-picture detail and introduce a difference where NR made none. This is the intended full-picture experiment, not a promise that its edit matches the residual-filtered route.

Changing the switch resets private temporal history and invalidates external exposure history, without changing creation flags or destroying/recreating the NGX feature. Its state is kept in the existing retired generation and cleared on teardown. Both separate and fused preparation implement the same input mode. No texture allocation, copy or extra dispatch is introduced; full-answer decode additionally reads the already existing proxy SRV. The switch is read/saved in INI, included in the NR profile field/reset logic and classified in the kept-key list.

The normal carrier cbuffer's otherwise unused `DirectDetailMode` selects the input mode. In `PrivateGuidesMain` only, the otherwise unused warp colour-filter selector (`gOptions.y`) carries it. Named setters and ABI offset assertions pin both usages; general joint/spatial guide entry points keep their original meaning.

## Independent saturation and depth colour experiments

The three transport controls can run individually or in any combination in either input mode. With full-answer mode off, positive gain 1, saturation 1 and depth colour off preserve the preceding branch's carrier output bit for bit. Colour controls do not gate the exposure methods or private preset choice.

Saturation acts on the signed transported edit, not on the original game image. For `e = min(d,0) + m*max(d,0)`, define `Y(e) = .25*e.r + .5*e.g + .25*e.b` and `S(e,s) = Y(e) + s*(e-Y(e))`. Its inverse is `S(e,1/s)`. This keeps grey edits unchanged and increases chroma without changing the weighted signed luminance. A zero edit remains zero under this transform.

Depth colour is added to the entire carrier, including zero-edit pixels, as explicitly requested. It uses the exact current private DLSS depth raster, point sampled at the carrier coordinate, including the matched or packed-guide path. Invalid depth maps to the far plane; reversed Z is normalized so the near endpoint always gets the near colour. Let `z = saturate(depth)` for normal Z and `z = 1-saturate(depth)` for reversed Z. Then `t = smoothstep(0,1,pow(z,curve))`, `colour = strength * lerp(nearRGB, 1-nearRGB, t)`. Palettes are saturated red/cyan, green/magenta and blue/yellow. This is normalized **device depth**, not linear eye-space distance in metres: no camera projection/near/far values are invented. The curve redistributes the non-linear range. The same current depth, curve, strength and palette produce the subtracted colour after DLSS.

The complete order is:

```text
e = min(NR-proxy,0) + multiplier*max(NR-proxy,0)
base = clamp(0.5 + S(e,saturation), 0, 65504)
input = min(base + depthColour, 65504)
filtered = privateDLSS(input)
restored = S(filtered - depthColour - 0.5, 1/saturation)
decodedEdit = min(restored,0) + max(restored,0)/multiplier
```

Depth colour is therefore removed before desaturation, then the positive multiplier is undone. With an identity filter, no clipping and exact arithmetic, a zero edit returns to carrier-neutral 0.5 after removing the colour transforms and to signed edit 0 after final decode. The code does not overwrite regions with a neutral mask: the actual DLSS result is retained. Temporal reconstruction can alter the artificial colour field, especially at moving depth boundaries/disocclusions, so exact colour cancellation after proprietary DLSS is not guaranteed. Clipped channels cannot be recovered. FP16 stores introduce a further error (roughly one half interval at the carrier magnitude); large additive strength can conceal a small original edit. These options are experiments, not established ghosting fixes.

Both separate carrier+motion and fused guides+carrier paths share the transform functions. Saturation and depth colouring add no dispatch, texture allocation, extra colour copy or CPU/GPU synchronization. The separate path reads the existing private depth once on encode and once on decode only while depth colouring is active; the fused preparation reuses its already reconstructed depth. Exposure metering still reads the final coloured carrier supplied to DLSS. Changing an active transport parameter resets private history and measured exposure history to avoid mixing incompatible encodings; it does not recreate the NGX instance. All state belongs to the existing GPU-retired generation. The existing multi-view exclusion is retained.

Named constant setters and offset assertions document the reused carrier fields and private-guide padding; the frozen 256-byte ABI is unchanged. The fused private entry point uses its existing padding for MV conversion, so its otherwise unused motionScale pair carries the depth curve/palette. General joint-guide entry points keep their original constants.

## Exposure implementation

The implementation follows the supplied specification and the referenced [d4r exposure kernel](https://github.com/countervolts/d4r/blob/linux/engine/exposure_k.comp). Numerical agreement with every NGX/DLL version is not established.

The meter reads **the finished FP16 carrier immediately before private DLSS evaluation**, after the multiplier and nonnegative storage limits. Its input rectangle is the full working carrier at origin (0,0), which is exactly the rectangle passed to private DLAA. It never meters the game image, an earlier proxy or the upscaled output. The shader also supports independent origins; numeric tests exercise them. Packed/compressed routes meter the packed carrier actually passed to DLSS.

Private DLAA input and output sizes are both `workWidth × workHeight`; each sampling axis is therefore floor(work/2). The common policy retains K's output/2 rule and the CNN/T2 Ultra Performance fallback independently per axis, tested with render/output ratios above two. The private carrier is already binary16, so K does not need another canonical input conversion. Private PreExposure and ExposureScale remain 1: this difference carrier has no scene pre-exposure to undo. The Legacy shader normalizes by PreExposure before taking the logarithm; tests vary it. T2 ignores it in the measurement.

Two optional dispatches implement measurement: 32×32 sample blocks, 64 threads per group, sixteen samples per thread in the reference's 4×4 assignment; sequential summation of 64 partials, then the reference's strided final reduction and sequential summation. Sampling is endpoint-aligned nearest neighbor. RGB weights are 0.25/0.5/0.25; Legacy uses nested multiply-add and the report's exact LN2/negative-LOG2E bit constants. T2 retains its arithmetic expression and clamp [0,10000]. The final exposure is explicitly truncated to binary16, then stored as FP32 in a 1×1 R32G32 texture; the first channel is supplied to NGX as ExposureTexture. Invalid Legacy luminance contributes zero with the full denominator retained. T2 treats NaN as zero; infinities follow the clamp. Invalid/nonpositive quantized exposure becomes 1.

Legacy CNN and K keep independent history and measure on evaluations 0,2,4,... and on reset. On skipped measurements a single reduce dispatch retains current and sets previous=current. T2 measures every evaluation and stores previous=0 on reset. Each method owns two ping-ponged current/previous surfaces to avoid SRV/UAV aliasing. The shader exports separate `CalculateLegacyDLSSAutoExposure` and `CalculateTransformer2DLSSAutoExposure` functions. There is no histogram, percentile meter or EMA.

The external NGX API receives current exposure; NVIDIA owns reconstruction's internal history/ratio. The stored previous value verifies the requested algorithm but cannot directly overwrite internal K/M parameter blocks. Accordingly this is a report-based exposure provider, not a replacement implementation of the proprietary reconstruction stages or a claim of bitwise NGX equivalence.

Native/unit add no per-frame exposure dispatches. Manual updates the existing 1×1 texture only when the value changes. Custom resources are lazy and reused, with no per-frame allocation, readback, CPU/GPU wait or telemetry. Switching the exposure source or either creation flag retires/recreates the private generation and its NGX history using the upstream GPU lifetime tracker and submission gate. Shutdown drops all state. Resource/PSO failures preserve the game frame instead of silently substituting a different measurement algorithm.

## Validation

Local RTX 4090 and WARP numeric tests: 1402 cases against an independent CPU reference (double natural-log/exp arithmetic, binary search over binary16 values and an independent RGB matrix), including the report's uniform values, black, very low light, >10000 HDR, 90/10 dark/bright mixtures, negative/NaN/Inf samples, independent origins, grids smaller/larger than input, changing PreExposure, dark/bright transitions, odd-evaluation reset, held exposure, FP16 interval boundaries, manual entry, positive-only carrier inversion, signed saturation and all combinations of the three transport controls. Depth tests cover all three palettes, normal/reversed Z, endpoints, curves, invalid depth, D16/D24-quantized values and FP32/real FP16 carrier storage. Exposure assertions allow one half interval for FP32 reduction/transcendental differences; dedicated truncation cases require exact values. FP32 colour round trips use a strict numeric bound; FP16 round trips allow one half interval at carrier magnitude plus FP32 arithmetic error, accounting for truncating typed UAV stores.

`private_guides_quality`: 768 cases, 258755520 compared words, separate vs fused preparation at gains 1/16/1024, saturation 1/4/16, depth colour off/on with all palettes and curves 1/8; independent origins, odd/partial groups, reversed/quantized depth, invalid colour/guides and Hold. Both input modes are covered. This is route parity, supplemented by the independent carrier/exposure numeric test. A separate 48-case comparison against the published `95f2e7c5` private carrier confirms bitwise FP16/FP32 output preservation with saturation 1 and depth colour off. The upstream Superior policy/guide/surface tests and `nr_profiles_smoke OptiScaler/Config.cpp` pass. All 54 Superior blobs rebuild; the frozen v59 identity/constant ABI and embedded-bytecode checks pass.

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


### Colour transport stage timing

RTX 4090, resident procedural FP16 carrier, positive multiplier 16 in both A and B. A uses the published `95f2e7c5` private carrier bytecode; B uses the new colour controls. Same 64-iteration separate warmups, four blocks of sixteen ABBA quartets and 128 GPU timestamp windows per variant as above. Depth uses a procedural R32 guide with curve 1 and red/cyan palette. These rows measure encode/decode with native private exposure (no external meter); NGX/NR and final composition are excluded. Allocation, upload and fences are outside windows.

| Carrier size | Saturation / depth strength | A median ms (P10–P90) | B median ms (P10–P90) | B−A ms |
|---|---|---|---|---|
| 1920×1080 | 1 / 0 | 0.034816 (0.034816–0.035840) | 0.034816 (0.034816–0.035840) | +0.000000 |
| 3840×2160 | 1 / 0 | 0.183808 (0.181248–0.204800) | 0.188416 (0.184320–0.207872) | +0.004608 |
| 1920×1080 | 4 / 0 | 0.031744 (0.031744–0.032768) | 0.031744 (0.031744–0.032768) | +0.000000 |
| 3840×2160 | 4 / 0 | 0.181248 (0.179200–0.202752) | 0.185344 (0.182272–0.204800) | +0.004096 |
| 1920×1080 | 1 / 1 | 0.031744 (0.031744–0.032768) | 0.032768 (0.031744–0.032768) | +0.001024 |
| 3840×2160 | 1 / 1 | 0.183296 (0.179200–0.203776) | 0.267776 (0.261120–0.296960) | +0.084480 |
| 1920×1080 | 4 / 1 | 0.031744 (0.031744–0.032768) | 0.032768 (0.031744–0.032768) | +0.001024 |
| 3840×2160 | 4 / 1 | 0.181248 (0.174080–0.199680) | 0.262144 (0.253952–0.289792) | +0.080896 |

Raw windows, including comparisons with the same Legacy/T2 metering enabled on both A and B: [private-colour-transport-timing.csv](validation/private-colour-transport-timing.csv). Small differences overlap clock/measurement spread; this does not establish an in-game quality or performance improvement. No additional dispatch is introduced by either colour option.


Full-answer numeric coverage adds 192 cases: varying current proxies, direct NR bypass, neutral/nonneutral edits, clipping/HDR, invalid RGB, all palettes and reversed Z, multiplier/saturation/depth combinations, real FP16 and FP32 storage, and a synthetic post-DLSS picture change. Decode is checked against an independent inverse matrix applied to the filtered answer minus the current proxy. Quantized neutral errors are bounded by one FP16 interval at carrier magnitude. The 48-case disabled-mode comparison also passes against published `0cc84770` bytecode. Proprietary DLSS temporal behaviour still requires in-game validation.

### Full NR answer stage timing

RTX 4090, same procedural resident FP16 fixture, separate 64-iteration warmups and 4 blocks of 16 ABBA quartets (128 samples per variant). A is published `0cc84770` edit transport; B is the full-answer path. Both use the same positive multiplier, saturation, depth colouring and exposure selection. The table shows native private exposure (no external meter); raw windows also include equal Legacy/T2 metering on both variants. Timed scope is carrier encode/decode plus transitions, without NGX, NR or final composition. Input/output carrier sizes match; depth is a procedural R32 guide. No upload/allocation/fence is inside a sample.

| Carrier size | Multiplier / saturation / depth strength | A edit median ms (P10–P90) | B full answer median ms (P10–P90) | B−A ms |
|---|---|---|---|---|
| 1920×1080 | 1 / 1 / 0 | 0.034816 (0.034816–0.035840) | 0.034816 (0.034816–0.035840) | +0.000000 |
| 3840×2160 | 1 / 1 / 0 | 0.167936 (0.159744–0.176128) | 0.174080 (0.164864–0.201728) | +0.006144 |
| 1920×1080 | 16 / 1 / 0 | 0.031744 (0.031744–0.032768) | 0.032768 (0.031744–0.032768) | +0.001024 |
| 3840×2160 | 16 / 1 / 0 | 0.185344 (0.175104–0.203776) | 0.209920 (0.197632–0.235520) | +0.024576 |
| 1920×1080 | 16 / 4 / 1 | 0.032768 (0.031744–0.032768) | 0.032768 (0.031744–0.032768) | +0.000000 |
| 3840×2160 | 16 / 4 / 1 | 0.256000 (0.243712–0.283648) | 0.275456 (0.263168–0.302080) | +0.019456 |

Raw windows: [private-nr-answer-timing.csv](validation/private-nr-answer-timing.csv). These synthetic stage measurements do not predict the full DLSS/NR GPU cost or image quality in games; no winner is claimed.


### Edit-dump and lower-gain validation

The 1402-case GPU suite passes on RTX 4090 and WARP, including gains 0.01/0.125/0.5 in the transport, signed-colour and full-answer cases. Fourteen additional mode-43 cases compare signed FP32 snapshots against an independent scalar reference at low/identity/high/invalid gains, in both input modes with saturation/depth flags deliberately set (they must not affect the saved edit). The GPU readback is serialized through the production DDS writer and the payload compared byte for byte. `nr_edit_dump_smoke` checks an independently indexed DDS/DX10 header, signed/HDR payloads, padded odd-width rows, write failure and filename rejection. The 768 fused-guide cases include low gains; normal gains preserve the preceding shader bit for bit in the 48-case comparison. Profile, shader/ABI and policy tests also pass. These fixtures validate shader/math/serialization; actual menu-triggered capture and private DLSS image quality still require game testing.

### Idle capture stage timing

RTX 4090, resident procedural FP16 carrier, capture idle. A uses published `86aaa412` private-carrier bytecode and B uses this revision; gain 16 in edit mode, gain 1 in full-answer mode, saturation 1/depth colour off on both. Same separate 64-iteration warmups, four blocks of sixteen ABBA quartets and 128 GPU windows per variant. Table scope is encode/decode and transitions, no NGX/NR/final composition, upload, allocation or fence in the windows. Equal Legacy/T2 metering variants are included in the raw data. This measures the effect on the ordinary path; the one-shot capture dispatch/copy/disk latency is not included or claimed to be measured.

| Carrier size | Input mode | A median ms (P10–P90) | B median ms (P10–P90) | B−A ms |
|---|---|---|---|---|
| 1920×1080 | Edit | 0.034816 (0.034816–0.035840) | 0.034816 (0.034816–0.035840) | +0.000000 |
| 3840×2160 | Edit | 0.191488 (0.185344–0.199680) | 0.191488 (0.186368–0.202752) | +0.000000 |
| 1920×1080 | Full NR answer | 0.032768 (0.031744–0.046080) | 0.032768 (0.031744–0.047104) | +0.000000 |
| 3840×2160 | Full NR answer | 0.163840 (0.160768–0.185344) | 0.167936 (0.164864–0.182272) | +0.004096 |

Raw GPU windows: [private-edit-dump-idle-timing.csv](validation/private-edit-dump-idle-timing.csv). No in-game performance or ghosting improvement is inferred.


### Capture completion regression

The production `ReadbackCopy` is exercised on RTX 4090 and WARP, on both DIRECT and COMPUTE queues. The test submits a snapshot, leaves the original game list alive and unreset, waits for the private copy fence and validates signed/HDR pixels plus DDS row packing. It then replays the original recording with changed input and confirms the first readback remains immutable and `Submit` cannot queue a second copy. Missing-queue setup is rejected, and an unsubmitted capture cannot become ready. This reproduces the previously untested readiness/lifetime distinction; shader arithmetic and rendering PSOs are unchanged by the fix. A full game/menu confirmation is still required.

### Matched object-guide diagnostics

`Save edit + object guides` next to `Save pre-DLSS edit` writes the same raw signed edit and `.json` settings plus `.depth.dds` (R32_FLOAT) and `.motion.dds` (R32G32_FLOAT) from that capture. Depth/MV are the actual private DLSS inputs, cropped to its active grid, origin zero; vectors use private-input pixels with scale [1,1]. Capture after the picture has settled. The raw edit remains before colour transforms/clipping and uses zero for neutral. This diagnostic does not adjust the image or settings and adds no INI fields.

All output names are reserved with CREATE_NEW before writing; a collision/error removes only files created by that capture. Snapshot states now form a COPY_SOURCE → producer → COPY_SOURCE cycle, allowing game-list replay without an invalid initial-state assumption. Snapshot retention is established before recording, including a partially failed dispatch. A single private copy submission/fence reads the raw edit and optional two guides; the original replayable game list cannot modify these readbacks. The existing cumulative 512 MiB capture budget includes retained snapshots and the current readbacks. No additional GPU work/resources are created while capture is idle. Production shaders/blobs are unchanged.

Metadata is frozen at command recording, uses locale-independent finite JSON numbers (nonfinite values become null), and includes gain, exposure controls and reset/hold flags. NR trim controls are not a readback of actual GPU white point; the missing GPU value is explicitly marked. [Object contrast study, capture procedure, validation and scoped GPU copy measurements](NR-object-contrast-study.md).


### Experimental multiplier preview

**Preview suggested positive multiplier** displays an R/G-coverage estimate with adaptive EMA; it never changes the transport multiplier. Default Off. **Require changing motion field** rejects frozen nonzero MV with a tolerance and minimum changed-sample count. Needs residual edit mode, saturation 1x and depth colour off. The saved controls, calibration, motion gate, asynchronous readback, limitations and scoped GPU measurements are documented in [NR-edit-gain-preview.md](NR-edit-gain-preview.md). This is an experimental suggestion, not a validated ghosting detector.


### Capture the shown suggestion and the actual gain

Both **Save pre-DLSS edit** and **Save edit + object guides** now write JSON schema 2. `positive_gain` remains the actual gain used for the captured edit. `positive_gain_at_request` records the requested slider value; `preview_request_suggested_gain` records the same status snapshot the menu displayed at the click, without polling a newer asynchronous value. `preview_capture_suggested_gain` is the latest completed preview when the next NR evaluation records the capture. These may differ, so both include valid/paused flags, raw estimate, measurement age/count and motion percentages; invalid estimates/ages serialize as null. Request delay, private preset at request/capture, preview settings and numeric calibration target are also included. `guides_included` distinguishes the two buttons. No new image dispatch/copy is added by these metadata.

To report "shown 3x, 2.5x already good", send the 2.5x DDS and matching JSON with that quality label. With depth-weighted gain off, positive samples can be divided by `positive_gain` to recover the unscaled input edit (within FP32 rounding), while negative samples stay unchanged. The saved edit is still after the actual multiplier and before DLSS/colour/clipping; it is not a picture of ghosting itself. Quality labels remain the user's observation. A matched view/motion and a settled private DLSS history after changing gain make paired reports more useful. JSON is frozen when recording the capture; it does not claim the older asynchronous estimate was computed from that exact frame.

Schema 1 guide bundles remain readable. Every schema 2 capture reserves the DDS and JSON together using CREATE_NEW, including plain edit captures. A collision or write failure removes only the files created by that attempt. Existing files remain untouched. The serialization test checks distinct actual/request/capture values, frozen snapshots, age, disabled state, locale independence and null for nonfinite values, with a separate JSON parse.


### Optional automatic positive gain

**Automatically apply suggested multiplier** enables the separate CPU actuator of the existing smoothed R/G meter, even without **Preview suggested positive multiplier**. Default `SuperiorAutoPositiveEditGain=false`; complete INI/profile round-trip and kept-key coverage. **Applied positive multiplier** shows the effective value. Auto has a hard **1x minimum** in all its paths, including warmup/fallback; manual values below 1x remain available when Auto is off. It waits for four usable moving-frame measurements, holds on pause, and uses a 0.5 s minimum change interval with 2% up / factor-1.05 down hysteresis. Manual preference is preserved and restored when Auto is disabled. Its ordinary slider is disabled while supported Auto is on. Unsupported colour/full-answer, depth-brightness and original-guide modes can turn Auto off but cannot newly enable it.

Every actual applied gain change still resets private DLSS and exposure history, as a manual change does; sampling noise alone does not reset them. One gain is shared by encode, decode and edit snapshot for a frame. Old-context estimates cannot be applied before a newly recorded measurement resets the meter. The main view owns the controller; secondary views use manual with a 1x floor while Auto is on. NR/NGX shutdown resets the controller. JSON records actual/manual/effective-request gain separately and whether automatic control was active for the captured edit. [Detailed behaviour, UI labels and validation](NR-edit-gain-preview.md#automatyczne-stosowanie-mnożnika).

No shader/blob changes or additional GPU passes relative to preview metering. Local validation in this iteration uses CPU only; CI runs the existing WARP regression. No new in-game GPU timing or temporal quality result is claimed for automatic regulation or its history resets.

### Depth brightness and original-guide extension

[Depth brightness and original-guide controls](Private-DLSS-depth-brightness.md) add complete INI/profile settings, separate gain/bias toggles, shared relative range/distribution and experimental NGX original input pointers. Preview/Auto pauses for these modes. JSON schema 2 gains optional spatial-transport fields, a depth-only companion when weighting is active, and the actual NGX guide geometry/scales; a spatial capture must not be undone with one global division. Guide bundles are disabled while original guides is selected; edit-only remains available.

### Depth-boundary multiplier measurement

**Positive multiplier measurement** adds a separate **Depth-boundary contrast (experimental)** method shared by preview and Auto. **Positive R/G coverage** remains the default with identical shader bytes. [Controls, target, EMA, metadata and validation](NR-edit-depth-boundary-runtime.md). The global positive gain still has the same encode/decode/capture contract; no game brightness/exposure control is adjusted.
