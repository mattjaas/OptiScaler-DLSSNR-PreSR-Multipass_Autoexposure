#define NOMINMAX
#include <cassert>
#include "../OptiScaler/dlssnr/DlssNr_EditDumpFiles.h"
#include <fstream>
#include <cstring>
#include <vector>
#include "../OptiScaler/dlssnr/DlssNr_EditDump.h"
#include "../OptiScaler/dlssnr/DlssNr_EditDumpMetadata.h"
#include <limits>
#include <iostream>


void FilenameReservationChecks()
{
    using namespace DlssNr::EditDump;
    namespace fs=std::filesystem;
    const auto folder=fs::path("artifacts/validation")/("capture-names-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    fs::create_directories(folder);
    const auto original=folder/"nr-edit.dds";
    std::vector<fs::path> created;
    const std::vector<std::string> extensions{".depth.dds",".motion.dds",".json"};
    Files first;
    assert(first.Reserve(original,extensions)&&first.paths[0]==original);
    DWORD written;const char message[]="original sample";
    assert(WriteFile(first.files[0],message,sizeof(message),&written,nullptr)&&written==sizeof(message));
    created.insert(created.end(),first.paths.begin(),first.paths.end());first.Finish(true);
    Files next;
    assert(next.Reserve(original,extensions)&&next.paths[0].filename()=="nr-edit-001.dds");
    assert(next.paths[1].filename()=="nr-edit-001.depth.dds"&&next.paths[2].filename()=="nr-edit-001.motion.dds"&&next.paths[3].filename()=="nr-edit-001.json");
    created.insert(created.end(),next.paths.begin(),next.paths.end());next.Finish(true);
    // Two outstanding reservations cannot select the same name, before either write is committed.
    Files a,b;assert(a.Reserve(original,extensions)&&b.Reserve(original,extensions));
    assert(a.paths[0].filename()=="nr-edit-002.dds"&&b.paths[0].filename()=="nr-edit-003.dds");
    const auto aa=a.paths,bb=b.paths;a.Finish(false);b.Finish(false);
    for(const auto& path:aa)assert(!fs::exists(path));for(const auto& path:bb)assert(!fs::exists(path));
    const auto orphan=folder/"orphan.depth.dds";
    {std::ofstream out(orphan);out<<"keep";}created.push_back(orphan);
    Files plain;assert(plain.Reserve(folder/"orphan.dds",{".json"}));
    assert(plain.paths[0].filename()=="orphan-001.dds");
    const auto abandoned=plain.paths;plain.Finish(false);
    for(const auto& path:abandoned)assert(!fs::exists(path));
    std::ifstream in(original,std::ios::binary);std::string value((std::istreambuf_iterator<char>(in)),{});in.close();
    assert(value==std::string(message,sizeof(message))&&fs::file_size(orphan)==4);
    for(const auto& path:created)assert(fs::remove(path));
    assert(fs::remove(folder));
}
int main(int argc, char** argv)
{
    FilenameReservationChecks();
    using namespace DlssNr::EditDump;
    assert(Filename("nr-edit") == "nr-edit.dds");
    assert(Filename(" test.DDS  ") == "test.dds");
    for (auto name : {"", ".dds", "../test", "a/b", "a\\b", "C:test", "CON", "NUL.dds", "COM1.dds", "a*", ".hidden"})
        assert(!Filename(name));
    // Two odd-width rows with padding. Inspect an independent fixed DDS/DX10 layout,
    // and ensure negative/HDR data are unmodified and row padding never reaches the file.
    constexpr uint32_t w = 3, h = 2;
    float data[2][16] {};
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w * 4; ++x) data[y][x] = float(int(x + y * 12) - 7) * .5f;
    for (auto& row : data) for (unsigned x = 12; x < 16; ++x) row[x] = 12345;
    std::vector<char> bytes;
    assert(WriteRows(w, h, data, sizeof(data[0]), [&](const void* p, size_t n)
    {
        bytes.insert(bytes.end(), static_cast<const char*>(p), static_cast<const char*>(p) + n); return true;
    }));
    assert(bytes.size() == 148 + w * h * 16);
    auto word = [&](size_t offset) { uint32_t v; std::memcpy(&v, bytes.data() + offset, 4); return v; };
    assert(word(0) == 0x20534444 && word(4) == 124 && word(12) == h && word(16) == w && word(20) == w * 16);
    assert(word(76) == 32 && word(80) == 4 && word(84) == 0x30315844);
    assert(word(108) == 0x1000 && word(128) == 2 && word(132) == 3 && word(140) == 1);
    for (unsigned y = 0; y < h; ++y) assert(std::memcmp(bytes.data() + 148 + y * w * 16, data[y], w * 16) == 0);
    const auto fail = [](const void*, size_t) { return false; };
    assert(!WriteRows(w, h, data, sizeof(data[0]), fail));
    assert(!WriteRows(0, h, data, sizeof(data[0]), fail));
    assert(!WriteRows(w, h, data, 4, fail));
    assert(!WriteRows(w, h, nullptr, sizeof(data[0]), fail));
    for (auto format : {Format::R32Float, Format::Rg32Float})
    {
        bytes.clear();
        const unsigned stride = format == Format::R32Float ? 4u : 8u;
        assert(WriteRows(w, h, data, sizeof(data[0]), [&](const void* p, size_t n)
        {
            bytes.insert(bytes.end(), static_cast<const char*>(p), static_cast<const char*>(p) + n); return true;
        }, format));
        assert(bytes.size() == 148 + w * h * stride);
        assert(word(20) == w * stride);
        assert(word(128) == (stride == 4 ? 41u : 16u));
        for (unsigned y = 0; y < h; ++y)
            assert(std::memcmp(bytes.data() + 148 + y * w * stride, data[y], w * stride) == 0);
    }
    assert(!WriteRows(w, h, data, sizeof(data[0]), fail, Format(999)));
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    const auto oldLocale = std::locale();
    std::locale::global(std::locale(oldLocale, new Comma));
    Metadata metadata;
    metadata.gain = 2.5f; metadata.gainAtRequest = 2.5f; metadata.width = 3; metadata.height = 2;
    metadata.frame = 1234567890123ull; metadata.invertedDepth = true;
    metadata.gameTrimControl = std::numeric_limits<float>::quiet_NaN();
    DlssNr::GainPreview::Status shown;
    shown.valid = true; shown.paused = false; shown.smoothed = 3; shown.raw = 3.25f;
    shown.measuredAt = 10; shown.measurements = 20; shown.motionPercent = 5; shown.changedPercent = 4;
    metadata.previewAtRequest = SnapshotPreview(shown, 10.5);
    shown.smoothed = 4; shown.raw = std::numeric_limits<float>::quiet_NaN();
    metadata.previewAtCapture = SnapshotPreview(shown, 10.75);
    metadata.previewEnabledAtRequest = metadata.previewEnabledAtCapture = true;
    metadata.privatePresetAtRequest = metadata.privatePreset = 11;
    metadata.requestDelaySeconds = .25;
    metadata.manualGain = 1; metadata.effectiveGainAtRequest = 2.5f;
    metadata.autoGainEnabledAtRequest = metadata.autoGainEnabledAtCapture = metadata.autoGainControlledAtCapture = true;
    metadata.autoGainChanges = 12;
    assert(metadata.previewAtRequest.suggestedGain == 3 && metadata.previewAtCapture.suggestedGain == 4);
    assert(metadata.previewAtRequest.ageSeconds == .5 && metadata.previewAtCapture.ageSeconds == .75);
    const auto text = MetadataJson(metadata);
    std::locale::global(oldLocale);
    assert(text.find("\"positive_gain\": 2.5") != std::string::npos);
    assert(text.find("\"frame\": 1234567890123") != std::string::npos);
    assert(text.find("\"depth_inverted\": true") != std::string::npos);
    assert(text.find("\"nr_game_trim_control\": null") != std::string::npos);
    assert(text.find("private_input_pixels") != std::string::npos);
    assert(text.find("\"manual_positive_gain\": 1") != std::string::npos);
    assert(text.find("\"effective_positive_gain_at_request\": 2.5") != std::string::npos);
    assert(text.find("\"auto_positive_gain_controlled_capture\": true") != std::string::npos);
    assert(text.find("\"schema\": 2") != std::string::npos);
    assert(text.find("\"preview_request_suggested_gain\": 3") != std::string::npos);
    assert(text.find("\"preview_request_raw_gain\": 3.25") != std::string::npos);
    assert(text.find("\"preview_capture_suggested_gain\": 4") != std::string::npos);
    assert(text.find("\"preview_capture_raw_gain\": null") != std::string::npos);
    assert(text.find("\"preview_request_measurement_age_seconds\": 0.5") != std::string::npos);
    assert(text.find("\"guides_included\": false") != std::string::npos);
    metadata.depthTransport.weightedGain = true; metadata.depthTransport.additive = true;
    metadata.depthTransport.fullRange = false; metadata.depthTransport.nearLimit = .125f;
    metadata.depthTransport.farLimit = .75f; metadata.depthTransport.bias = .25f;
    metadata.originalGuides = true; metadata.depthIncluded = true;
    metadata.dlssDepthW = 1920; metadata.dlssMotionH = 1080; metadata.dlssMotionScaleX = .5f;
    const auto spatial = MetadataJson(metadata);
    assert(spatial.find("\"depth_weighted_positive_gain\": true") != std::string::npos);
    assert(spatial.find("\"depth_additive_brightness\": true") != std::string::npos);
    assert(spatial.find("\"depth_brightness_full_range\": false") != std::string::npos);
    assert(spatial.find("\"depth_brightness_near\": 0.125") != std::string::npos);
    assert(spatial.find("\"private_depth_allocation_width\": 1920") != std::string::npos);
    assert(spatial.find("\"private_motion_scale_x\": 0.5") != std::string::npos);
    assert(spatial.find("\"captured_guides_match_private_dlss_inputs\": false") != std::string::npos);
    metadata.previewSettings.method = DlssNr::GainPreview::DepthBoundary;
    metadata.previewSettings.boundaryTarget = .15f;
    shown.method=DlssNr::GainPreview::DepthBoundary; shown.boundaryPairs=123;
    shown.boundaryBase=.02f; shown.boundarySlope=.01f; shown.boundaryTarget=.15f;
    metadata.previewAtCapture=SnapshotPreview(shown,11);
    const auto methodJson=MetadataJson(metadata);
    assert(methodJson.find("\"gain_preview_method_at_capture\": 1")!=std::string::npos);
    assert(methodJson.find("\"preview_capture_boundary_pairs\": 123")!=std::string::npos);
    assert(methodJson.find("\"preview_capture_method\": 1")!=std::string::npos);
    metadata.previewSettings.downSeconds=1.25f;
    metadata.previewSettings.boundaryUseMotion=false;metadata.previewSettings.boundaryMotionGate=false;
    metadata.previewSettings.boundaryMinPairs=16;metadata.cnnPresetOverride=3;metadata.transformerPresetOverride=10;
    const auto controls=MetadataJson(metadata);
    assert(controls.find("\"gain_preview_fast_down_seconds\": 1.25")!=std::string::npos);
    assert(controls.find("\"gain_preview_boundary_use_motion\": false")!=std::string::npos);
    assert(controls.find("\"gain_preview_boundary_motion_gate\": false")!=std::string::npos);
    assert(controls.find("\"gain_preview_boundary_min_pairs\": 16")!=std::string::npos);
    assert(controls.find("\"private_cnn_preset_override\": 3")!=std::string::npos);
    assert(controls.find("\"private_transformer_preset_override\": 10")!=std::string::npos);
    const auto noPreview = MetadataJson({});
    assert(noPreview.find("\"preview_request_suggested_gain\": null") != std::string::npos);
    assert(noPreview.find("\"preview_request_measurement_age_seconds\": null") != std::string::npos);
    if (argc > 1 && std::string(argv[1]) == "--metadata") std::cout << text;
}
