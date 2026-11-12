#pragma once
// Plain-value snapshot of every engine parameter (read once per block on the audio thread)
// and the single table that defines parameter IDs, names and ranges for the plugin.

#include <string>
#include <vector>
#include "NAMMetadata/CaptureMetadata.h"
#include "HarmonicEngine/HarmonicEngine.h"

namespace namified
{
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
} // namespace pid
} // namespace namified
