from pathlib import Path
import sys


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: adapt_autoexposure_patchers_for_presr.py <patch-tools-dir>")

    tools = Path(sys.argv[1]).resolve()
    path = tools / "apply_dlssnr_autoexposure.py"
    script = path.read_text(encoding="utf-8")

    # The PreSR/Multipass fork has the same pre-patch controls, but some menu
    # labels/help text were shortened after the AutoExposure patcher was authored.
    # Only the patcher's OLD anchors are changed here. Its replacement text and
    # exact-match safety checks stay untouched.
    old = """r'''            static const char* sourceNames[] = { \"Paper white only\", \"The game's own exposure\",\n                                                 \"A buffer the scan found\" };'''"""
    new = """r'''            static const char* sourceNames[] = { \"Manual paper white\", \"Game exposure\",\n                                                 \"Scanned exposure (experimental)\" };'''"""
    script = replace_once(script, old, new, "menu source-name anchor in patcher")

    old = """r'''            HelpMarker(\"Where the number that divides the frame comes from.\"\n                           \"\\n\\nPaper white only -- the slider below and nothing else. Right for a\"\n                           \"\\ngame whose exposure never moves, wrong the moment it does: one\"\n                           \"\\nconstant cannot serve a cave and a field.\"\n                           \"\\n\\nThe game's own exposure -- read from the texture the game hands\"\n                           \"\\nthe upscaler. The best source there is, because it is decided\"\n                           \"\\nupstream and nothing this pass does can move it. Not every game\"\n                           \"\\nsupplies one.\"\n                           \"\\n\\nA buffer the scan found -- for games that compute an exposure and\"\n                           \"\\nnever pass it on. A GUESS: candidates are matched by shape, and in\"\n                           \"\\nGTA V the best one tracks the real exposure but at its own scale,\"\n                           \"\\nwhich the anchor's ratio cancels. Needs anchoring once, and checking\"\n                           \"\\nafterwards.\");'''"""
    new = """r'''            HelpMarker(\"Manual: use Paper white. Game exposure: use exposure supplied by the game.\\nScanned exposure: estimate it from game buffers; requires calibration and may select the wrong buffer.\");'''"""
    script = replace_once(script, old, new, "menu help anchor in patcher")

    old = """    \"                else if (!haveExposure)\\n\"\n    \"                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),\\n\"\n    \"                                       \\\"This game supplies no exposure -- paper white is in use. Try \\\"\\n\"\n    \"                                       \\\"the scan instead.\\\");\\n\","""
    new = """    \"                else if (!haveExposure)\\n\"\n    \"                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),\\n\"\n    \"                                       \\\"No game exposure available. Using manual paper white.\\\");\\n\","""
    script = replace_once(script, old, new, "no-game-exposure anchor in patcher")

    old = """            HelpMarker(\"A multiplier on the exposure the game supplied. 1.00x takes its number\"\n                           \"\\nexactly, and that is the right answer here.\"\n                           \"\\n\\nThis is not a fudge factor. If a game needs the trim far from 1 to look\"\n                           \"\\nright, that is evidence the exposure being read is wrong for that game,\"\n                           \"\\nnot that the game wants trimming. Somewhere around 0.8 to 1.25 is honest\"\n                           \"\\ntuning; reaching for 4 means something upstream is broken and the trim is\"\n                           \"\\nhiding it.\"\n                           \"\\n\\nYour manual paper white is kept separately and comes back untouched if\"\n                           \"\\nyou switch the option above off.\");"""
    new = """            HelpMarker(\"Multiply the white point derived from game exposure. 1 = no adjustment.\");"""
    script = replace_once(script, old, new, "game-trim help anchor in patcher")

    # PreSR/Multipass already owns modes 5..11. Preserve those modes and place
    # AutoExposure at the next free value. Both the C++ enum and HLSL branch are
    # adapted together so the dispatch contract remains identical.
    old = '''    "    DlssNrMode_Meter = 3,      // the exposure texture -> tile (0,0), for the white point\\n"\n    "    DlssNrMode_Calibrate = 4   // the untouched frame -> a grid of tile peak luminances\\n",'''
    new = '''    "    DlssNrMode_Meter = 3,      // the exposure texture -> tile (0,0), for the white point\\n"\n    "    DlssNrMode_Calibrate = 4,  // the untouched frame -> a grid of tile peak luminances\\n"\n    "    DlssNrMode_EncodeResidual = 5, // NR-composed minus original; signed difference encoded around 0.5\\n"\n    "    DlssNrMode_ApplyResidual = 6,  // decode private DLSS result and add to clean SR output\\n"\n    "    DlssNrMode_UnitExposure = 7,   // constant exposure for the private DLSS feature\\n"\n    "    DlssNrMode_NormalizeMotion = 8, // current-to-previous motion in normalized image coordinates\\n"\n    "    DlssNrMode_ComposeMotion = 9, // compose two successive fields at the displaced coordinate\\n"\n    "    DlssNrMode_ApplyInterpolatedResidual = 10, // t4: R8_UNORM NVIDIA suppression flag\\n"\n    "    DlssNrMode_ZeroMotion = 11 // private reset-only NR/SR guide, never passed to residual FG\\n",'''
    script = replace_once(script, old, new, "mode-enum old anchor in patcher")

    old = '''    "    DlssNrMode_Meter = 3,      // exposure copy, or frame -> 64x64 tile luminance means\\n"\n    "    DlssNrMode_Calibrate = 4,  // the untouched frame -> a grid of tile peak luminances\\n"\n    "    DlssNrMode_AutoExposure = 5 // 64x64 tile means -> NVIDIA-style 1x1 exposure texture\\n",'''
    new = '''    "    DlssNrMode_Meter = 3,      // exposure copy, or frame -> 64x64 tile luminance means\\n"\n    "    DlssNrMode_Calibrate = 4,  // the untouched frame -> a grid of tile peak luminances\\n"\n    "    DlssNrMode_EncodeResidual = 5, // NR-composed minus original; signed difference encoded around 0.5\\n"\n    "    DlssNrMode_ApplyResidual = 6,  // decode private DLSS result and add to clean SR output\\n"\n    "    DlssNrMode_UnitExposure = 7,   // constant exposure for the private DLSS feature\\n"\n    "    DlssNrMode_NormalizeMotion = 8, // current-to-previous motion in normalized image coordinates\\n"\n    "    DlssNrMode_ComposeMotion = 9, // compose two successive fields at the displaced coordinate\\n"\n    "    DlssNrMode_ApplyInterpolatedResidual = 10, // t4: R8_UNORM NVIDIA suppression flag\\n"\n    "    DlssNrMode_ZeroMotion = 11, // private reset-only NR/SR guide, never passed to residual FG\\n"\n    "    DlssNrMode_AutoExposure = 12 // 64x64 tile means -> NVIDIA-style 1x1 exposure texture\\n",'''
    script = replace_once(script, old, new, "mode-enum replacement in patcher")
    script = replace_once(script, "    if (gMode == 5)\n", "    if (gMode == 12)\n", "HLSL AutoExposure mode number")

    # The target C++ constants already have four residual-history scalars after
    # EnvironmentColour. Append AutoExposure after them so none of the PreSR/
    # Multipass offsets move. The main HLSL shader declares four matching padding
    # scalars before its AutoExposure fields; the residual shader keeps using them.
    old = '''    "    float DebugScale;\\n"\n    "};\\n",'''
    new = '''    "    uint32_t ResidualMotionBaseY;\\n"\n    "};\\n",'''
    script = replace_once(script, old, new, "C++ constants old tail anchor in patcher")

    old = '''    "    float DebugScale;\\n"\n    "\\n"\n    "    // Auto-exposure only. Appended so every existing field keeps its byte offset for Vulkan.\\n"\n    "    float PreExposure;\\n"\n    "    uint32_t ExposureSourceWidth;\\n"\n    "    uint32_t ExposureSourceHeight;\\n"\n    "    uint32_t MeterCopiesExposure;\\n"\n    "};\\n",'''
    new = '''    "    uint32_t ResidualMotionBaseY;\\n"\n    "\\n"\n    "    // Auto-exposure only. Appended after the PreSR/Multipass residual fields.\\n"\n    "    float PreExposure;\\n"\n    "    uint32_t ExposureSourceWidth;\\n"\n    "    uint32_t ExposureSourceHeight;\\n"\n    "    uint32_t MeterCopiesExposure;\\n"\n    "};\\n",'''
    script = replace_once(script, old, new, "C++ constants replacement in patcher")

    old = '''    "    uint  gTransfer;     // 0 classic, 1 matched residual -- how a below-size model comes back\\n"\n    "    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves\\n"\n    "};\\n",'''
    new = '''    "    float gEnvironmentColour;\\n"\n    "};\\n",'''
    script = replace_once(script, old, new, "HLSL constants old tail anchor in patcher")

    old = '''    "    uint  gTransfer;     // 0 classic, 1 matched residual -- how a below-size model comes back\\n"\n    "    float gDebugScale;   // what the debug views are scaled by, held still while the meter moves\\n"\n    "\\n"\n    "    // Auto exposure. Appended so the layout of every existing field is unchanged.\\n"\n    "    float gPreExposure;\\n"\n    "    uint  gExposureSourceWidth;\\n"\n    "    uint  gExposureSourceHeight;\\n"\n    "    uint  gMeterCopiesExposure;\\n"\n    "};\\n",'''
    new = '''    "    float gEnvironmentColour;\\n"\n    "    // Match the four PreSR/Multipass-only scalars present in DlssNrConstants.\\n"\n    "    float gResidualPadding0;\\n"\n    "    uint  gResidualPadding1;\\n"\n    "    uint  gResidualPadding2;\\n"\n    "    uint  gResidualPadding3;\\n"\n    "\\n"\n    "    // Auto exposure. Appended without moving any existing target-field offset.\\n"\n    "    float gPreExposure;\\n"\n    "    uint  gExposureSourceWidth;\\n"\n    "    uint  gExposureSourceHeight;\\n"\n    "    uint  gMeterCopiesExposure;\\n"\n    "};\\n",'''
    script = replace_once(script, old, new, "HLSL constants replacement in patcher")

    path.write_text(script, encoding="utf-8", newline="\n")
    print(f"Adapted pinned AutoExposure patchers for PreSR/Multipass base: {path}")


if __name__ == "__main__":
    main()
