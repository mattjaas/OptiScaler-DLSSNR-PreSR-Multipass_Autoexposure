// Rescue only an existing timer on a smoothly moving, locally planar surface.
// Comparing two NDC frames directly can mistake forward motion over the road for a new closer object.
// Never seeds or renews a window, and never reads previous edits/changes NGX history.
bool ContinueGhostSurface(float4 current, float4 old, float2 chosen, float tolerance, float precision, float gate)
{
    float2 endPosition = chosen + current.zw;
    float2 midPosition = chosen + current.zw * .5;
    int2 endPixel = int2(round(endPosition)), midPixel = int2(round(midPosition));
    if (any(endPixel < 0) || any(midPixel < 0) || endPixel.x >= int(fullWidth) || endPixel.y >= int(fullHeight) ||
        midPixel.x >= int(fullWidth) || midPixel.y >= int(fullHeight))
        return false;
    float endZ = Depth.Load(int3(endPixel, 0)), midZ = Depth.Load(int3(midPixel, 0));
    if (!isfinite(endZ) || !isfinite(midZ) || endZ < 0 || endZ > 1 || midZ < 0 || midZ > 1)
        return false;
    endZ = inverted != 0 ? endZ : 1 - endZ;
    midZ = inverted != 0 ? midZ : 1 - midZ;
    if (endZ <= 0 || midZ <= 0)
        return false;
    // The reprojected point must still belong to the old surface at its screen position.
    if (abs(endZ - old.x) > max(endZ, old.x) * tolerance + precision)
        return false;
    // An intervening silhouette/depth step is not a continuous road. Near depth is affine on a plane.
    if (abs(midZ - (current.x + endZ) * .5) > max(current.x, endZ) * min(tolerance * .25, .0125) + precision)
        return false;
    float2 endMv = Motion.Load(int3(endPixel, 0)), midMv = Motion.Load(int3(midPixel, 0));
    if (!all(isfinite(endMv)) || !all(isfinite(midMv)) || any(abs(endMv) >= 65504) || any(abs(midMv) >= 65504))
        return false;
    float2 curvature = midMv - (current.zw + endMv) * .5;
    return dot(curvature, curvature) <= gate * gate * 4;
}
