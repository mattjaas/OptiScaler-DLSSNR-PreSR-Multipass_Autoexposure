# Archived shaders for the P65 regression benchmark

These are the actual DXIL blobs shipped in the v14 package, sourced from
`c88b3c565a272c06cab3abef0a02ef2047d5fcc4`, release
`nr-interpass-v14-rgb-linear20-20261010`. Their presence in that release DLL
was checked before adding them. They are benchmark controls, never production
Optimized choices. Do not recompile them from the current HLSL.

| File | Bytes | SHA256 |
|---|---:|---|
| standard.cso | 318704 | 5599ee0b01fc50b9bea4c2d02c8bfc1ce24287e9867dbc9fa0158db883bab9fc |
| rgb16.cso | 47580 | e0b673de1c16be5c1100ccdbac751f601bc8a2d275a70596c9101164f7cbbd3f |

The adjacent headers embed exactly these bytes. The test
`tests/test_nr_interpass_revision.py` checks the hashes and header equality.
Existing production shaders continue to be compiled by `compile_nr_interpass.ps1`.

The v23 screenshot profile additionally embeds `guide16.cso` and `guide16.h`:
SHA256 `f55dadbcf9b48e11d7e623843e28606f6bd0d37ae136d6ceb87084eed8f48c3a`.
Exact bytes were also verified in the v14 release DLL. This is **RGBA 16x16**,
isolated weights, compile-time Mode28 and exact guide-one eligibility, RGB OFF.
It is not the RGB16 historical control. V23 swaps only this inter-pass PSO;
the standard shader remains current in both ABBA routes. The old standard
and RGB16 blobs are used only by the separate v22 revision profile.
The archived standard shader has the same constant buffer and descriptor layout.
The RGB16 shader uses compile-time Mode28 / isolated weights / RGB tile / pitch16,
the historical winner, not wide tiles or pair loads.
