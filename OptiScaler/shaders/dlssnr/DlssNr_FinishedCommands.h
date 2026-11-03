#pragma once
#include <d3d12.h>
#include <wrl/client.h>

namespace DlssNr
{
// Only for our own finished-picture lists, which Cancel promises never to replay.
// Destroying a completed list closes its recording in GpuLifetime's destruction watch.
// Game-owned lists must stay tracked until the game actually resets or destroys them.
inline bool ReleaseFinishedPictureCommands(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commands,
                                           ID3D12Fence* fence, UINT64 done)
{
    if (!commands || !fence)
        return false;
    const auto completed = fence->GetCompletedValue();
    if (completed == UINT64_MAX || completed < done)
        return false;
    commands.Reset();
    return true;
}
} // namespace DlssNr
