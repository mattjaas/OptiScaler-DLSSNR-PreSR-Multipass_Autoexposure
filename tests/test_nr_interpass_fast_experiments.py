"""Approximation bounds and low-filter frequency response, independent of GPU timings."""
import cmath
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def test_weights():
    # Hermite endpoints and derivatives constrain the fractional exponential.
    def approx(x):
        whole = int(x)
        f = x - whole
        return 2.0 ** (-whole) * (1 + f * (-0.69314718056 + f * (0.2328679514 - f * 0.03972077084)))
    values = [approx(i / 10000) for i in range(10001)]
    error = max(abs(v / 2.0 ** (-i/10000) - 1) for i, v in enumerate(values))
    assert error < 0.00062
    assert all(a >= b > 0 for a, b in zip(values, values[1:]))
    for exponent in (0, 1, 16, 64, 125):
        assert approx(exponent) == 2.0 ** (-exponent)
    print(f'Weight interpolation max relative error: {error*100:.6f}% (not an image error bound)')

def stopband_energy(weights, offsets, footprint):
    # Average sinusoidal response power over source-grid phases with actual bilinear taps.
    total = 0.0
    for fi in range(256):
        frequency = 0.5/footprint + (0.5-0.5/footprint)*(fi+0.5)/256
        omega = 2*math.pi*frequency
        for phase in range(32):
            h = 0j
            for weight, offset in zip(weights, offsets):
                p = offset + phase/32
                integer = math.floor(p)
                fraction = p-integer
                h += weight * cmath.exp(1j*omega*integer) * (1-fraction+fraction*cmath.exp(1j*omega))
            total += abs(h/sum(weights))**2
    return total/(256*32)

def test_low_filter():
    code = (ROOT/'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl').read_text()
    low = code[code.index('if (gMode == 34u)'):code.index('if (gMode == 29)')]
    weights = [float(v) for v in re.search(r'weights\[6\] = \{([^}]+)\}', low)[1].split(',')]
    assert weights == [11,15,17,17,15,11] and sum(weights) == 86 and min(weights) > 0
    step = float(re.search(r'tapStep = ([\d.]+) \* footprint', low)[1])
    assert step == 0.25  # Keep the corrected filter's spacing rather than introduce a new comb.
    assert abs(2.5*step-0.625) < 1e-12  # Positive overlapping 1.25-pixel support.
    for footprint in (8.0, 1274/160, 1404/176):
        box = stopband_energy([1]*8, [i-3.5 for i in range(8)], footprint)
        current = stopband_energy([7,11,15,17,17,15,11,7], [(i-3.5)*.25*footprint for i in range(8)], footprint)
        candidate = stopband_energy(weights, [(i-2.5)*step*footprint for i in range(6)], footprint)
        print(f'Footprint {footprint:.6f}: phase-averaged 1D stopband power box={box:.8f}, current64={current:.8f}, low36={candidate:.8f}')
        assert 0 < candidate < 1  # Diagnostic only: no unsupported claim of superiority over the box.

def test_scope():
    run = (ROOT/'OptiScaler/shaders/dlssnr/DlssNr_Dx12_Run.cpp').read_text()
    assert run.count('DlssNrMode_InterPassLow36') == 1
    interpass = run[run.index('const auto BuildInterPassInput'):run.index('const auto BuildInterPassInput')+6000]
    assert 'Path::FastLow36' in interpass and 'DlssNrMode_InterPassLow36' in interpass
    assert 'low.Transfer = 0u' in interpass
    hlsl = (ROOT/'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl').read_text()
    assert 'if (gMode == 18)' in hlsl and 'weights[8]' in hlsl

if __name__ == '__main__':
    test_weights()
    test_low_filter()
    test_scope()
    print('Fast experiment arithmetic/scope PASS; visual quality and performance need game validation')
