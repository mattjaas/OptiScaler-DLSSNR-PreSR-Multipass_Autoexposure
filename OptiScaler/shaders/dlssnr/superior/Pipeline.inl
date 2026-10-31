// Included by DlssNr_Dx12.cpp after janblade's model/resource helpers.
// Model creation, pass profiles, native/bridge entry points and the UI remain janblade's.
// Only Superior's scaling, guides, inter-pass and stabilization live here. Shader ABI/bytecode: v59.
namespace
{
namespace SuperiorRun
{
using Constants = DlssNr::Superior::DlssNrConstants;
namespace Fields = DlssNr::Superior::Fields;
using Microsoft::WRL::ComPtr;
constexpr auto Uav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr auto Srv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// Frames to wait for the private DLSS creation's submission before giving up on it for this session.
constexpr unsigned kCreationWaitFrames = 240;
// The destruction watch must never take NR's owner lock on a driver/engine destruction thread.
// This identity alone lets it close a capture without dereferencing state or retaining a list.
std::atomic<const void*> g_editDumpCommands { nullptr };
void EditDumpListDestroyed(const void* identity)
{
    auto expected = identity;
    g_editDumpCommands.compare_exchange_strong(expected, nullptr);
}

struct Texture
{
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = Uav;
    ID3D12Resource* Get() const { return resource.Get(); }
    void To(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES next)
    {
        if (resource && state != next)
            Barrier(cmd, resource.Get(), state, next);
        state = next;
    }
};

// Whether a handle made under NGX runtime `epoch` still exists. NGX shutdown destroys every feature and parameter
// block itself, so after it (or after a restart) the handle is dropped rather than released or evaluated.
bool NgxAlive(uint64_t epoch) { return NVNGXProxy::IsDx12Inited() && NVNGXProxy::Dx12Epoch() == epoch; }

struct Generation
{
    ComPtr<ID3D12Device> device;
    unsigned width = 0, height = 0, workWidth = 0, workHeight = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool inverted = false, beforeSr = false, reset = true;
    bool temporal = false, packed = false;
    bool disableIsHDR = false; // creation identity, independent of the game's HDR and AutoExposure
    uint32_t exposureMode = DlssNr::PrivateExposure::Native;
    float positiveGain = 1.f, editSaturation = 1.f, manualExposure = 1.f;
    float depthStrength = 0.f, depthCurve = 1.f;
    uint32_t depthPair = 0;
    bool unitInitialized = false;
    bool filterNrAnswer = false;
    struct ExposureHistory
    {
        Texture state[2]; // independent current/previous pair per algorithm, ping-ponged without SRV/UAV aliasing
        uint64_t evaluations = 0;
        unsigned index = 0;
        bool initialized = false;
    };
    ExposureHistory exposure[3];
    Texture exposureSums;
    bool guidesMatched = false;
    DlssNr::Spatial::Layout layout {};
    Texture warp, warpCache;
    ComPtr<ID3D12Resource> warpUpload;
    unsigned preset = 1;
    // Creation is gated like the NR feature's: the first evaluate waits for a later submission epoch.
    const void* creation = nullptr; // identity only, to notice a creation recording thrown away by Reset
    bool creationSubmitted = false;
    unsigned long long createEpoch = 0;
    unsigned waitedFrames = 0;
    uint64_t ngxEpoch = 0;
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    Texture depth, motion, interLow, interWorking, finalLow;
    Texture carrier, temporalOutput, temporalMotion, unit;
    uint32_t expectedMask = 0, successfulMask = 0;
    unsigned previousPasses = 0, lastCompleted = 0, lastCovered = 0;
    bool haveSchedule = false;
    // Forget the NGX handles without releasing them: NGX shut down (or restarted) and destroyed them already.
    void DropNgx()
    {
        feature = nullptr;
        parameters = nullptr;
    }
    ~Generation()
    {
        if (!NgxAlive(ngxEpoch))
            return;
        if (feature && NVNGXProxy::D3D12_ReleaseFeature())
            NVNGXProxy::D3D12_ReleaseFeature()(feature);
        if (parameters && NVNGXProxy::D3D12_DestroyParameters())
            NVNGXProxy::D3D12_DestroyParameters()(parameters);
    }
};

// Superior's identities that the shared 64-entry watched table could not take (DlssNr_WatchedLists.h). Counted in
// g_overflow, so the hooks still take the lock for them, and dropped when the list is reset or destroyed.
struct Overflow
{
    std::mutex mutex;
    std::vector<const void*> lists;
};
Overflow& GetOverflow()
{
    static auto* value = new Overflow;
    return *value;
}
void Watch(const void* identity)
{
    if (identity == nullptr)
        return;
    auto& overflow = GetOverflow();
    std::lock_guard lock(overflow.mutex);
    if (std::find(overflow.lists.begin(), overflow.lists.end(), identity) != overflow.lists.end())
        return;
    if (DlssNrWatchedLists::TryAdd(identity))
        return;
    overflow.lists.push_back(identity);
    ++DlssNrWatchedLists::g_overflow;
}
void Forget(const void* identity)
{
    auto& overflow = GetOverflow();
    std::lock_guard lock(overflow.mutex);
    if (const auto it = std::find(overflow.lists.begin(), overflow.lists.end(), identity); it != overflow.lists.end())
    {
        overflow.lists.erase(it);
        --DlssNrWatchedLists::g_overflow;
    }
}
// A recorded list was destroyed (GpuLifetime's watch, on the destroying thread): it can never be submitted again.
void Destroyed(const void* identity)
{
    DlssNrWatchedLists::Remove(1, &identity);
    Forget(identity);
    EditDumpListDestroyed(identity);
}

struct EditDumpGuide
{
    Texture image;
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 bytes = 0, allocationBytes = 0;
    DlssNr::EditDump::Format format = DlssNr::EditDump::Format::R32Float;
    const char* extension = nullptr;
};
struct EditDumpJob
{
    ComPtr<ID3D12Device> device;
    Texture image;
    ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 bytes = 0;
    unsigned width = 0, height = 0;
    const void* commands = nullptr; // identity only, never dereferenced
    DlssNr::EditDump::ReadbackCopy copy;
    std::filesystem::path path;
    std::array<EditDumpGuide, 2> guides;
    unsigned guideCount = 0;
    std::string metadata;
};

struct State
{
    DlssNr::GpuLifetime lifetime { &Destroyed };
    DlssNr::GainPreview::Runtime preview;
    std::shared_ptr<Generation> current;
    bool wasSelected = false, wasActive = false;
    std::string status;
    std::string unavailable;   // why Superior is not running this session; empty while it runs
    bool srUnavailable = false; // the NGX DLSS SR runtime itself failed: the menu greys Superior out
    std::string pendingEditDump, editDumpStatus;
    bool pendingEditDumpGuides = false, pendingEditDumpPreviewEnabled = false;
    DlssNr::EditDump::PreviewSnapshot pendingEditDumpPreview;
    uint32_t pendingEditDumpPrivatePreset = 0;
    float pendingEditDumpGain = 1;
    double pendingEditDumpRequestedAt = 0;
    std::shared_ptr<EditDumpJob> editDump;
    std::shared_ptr<std::atomic_uint64_t> editDumpRetainedBytes = std::make_shared<std::atomic_uint64_t>(0);
};
// Same process-local ownership as janblade's g_nr. Keep unresolved GPU ownership at teardown.
State& GetState()
{
    static auto* value = new State;
    return *value;
}
// File readiness belongs to the capture-owned copy fence, not the lifetime of replayable game lists.
// Map/write on an owner/menu call only; original snapshot ownership is retired independently.
void CollectEditDump()
{
    auto& s = GetState();
    if (!s.editDump) return;
    const auto state = s.editDump->copy.Poll();
    using CopyState = DlssNr::EditDump::ReadbackCopy::State;
    if (state == CopyState::NotSubmitted && g_editDumpCommands == s.editDump->commands) return;
    if (state == CopyState::Pending)
    {
        if (s.editDump->copy.SignalFailed())
            s.editDumpStatus = "Edit not saved: capture fence signal failed; GPU resources retained for safety.";
        return;
    }
    auto job = std::move(s.editDump);
    auto identity = job->commands;
    g_editDumpCommands.compare_exchange_strong(identity, nullptr);
    if (state != CopyState::Complete || FAILED(job->device->GetDeviceRemovedReason()))
    {
        s.editDumpStatus = "Edit not saved: snapshot was discarded, copy submission failed, or the device was removed.";
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(job->path.parent_path(), ec);
    std::vector<std::filesystem::path> paths { job->path };
    for (unsigned i = 0; i < job->guideCount; ++i)
        paths.push_back(std::filesystem::path(job->path).replace_extension(job->guides[i].extension));
    if (!job->metadata.empty()) paths.push_back(std::filesystem::path(job->path).replace_extension(".json"));
    std::vector<HANDLE> files;
    files.reserve(paths.size());
    bool written = !ec;
    // Reserve every output first: no overwrite, and no partial bundle when a companion already exists.
    for (const auto& path : paths)
    {
        if (!written) break;
        const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        written = file != INVALID_HANDLE_VALUE;
        if (written) files.push_back(file);
    }
    auto writeBytes = [](HANDLE file, const void* data, size_t bytes)
    {
        DWORD count = 0;
        return bytes <= MAXDWORD && WriteFile(file, data, DWORD(bytes), &count, nullptr) && count == bytes;
    };
    auto writeImage = [&](HANDLE file, ID3D12Resource* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp,
                          UINT64 bytes, DlssNr::EditDump::Format format)
    {
        void* mapped = nullptr;
        const D3D12_RANGE range { 0, size_t(bytes) };
        if (FAILED(readback->Map(0, &range, &mapped)) || !mapped) return false;
        const bool ok = DlssNr::EditDump::WriteRows(job->width, job->height,
            static_cast<const char*>(mapped) + fp.Offset, fp.Footprint.RowPitch,
            [&](const void* data, size_t count) { return writeBytes(file, data, count); }, format);
        const D3D12_RANGE none { 0, 0 };
        readback->Unmap(0, &none);
        return ok;
    };
    if (written)
        written = writeImage(files[0], job->readback.Get(), job->footprint, job->bytes,
                             DlssNr::EditDump::Format::Rgba32Float);
    for (unsigned i = 0; written && i < job->guideCount; ++i)
    {
        const auto& guide = job->guides[i];
        written = writeImage(files[i + 1], guide.readback.Get(), guide.footprint, guide.bytes, guide.format);
    }
    if (written && !job->metadata.empty())
        written = writeBytes(files.back(), job->metadata.data(), job->metadata.size());
    for (size_t i = 0; i < files.size(); ++i)
    {
        CloseHandle(files[i]);
        if (!written) DeleteFileW(paths[i].c_str()); // only outputs reserved by this capture
    }
    const auto utf8 = job->path.u8string();
    s.editDumpStatus = written ? (job->guideCount ? "Saved edit + depth + motion + settings: " : "Saved edit + settings: ") +
                                   std::string(utf8.begin(), utf8.end())
                               : "Edit not saved: an output exists, or GPU readback/file writing failed. Choose another name if needed.";
}
// Set by the first Superior frame. Until then the NGX shutdown path does not touch the NR lock at all.
std::atomic_bool g_used = false;

void Say(const char* text)
{
    auto& s = GetState();
    if (s.status == text)
        return;
    s.status = text;
    if (*text)
        LOG_INFO("Superior Scaling: {}", text);
}

// Retire the current generation: its resources and NGX handles go once every list that used them has completed.
void Drop()
{
    auto& s = GetState();
    if (!s.current)
        return;
    auto old = std::move(s.current);
    s.lifetime.Retire([old] {});
    s.lifetime.BeginGeneration();
}

// For the rest of the session (until Superior is selected again) the ordinary path runs, with Matched residual.
void GiveUp(const std::string& reason, bool srUnavailable)
{
    auto& s = GetState();
    s.unavailable = reason;
    s.srUnavailable = s.srUnavailable || srUnavailable;
    LOG_WARN("Superior Scaling unavailable: {}; using Matched residual instead", reason);
    Say("");
    Drop();
}

void Record(ID3D12GraphicsCommandList* cmd)
{
    auto& s = GetState();
    s.lifetime.Record(cmd);
    Watch(Util::RealObjectIdentity(cmd));
}

void Submitted(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    auto& s = GetState();
    s.lifetime.Submitted(queue, count, lists);
    s.preview.SignalSubmitted(queue, count, lists);
    for (UINT i = 0; i < count; ++i)
        s.preview.CopySubmitted(queue, Util::RealObjectIdentity(lists[i]));
    if (s.editDump && s.editDump->copy.Poll() == DlssNr::EditDump::ReadbackCopy::State::NotSubmitted &&
        g_editDumpCommands == s.editDump->commands)
        for (UINT i = 0; i < count; ++i)
            if (Util::RealObjectIdentity(lists[i]) == s.editDump->commands)
            {
                // The real game Execute has already happened. Queue order captures the image once;
                // replay never writes this private readback, even while the file is being mapped.
                std::array<DlssNr::EditDump::ReadbackCopy::Item, 3> items;
                items[0] = { s.editDump->image.Get(), s.editDump->readback.Get(), s.editDump->footprint };
                for (unsigned j = 0; j < s.editDump->guideCount; ++j)
                {
                    const auto& guide = s.editDump->guides[j];
                    items[j + 1] = { guide.image.Get(), guide.readback.Get(), guide.footprint };
                }
                s.editDump->copy.SubmitMany(queue, items.data(), 1 + s.editDump->guideCount);
                s.editDumpStatus = "Snapshot submitted; waiting for capture GPU fence.";
                break;
            }
    // The hook stopped watching these lists after this submission. A list whose recording is still open can be
    // submitted again (replayed) and is still to be Reset, so it stays watched until then; others are forgotten.
    if (s.current && s.current->creation)
        for (UINT i = 0; i < count; ++i)
            if (lists[i] == s.current->creation)
                s.current->creationSubmitted = true;
    for (UINT i = 0; i < count; ++i)
        if (s.lifetime.Open(lists[i]))
            Watch(lists[i]);
        else
            Forget(lists[i]);
}

void ResetRecording(ID3D12CommandList* cmd)
{
    auto& s = GetState();
    s.lifetime.ResetRecording(cmd);
    s.preview.ResetRecording(cmd);
    if (s.editDump && s.editDump->commands == Util::RealObjectIdentity(cmd))
    {
        EditDumpListDestroyed(s.editDump->commands);
    }
    Forget(cmd);
    // The private DLSS creation was recorded on a list that is being reset without ever being submitted:
    // those creation commands never run, so the feature must be built again.
    if (s.current && s.current->feature && s.current->creation == cmd && !s.current->creationSubmitted)
    {
        Drop();
        g_nr.reset = true;
    }
}

bool Ensure(Texture& t, ID3D12Device* device, DXGI_FORMAT format, unsigned w, unsigned h)
{
    if (t.resource)
    {
        const auto d = t.resource->GetDesc();
        if (d.Width == w && d.Height == h && d.Format == format)
            return true;
        auto old = std::move(t.resource);
        GetState().lifetime.Retire([old] {});
    }
    t.state = Uav;
    t.resource.Attach(CreateScratch(device, format, w, h));
    return t.resource != nullptr;
}

bool Dispatch(DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, const Constants& c, ID3D12Resource* source,
              ID3D12Resource* model, ID3D12Resource* original, ID3D12Resource* motion, ID3D12Resource* previous,
              ID3D12Resource* low, Texture& output, Texture* keep = nullptr, uint32_t* immutableSlot = nullptr)
{
    output.To(cmd, Uav);
    if (keep)
        keep->To(cmd, Uav);
    if (!shader.DispatchSuperiorPass(cmd, c, source, model, original, motion, previous, low, output.Get(),
                                     keep ? keep->Get() : nullptr, immutableSlot))
        return false;
    output.To(cmd, Srv);
    if (keep)
        keep->To(cmd, Srv);
    return true;
}

bool PrepareDumpGuide(EditDumpJob& job, unsigned index, ID3D12Resource* source,
                      DlssNr::EditDump::Format format, const char* extension,
                      UINT64& snapshots, UINT64& readbacks)
{
    if (!source) return false;
    auto desc = source->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count != 1 ||
        desc.DepthOrArraySize != 1 || desc.Width < job.width || desc.Height < job.height ||
        uint32_t(desc.Format) != uint32_t(format)) return false;
    desc.Width = job.width; desc.Height = job.height;
    desc.MipLevels = 1; desc.Flags = D3D12_RESOURCE_FLAG_NONE; desc.Alignment = 0;
    auto& guide = job.guides[index];
    guide.format = format; guide.extension = extension;
    job.device->GetCopyableFootprints(&desc, 0, 1, 0, &guide.footprint, nullptr, nullptr, &guide.bytes);
    guide.allocationBytes = job.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    constexpr UINT64 budget = 512ull << 20;
    if (guide.allocationBytes == UINT64_MAX || guide.allocationBytes > budget || guide.bytes > budget ||
        GetState().editDumpRetainedBytes->load() + snapshots + readbacks + guide.allocationBytes + guide.bytes > budget)
        return false;
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    if (FAILED(job.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&guide.image.resource)))) return false;
    guide.image.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    const auto readHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(guide.bytes);
    if (FAILED(job.device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&guide.readback)))) return false;
    snapshots += guide.allocationBytes; readbacks += guide.bytes;
    ++job.guideCount;
    return true;
}

void RecordEditDump(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd,
                   ID3D12Resource* proxy, ID3D12Resource* answer, float gain,
                   ID3D12Resource* depth, ID3D12Resource* motion, const DlssNrFrameInfo& frame, bool reset, bool hold)
{
    auto& s = GetState();
    if (s.pendingEditDump.empty() || s.editDump) return;
    auto name = std::move(s.pendingEditDump);
    s.pendingEditDump.clear();
    const bool withGuides = std::exchange(s.pendingEditDumpGuides, false);
    auto job = std::make_shared<EditDumpJob>();
    job->device = g.device;
    job->width = g.workWidth;
    job->height = g.workHeight;
    job->commands = Util::RealObjectIdentity(cmd);
    job->path = Util::DllPath().remove_filename() / "dlssnr-edit" / std::filesystem::u8path(name);
    // Bound a diagnostic's transient allocation to 512 MiB for the texture + readback together.
    if (!job->width || !job->height || job->width > 16384 || job->height > 16384 ||
        s.editDumpRetainedBytes->load() + uint64_t(job->width) * job->height * 32u > (512ull << 20))
    {
        s.editDumpStatus = "Edit not saved: image allocation failed or the 512 MiB capture limit was exceeded.";
        return;
    }
    const auto imageHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto imageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT,
        job->width, job->height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    // A replayable recording must start/end in the same snapshot state. The
    // private readback copy also leaves COPY_SOURCE, so every replay is valid.
    if (FAILED(g.device->CreateCommittedResource(&imageHeap, D3D12_HEAP_FLAG_NONE, &imageDesc,
        D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&job->image.resource))))
    {
        s.editDumpStatus = "Edit not saved: image allocation failed.";
        return;
    }
    job->image.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    const auto desc = job->image.Get()->GetDesc();
    g.device->GetCopyableFootprints(&desc, 0, 1, 0, &job->footprint, nullptr, nullptr, &job->bytes);
    const auto snapshotBytes = g.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    if (snapshotBytes == UINT64_MAX || snapshotBytes > (512ull << 20) ||
        s.editDumpRetainedBytes->load() + snapshotBytes + job->bytes > (512ull << 20))
    {
        s.editDumpStatus = "Edit not saved: 512 MiB capture budget reached; older game recordings still retain snapshots.";
        return;
    }
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(job->bytes);
    if (FAILED(g.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&job->readback))))
    {
        s.editDumpStatus = "Edit not saved: readback allocation failed.";
        return;
    }
    UINT64 snapshotTotal = snapshotBytes, readbackTotal = job->bytes;
    if (withGuides)
    {
        if (!PrepareDumpGuide(*job, 0, depth, DlssNr::EditDump::Format::R32Float, ".depth.dds", snapshotTotal, readbackTotal) ||
            !PrepareDumpGuide(*job, 1, motion, DlssNr::EditDump::Format::Rg32Float, ".motion.dds", snapshotTotal, readbackTotal))
        {
            s.editDumpStatus = "Diagnostic not saved: guides unavailable/unsupported or the 512 MiB capture budget was exceeded.";
            return;
        }
    }
    // Settings and the shown recommendation accompany every edit, with or without guides.
    {
        const auto& cfg = *Config::Instance();
        DlssNr::EditDump::Metadata m;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        m.guidesIncluded = withGuides;
        m.previewEnabledAtRequest = s.pendingEditDumpPreviewEnabled;
        m.previewEnabledAtCapture = cfg.DlssNrSuperiorGainPreview.value_or_default();
        m.previewAtRequest = s.pendingEditDumpPreview;
        m.previewAtCapture = DlssNr::EditDump::SnapshotPreview(s.preview.Get(), now);
        m.privatePresetAtRequest = s.pendingEditDumpPrivatePreset; m.gainAtRequest = s.pendingEditDumpGain;
        m.requestDelaySeconds = std::max(0., now - s.pendingEditDumpRequestedAt);
        m.previewSettings = { cfg.DlssNrSuperiorGainPreviewSlowSeconds.value_or_default(),
            cfg.DlssNrSuperiorGainPreviewFastSeconds.value_or_default(),
            cfg.DlssNrSuperiorGainPreviewMotionThreshold.value_or_default(),
            cfg.DlssNrSuperiorGainPreviewChangingMotion.value_or_default() };
        m.frame = g_frames; m.submissionEpoch = frame.SubmissionEpoch;
        m.width = job->width; m.height = job->height;
        m.gain = gain; m.saturation = g.editSaturation; m.depthStrength = g.depthStrength;
        m.fullNrAnswer = g.filterNrAnswer; m.privatePreset = g.preset;
        m.nrPresetSetting = cfg.DlssNrPreset.value_or_default();
        m.privateIsHdr = !g.disableIsHDR; m.exposureMode = g.exposureMode;
        m.manualExposure = DlssNr::PrivateExposure::ManualValue(cfg.DlssNrSuperiorPrivateExposureValue.value_or_default());
        m.invertedDepth = g.inverted; m.packed = g.packed; m.reset = reset; m.held = hold;
        m.expectedPassMask = g.expectedMask; m.completedPassMask = g.successfulMask;
        m.whitePointSource = DlssNr::WhitePointSourceEffective(cfg);
        m.autoTrimControl = DlssNr::AutoTrimEffective(cfg);
        m.gameTrimControl = cfg.DlssNrWhitePointTrim.value_or_default();
        m.scanTrimControl = cfg.DlssNrScanTrim.value_or_default();
        job->metadata = DlssNr::EditDump::MetadataJson(m);
    }
    Constants c {};
    c.Mode = 43u;
    c.Width = job->width;
    c.Height = job->height;
    Fields::SetPositiveCarrierGain(c, gain);
    Record(cmd);
    // Even a failed dispatch may have recorded a barrier referencing the snapshot.
    // Retain before recording anything that could leave a partial command sequence.
    std::vector<ComPtr<ID3D12Resource>> snapshots { job->image.resource };
    for (unsigned i = 0; i < job->guideCount; ++i) snapshots.push_back(job->guides[i].image.resource);
    auto retainedBytes = s.editDumpRetainedBytes;
    retainedBytes->fetch_add(snapshotTotal);
    s.lifetime.Retire([snapshots, retainedBytes, snapshotTotal] { retainedBytes->fetch_sub(snapshotTotal); });
    if (!Dispatch(shader, cmd, c, proxy, answer, nullptr, nullptr, nullptr, nullptr, job->image))
    {
        s.editDumpStatus = "Edit not saved: snapshot dispatch failed.";
        return;
    }
    job->image.To(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (unsigned i = 0; i < job->guideCount; ++i)
    {
        auto* source = i == 0 ? depth : motion;
        auto& guide = job->guides[i];
        guide.image.To(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier(cmd, source, Srv, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
        src.pResource = source; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = guide.image.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX box { 0, 0, 0, job->width, job->height, 1 };
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        Barrier(cmd, source, D3D12_RESOURCE_STATE_COPY_SOURCE, Srv);
        guide.image.To(cmd, D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    s.editDump = job;
    g_editDumpCommands = job->commands;
    // Only the snapshot is referenced by the game list. Keep it until replay is impossible;
    // private copy/readback ownership is separately held by the job until its fence completes.
    s.editDumpStatus = "Snapshot recorded; waiting for game submission.";
}

Constants GuideConstants(unsigned w, unsigned h, const DlssNr::GuideRegions& regions, const DlssNrFrameInfo& frame,
                         bool pixelUnits, bool hold)
{
    Constants c {};
    c.Mode = 10u;
    c.Width = w;
    c.Height = h;
    Fields::SetDepthRegion(c, regions.depth.x, regions.depth.y, regions.depth.width, regions.depth.height);
    Fields::SetMotionRegion(c, regions.motion.x, regions.motion.y, regions.motion.width, regions.motion.height);
    c.MvScaleX = c.MvScaleY = 1.0f;
    if (pixelUnits)
    {
        const auto rw = frame.MotionVectorsLowResolution ? frame.RenderSubrectWidth : frame.OutputWidth;
        const auto rh = frame.MotionVectorsLowResolution ? frame.RenderSubrectHeight : frame.OutputHeight;
        c.MvScaleX = frame.MvScaleX * (float(w) / std::max(rw ? rw : regions.motion.width, 1u));
        c.MvScaleY = frame.MvScaleY * (float(h) / std::max(rh ? rh : regions.motion.height, 1u));
        if (hold)
            c.MvScaleX = c.MvScaleY = 0;
    }
    return c;
}

// Immutable 11-float4 warp metadata uses the resolve's otherwise unused D3D12 motion SRV (t3).
// The generation owns its upload until all recorded command lists have retired. No per-frame copy/dispatch.
bool CreateWarp(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd)
{
    if (!g.packed)
        return true;
    if (!Ensure(g.warp, g.device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, 11, 1))
        return false;
    const auto data = pw::MakeShaderConstants(g.layout.warp);
    static_assert(sizeof(data) == 11 * 16);
    const auto desc = g.warp.Get()->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 bytes = 0;
    g.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes);
    if (FAILED(g.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&g.warpUpload))))
        return false;
    void* mapped = nullptr;
    const D3D12_RANGE noRead { 0, 0 };
    if (FAILED(g.warpUpload->Map(0, &noRead, &mapped)))
        return false;
    std::memcpy(static_cast<char*>(mapped) + footprint.Offset, &data, sizeof(data));
    g.warpUpload->Unmap(0, nullptr);
    g.warp.To(cmd, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src {}, dst {};
    src.pResource = g.warpUpload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    dst.pResource = g.warp.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    g.warp.To(cmd, Srv);
    // Optional acceleration. Failure leaves the immutable metadata and arithmetic PSOs usable.
    if (Ensure(g.warpCache, g.device.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, std::max(11u, std::max(g.width, g.height)),
               3) &&
        shader.BuildSuperiorWarpCache(cmd, g.width, g.height, g.warp.Get(), g.warpCache.Get()))
        g.warpCache.To(cmd, Srv);
    else
        g.warpCache.resource.Reset();
    return true;
}

enum class Created
{
    Ok,
    ResourcesFailed, // our own surfaces or passes: worth another try once Superior is selected again
    SrUnavailable    // the NGX DLSS SR runtime: not here on this machine/runtime
};

NVSDK_NGX_Result CreateFeature(Generation& g, ID3D12GraphicsCommandList* cmd, unsigned preset)
{
    auto* p = g.parameters;
    for (const char* key :
         { NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
           NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,
           NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
           NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance,
           NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality })
        p->Set(key, preset);
    g.feature = nullptr;
    return NVNGXProxy::D3D12_CreateFeature()(cmd, NVSDK_NGX_Feature_SuperSampling, p, &g.feature);
}

Created CreateTemporal(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd)
{
    Record(cmd); // includes unit-exposure initialization even if NGX creation fails
    const auto w = g.workWidth, h = g.workHeight;
    if (!Ensure(g.carrier, g.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, w, h) ||
        !Ensure(g.temporalOutput, g.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, w, h) ||
        !Ensure(g.temporalMotion, g.device.Get(), DXGI_FORMAT_R32G32_FLOAT, w, h) || !CreateWarp(g, shader, cmd) ||
        !Ensure(g.unit, g.device.Get(), DXGI_FORMAT_R32_FLOAT, 1, 1))
        return Created::ResourcesFailed;
    Constants unit {};
    unit.Mode = 7u;
    unit.Width = unit.Height = 1u;
    // Mode 7 only writes 1.0 (precompile/dlssnr.hlsl, "gMode == 7") and reads no SRV. g_nr.output, at rest in UAV
    // state, is just the stand-in every SRV slot must hold; it is never accessed through that descriptor.
    if (!Dispatch(shader, cmd, unit, g_nr.output, nullptr, nullptr, nullptr, nullptr, nullptr, g.unit))
        return Created::ResourcesFailed;
    if (!NVNGXProxy::InitDx12(g.device.Get()) || !NVNGXProxy::D3D12_AllocateParameters() ||
        !NVNGXProxy::D3D12_DestroyParameters() || !NVNGXProxy::D3D12_CreateFeature() ||
        !NVNGXProxy::D3D12_EvaluateFeature() || !NVNGXProxy::D3D12_ReleaseFeature())
        return Created::SrUnavailable;
    g.ngxEpoch = NVNGXProxy::Dx12Epoch();
    if (NVNGXProxy::D3D12_AllocateParameters()(&g.parameters) != NVSDK_NGX_Result_Success || !g.parameters)
    {
        g.parameters = nullptr;
        return Created::SrUnavailable;
    }
    auto* p = g.parameters;
    p->Set(NVSDK_NGX_Parameter_Width, w);
    p->Set(NVSDK_NGX_Parameter_Height, h);
    p->Set(NVSDK_NGX_Parameter_OutWidth, w);
    p->Set(NVSDK_NGX_Parameter_OutHeight, h);
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u);
    p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue, (int) NVSDK_NGX_PerfQuality_Value_DLAA);
    const unsigned flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                           (g.exposureMode == DlssNr::PrivateExposure::Native ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0u) |
                           (g.disableIsHDR ? 0u : NVSDK_NGX_DLSS_Feature_Flags_IsHDR) |
                           (g.inverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0u);
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
    LOG_INFO("Superior Scaling: creating private DLSS preset {}, IsHDR {}, exposure mode {}", g.preset,
             !g.disableIsHDR, g.exposureMode);
    Record(cmd);
    auto result = CreateFeature(g, cmd, g.preset);
    if (result != NVSDK_NGX_Result_Success || !g.feature)
    {
        // A runtime without the requested preset (newer 310.x DLLs dropped the CNN presets A-E): its default.
        static std::once_flag once;
        std::call_once(once, [&]
                       { LOG_WARN("Superior Scaling: private DLSS preset {} unavailable ({:X}); using the runtime's "
                                  "default preset", g.preset, (unsigned) result); });
        result = CreateFeature(g, cmd, NVSDK_NGX_DLSS_Hint_Render_Preset_Default);
    }
    g.creation = Util::RealObjectIdentity(cmd);
    return result == NVSDK_NGX_Result_Success && g.feature ? Created::Ok : Created::SrUnavailable;
}

static_assert(unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_A) == 1u &&
              unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_E) == 5u &&
              unsigned(NVSDK_NGX_DLSS_Hint_Render_Preset_K) == 11u);

Constants Shaping(const Config& cfg, unsigned first, unsigned completed,
                  DlssNr::SuperiorScaling::FrequencyScales scales)
{
    Constants c {};
    c.ResidualHistoryValid = 1u;
    c.ResidualBlend = .015f;
    c.ResidualScale = 3.0f;
    c.ResidualConfidenceSensitivity = 1.0f;
    const auto gains = DlssNr::SuperiorScaling::BlockGains(cfg, first, completed, scales, g_nr.superiorMainOnlyGains);
    Fields::SetBandGains(c, gains.high, gains.low);
    Fields::SetInterPassLowField(c, true);
    c.DirectResolveFlags = DlssNr::SuperiorScaling::ShaderFlag;
    return c;
}

bool InterPass(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, const Config& cfg,
               ID3D12Resource* nativeProxy, ID3D12Resource* originalBase, ID3D12Resource* answer, unsigned covered,
               unsigned completed, DlssNr::SuperiorScaling::FrequencyScales scales)
{
    const auto desc = originalBase->GetDesc();
    const unsigned workW = unsigned(desc.Width), workH = desc.Height;
    auto c = Shaping(cfg, covered, completed, scales);
    c.DirectResolveFlags |= 16u | 64u;
    if (Fields::HighGain(c) == Fields::LowGain(c))
    {
        Fields::SetInterPassLowField(c, false);
        c.DirectResolveFlags |= 32u;
    }
    ID3D12Resource* low = nullptr;
    if (Fields::InterPassLowField(c))
    {
        if (!Ensure(g.interLow, g.device.Get(), desc.Format, (workW + 7u) / 8u, (workH + 7u) / 8u))
            return false;
        Constants l {};
        l.Mode = 34u;
        l.Width = (workW + 7u) / 8u;
        l.Height = (workH + 7u) / 8u;
        l.DirectResolveFlags = 32768u;
        if (!Dispatch(shader, cmd, l, originalBase, answer, nullptr, nullptr, nullptr, nullptr, g.interLow))
            return false;
        low = g.interLow.Get();
    }
    if (!Ensure(g.interWorking, g.device.Get(), desc.Format, workW, workH))
        return false;
    c.Mode = 36u;
    c.Width = workW;
    c.Height = workH;
    return Dispatch(shader, cmd, c, originalBase, answer, nativeProxy, nullptr, nullptr, low, g.interWorking);
}

DlssNr::Spatial::Constants UniformGuideConstants(const Generation& g, const DlssNr::GuideRegions& regions,
                                                 const DlssNrFrameInfo& frame, bool haveDepth)
{
    DlssNr::Spatial::Layout l {};
    l.nativeW = g.width;
    l.nativeH = g.height;
    l.modelW = g.workWidth;
    l.modelH = g.workHeight;
    l.warp.x = pw::BuildAxis(g.width, g.width, g.workWidth, 1, 0, 0);
    l.warp.y = pw::BuildAxis(g.height, g.height, g.workHeight, 1, 0, 0);
    const DlssNr::GuideExtent reference =
        frame.MotionVectorsLowResolution ? DlssNr::GuideExtent { frame.RenderSubrectWidth, frame.RenderSubrectHeight }
                                         : DlssNr::GuideExtent { frame.OutputWidth, frame.OutputHeight };
    auto c = DlssNr::Spatial::MakeGuideConstants(l, regions, reference, frame.MvScaleX, frame.MvScaleY);
    c.mode = 104u;
    c.padding[0] = haveDepth && regions.depth.valid() ? 1.f : 0.f;
    if (!haveDepth)
    {
        c.depthRect[0] = c.depthRect[1] = 0;
        c.depthRect[2] = c.depthRect[3] = 1;
    }
    return c;
}

bool MatchGuides(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, ID3D12Resource* depth,
                 ID3D12Resource* motion, const DlssNr::GuideRegions& regions, const DlssNrFrameInfo& frame,
                 float depthPrecision = 0.f);

// Called only after the carrier is complete: meter exactly the texture and rectangle NGX will read.
// Native / unit: no additional per-frame GPU work. Manual: update only when the value changes.
ID3D12Resource* PrivateExposureTexture(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, bool reset)
{
    namespace PE = DlssNr::PrivateExposure;
    if (g.exposureMode == PE::Native || g.exposureMode == PE::Unit)
        return g.unit.Get();
    if (g.exposureMode == PE::Manual)
    {
        const float value = PE::ManualValue(Config::Instance()->DlssNrSuperiorPrivateExposureValue.value_or_default());
        if (!g.unitInitialized || value != g.manualExposure)
        {
            Constants c {};
            c.Mode = 42u;
            c.Width = c.Height = 1u;
            c.WhitePoint = value;
            if (!Dispatch(shader, cmd, c, g.carrier.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, g.unit))
                return nullptr;
            g.manualExposure = value;
            g.unitInitialized = true;
        }
        return g.unit.Get();
    }
    auto& h = g.exposure[g.exposureMode - PE::LegacyCNN];
    reset = reset || !h.initialized;
    const bool measure = PE::Measure(g.exposureMode, h.evaluations, reset);
    const auto sw = PE::SampleExtent(g.workWidth, g.workWidth, g.exposureMode);
    const auto sh = PE::SampleExtent(g.workHeight, g.workHeight, g.exposureMode);
    const auto bx = (sw + 31u) / 32u, by = (sh + 31u) / 32u;
    if (!Ensure(g.exposureSums, g.device.Get(), DXGI_FORMAT_R32_FLOAT, bx, by) ||
        !Ensure(h.state[0], g.device.Get(), DXGI_FORMAT_R32G32_FLOAT, 1, 1) ||
        !Ensure(h.state[1], g.device.Get(), DXGI_FORMAT_R32G32_FLOAT, 1, 1))
        return nullptr;
    Constants c {};
    Fields::SetPrivateExposure(c, g.exposureMode, g.workWidth, g.workHeight, measure, reset, h.initialized);
    if (measure)
    {
        c.Mode = 40u;
        c.Width = sw;
        c.Height = sh;
        if (!Dispatch(shader, cmd, c, g.carrier.Get(), nullptr, nullptr, nullptr, nullptr, nullptr, g.exposureSums))
            return nullptr;
    }
    c.Mode = 41u;
    c.Width = bx;
    c.Height = by;
    const unsigned next = h.index ^ 1u;
    // No sample reads on an uninitialized history; bind the already readable carrier as a stand-in.
    auto* previous = h.initialized ? h.state[h.index].Get() : g.carrier.Get();
    auto* sums = measure ? g.exposureSums.Get() : g.carrier.Get();
    if (!Dispatch(shader, cmd, c, sums, previous, nullptr, nullptr, nullptr, nullptr, h.state[next]))
        return nullptr;
    h.index = next;
    h.initialized = true;
    return h.state[next].Get();
}

ID3D12Resource* Stabilize(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, ID3D12Resource* proxy,
                          ID3D12Resource* answer, ID3D12Resource* originalMotion, const DlssNr::GuideRegions& regions,
                          const DlssNrFrameInfo& frame, bool reset, bool hold, ID3D12Resource* packedDepth,
                          ID3D12Resource* packedMotion, DlssNr::GuideExtent packedGuideExtent,
                          float packedDepthPrecision)
{
    if (!g.feature || !g.parameters || !NgxAlive(g.ngxEpoch))
        return nullptr;
    const float gain = DlssNr::PrivateExposure::PositiveGain(Config::Instance()->DlssNrSuperiorPositiveEditGain.value_or_default());
    const float saturation = DlssNr::PrivateExposure::EditSaturation(Config::Instance()->DlssNrSuperiorEditSaturation.value_or_default());
    const auto& cfg = *Config::Instance();
    const float depthStrength = cfg.DlssNrSuperiorDepthColour.value_or_default()
                                    ? DlssNr::PrivateExposure::DepthStrength(cfg.DlssNrSuperiorDepthColourStrength.value_or_default()) : 0.f;
    const float depthCurve = depthStrength > 0 ? DlssNr::PrivateExposure::DepthCurve(cfg.DlssNrSuperiorDepthColourCurve.value_or_default()) : 1.f;
    const uint32_t depthPair = depthStrength > 0 ? DlssNr::PrivateExposure::DepthPair(cfg.DlssNrSuperiorDepthColourPair.value_or_default()) : 0u;
    const bool filterNrAnswer = cfg.DlssNrSuperiorFilterNrAnswer.value_or_default();
    if (gain != g.positiveGain || saturation != g.editSaturation || depthStrength != g.depthStrength ||
        depthCurve != g.depthCurve || depthPair != g.depthPair || filterNrAnswer != g.filterNrAnswer)
    {
        g.positiveGain = gain;
        g.editSaturation = saturation;
        g.depthStrength = depthStrength;
        g.depthCurve = depthCurve;
        g.depthPair = depthPair;
        g.filterNrAnswer = filterNrAnswer;
        g.reset = true; // never mix histories encoded with different transport transforms
        for (auto& h : g.exposure) h.initialized = false;
    }
    auto encode = GuideConstants(g.workWidth, g.workHeight, regions, frame, true, hold);
    Fields::SetPositiveCarrierGain(encode, gain);
    Fields::SetCarrierSaturation(encode, saturation);
    Fields::SetCarrierDepthColour(encode, depthStrength, depthCurve, depthPair, g.inverted);
    Fields::SetCarrierNrAnswer(encode, filterNrAnswer);
    encode.Mode = 39u; // v50: fixed carrier + motion; reuse this frame's exact NR depth.
    auto* motion = originalMotion;
    auto* temporalDepth = g.packed ? packedDepth : g.depth.Get();
    bool prepared = false;
    if (g.packed)
    {
        if (!packedDepth || !packedMotion)
            return nullptr;
        // Packed vectors already contain PwPack(p+v)-PwPack(p), in packed pixels. Never scale/warp twice.
        motion = packedMotion;
        Fields::SetMotionRegion(encode, 0, 0, g.workWidth, g.workHeight);
        encode.MvScaleX = encode.MvScaleY = hold ? 0.f : 1.f;
        // The allocation may exceed the active subrect under dynamic resolution.
        if (packedGuideExtent.width != g.workWidth || packedGuideExtent.height != g.workHeight)
        {
            // NR accepts an independent guide subrect. Private DLAA has no independent
            // depth-size parameter: reconstruct its guides on the carrier's raster.
            // These source vectors already contain the non-linear endpoint warp, in
            // the smaller guide lattice's pixels; never apply PwPack a second time.
            const unsigned gw = packedGuideExtent.width, gh = packedGuideExtent.height;
            const DlssNr::GuideRegions packedRegions { { 0, 0, gw, gh }, { 0, 0, gw, gh } };
            auto packedFrame = frame;
            packedFrame.MotionVectorsLowResolution = true;
            packedFrame.RenderSubrectWidth = packedFrame.OutputWidth = gw;
            packedFrame.RenderSubrectHeight = packedFrame.OutputHeight = gh;
            packedFrame.MvScaleX = packedFrame.MvScaleY = 1.f;
            encode.MvScaleX = hold ? 0.f : float(g.workWidth) / gw;
            encode.MvScaleY = hold ? 0.f : float(g.workHeight) / gh;
            if (!Ensure(g.depth, g.device.Get(), DXGI_FORMAT_R32_FLOAT, g.workWidth, g.workHeight))
                return nullptr;
            auto fused = UniformGuideConstants(g, packedRegions, packedFrame, true);
            DlssNr::Spatial::SetDepthPrecision(fused, packedDepthPrecision);
            fused.padding[1] = encode.MvScaleX;
            fused.padding[2] = encode.MvScaleY;
            DlssNr::Spatial::SetPrivateCarrierGain(fused, gain);
            DlssNr::Spatial::SetPrivateCarrierSaturation(fused, saturation);
            DlssNr::Spatial::SetPrivateCarrierDepthColour(fused, depthStrength, depthCurve, depthPair);
            DlssNr::Spatial::SetPrivateCarrierNrAnswer(fused, filterNrAnswer);
            Record(cmd);
            g.depth.To(cmd, Uav);
            g.carrier.To(cmd, Uav);
            g.temporalMotion.To(cmd, Uav);
            prepared = shader.DispatchSuperiorPrivateGuides(cmd, fused, proxy, answer, packedDepth, packedMotion,
                                                            g.depth.Get(), g.temporalMotion.Get(), g.carrier.Get());
            if (prepared)
            {
                g.depth.To(cmd, Srv);
                g.carrier.To(cmd, Srv);
                g.temporalMotion.To(cmd, Srv);
            }
            else
            {
                if (!MatchGuides(g, shader, cmd, packedDepth, packedMotion, packedRegions, packedFrame,
                                 packedDepthPrecision))
                    return nullptr;
                motion = g.motion.Get();
            }
            temporalDepth = g.depth.Get();
        }
        // Carrier preparation converts vector units once and writes zero motion while held.
        // The default, colour-sized guide path needs no extra guide dispatch.
    }
    else if (g.guidesMatched)
    {
        // Reuse the raw resampled NR field. Convert to working pixels once in this fused carrier pass.
        motion = g.motion.Get();
        Fields::SetMotionRegion(encode, 0, 0, g.workWidth, g.workHeight);
    }
    if (!prepared &&
        !Dispatch(shader, cmd, encode, proxy, answer, motion, depthStrength > 0 ? temporalDepth : nullptr,
                  nullptr, nullptr, g.carrier, &g.temporalMotion))
        return nullptr;
    // The estimate samples raw NR differences, independent of the manually selected gain.
    // Secondary views do not feed a different image into the primary-view smoother.
    if (!g_viewSecondary && cfg.DlssNrSuperiorGainPreview.value_or_default())
    {
        auto& preview = GetState().preview;
        if (filterNrAnswer || saturation != 1.f || depthStrength > 0.f)
            preview.Pause("Preview needs edit mode, saturation 1x and depth colour off.");
        else if (hold)
            preview.Pause("Paused: frame held.");
        else
        {
            const uint64_t context = uint64_t(g.workWidth) | (uint64_t(g.workHeight) << 16) |
                (uint64_t(g.preset) << 32) | (uint64_t(cfg.DlssNrPreset.value_or_default()) << 40) |
                (uint64_t(g.exposureMode) << 48) | (uint64_t(g.disableIsHDR) << 52) |
                (uint64_t(g.inverted) << 53) | (uint64_t(g.packed) << 54) | (uint64_t(g.beforeSr) << 55);
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            Record(cmd);
            preview.Record(g.device.Get(), cmd, Util::RealObjectIdentity(cmd), proxy, answer, g.temporalMotion.Get(),
                           g.workWidth, g.workHeight, g.preset == 11, context, now);
        }
    }
    RecordEditDump(g, shader, cmd, proxy, answer, gain, temporalDepth, g.temporalMotion.Get(), frame,
                   reset || g.reset, hold);
    auto* exposure = PrivateExposureTexture(g, shader, cmd, reset || g.reset);
    if (!exposure)
    {
        g.reset = true;
        Say("Private exposure calculation failed; keeping the game frame.");
        return nullptr;
    }
    g.temporalOutput.To(cmd, Uav);
    auto* p = g.parameters;
    p->Set(NVSDK_NGX_Parameter_Color, g.carrier.Get());
    p->Set(NVSDK_NGX_Parameter_Output, g.temporalOutput.Get());
    p->Set(NVSDK_NGX_Parameter_Depth, temporalDepth);
    p->Set(NVSDK_NGX_Parameter_MotionVectors, g.temporalMotion.Get());
    p->Set(NVSDK_NGX_Parameter_ExposureTexture, exposure);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, g.workWidth);
    p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, g.workHeight);
    p->Set(NVSDK_NGX_Parameter_Reset, (unsigned) (reset || g.reset));
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, 0.0f);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, 0.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec,
           std::isfinite(frame.FrameTimeMs) && frame.FrameTimeMs > 0 ? frame.FrameTimeMs : 16.67f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.0f);
    p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.0f);
    p->Set(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    if (NVNGXProxy::D3D12_EvaluateFeature()(cmd, g.feature, p, nullptr) != NVSDK_NGX_Result_Success)
    {
        g.reset = true;
        Say("Private DLSS evaluation failed; keeping the game frame.");
        return nullptr;
    }
    g.temporalOutput.To(cmd, Srv);
    if (g.exposureMode >= DlssNr::PrivateExposure::LegacyCNN)
        ++g.exposure[g.exposureMode - DlssNr::PrivateExposure::LegacyCNN].evaluations;
    Constants decode {};
    decode.Mode = 38u;
    decode.Width = g.workWidth;
    decode.Height = g.workHeight;
    Fields::SetPositiveCarrierGain(decode, gain);
    Fields::SetCarrierSaturation(decode, saturation);
    Fields::SetCarrierDepthColour(decode, depthStrength, depthCurve, depthPair, g.inverted);
    Fields::SetCarrierNrAnswer(decode, filterNrAnswer);
    if (!Dispatch(shader, cmd, decode, g.temporalOutput.Get(), filterNrAnswer ? proxy : nullptr, nullptr,
                  depthStrength > 0 ? temporalDepth : nullptr, nullptr, nullptr, g.carrier))
        return nullptr;
    g.reset = false;
    return g.carrier.Get();
}

// The private preset Begin picks (PrivatePresetChoice): kept for the session so a moving Auto scale does not recreate
// the private DLSS and reset history; NR and NGX shutdown clear it, so a new device or runtime picks again.
DlssNr::SuperiorScaling::PrivatePresetChoice& PresetChoice()
{
    static DlssNr::SuperiorScaling::PrivatePresetChoice choice;
    return choice;
}

enum class Began
{
    Run,        // record Superior this frame
    Wait,       // private DLSS creation recorded; skip NR until its submission epoch has passed (one frame)
    Unavailable // Superior cannot run: GiveUp() was called, skip this frame, the next one runs the ordinary path
};

// Called before janblade records its normal input/exposure path. Only private DLSS
// creation gets a submission-only frame, exactly like NR feature creation above it.
Began Begin(DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, ID3D12Device* device, DXGI_FORMAT format,
            unsigned width, unsigned height, unsigned workWidth, unsigned workHeight, float modelScale,
            const DlssNrFrameInfo& frame, bool temporal, bool haveDepth, const DlssNr::Spatial::Layout* layout)
{
    auto& state = GetState();
    g_used = true;
    state.lifetime.Collect();
    if (temporal && !haveDepth)
    {
        GiveUp("temporal stabilization needs the game's depth and motion guides", false);
        return Began::Unavailable;
    }
    // WarpMap.hlsli maps uv * (gWidth, gHeight) -- the frame size the resolve and warp cache are dispatched at -- through
    // PwPack, which expects pixels of the layout's native size (gNativeWorkSize.xy). The packed layout is built from this
    // same frame, so they are equal; refuse a layout that is not rather than warp with the wrong scale.
    if (layout && (layout->nativeW != width || layout->nativeH != height))
    {
        GiveUp("its packed layout does not match the frame size", false);
        return Began::Unavailable;
    }
    const auto& cfg = *Config::Instance();
    const bool disableIsHDR = cfg.DlssNrSuperiorScalingDisableIsHDR.value_or_default();
    const auto selectedExposure = DlssNr::PrivateExposure::SanitizeMode(cfg.DlssNrSuperiorPrivateExposureMode.value_or_default());
    const auto exposureMode = cfg.DlssNrSuperiorScalingDisableAutoExposure.value_or_default()
                                  ? std::max(uint32_t(DlssNr::PrivateExposure::Unit), selectedExposure)
                                  : uint32_t(DlssNr::PrivateExposure::Native);
    const unsigned preset = PresetChoice().Select(cfg.DlssNrSuperiorScalingPreset.value_or_default(),
                                                  cfg.DlssNrModelResolutionAuto.value_or_default(),
                                                  cfg.DlssNrWorkingScale.value_or_default(), modelScale);
    if (auto* previous = state.current.get())
    {
        // Handles from a runtime NGX has since shut down are gone already: never evaluate or release them.
        const bool ngxGone = (previous->feature || previous->parameters) && !NgxAlive(previous->ngxEpoch);
        if (ngxGone)
            previous->DropNgx();
        // The game's queue is not part of the key: NGX records on the given list and the lifetime tracker follows
        // the lists to whichever queue runs them.
        if (ngxGone || previous->device.Get() != device || previous->width != width || previous->height != height ||
            previous->workWidth != workWidth || previous->workHeight != workHeight || previous->format != format ||
            previous->inverted != frame.DepthInverted || previous->beforeSr != frame.BeforeUpscale ||
            previous->temporal != temporal || previous->packed != (layout != nullptr) ||
            (temporal && (previous->disableIsHDR != disableIsHDR || previous->exposureMode != exposureMode)) ||
            (layout && previous->layout != *layout) ||
            DlssNr::SuperiorScaling::PrivatePresetChanged(previous->preset, preset))
        {
            Drop();
            g_nr.reset = true;
        }
    }
    if (!state.current)
    {
        state.current = std::make_shared<Generation>();
        auto& g = *state.current;
        g.device = device;
        g.width = width;
        g.height = height;
        g.workWidth = workWidth;
        g.workHeight = workHeight;
        g.format = format;
        g.inverted = frame.DepthInverted;
        g.beforeSr = frame.BeforeUpscale;
        g.temporal = temporal;
        g.disableIsHDR = disableIsHDR;
        g.exposureMode = exposureMode;
        g.packed = layout != nullptr;
        if (layout)
            g.layout = *layout;
        g.preset = preset;
    }
    auto& g = *state.current;
    if (temporal && !g.feature)
    {
        switch (CreateTemporal(g, shader, cmd))
        {
        case Created::Ok:
            g.createEpoch = frame.SubmissionEpoch;
            g.waitedFrames = 0;
            Say("Waiting for private DLSS creation submission.");
            return Began::Wait;
        case Created::ResourcesFailed:
            GiveUp("its surfaces or passes could not be created", false);
            return Began::Unavailable;
        case Created::SrUnavailable:
            GiveUp("private NVIDIA DLSS SR (NGX) is not available", true);
            return Began::Unavailable;
        }
    }
    // A later call is not proof the creation was submitted (some engines record more than one upscale per list):
    // the submission epoch must have moved on, as for the NR feature itself.
    if (temporal && g.waitedFrames != UINT_MAX)
    {
        if (frame.SubmissionEpoch == g.createEpoch)
        {
            if (++g.waitedFrames > kCreationWaitFrames)
            {
                GiveUp("the private DLSS creation was never submitted", false);
                return Began::Unavailable;
            }
            return Began::Wait;
        }
        g.waitedFrames = UINT_MAX; // ready
        Say("");
    }
    Record(cmd);
    return Began::Run;
}

// The NGX D3D12 runtime is going away; it destroys every handle itself, so ours are dropped, not released.
void NgxShutdown()
{
    auto& s = GetState();
    if (s.current)
        s.current->DropNgx();
    Drop();
    if (!s.pendingEditDump.empty()) s.editDumpStatus = "Edit capture cancelled: NGX shutdown.";
    s.pendingEditDump.clear();
    s.pendingEditDumpGuides = false;
    s.preview.Configure(false, {});
    s.preview.Reset("Preview stopped: NR/NGX shutdown.");
    s.preview.Collect();
    CollectEditDump();
    PresetChoice() = {};
}

// NR shutdown: retire the current generation and run what has already completed; anything still in flight keeps its
// ownership for process teardown (like DeferredSr::Shutdown), never released under the GPU.
void Shutdown()
{
    auto& s = GetState();
    Drop();
    s.lifetime.Collect();
    if (!s.pendingEditDump.empty()) s.editDumpStatus = "Edit capture cancelled: NR shutdown.";
    s.pendingEditDump.clear();
    s.pendingEditDumpGuides = false;
    s.preview.Configure(false, {});
    s.preview.Reset("Preview stopped: NR/NGX shutdown.");
    s.preview.Collect();
    CollectEditDump();
    PresetChoice() = {};
}

bool MatchGuides(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, ID3D12Resource* depth,
                 ID3D12Resource* motion, const DlssNr::GuideRegions& regions, const DlssNrFrameInfo& frame,
                 float depthPrecision)
{
    if (!Ensure(g.depth, g.device.Get(), DXGI_FORMAT_R32_FLOAT, g.workWidth, g.workHeight) ||
        !Ensure(g.motion, g.device.Get(), DXGI_FORMAT_R32G32_FLOAT, g.workWidth, g.workHeight))
        return false;
    constexpr unsigned method = 2u;
    auto c = UniformGuideConstants(g, regions, frame, depth != nullptr);
    DlssNr::Spatial::SetDepthPrecision(c, depthPrecision);
    Record(cmd);
    g.depth.To(cmd, Uav);
    g.motion.To(cmd, Uav);
    g.guidesMatched =
        shader.DispatchSpatial(cmd, c, depth ? depth : motion, depth, motion, g.depth.Get(), g.motion.Get(), method);
    g.depth.To(cmd, Srv);
    g.motion.To(cmd, Srv);
    return g.guidesMatched;
}

ID3D12Resource* FinalLow(Generation& g, DlssNr_Dx12& shader, ID3D12GraphicsCommandList* cmd, ID3D12Resource* proxy,
                         ID3D12Resource* residual, bool signedResidual)
{
    const auto desc = proxy->GetDesc();
    const unsigned lowW = (unsigned(desc.Width) + 7u) / 8u, lowH = (desc.Height + 7u) / 8u;
    if (!Ensure(g.finalLow, g.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, lowW, lowH))
        return nullptr;
    Constants c {};
    c.Mode = 18u;
    c.Transfer = signedResidual ? 1u : 0u;
    c.DirectResolveFlags = DlssNr::SuperiorScaling::ShaderFlag;
    c.Width = lowW;
    c.Height = lowH;
    return Dispatch(shader, cmd, c, proxy, residual, nullptr, nullptr, nullptr, nullptr, g.finalLow) ? g.finalLow.Get()
                                                                                                     : nullptr;
}
} // namespace SuperiorRun
} // namespace
