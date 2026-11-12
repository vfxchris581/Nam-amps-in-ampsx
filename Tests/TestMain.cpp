// Nam VX automated tests: metadata, grouping, channels, interpolation, NAM inference,
// resampling, analysis, tube DSP, harmonic engine, oversampling/aliasing, dual IR,
// end-to-end signal chain (controls really change the sound), click-free switching, presets.

#include <filesystem>
#include <fstream>
#include <thread>
#include <json.hpp>
#include "TestHelpers.h"

#include "NAMMetadata/CaptureMetadata.h"
#include "NAMEngine/NamModel.h"
#include "NAMEngine/SincResampler.h"
#include "ProfileAnalyzer/ProfileAnalyzer.h"
#include "ProfileInterpolation/ProfileInterpolator.h"
#include "ProfileInterpolation/CaptureBlender.h"
#include "AmplifierEngine/AmpRuntime.h"
#include "ChannelManager/ChannelManager.h"
#include "PresetManager/AmpLibrary.h"
#include "PresetManager/PresetManager.h"
#include "TubeCircuitDSP/TriodeStage.h"
#include "TubeCircuitDSP/PowerAmpStage.h"
#include "HarmonicEngine/HarmonicEngine.h"
#include "Convolution/DualCabinet.h"
#include "AudioEngine/SignalChain.h"

namespace fs = std::filesystem;
using namespace namvx;

static fs::path examples() { return fs::path (NAMVX_EXAMPLE_MODELS); }

static fs::path tempDir (const std::string& name)
{
    auto d = fs::temp_directory_path() / ("namvx_tests_" + name);
    fs::remove_all (d);
    fs::create_directories (d);
    return d;
}

/** Writes a copy of an example model with replaced metadata. */
static fs::path makeCapture (const fs::path& dir, const std::string& fileName, const std::string& make,
                             const std::string& model, const std::string& name = {},
                             const std::string& source = "wavenet.nam")
{
    std::ifstream in (examples() / source);
    nlohmann::json j; in >> j;
    if (! j.contains ("metadata") || ! j["metadata"].is_object()) j["metadata"] = nlohmann::json::object();
    auto& md = j["metadata"];
    if (make.empty()) md.erase ("gear_make"); else md["gear_make"] = make;
    if (model.empty()) md.erase ("gear_model"); else md["gear_model"] = model;
    md["name"] = name.empty() ? fileName : name;
    const auto p = dir / (fileName + ".nam");
    std::ofstream out (p); out << j.dump();
    return p;
}

// --------------------------------------------------------------------------------------------
TEST (metadata_knob_and_channel_tokens)
{
    CaptureMetadata m;
    parseSettingsFromText ("5150 Lead G7 B4 M3.5 T8 P6 R2 MV3", m);
    CHECK (m.knobs[(size_t) Control::Gain] == 7.0);
    CHECK (m.knobs[(size_t) Control::Bass] == 4.0);
    CHECK (m.knobs[(size_t) Control::Middle] == 3.5);
    CHECK (m.knobs[(size_t) Control::Treble] == 8.0);
    CHECK (m.knobs[(size_t) Control::Presence] == 6.0);
    CHECK (m.knobs[(size_t) Control::Resonance] == 2.0);
    CHECK (m.knobs[(size_t) Control::Master] == 3.0);
    CHECK (m.channelHint == "Lead");

    CaptureMetadata a;
    parseSettingsFromText ("JCM800_ch2_gain=65_treble_7_bass-4", a);
    CHECK (a.channelNumberHint == 2);
    CHECK (std::abs (a.knobs[(size_t) Control::Gain] - 6.5) < 1e-9); // 0..100 scale normalised
    CHECK (a.knobs[(size_t) Control::Treble] == 7.0);
    CHECK (a.knobs[(size_t) Control::Bass] == 4.0);

    CaptureMetadata v; // "v2" is a version, never a volume
    parseSettingsFromText ("Microtubes 900 v2", v);
    for (double k : v.knobs) CHECK (! isKnown (k));
}

TEST (metadata_from_nam_file)
{
    const auto m = readCaptureMetadata (examples() / "lstm.nam");
    CHECK (m.manufacturer == "Darkglass Electronics");
    CHECK (m.model == "Microtubes 900 v2");
    CHECK (m.identityFromMetadata);
    CHECK (m.loudnessDb.has_value());
    CHECK (m.architecture == "LSTM");

    const auto dir = tempDir ("meta");
    const auto p = makeCapture (dir, "Plexi Super Lead Treble 6 Gain 9", "", "");
    const auto u = readCaptureMetadata (p);
    CHECK (! u.hasIdentity());
    CHECK (u.knobs[(size_t) Control::Gain] == 9.0);
    CHECK (u.suggestedManufacturer == "Plexi");
    CHECK (u.knobs[(size_t) Control::Treble] == 6.0);
}

TEST (library_groups_by_identity_and_never_mixes_models)
{
    const auto dir = tempDir ("group");
    makeCapture (dir, "Rect Clean G3", "Mesa/Boogie", "Dual Rectifier", "Rect ch1 G3");
    makeCapture (dir, "Rect Clean G6", "Mesa/Boogie", "Dual Rectifier", "Rect ch1 G6");
    makeCapture (dir, "Rect Modern G5", "MESA BOOGIE", "dual rectifier", "Rect ch3 G5");
    makeCapture (dir, "Marshall JCM800 G7", "Marshall", "JCM800 2203", "JCM800 G7");
    makeCapture (dir, "mystery amp G4", "", "", "mystery amp G4");

    AmpLibrary lib (tempDir ("group_lib"));
    const auto r = lib.importFiles ({ dir }, {}, false);
    CHECK (r.errors.empty());
    CHECK (r.imported == 4);
    CHECK (r.pending.size() == 1);
    CHECK (lib.getAmps().size() == 2); // Rectifier + JCM800; the unidentified capture waits for the user

    // Normalisation: "Mesa/Boogie" → "mesa boogie" == "MESA BOOGIE" → same amp
    int rectAmps = 0;
    for (auto& a : lib.getAmps()) if (a.identityKey == "mesa boogie|dual rectifier") ++rectAmps;
    CHECK (rectAmps == 1);

    for (auto& a : lib.getAmps())
    {
        if (a.identityKey == "mesa boogie|dual rectifier")
        {
            CHECK (a.channels.size() == 2);       // ch1, ch3
            CHECK (a.totalCaptures() == 3);
            CHECK (a.channels[0].name == "Channel 1");
            CHECK (a.channels[0].captures.size() == 2);
        }
        if (a.identityKey == "marshall|jcm800 2203")
            CHECK (a.channels.size() == 1);       // single-channel amp stays single-channel
    }

    // manual assignment of the unidentified capture creates a separate amp
    const auto id = lib.assignPending (r.pending[0], "Mystery", "Amp", {});
    CHECK (lib.find (id) != nullptr);
    CHECK (lib.getAmps().size() == 3);

    // re-importing the same files is detected as duplicates
    const auto r2 = lib.importFiles ({ dir }, {}, false);
    CHECK (r2.duplicates == 4);

    // persistence round trip
    AmpLibrary again (lib.getRoot());
    CHECK (again.getAmps().size() == lib.getAmps().size());
}

TEST (amp_json_roundtrip)
{
    AmpDefinition a;
    a.id = "abc"; a.name = "Test"; a.manufacturer = "M"; a.model = "X"; a.identityKey = "m|x";
    ChannelDefinition ch; ch.name = "Lead";
    CaptureEntry e; e.id = "1"; e.path = "/x.nam"; e.coords[2] = 7.5; e.source[2] = CoordSource::User; e.trimDb = -1.5;
    CaptureAnalysis an; an.valid = true; an.latencySamples = 12; an.bandsDb[3] = 2.5; e.analysis = an;
    ch.captures.push_back (e);
    a.channels.push_back (ch);
    const auto b = ampFromJson (toJson (a));
    CHECK (b.channels.size() == 1);
    CHECK (b.channels[0].captures[0].coords[2] == 7.5);
    CHECK (! isKnown (b.channels[0].captures[0].coords[0]));
    CHECK (b.channels[0].captures[0].source[2] == CoordSource::User);
    CHECK (b.channels[0].captures[0].analysis->latencySamples == 12);
    CHECK (b.channels[0].captures[0].analysis->bandsDb[3] == 2.5);
}

// --------------------------------------------------------------------------------------------
TEST (interpolator_weights_are_exact_normalised_and_continuous)
{
    std::vector<KnobCoords> c (4, unknownCoords());
    c[0][(size_t) Control::Gain] = 2; c[0][(size_t) Control::Treble] = 3;
    c[1][(size_t) Control::Gain] = 2; c[1][(size_t) Control::Treble] = 8;
    c[2][(size_t) Control::Gain] = 8; c[2][(size_t) Control::Treble] = 3;
    c[3][(size_t) Control::Gain] = 8; c[3][(size_t) Control::Treble] = 8;
    ProfileInterpolator ip;
    ip.setCaptures (c, { true, true, true, true });
    ip.setMaxActive (3);
    CHECK (ip.isCovered (Control::Gain) && ip.isCovered (Control::Treble) && ! ip.isCovered (Control::Bass));

    KnobCoords k { 5, 5, 5, 5, 5, 5, 5, 5 };
    k[(size_t) Control::Gain] = 8; k[(size_t) Control::Treble] = 8;
    auto r = ip.compute (k);
    CHECK (r.numActive == 1 && r.index[0] == 3 && r.weight[0] == 1.0);

    // Treble up favours the high-treble captures
    double prevHi = -1, maxJump = 0;
    std::array<double, 4> prevW {};
    for (int s = 0; s <= 200; ++s)
    {
        k[(size_t) Control::Gain] = 3.1;
        k[(size_t) Control::Treble] = 2.0 + 7.0 * s / 200.0;
        r = ip.compute (k);
        double sum = 0, hi = 0;
        std::array<double, 4> w {};
        for (int a = 0; a < r.numActive; ++a) { sum += r.weight[(size_t) a]; w[(size_t) r.index[(size_t) a]] = r.weight[(size_t) a]; }
        CHECK (std::abs (sum - 1.0) < 1e-9);
        hi = w[1] + w[3];
        if (prevHi >= 0) CHECK (hi >= prevHi - 1e-9); // monotonic in treble
        if (s > 0) for (int q = 0; q < 4; ++q) maxJump = std::max (maxJump, std::abs (w[(size_t) q] - prevW[(size_t) q]));
        prevHi = hi; prevW = w;
    }
    INFO ("max weight step across a 200-step treble sweep: %.4f", maxJump);
    CHECK (maxJump < 0.1); // continuous, even when the active set changes
}

// --------------------------------------------------------------------------------------------
TEST (sinc_resampler_round_trip)
{
    SincResampler up, down;
    up.prepare (44100, 48000, 512);
    down.prepare (48000, 44100, 600);
    SampleFifo mid, out;
    mid.allocate (1 << 16); out.allocate (1 << 16);
    auto x = tst::sine (1000.0, 44100, 44100, 0.5);
    std::vector<double> buf (600);
    for (size_t i = 0; i < x.size(); i += 512)
    {
        up.process (x.data() + i, (int) std::min<size_t> (512, x.size() - i), mid);
        size_t m = 0;
        while (mid.size() > 0 && m < buf.size()) buf[m++] = mid.pop();
        down.process (buf.data(), (int) m, out);
    }
    std::vector<double> y;
    while (out.size() > 0) y.push_back (out.pop());
    CHECK (y.size() > 40000);
    const double a = tst::tone (y, 10000, 16384, 1000.0, 44100);
    CHECK_MSG (std::abs (tst::db (a / 0.5)) < 0.05, "gain %.3f dB", tst::db (a / 0.5));
    // image / alias products must be far down
    double spur = 0;
    for (double f : { 3100.0, 7000.0, 12000.0, 19000.0 }) spur = std::max (spur, tst::tone (y, 10000, 16384, f, 44100));
    CHECK_MSG (tst::db (spur / 0.5) < -90.0, "spur %.1f dB", tst::db (spur / 0.5));
}

TEST (nam_model_runs_real_inference_at_any_host_rate)
{
    for (double sr : { 48000.0, 44100.0, 96000.0 })
    {
        NamModel m;
        m.load (examples() / "wavenet_a1_standard.nam");
        m.prepare (sr, 256);
        auto x = tst::sine (220.0, sr, (size_t) sr, 0.2);
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); i += 256) m.process (x.data() + i, y.data() + i, (int) std::min<size_t> (256, x.size() - i));
        CHECK (tst::allFinite (y));
        CHECK_MSG (tst::rms (y, y.size() / 2) > 1e-4, "sr %.0f rms %.2e", sr, tst::rms (y, y.size() / 2));
        CHECK ((sr == 48000.0) == (m.getResamplingLatency() == 0));
        INFO ("%.0f Hz host: resampling=%d latency=%d samples", sr, (int) m.isResampling(), m.getResamplingLatency());
    }
}

TEST (nam_loads_every_architecture)
{
    // Every model format NAM ships examples for, including the newer A2 / Slimmable files
    for (const char* f : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam", "A2.nam", "slimmable_container.nam",
                           "slimmable_wavenet.nam", "wavenet_a2_feature_test.nam", "wavenet_condition_dsp.nam" })
    {
        try
        {
            NamModel m;
            m.load (examples() / f);
            m.prepare (44100.0, 256);
            auto x = tst::sine (220.0, 44100.0, 22050, 0.2);
            std::vector<double> y (x.size());
            for (size_t i = 0; i < x.size(); i += 256) m.process (x.data() + i, y.data() + i, (int) std::min<size_t> (256, x.size() - i));
            CHECK_MSG (tst::allFinite (y) && tst::rms (y, y.size() / 2) > 1e-6, "%s produced no usable output", f);
            const auto meta = readCaptureMetadata (examples() / f);
            INFO ("%-28s %-18s ok", f, meta.architecture.c_str());
        }
        catch (const std::exception& e) { CHECK_MSG (false, "%s: %s", f, e.what()); }
    }
}

TEST (nam_resampled_output_matches_native)
{
    // The same model at 44.1 kHz host must sound like it does natively (fundamental level match).
    NamModel a, b;
    a.load (examples() / "wavenet_a1_standard.nam");
    b.load (examples() / "wavenet_a1_standard.nam");
    a.prepare (48000, 256);
    b.prepare (44100, 256);
    auto xa = tst::sine (220.0, 48000, 48000, 0.1), xb = tst::sine (220.0, 44100, 44100, 0.1);
    std::vector<double> ya (xa.size()), yb (xb.size());
    for (size_t i = 0; i < xa.size(); i += 256) a.process (xa.data() + i, ya.data() + i, (int) std::min<size_t> (256, xa.size() - i));
    for (size_t i = 0; i < xb.size(); i += 256) b.process (xb.data() + i, yb.data() + i, (int) std::min<size_t> (256, xb.size() - i));
    const double fa = tst::tone (ya, 24000, 16384, 220, 48000), fb = tst::tone (yb, 22050, 16384, 220, 44100);
    CHECK_MSG (std::abs (tst::db (fa / fb)) < 0.25, "native %.4f vs resampled %.4f", fa, fb);
}

TEST (profile_analyzer_measures_captures)
{
    for (const char* f : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam" })
    {
        NamModel m;
        m.load (examples() / f);
        const auto a = ProfileAnalyzer::analyse (m);
        CHECK (a.valid);
        CHECK (std::isfinite (a.loudnessDb) && std::isfinite (a.thdDb) && std::isfinite (a.compression));
        for (double b : a.bandsDb) CHECK (std::isfinite (b));
        CHECK (a.latencySamples >= 0 && a.latencySamples < 4096);
        INFO ("%-26s loud %.1f dB  thd %.1f dB  comp %.2f  lat %d  pol %+d  bands %.1f %.1f %.1f %.1f %.1f", f,
              a.loudnessDb, a.thdDb, a.compression, a.latencySamples, a.polarity,
              a.bandsDb[0], a.bandsDb[1], a.bandsDb[2], a.bandsDb[3], a.bandsDb[4]);
    }
}

TEST (settings_estimated_from_sound_when_names_lack_them)
{
    // Three analyses that differ clearly in treble balance and distortion.
    std::vector<CaptureAnalysis> an (3);
    for (int i = 0; i < 3; ++i) { an[(size_t) i].valid = true; an[(size_t) i].bandsDb = { 0, 0, 0, -6.0 + 6.0 * i, 0 }; an[(size_t) i].thdDb = -40 + 15 * i; }
    std::vector<KnobCoords> c (3, unknownCoords());
    const auto est = ProfileAnalyzer::estimateCoordinates (c, an);
    CHECK (est[0][(size_t) Control::Treble] && est[2][(size_t) Control::Treble]);
    CHECK (c[0][(size_t) Control::Treble] < c[1][(size_t) Control::Treble] && c[1][(size_t) Control::Treble] < c[2][(size_t) Control::Treble]);
    CHECK (c[0][(size_t) Control::Gain] < c[2][(size_t) Control::Gain]);
    CHECK (! est[0][(size_t) Control::Bass]); // no audible bass difference → not invented
}

// --------------------------------------------------------------------------------------------
TEST (triode_stage_is_asymmetric_and_compresses)
{
    const double fs = 192000;
    TriodeStage t;
    t.prepare (fs);
    INFO ("12AX7 operating point: Vp = %.1f V, small-signal gain = %.1f", t.getOperatingPlateVoltage(), t.getSmallSignalGain());
    CHECK (t.getOperatingPlateVoltage() > 100 && t.getOperatingPlateVoltage() < 220); // datasheet-like bias
    CHECK (t.getSmallSignalGain() > 30 && t.getSmallSignalGain() < 100);               // 12AX7 μ≈100 stage gain

    auto run = [&] (double drive, double amp) {
        t.setDrive (drive); t.reset();
        auto x = tst::sine (200.0, fs, (size_t) fs / 2, amp);
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = t.process (x[i]);
        return y;
    };
    auto y = run (0.6, 0.5);
    CHECK (tst::allFinite (y));
    const size_t n = 38400, from = y.size() - n;
    const double h1 = tst::tone (y, from, n, 200, fs), h2 = tst::tone (y, from, n, 400, fs), h3 = tst::tone (y, from, n, 600, fs);
    INFO ("drive 0.6: H2 %.1f dB, H3 %.1f dB re H1", tst::db (h2 / h1), tst::db (h3 / h1));
    CHECK (tst::db (h2 / h1) > -40); // even harmonics from the asymmetric triode

    // compression: 20 dB more input gives clearly less than 20 dB more output at high drive
    auto lo = run (1.0, 0.1), hi = run (1.0, 1.0);
    const double gainDiff = tst::db (tst::rms (hi, hi.size() / 2) / tst::rms (lo, lo.size() / 2));
    INFO ("+20 dB input → %+.1f dB output at high drive", gainDiff);
    CHECK (gainDiff < 14.0);
}

TEST (power_amp_push_pull_sag_and_bias)
{
    const double fs = 192000;
    auto measure = [&] (double mismatch, double bias, double sag, double& h2r, double& h3r, double& vmin) {
        PowerAmpStage p;
        p.prepare (fs);
        p.setMismatch (mismatch); p.setBias (bias); p.setSag (sag); p.setDrive (0.8); p.setFeedback (0.3);
        p.setSpeakerInteraction (0.0); p.setTransformer (0.0);
        auto x = tst::sine (200.0, fs, (size_t) fs / 2, 0.6);
        std::vector<double> y (x.size());
        vmin = 1e9;
        for (size_t i = 0; i < x.size(); ++i) { y[i] = p.process (x[i]); vmin = std::min (vmin, p.getSupplyVoltage()); }
        const size_t n = 38400, from = y.size() - n;
        const double h1 = tst::tone (y, from, n, 200, fs);
        h2r = tst::db (tst::tone (y, from, n, 400, fs) / h1);
        h3r = tst::db (tst::tone (y, from, n, 600, fs) / h1);
        CHECK (tst::allFinite (y));
    };
    double h2a, h3a, h2b, h3b, v1, v2;
    measure (0.0, 0.5, 0.0, h2a, h3a, v1);
    measure (1.0, 0.5, 0.9, h2b, h3b, v2);
    INFO ("matched pair: H2 %.1f H3 %.1f | mismatched: H2 %.1f H3 %.1f | supply min %.0f V → %.0f V", h2a, h3a, h2b, h3b, v1, v2);
    CHECK (h2a < h3a - 20);   // push-pull cancels even harmonics
    CHECK (h2b > h2a + 15);   // mismatch brings them back
    CHECK (v2 < v1 - 20);     // sag pulls the supply down under load
}

TEST (harmonic_engine_adds_harmonics_and_respects_zero)
{
    const double fs = 192000;
    HarmonicEngine h;
    h.prepare (fs);
    HarmonicEngine::Settings s; s.amount = 0.0;
    h.setSettings (s);
    auto x = tst::sine (300.0, fs, 96000, 0.3);
    double err = 0;
    for (double v : x) err = std::max (err, std::abs (h.process (v) - v));
    CHECK (err < 1e-12);

    s.amount = 0.6; s.evenOdd = 1.0; h.setSettings (s); h.reset();
    std::vector<double> y (x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = h.process (x[i]);
    const size_t n = 64000, from = y.size() - n;
    const double h1 = tst::tone (y, from, n, 300, fs), h2 = tst::tone (y, from, n, 600, fs);
    INFO ("harmonics 0.6 / even: H2 %.1f dB", tst::db (h2 / h1));
    CHECK (tst::db (h2 / h1) > -45);
    CHECK (std::abs (tst::db (h1 / 0.3)) < 3.0); // level-matched
}

// --------------------------------------------------------------------------------------------
static void waitForConvolution (DualCabinet& cab, double fs)
{
    std::vector<float> z (512, 0.0f);
    juce::AudioBuffer<float> out (2, 512);
    for (int i = 0; i < 60; ++i) { cab.process (z.data(), out, 512); std::this_thread::sleep_for (std::chrono::milliseconds (10)); }
}

TEST (dual_ir_identity_polarity_delay_and_blend)
{
    const double fs = 48000;
    DualCabinet cab;
    cab.prepare ({ fs, 512, 2 });
    juce::AudioBuffer<float> imp (1, 64); imp.clear(); imp.setSample (0, 0, 1.0f);
    CHECK (cab.slot (0).loadBuffer (imp, fs).isEmpty());
    DualCabinet::Global g; g.blend = 0.0f; g.stereo = true;
    cab.setGlobal (g);
    waitForConvolution (cab, fs);

    auto runImpulse = [&] () {
        std::vector<float> x (512, 0.0f); x[10] = 1.0f;
        juce::AudioBuffer<float> out (2, 512);
        cab.process (x.data(), out, 512);
        std::vector<double> l (512);
        for (int i = 0; i < 512; ++i) l[(size_t) i] = out.getSample (0, i);
        return l;
    };
    auto y = runImpulse();
    CHECK_MSG (std::abs (y[10] - 1.0) < 1e-3, "identity IR peak %.4f", y[10]);

    IRSlot::Params p; p.invert = true;
    cab.setSlotParams (0, p);
    for (int i = 0; i < 8; ++i) y = runImpulse(); // let smoothers settle
    CHECK (y[10] < -0.99);

    p.invert = false; p.delayMs = 1.0f;
    cab.setSlotParams (0, p);
    for (int i = 0; i < 12; ++i) y = runImpulse();
    CHECK_MSG (std::abs (y[10 + 48]) > 0.9 && std::abs (y[10]) < 0.05, "delayed peak %.3f", y[58]);

    // slot 2: identical IR delayed by 0.5 ms → auto alignment finds the 0.5 ms offset
    juce::AudioBuffer<float> imp2 (1, 64); imp2.clear(); imp2.setSample (0, 24, 1.0f);
    CHECK (cab.slot (1).loadBuffer (imp2, fs).isEmpty());
    cab.updateAlignment();
    const auto al = cab.getAlignment();
    INFO ("alignment: slot1 +%.3f ms, slot2 +%.3f ms, corr %.2f", al.slot1DelayMs, al.slot2DelayMs, al.correlation);
    CHECK (std::abs (al.slot1DelayMs - 0.5f) < 0.03f && al.slot2DelayMs == 0.0f);
}

TEST (ir_files_load_from_disk)
{
    // Real files through the same path the Load button / drag & drop use.
    const auto dir = tempDir ("irfiles");
    struct Fmt { const char* name; int bits; bool aiff; int chans; double sr; bool flt; };
    const Fmt fmts[] = { { "mono16.wav", 16, false, 1, 44100, false }, { "mono24.wav", 24, false, 1, 48000, false },
                         { "stereo24_96k.wav", 24, false, 2, 96000, false }, { "float32.wav", 32, false, 1, 48000, true },
                         { "UPPER.WAV", 24, false, 1, 48000, false }, { "mono24.aif", 24, true, 1, 48000, false },
                         { "long_5s.wav", 24, false, 1, 48000, false } };
    for (const auto& f : fmts)
    {
        const juce::File file (juce::String ((dir / f.name).string()));
        const int len = juce::String (f.name).startsWith ("long") ? (int) (f.sr * 5) : (int) (f.sr * 0.1);
        juce::AudioBuffer<float> ir (f.chans, len);
        ir.clear();
        for (int ch = 0; ch < f.chans; ++ch)
            for (int i = 0; i < std::min (len, 2000); ++i) ir.setSample (ch, i, (float) (std::exp (-i / 200.0) * std::cos (i * 0.3)));
        {
            std::unique_ptr<juce::AudioFormat> fmt;
            if (f.aiff) fmt = std::make_unique<juce::AiffAudioFormat>(); else fmt = std::make_unique<juce::WavAudioFormat>();
            std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
            CHECK (os != nullptr);
            juce::StringPairArray meta;
            auto* w = fmt->createWriterFor (os.get(), f.sr, (unsigned) f.chans, f.bits, meta, 0);
            CHECK (w != nullptr);
            if (w == nullptr) continue;
            os.release();
            std::unique_ptr<juce::AudioFormatWriter> writer (w);
            writer->writeFromAudioSampleBuffer (ir, 0, len);
        }
        DualCabinet cab;
        cab.prepare ({ 48000, 512, 2 });
        const auto err = cab.slot (0).loadFile (file);
        CHECK_MSG (err.isEmpty(), "%s: %s", f.name, err.toRawUTF8());
        CHECK_MSG (cab.slot (0).isLoaded(), "%s not loaded", f.name);
        DualCabinet::Global g; g.blend = 0.0f; cab.setGlobal (g);
        waitForConvolution (cab, 48000);
        std::vector<float> x (512, 0.0f); x[0] = 1.0f;
        juce::AudioBuffer<float> out (2, 512);
        cab.process (x.data(), out, 512);
        const float e = out.getMagnitude (0, 0, 512);
        CHECK_MSG (e > 1e-3f, "%s produced silence", f.name);
        INFO ("%-18s %d ch %6.0f Hz -> %s", f.name, f.chans, f.sr, err.isEmpty() ? "loaded" : err.toRawUTF8());
    }
}

TEST (ir_level_independent_of_ir_sample_rate)
{
    // same "cabinet" (decaying noise) stored at 44.1 and 96 kHz → same loudness after loading
    double levels[2] {};
    int k = 0;
    for (double irRate : { 44100.0, 96000.0 })
    {
        DualCabinet cab;
        cab.prepare ({ 48000, 512, 2 });
        juce::AudioBuffer<float> ir (1, (int) (irRate * 0.05));
        juce::Random rnd (7);
        for (int i = 0; i < ir.getNumSamples(); ++i)
            ir.setSample (0, i, (float) ((rnd.nextFloat() * 2 - 1) * std::exp (-i / (irRate * 0.008))));
        // band-limit to 8 kHz so both versions describe the same cabinet
        juce::IIRFilter lp; lp.setCoefficients (juce::IIRCoefficients::makeLowPass (irRate, 6000.0));
        lp.processSamples (ir.getWritePointer (0), ir.getNumSamples());
        cab.slot (0).loadBuffer (ir, irRate);
        DualCabinet::Global g; g.blend = 0.0f; cab.setGlobal (g);
        waitForConvolution (cab, 48000);
        juce::Random r2 (3);
        std::vector<double> acc;
        for (int b = 0; b < 40; ++b)
        {
            std::vector<float> x (512);
            for (auto& v : x) v = (r2.nextFloat() * 2 - 1) * 0.2f;
            juce::AudioBuffer<float> out (2, 512);
            cab.process (x.data(), out, 512);
            if (b > 10) for (int i = 0; i < 512; ++i) acc.push_back (out.getSample (0, i));
        }
        levels[k++] = tst::rms (acc);
    }
    INFO ("IR @44.1k → %.4f rms, IR @96k → %.4f rms", levels[0], levels[1]);
    CHECK (std::abs (tst::db (levels[0] / levels[1])) < 1.5);
}

// --------------------------------------------------------------------------------------------
static AmpDefinition twoCaptureAmp (const fs::path& dir)
{
    // Capture A: low treble, capture B: high treble – different networks so the blend is audible.
    AmpDefinition a; a.id = "t"; a.name = "Test"; a.identityKey = "t|t";
    ChannelDefinition ch; ch.name = "Main";
    CaptureEntry ea, eb;
    ea.path = makeCapture (dir, "A", "T", "T", "A", "wavenet_a1_standard.nam").string();
    eb.path = makeCapture (dir, "B", "T", "T", "B", "wavenet.nam").string();
    ea.coords[(size_t) Control::Treble] = 2.0;
    eb.coords[(size_t) Control::Treble] = 8.0;
    for (auto* e : { &ea, &eb })
    {
        NamModel m; m.load (e->path);
        e->analysis = ProfileAnalyzer::analyse (m);
        e->meta = readCaptureMetadata (e->path);
    }
    ch.captures = { ea, eb };
    a.channels.push_back (ch);
    ChannelDefinition ch2; ch2.name = "Lead";
    ch2.captures = { eb };
    a.channels.push_back (ch2);
    return a;
}

TEST (runtime_treble_knob_moves_between_captures)
{
    const auto dir = tempDir ("runtime");
    const auto def = twoCaptureAmp (dir);
    std::vector<std::string> errors;
    RuntimeOptions opt;
    auto rt = AmpRuntime::build (def, 48000, 256, opt, errors);
    CHECK (errors.empty());
    CHECK (rt->numChannels() == 2);
    CHECK (rt->isCovered (0, Control::Treble));
    CHECK (! rt->isCovered (0, Control::Bass));

    auto output = [&] (double treble) {
        KnobCoords k { 5, 5, 5, 5, treble, 5, 5, 5 };
        auto x = tst::sine (330.0, 48000, 48000, 0.2);
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); i += 256)
        {
            rt->setKnobs (0, k);
            rt->process (0, x.data() + i, y.data() + i, (int) std::min<size_t> (256, x.size() - i));
        }
        return y;
    };
    auto y2 = output (2.0), y8 = output (8.0);
    // Treble 8 must sound like capture B alone: compare with B run directly.
    NamModel b; b.load (def.channels[0].captures[1].path); b.prepare (48000, 256);
    auto x = tst::sine (330.0, 48000, 48000, 0.2);
    std::vector<double> yb (x.size());
    for (size_t i = 0; i < x.size(); i += 256) b.process (x.data() + i, yb.data() + i, (int) std::min<size_t> (256, x.size() - i));
    const double gB = dbToGain (opt.loudnessCompensation * (opt.referenceLoudnessDb - *def.channels[0].captures[1].meta.loudnessDb));
    // amplitude at 330 Hz: runtime(treble 8) ≈ capture B (after its loudness normalisation)
    const double t8 = tst::tone (y8, 24000, 16384, 330, 48000), tb = tst::tone (yb, 24000, 16384, 330, 48000) * gB;
    const double t2 = tst::tone (y2, 24000, 16384, 330, 48000);
    INFO ("330 Hz level: treble2 %.4f  treble8 %.4f  captureB %.4f", t2, t8, tb);
    CHECK (std::abs (tst::db (t8 / tb)) < 0.5);
    CHECK (std::abs (tst::db (t2 / t8)) > 0.5); // the two ends really are different captures
}

TEST (model_blend_mixes_different_amps)
{
    const auto dir = tempDir ("blend");
    const auto a = makeCapture (dir, "Plexi Crunch", "Marshall", "1959 SLP", "Plexi Crunch", "wavenet_a1_standard.nam");
    const auto b = makeCapture (dir, "Recto Modern", "Mesa/Boogie", "Dual Rectifier", "Recto Modern", "wavenet.nam");

    AmpLibrary lib (tempDir ("blend_lib"));
    std::string err;
    const auto id = lib.createBlend ("Plexi + Recto", { a, b }, err);
    CHECK_MSG (! id.empty(), "%s", err.c_str());
    const auto* def = lib.find (id);
    CHECK (def != nullptr && def->isBlend && def->channels.size() == 1 && def->channels[0].captures.size() == 2);

    // an automatic import of a Marshall capture must create its own amp, never join the blend
    lib.importFiles ({ makeCapture (dir, "Plexi Clean", "Marshall", "1959 SLP", "Plexi Clean") }, {}, false);
    CHECK (lib.find (id)->totalCaptures() == 2);
    AmpLibrary reloaded (lib.getRoot());
    CHECK (reloaded.find (id) != nullptr && reloaded.find (id)->isBlend);

    std::vector<std::string> errors;
    auto rt = AmpRuntime::build (*lib.find (id), 48000, 256, {}, errors);
    CHECK (errors.empty() && rt->isBlend());

    auto run = [&] (std::array<double, 4> levels) {
        auto x = tst::sine (330.0, 48000, 48000, 0.2);
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); i += 256)
        {
            rt->setKnobs (0, { 5, 5, 5, 5, 5, 5, 5, 5 });
            rt->setBlendLevels (0, levels);
            rt->process (0, x.data() + i, y.data() + i, (int) std::min<size_t> (256, x.size() - i));
        }
        return tst::tone (y, 24000, 16384, 330, 48000);
    };
    const double onlyA = run ({ 1, 0, 0, 0 }), onlyB = run ({ 0, 1, 0, 0 }), both = run ({ 1, 1, 0, 0 });
    INFO ("330 Hz: only A %.4f  only B %.4f  50/50 %.4f", onlyA, onlyB, both);
    CHECK (std::abs (tst::db (onlyA / onlyB)) > 0.5);                  // really two different amps
    CHECK (both > std::min (onlyA, onlyB) * 0.5 && both < std::max (onlyA, onlyB) * 1.5);
    CHECK (rt->numRunningModels (0) >= 1);
}

static std::vector<double> runChain (SignalChain& chain, EngineParameters p, const std::vector<double>& x, int block = 256)
{
    std::vector<double> l (x.size()), r (x.size());
    std::vector<double> buf ((size_t) block), buf2 ((size_t) block);
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - i);
        std::copy (x.begin() + (long) i, x.begin() + (long) i + n, buf.begin());
        double* io[2] = { buf.data(), buf2.data() };
        chain.setParameters (p);
        chain.process (io, 1, 2, n);
        std::copy (buf.begin(), buf.begin() + n, l.begin() + (long) i);
    }
    return l;
}

static double bandLevel (const std::vector<double>& y, double fs, double f)
{
    return tst::tone (y, y.size() - 16384, 16384, f, fs);
}

TEST (signal_chain_controls_change_the_sound)
{
    const double fs = 48000;
    const auto dir = tempDir ("chain");
    auto def = twoCaptureAmp (dir);
    SignalChain chain;
    chain.prepare (fs, 256);
    std::vector<std::string> errors;
    chain.publishRuntime (AmpRuntime::build (def, fs, 256, {}, errors));
    EngineParameters p;
    p.cabOn = false; p.harmonicsOn = false;

    // multitone probe
    std::vector<double> x (48000 * 2, 0.0);
    for (double f : { 100.0, 800.0, 5000.0 })
    {
        auto s = tst::sine (f, fs, x.size(), 0.05);
        for (size_t i = 0; i < x.size(); ++i) x[i] += s[i];
    }

    auto measure = [&] (Control c, double v, double f) {
        auto q = p; q.knobs[(size_t) c] = v;
        return bandLevel (runChain (chain, q, x), fs, f);
    };
    const double bassLo = measure (Control::Bass, 1.0, 100.0), bassHi = measure (Control::Bass, 9.0, 100.0);
    const double midLo = measure (Control::Middle, 1.0, 800.0), midHi = measure (Control::Middle, 9.0, 800.0);
    const double volLo = measure (Control::Volume, 3.0, 800.0), volHi = measure (Control::Volume, 8.0, 800.0);
    const double prLo = measure (Control::Presence, 1.0, 5000.0), prHi = measure (Control::Presence, 9.0, 5000.0);
    INFO ("bass 1→9 @100Hz %+.1f dB | middle 1→9 @800Hz %+.1f dB | presence 1→9 @5k %+.1f dB | volume 3→8 %+.1f dB",
          tst::db (bassHi / bassLo), tst::db (midHi / midLo), tst::db (prHi / prLo), tst::db (volHi / volLo));
    CHECK (tst::db (bassHi / bassLo) > 6.0);
    CHECK (tst::db (midHi / midLo) > 5.0);
    CHECK (tst::db (prHi / prLo) > 4.0); // 5 kHz sits at the presence shelf's midpoint
    CHECK (tst::db (volHi / volLo) > 6.0);

    // power amp on: master drives the output stage (more compression at high master).
    // Measured without captures: the example models are already fully saturated.
    SignalChain clean;
    clean.prepare (fs, 256);
    p.powerAmpOn = true;
    auto loud = [&] (double master, double amp) {
        auto q = p; q.knobs[(size_t) Control::Master] = master;
        std::vector<double> s = tst::sine (200.0, fs, 48000, amp);
        auto y = runChain (clean, q, s);
        return tst::rms (y, 24000);
    };
    const double c2 = tst::db (loud (2.0, 0.5) / loud (2.0, 0.05));
    const double c9 = tst::db (loud (9.5, 0.5) / loud (9.5, 0.05));
    INFO ("+20 dB input with power amp: master 2 → %+.1f dB, master 9.5 → %+.1f dB", c2, c9);
    CHECK (c9 < c2 - 1.0);

    // never NaN, even with absurd input
    std::vector<double> crazy (8192, 0.0);
    for (size_t i = 0; i < crazy.size(); ++i) crazy[i] = (i % 2 ? 50.0 : -50.0);
    CHECK (tst::allFinite (runChain (chain, p, crazy)));
    CHECK (chain.getLatencySamples() >= 0);
    INFO ("total reported latency: %d samples", chain.getLatencySamples());
}

TEST (oversampling_keeps_aliasing_down)
{
    const double fs = 48000;
    SignalChain chain;
    chain.prepare (fs, 256);
    EngineParameters p;
    p.cabOn = false; p.harmonicsOn = true; p.harm.amount = 1.0; p.harm.evenOdd = 0.0;
    p.powerAmpOn = true; p.knobs[(size_t) Control::Master] = 9.0;
    const double f0 = 4700.0;
    auto y = runChain (chain, p, tst::sine (f0, fs, 48000, 0.5));
    const size_t n = 16384, from = y.size() - n;
    const double h1 = tst::tone (y, from, n, f0, fs);
    // aliases of the 5th..9th harmonics fold to non-harmonic frequencies
    double worst = 0;
    for (int k = 5; k <= 9; ++k)
    {
        double f = std::fmod (k * f0, fs);
        if (f > fs / 2) f = fs - f;
        if (std::fmod (f, f0) < 50 || f0 - std::fmod (f, f0) < 50) continue;
        worst = std::max (worst, tst::tone (y, from, n, f, fs));
    }
    INFO ("worst alias %.1f dB re fundamental (4x IIR oversampling)", tst::db (worst / h1));
    CHECK (tst::db (worst / h1) < -60.0);
}

TEST (channel_and_amp_switching_is_click_free)
{
    const double fs = 48000;
    const auto dir = tempDir ("click");
    auto def = twoCaptureAmp (dir);
    SignalChain chain;
    chain.prepare (fs, 256);
    std::vector<std::string> errors;
    chain.publishRuntime (AmpRuntime::build (def, fs, 256, {}, errors));
    EngineParameters p; p.cabOn = false;

    auto x = tst::sine (110.0, fs, 48000 * 3, 0.3);
    std::vector<double> y (x.size()), buf (256), buf2 (256);
    double maxStepNormal = 0, maxStepSwitch = 0, prev = 0;
    for (size_t i = 0; i < x.size(); i += 256)
    {
        if (i == 48000 / 256 * 256) p.channel = 1;            // channel switch at 1 s
        if (i == 96000 / 256 * 256) chain.publishRuntime (AmpRuntime::build (def, fs, 256, {}, errors)); // amp reload at 2 s
        const int n = (int) std::min<size_t> (256, x.size() - i);
        std::copy (x.begin() + (long) i, x.begin() + (long) i + n, buf.begin());
        double* io[2] = { buf.data(), buf2.data() };
        chain.setParameters (p);
        chain.process (io, 1, 2, n);
        for (int s = 0; s < n; ++s)
        {
            const double d = std::abs (buf[(size_t) s] - prev);
            prev = buf[(size_t) s];
            const size_t t = i + (size_t) s;
            if (t > 24000 && t < 47000) maxStepNormal = std::max (maxStepNormal, d);
            if ((t > 48000 && t < 52000) || (t > 96000 && t < 100000)) maxStepSwitch = std::max (maxStepSwitch, d);
        }
    }
    chain.collectGarbage();
    INFO ("max sample step: steady %.4f, around switches %.4f", maxStepNormal, maxStepSwitch);
    CHECK (maxStepSwitch < 2.0 * maxStepNormal + 1e-3);
}

TEST (preset_and_cab_preset_round_trip)
{
    const auto dir = tempDir ("presets");
    PresetManager pm (juce::File (dir.string()).getChildFile ("P"), juce::File (dir.string()).getChildFile ("C"));
    juce::ValueTree t ("STATE");
    t.setProperty ("gain", 7.5, nullptr);
    t.setProperty ("ampId", "abc", nullptr);
    CHECK (pm.savePreset ("My Lead", t));
    CHECK (pm.listPresets().size() == 1);
    auto back = pm.loadPreset (pm.listPresets()[0]);
    CHECK ((double) back.getProperty ("gain") == 7.5);
    CHECK (back.getProperty ("ampId").toString() == "abc");
    CHECK (pm.renamePreset (pm.listPresets()[0], "Lead 2"));
    CHECK (pm.listPresets()[0].getFileNameWithoutExtension() == "Lead 2");

    PresetManager::CabPreset c; c.name = "V30 blend"; c.ir1Path = "/a.wav"; c.ir2Path = "/b.wav";
    c.values.set ("cabBlend", 0.3);
    CHECK (pm.saveCabPreset (c));
    PresetManager::CabPreset d;
    CHECK (pm.loadCabPreset (pm.listCabPresets()[0], d));
    CHECK (d.ir2Path == "/b.wav" && std::abs ((double) d.values["cabBlend"] - 0.3) < 1e-9);
}

// --------------------------------------------------------------------------------------------
int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    int failedCases = 0;
    for (auto& c : tst::registry())
    {
        const int before = tst::failures();
        std::printf ("[ RUN  ] %s\n", c.name);
        const auto t0 = std::chrono::steady_clock::now();
        try { c.fn(); }
        catch (const std::exception& e) { ++tst::failures(); std::printf ("    EXCEPTION: %s\n", e.what()); }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        const bool ok = tst::failures() == before;
        if (! ok) ++failedCases;
        std::printf ("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", c.name, ms);
    }
    std::printf ("\n%zu tests, %d checks, %d failed checks, %d failed tests\n",
                 tst::registry().size(), tst::checks(), tst::failures(), failedCases);
    return failedCases == 0 ? 0 : 1;
}
