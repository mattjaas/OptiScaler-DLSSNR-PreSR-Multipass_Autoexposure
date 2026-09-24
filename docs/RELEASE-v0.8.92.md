# v0.8.92 (fork) - matched guides below 100% Model resolution

This is a fork release by [jlrouzies-fr](https://github.com/jlrouzies-fr/OptiScaler-DLSSNR-PreSR-Multipass), built from v0.8.91 plus one fix. It is offered to upstream as a pull request.

## The problem

Every Model resolution below 100% flickered and kept "settling" for several frames after the camera stopped, while 100% was steady. The same model at 100% of a frame the game had already shrunk (DLSS5-Feeder's own work resolution: same pixel count, guides made at that size) was steady too, which put the difference inside this fork's reduced path.

Below 100% the model was given a colour at the working size and depth and motion vectors at the frame's size, with only the vector magnitudes rescaled (`DLSSNR.Width` = working size, `DLSSNR.DepthSubrectWidth` / `MVecSubrectWidth` = frame size). Nothing documents that the model resamples a guide larger than its colour, and the picture says it does not: each pixel's depth and motion belonged to a different place in the frame, the history never lined up, and the model re-decided every frame.

## The fix

Depth and motion are resampled to the working size with the point resample the DLSS-enlargement path already builds for its private upscaler (`DlssNrMode_ResizePrivateGuides`), and handed to the model as a full zero-origin region. The vectors keep the game's units; the working-size scale still applies. Two work-size scratch textures, parked with the other per-size resources. Peripheral spatial compression is untouched: it packs its own guides already.

`[DlssNr] MatchGuides=true` (default) switches it; `false` restores the previous contract for an A/B on the same build. `OptiScaler.log` prints once which guides the model got:

```
DLSS-NR guides matched to the working size: depth and motion 2150x1210 for a 2150x1210 model (the frame's guides are 3072x1728)
```

## Verified

- Fable Anniversary (DirectX 9 through dgVoodoo2, 4K, DLSS5-Feeder 32-bit helper, RTX 5090, driver 617.14): 70% Model resolution is as steady as 100% with `MatchGuides=true`, and flickers with `false`, on the same build.
- The helper's 300-evaluate self-test at 640x360 with `WorkingScale=0.7`: 300/300 both ways, no measurable cost.

Not run: native 64-bit games, Vulkan (the fix is DirectX 12 only; the Vulkan path has the same shape and is untouched), RTX40 MFG.

## Package

Same layout as v0.8.91 with `OptiScaler.dll` replaced and this file added. NVIDIA model/FG runtime DLLs are not bundled. Source: branch `fix/reduced-scale-guides-0.8.91` on the fork.
