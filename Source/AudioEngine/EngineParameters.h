#pragma once
// Plain-value snapshot of every engine parameter (read once per block on the audio thread)
// and the single table that defines parameter IDs, names and ranges for the plugin.

#include <array>
#include <string>
#include <vector>
#include "NAMMetadata/CaptureMetadata.h"
#include "HarmonicEngine/HarmonicEngine.h"

namespace namvx
{
/** Number of NAM loader slots in a blend (Tone Matrix columns). */
constexpr int kBlendSlots = 8;

struct SlotParams
{
    bool enabled = true;
    double levelDb = 0, pan = 0, delayMs = 0;
    bool invert = false;
};

struct EngineParameters
{
    KnobCoords knobs { 5, 5, 5, 5, 5, 5, 5, 5 };
    int channel = 0;
    double inputDb = 0.0, outputDb = 0.0;

    bool preTubeOn = false;
    double preTubeDrive = 0.3, preTubeMix = 1.0;

    bool powerAmpOn = false;
    int tubeType = 0;
    double bias = 0.5, sag = 0.4, nfb = 0.5, transformer = 0.3, mismatch = 0.15, speaker = 0.5;

    bool harmonicsOn = true;
    HarmonicEngine::Settings harm;

    bool cabOn = true, cabStereo = true, cabAutoAlign = true;
    double cabBlend = 0.5, cabLevelDb = 0.0;
    SlotParams ir1, ir2;

    bool matrixAuto = true; // blends: amp knobs drive the tone matrix automatically
    std::array<double, kBlendSlots> blendMix { 1, 1, 1, 1, 1, 1, 1, 1 }; // blend amps: gain source = audio mix of the slots
    // blend amps: tone source per band [resonance, bass, middle, treble, presence][model]
    std::array<std::array<double, kBlendSlots>, 5> toneSource = [] { std::array<std::array<double, kBlendSlots>, 5> t {}; for (auto& r : t) r.fill (1.0); return t; }();
};

enum class ParamKind { Float, Bool, Choice };

struct ParamSpec
{
    std::string id, name;
    ParamKind kind;
    double min, max, def, step, skewCentre; // skewCentre < 0: linear
    std::vector<std::string> choices;
    std::string unit;
};

/** All automatable parameters, in display order. */
const std::vector<ParamSpec>& parameterSpecs();

namespace pid
{
    // amp knobs use controlId(Control) ("gain", "bass", ...)
    inline const char* channel = "channel";
    inline const char* input = "inputDb";
    inline const char* output = "outputDb";
    inline const char* preTubeOn = "preTubeOn";
    inline const char* preTubeDrive = "preTubeDrive";
    inline const char* preTubeMix = "preTubeMix";
    inline const char* powerAmpOn = "powerAmpOn";
    inline const char* tubeType = "tubeType";
    inline const char* bias = "bias";
    inline const char* sag = "sag";
    inline const char* nfb = "nfb";
    inline const char* transformer = "transformer";
    inline const char* mismatch = "mismatch";
    inline const char* speaker = "speaker";
    inline const char* harmOn = "harmOn";
    inline const char* harmAmount = "harmAmount";
    inline const char* harmEvenOdd = "harmEvenOdd";
    inline const char* harmTouch = "harmTouch";
    inline const char* harmAttack = "harmAttack";
    inline const char* harmLowDef = "harmLowDef";
    inline const char* harmHighDetail = "harmHighDetail";
    inline const char* cabOn = "cabOn";
    inline const char* cabStereo = "cabStereo";
    inline const char* cabAutoAlign = "cabAutoAlign";
    inline const char* cabBlend = "cabBlend";
    inline const char* cabLevel = "cabLevel";
    inline const char* ir1On = "ir1On";
    inline const char* ir1Level = "ir1Level";
    inline const char* ir1Pan = "ir1Pan";
    inline const char* ir1Invert = "ir1Invert";
    inline const char* ir1Delay = "ir1Delay";
    inline const char* ir2On = "ir2On";
    inline const char* ir2Level = "ir2Level";
    inline const char* ir2Pan = "ir2Pan";
    inline const char* ir2Invert = "ir2Invert";
    inline const char* ir2Delay = "ir2Delay";
    inline const char* blendMix[kBlendSlots] = { "blendMix1", "blendMix2", "blendMix3", "blendMix4", "blendMix5", "blendMix6", "blendMix7", "blendMix8" };
    // toneSource[band][model]: band 0 resonance, 1 bass, 2 middle, 3 treble, 4 presence
    inline const char* toneSource[5][kBlendSlots] = {
        { "toneRes1", "toneRes2", "toneRes3", "toneRes4", "toneRes5", "toneRes6", "toneRes7", "toneRes8" },
        { "toneBass1", "toneBass2", "toneBass3", "toneBass4", "toneBass5", "toneBass6", "toneBass7", "toneBass8" },
        { "toneMid1", "toneMid2", "toneMid3", "toneMid4", "toneMid5", "toneMid6", "toneMid7", "toneMid8" },
        { "toneTreb1", "toneTreb2", "toneTreb3", "toneTreb4", "toneTreb5", "toneTreb6", "toneTreb7", "toneTreb8" },
        { "tonePres1", "tonePres2", "tonePres3", "tonePres4", "tonePres5", "tonePres6", "tonePres7", "tonePres8" } };
    inline const char* matrixAuto = "matrixAuto";
    inline const char* toneBandName[5] = { "Resonance", "Bass", "Middle", "Treble", "Presence" };
} // namespace pid
} // namespace namvx
