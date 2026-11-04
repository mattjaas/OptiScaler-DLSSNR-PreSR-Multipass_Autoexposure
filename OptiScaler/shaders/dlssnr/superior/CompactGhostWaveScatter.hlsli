// Merge the right splat of a lane with the left splat of its next active lane,
// only when the actual target coordinates coincide. No lane-to-pixel ordering is assumed.
// Max of positive finite depth is associative and exact; all other splats remain independent.
void ScatterCompactWave(uint2 chosen, float nearZ, float2 mv, bool valid)
{
    if (initialized == 0)
        return;
    float2 safeMotion = valid ? mv : float2(0, 0);
    float2 previousPos = (float2(chosen) + .5 + safeMotion) * .5 - .5;
    int2 base = int2(floor(previousPos));
    float2 f = frac(previousPos);
    uint coverage = 0;
    [unroll] for (uint y = 0; y < 2; ++y)
    {
        [unroll] for (uint x = 0; x < 2; ++x)
        {
            if (valid && (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) > .05)
                coverage |= 1u << (y * 2 + x);
        }
    }
    const uint lane = WaveGetLaneIndex(), count = WaveGetLaneCount();
    uint4 active = WaveActiveBallot(true);
    uint prevLane = lane > 0 ? lane - 1 : lane, nextLane = min(lane + 1, count - 1);
    const bool prevActive = lane > 0 && (active[prevLane / 32] & (1u << (prevLane % 32))) != 0;
    const bool nextActive = lane + 1 < count && (active[nextLane / 32] & (1u << (nextLane % 32))) != 0;
    // Never read an inactive lane, even when its value would subsequently be ignored.
    prevLane = prevActive ? prevLane : lane;
    nextLane = nextActive ? nextLane : lane;
    int2 prevBase = WaveReadLaneAt(base, prevLane), nextBase = WaveReadLaneAt(base, nextLane);
    uint prevCoverage = WaveReadLaneAt(coverage, prevLane), nextCoverage = WaveReadLaneAt(coverage, nextLane);
    float prevDepth = WaveReadLaneAt(nearZ, prevLane);
    bool leftConnected = prevActive && coverage != 0 && prevCoverage != 0 && all(prevBase == base - int2(1, 0));
    bool rightConnected = nextActive && coverage != 0 && nextCoverage != 0 && all(nextBase == base + int2(1, 0));
    [unroll] for (uint y = 0; y < 2; ++y)
    {
        int2 q = base + int2(0, y);
        bool own = (coverage & (1u << (y * 2))) != 0;
        bool delegated = leftConnected && (prevCoverage & (1u << (y * 2 + 1))) != 0;
        if (Inside(q) && (own || delegated))
            InterlockedMax(WriteDepth[q], asuint(max(own ? nearZ : 0, delegated ? prevDepth : 0)));
        q = base + int2(1, y);
        if (Inside(q) && (coverage & (1u << (y * 2 + 1))) != 0 && !rightConnected)
            InterlockedMax(WriteDepth[q], asuint(nearZ));
    }
}
