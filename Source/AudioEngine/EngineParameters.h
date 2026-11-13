#pragma once
// Plain-value snapshot of every engine parameter (read once per block on the audio thread)
// and the single table that defines parameter IDs, names and ranges for the plugin.

#include <array>
#include <string>
#include <vector>
#include "NAMMetadata/CaptureMetadata.h"
#include "HarmonicEngine/HarmonicEngine.h"
#include "ParametricEQ.h"

namespace namvx
{
/** Number of NAM loader slots in a blend (Tone Matrix columns). */
constexpr int kBlendSlots = 18;

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
    int phaseMode = 2;      // 0 off, 1 time-aligned, 2 phase-matched
    std::array<bool, kBlendSlots> slotInvert {}; // blends: user polarity flip per slot
    EqSettings eq;
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
    inline const char* blendMix[kBlendSlots] = { "blendMix1", "blendMix2", "blendMix3", "blendMix4", "blendMix5", "blendMix6", "blendMix7", "blendMix8", "blendMix9", "blendMix10", "blendMix11", "blendMix12", "blendMix13", "blendMix14", "blendMix15", "blendMix16", "blendMix17", "blendMix18" };
    // toneSource[band][model]: band 0 resonance, 1 bass, 2 middle, 3 treble, 4 presence
    inline const char* toneSource[5][kBlendSlots] = {
        { "toneRes1", "toneRes2", "toneRes3", "toneRes4", "toneRes5", "toneRes6", "toneRes7", "toneRes8", "toneRes9", "toneRes10", "toneRes11", "toneRes12", "toneRes13", "toneRes14", "toneRes15", "toneRes16", "toneRes17", "toneRes18" },
        { "toneBass1", "toneBass2", "toneBass3", "toneBass4", "toneBass5", "toneBass6", "toneBass7", "toneBass8", "toneBass9", "toneBass10", "toneBass11", "toneBass12", "toneBass13", "toneBass14", "toneBass15", "toneBass16", "toneBass17", "toneBass18" },
        { "toneMid1", "toneMid2", "toneMid3", "toneMid4", "toneMid5", "toneMid6", "toneMid7", "toneMid8", "toneMid9", "toneMid10", "toneMid11", "toneMid12", "toneMid13", "toneMid14", "toneMid15", "toneMid16", "toneMid17", "toneMid18" },
        { "toneTreb1", "toneTreb2", "toneTreb3", "toneTreb4", "toneTreb5", "toneTreb6", "toneTreb7", "toneTreb8", "toneTreb9", "toneTreb10", "toneTreb11", "toneTreb12", "toneTreb13", "toneTreb14", "toneTreb15", "toneTreb16", "toneTreb17", "toneTreb18" },
        { "tonePres1", "tonePres2", "tonePres3", "tonePres4", "tonePres5", "tonePres6", "tonePres7", "tonePres8", "tonePres9", "tonePres10", "tonePres11", "tonePres12", "tonePres13", "tonePres14", "tonePres15", "tonePres16", "tonePres17", "tonePres18" } };
    inline const char* matrixAuto = "matrixAuto";
    inline const char* phaseMode = "phaseMode";
    inline const char* eqOn = "eqOn";
    inline const char* eqPost = "eqPostCab";
    inline const char* eqOut = "eqOutput";
    // per band: [band] = { on, freq, gain, q, slope } ("" where the band has no such control)
    inline const char* eqBand[7][5] = {
        { "eqLcOn", "eqLcFreq", "", "", "eqLcSlope" },
        { "eqLsOn", "eqLsFreq", "eqLsGain", "eqLsQ", "" },
        { "eqB1On", "eqB1Freq", "eqB1Gain", "eqB1Q", "" },
        { "eqB2On", "eqB2Freq", "eqB2Gain", "eqB2Q", "" },
        { "eqB3On", "eqB3Freq", "eqB3Gain", "eqB3Q", "" },
        { "eqHsOn", "eqHsFreq", "eqHsGain", "eqHsQ", "" },
        { "eqHcOn", "eqHcFreq", "", "", "eqHcSlope" } };
    inline const char* eqBandName[7] = { "Low Cut", "Low Shelf", "Bell 1", "Bell 2", "Bell 3", "High Shelf", "High Cut" };
    inline const char* slotInvert[kBlendSlots] = { "slotInv1", "slotInv2", "slotInv3", "slotInv4", "slotInv5", "slotInv6", "slotInv7", "slotInv8", "slotInv9", "slotInv10", "slotInv11", "slotInv12", "slotInv13", "slotInv14", "slotInv15", "slotInv16", "slotInv17", "slotInv18" };
    inline const char* toneBandName[5] = { "Resonance", "Bass", "Middle", "Treble", "Presence" };
} // namespace pid
} // namespace namvx
