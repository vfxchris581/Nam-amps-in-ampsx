#include "EngineParameters.h"

namespace namified
{
const std::vector<ParamSpec>& parameterSpecs()
{
    static const std::vector<ParamSpec> specs = [] {
        std::vector<ParamSpec> s;
        auto f = [&] (std::string id, std::string name, double mn, double mx, double def, std::string unit = {}, double skew = -1)
        { s.push_back ({ std::move (id), std::move (name), ParamKind::Float, mn, mx, def, 0.0, skew, {}, std::move (unit) }); };
        auto b = [&] (std::string id, std::string name, bool def)
        { s.push_back ({ std::move (id), std::move (name), ParamKind::Bool, 0, 1, def ? 1.0 : 0.0, 1, -1, {}, {} }); };
        auto c = [&] (std::string id, std::string name, std::vector<std::string> choices, int def)
        { s.push_back ({ std::move (id), std::move (name), ParamKind::Choice, 0, (double) choices.size() - 1, (double) def, 1, -1, std::move (choices), {} }); };

        for (int k = 0; k < kNumControls; ++k)
            f (controlId ((Control) k), controlName ((Control) k), 0.0, 10.0, 5.0);
        c (pid::channel, "Channel", { "1", "2", "3", "4", "5", "6" }, 0);
        f (pid::input, "Input", -24.0, 24.0, 0.0, "dB");
        f (pid::output, "Output", -36.0, 12.0, 0.0, "dB");

        b (pid::preTubeOn, "12AX7 Stage", false);
        f (pid::preTubeDrive, "Tube Drive", 0.0, 1.0, 0.3);
        f (pid::preTubeMix, "Tube Mix", 0.0, 1.0, 1.0);

        b (pid::powerAmpOn, "Power Amp", false);
        c (pid::tubeType, "Power Tubes", { "EL34", "6L6" }, 0);
        f (pid::bias, "Bias", 0.0, 1.0, 0.5);
        f (pid::sag, "Sag", 0.0, 1.0, 0.4);
        f (pid::nfb, "Negative Feedback", 0.0, 1.0, 0.5);
        f (pid::transformer, "Transformer", 0.0, 1.0, 0.3);
        f (pid::mismatch, "Tube Mismatch", 0.0, 1.0, 0.15);
        f (pid::speaker, "Speaker Interaction", 0.0, 1.0, 0.5);

        b (pid::harmOn, "Harmonic Engine", true);
        f (pid::harmAmount, "Harmonics", 0.0, 1.0, 0.2);
        f (pid::harmEvenOdd, "Even / Odd", 0.0, 1.0, 0.5);
        f (pid::harmTouch, "Touch", 0.0, 1.0, 0.5);
        f (pid::harmAttack, "Attack Preserve", 0.0, 1.0, 0.6);
        f (pid::harmLowDef, "Low Definition", 0.0, 1.0, 0.5);
        f (pid::harmHighDetail, "High Detail", 0.0, 1.0, 0.3);

        b (pid::cabOn, "Cabinet", true);
        b (pid::cabStereo, "Stereo Cab", true);
        b (pid::cabAutoAlign, "Auto Phase Align", true);
        f (pid::cabBlend, "IR Blend", 0.0, 1.0, 0.5);
        f (pid::cabLevel, "Cab Level", -24.0, 12.0, 0.0, "dB");
        b (pid::ir1On, "IR 1 On", true);
        f (pid::ir1Level, "IR 1 Level", -24.0, 12.0, 0.0, "dB");
        f (pid::ir1Pan, "IR 1 Pan", -1.0, 1.0, 0.0);
        b (pid::ir1Invert, "IR 1 Polarity", false);
        f (pid::ir1Delay, "IR 1 Delay", 0.0, 10.0, 0.0, "ms");
        b (pid::ir2On, "IR 2 On", true);
        f (pid::ir2Level, "IR 2 Level", -24.0, 12.0, 0.0, "dB");
        f (pid::ir2Pan, "IR 2 Pan", -1.0, 1.0, 0.0);
        b (pid::ir2Invert, "IR 2 Polarity", false);
        f (pid::ir2Delay, "IR 2 Delay", 0.0, 10.0, 0.0, "ms");
        return s;
    }();
    return specs;
}
} // namespace namified
