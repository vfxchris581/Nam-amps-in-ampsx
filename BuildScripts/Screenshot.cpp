// Renders the real Nam Knobs editor (every tab) to PNG files with a demo library built from the
// NeuralAmpModelerCore example models. Used for README/documentation previews and as a UI smoke
// test.  Usage:  NamKnobsScreenshot <output-dir>

#include <fstream>
#include <json.hpp>
#include "Plugin/PluginProcessor.h"
#include "GUI/PluginEditor.h"

namespace
{
using namespace namknobs;

void writeDemoCapture (const juce::File& dir, const juce::String& file, const juce::String& name, const char* source = "wavenet.nam")
{
    std::ifstream in (juce::File (NAMKNOBS_EXAMPLE_MODELS).getChildFile (source).getFullPathName().toStdString());
    nlohmann::json j; in >> j;
    j["metadata"]["gear_make"] = "Demo Amplification";
    j["metadata"]["gear_model"] = "Lead 100";
    j["metadata"]["name"] = name.toStdString();
    j["metadata"]["modeled_by"] = "Nam Knobs demo";
    std::ofstream out (dir.getChildFile (file + ".nam").getFullPathName().toStdString());
    out << j.dump();
}

juce::File writeDemoIR (const juce::File& dir, const juce::String& name, double sr, float tone, int seed, double offsetMs = 0.0)
{
    juce::AudioBuffer<float> ir (1, (int) (sr * 0.12));
    juce::Random r (seed);
    float lp = 0.0f;
    const int off = (int) (offsetMs * 0.001 * sr);
    for (int i = 0; i < ir.getNumSamples(); ++i)
    {
        // same "cabinet" (noise seeded per 48 kHz tick) heard through a different mic / distance
        const int k = i - off;
        if (k < 0) { ir.setSample (0, i, 0.0f); continue; }
        juce::Random rk (seed * 100003 + (int) (k * 48000.0 / sr));
        const float n = (rk.nextFloat() * 2 - 1) * std::exp (-(float) k / (float) (sr * 0.006));
        lp += tone * (n - lp);
        ir.setSample (0, i, lp);
    }
    auto f = dir.getChildFile (name + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (auto* w = wav.createWriterFor (os.get(), sr, 1, 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
    }
    return f;
}
} // namespace

class ScreenshotApp : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "NamKnobsScreenshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& cmd) override
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (cmd.isEmpty() ? "screenshots" : cmd.unquoted());
        outDir.createDirectory();
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("namknobs_screenshot");
        root.deleteRecursively();
        auto caps = root.getChildFile ("captures");
        caps.createDirectory();
        writeDemoCapture (caps, "Lead100 ch1 Clean G3 B5 T5", "Lead 100 Clean G3 B5 T5");
        writeDemoCapture (caps, "Lead100 ch1 Clean G6 B5 T7", "Lead 100 Clean G6 B5 T7");
        writeDemoCapture (caps, "Lead100 ch2 Crunch G5 B6 T5", "Lead 100 Crunch G5 B6 T5");
        writeDemoCapture (caps, "Lead100 ch2 Crunch G8 B6 T6", "Lead 100 Crunch G8 B6 T6");
        writeDemoCapture (caps, "Lead100 ch3 Lead G7 B4 T6 P5", "Lead 100 Lead G7 B4 T6 P5");
        writeDemoCapture (caps, "Lead100 ch3 Lead G10 B4 T8 P7", "Lead 100 Lead G10 B4 T8 P7");

        {
            AmpLibrary lib (root.getChildFile ("Library").getFullPathName().toStdString());
            lib.importFiles ({ caps.getFullPathName().toStdString() });
            // a model blend of three different "amps"
            auto bl = root.getChildFile ("blend");
            bl.createDirectory();
            writeDemoCapture (bl, "Plexi 1959 Crunch", "Plexi 1959 Crunch", "wavenet_a1_standard.nam");
            writeDemoCapture (bl, "Recto Modern Lead", "Recto Modern Lead");
            writeDemoCapture (bl, "Deluxe Clean Edge", "Deluxe Clean Edge", "lstm.nam");
            std::string err;
            const char* extra[][2] = { { "JCM800 Lead", "wavenet_a1_standard.nam" }, { "AC30 Top Boost", "lstm.nam" },
                                       { "5150 Red", "wavenet.nam" }, { "Twin Reverb Clean", "lstm.nam" },
                                       { "Bogner Uber", "wavenet_a1_standard.nam" }, { "Dumble ODS", "wavenet.nam" },
                                       { "SLO Crunch", "wavenet_a1_standard.nam" } };
            for (auto& e : extra) writeDemoCapture (bl, e[0], e[0], e[1]);
            std::vector<std::filesystem::path> files { bl.getChildFile ("Plexi 1959 Crunch.nam").getFullPathName().toStdString(),
                                                       bl.getChildFile ("Recto Modern Lead.nam").getFullPathName().toStdString(),
                                                       bl.getChildFile ("Deluxe Clean Edge.nam").getFullPathName().toStdString() };
            for (auto& e : extra) files.push_back (bl.getChildFile (juce::String (e[0]) + ".nam").getFullPathName().toStdString());
            blendId = lib.createBlend ("Ten Amp Stack", files, err);
        }
        processor = std::make_unique<NamKnobsProcessor> (root);
        for (auto& a : processor->getLibrary().getAmps())
            if (! a.isBlend) { processor->loadAmp (a.id); break; }
        processor->loadImpulseResponse (0, writeDemoIR (root, "4x12 V30 SM57 Cap Edge", 48000, 0.35f, 1));
        processor->loadImpulseResponse (1, writeDemoIR (root, "4x12 V30 R121 Cone", 96000, 0.25f, 1, 0.35));
        processor->savePreset ("Crunch Rhythm");
        processor->savePreset ("Lead Boosted");
        processor->savePreset ("Clean Chime");
        auto setP = [this] (const char* id, float v) {
            if (auto* p = processor->getAPVTS().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        setP ("gain", 6.5f); setP ("bass", 5.5f); setP ("middle", 6.0f); setP ("treble", 6.5f);
        setP ("presence", 6.0f); setP ("resonance", 4.5f); setP ("volume", 6.0f); setP ("master", 4.0f);
        setP (pid::harmOn, 1.0f); setP (pid::cabOn, 1.0f);
        editor.reset (dynamic_cast<NamKnobsEditor*> (processor->createEditor()));
        processor->setPlayConfigDetails (2, 2, 48000.0, 512);
        processor->prepareToPlay (48000.0, 512);
        startTimer (3000); // let the background runtime build finish and the UI settle
    }

    void timerCallback() override
    {
        stopTimer();
        if (phase == 0) { ++phase; playAudio(); startTimer (400); return; } // let meters/status timers repaint
        if (phase == 1)
        {
            ++phase;
            snapshot();
            if (! blendId.empty())
            {
                processor->loadAmp (blendId);
                if (auto* prm = processor->getAPVTS().getParameter (pid::matrixAuto)) prm->setValueNotifyingHost (0.0f);
                // gain mostly from model 1, bass only from model 2, mids blended 2+3, treble/presence from 3
                const float gain[kBlendSlots] = { 1.0f, 0.25f, 0.0f, 0.0f };
                const float tone[5][kBlendSlots] = { { 0, 1, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0.6f, 0.4f, 0 }, { 0, 0, 1, 0 }, { 0.3f, 0, 1, 0 } };
                auto setP = [this] (const char* id, float v) {
                    if (auto* prm = processor->getAPVTS().getParameter (id)) prm->setValueNotifyingHost (v); };
                for (int i = 0; i < kBlendSlots; ++i) setP (pid::blendMix[i], gain[i]);
                for (int b = 0; b < 5; ++b) for (int m = 0; m < kBlendSlots; ++m) setP (pid::toneSource[b][m], tone[b][m]);
                startTimer (3000); // runtime build + UI refresh
                return;
            }
            finish();
            return;
        }
        if (phase == 2) { ++phase; playAudio(); startTimer (400); return; }
        if (phase == 10) { autoShot(); finish(); return; }
        // phase 3: blend shots
        auto shot = [this] (int tab, const char* file) {
            editor->showTab (tab);
            editor->setSize (NamKnobsEditor::kWidth, NamKnobsEditor::kHeight);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile (file);
            f.deleteFile();
            juce::PNGImageFormat png;
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        shot (NamKnobsEditor::tabAmp, "namknobs-blend.png");
        shot (NamKnobsEditor::tabToneMatrix, "namknobs-tone-matrix-manual.png");
        shot (NamKnobsEditor::tabLibrary, "namknobs-library.png");
        if (auto* prm = processor->getAPVTS().getParameter (pid::matrixAuto)) prm->setValueNotifyingHost (1.0f);
        auto setK = [this] (const char* id, float v) {
            if (auto* prm = processor->getAPVTS().getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        setK ("gain", 7.0f); setK ("bass", 8.0f); setK ("middle", 3.0f); setK ("treble", 6.5f); setK ("presence", 4.0f); setK ("resonance", 9.0f);
        playAudio();
        juce::Thread::sleep (50);
        startTimer (500);
        phase = 10;
        return;
    }

    void autoShot()
    {
        editor->showTab (NamKnobsEditor::tabToneMatrix);
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
        auto f = outDir.getChildFile ("namknobs-tone-matrix.png");
        f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());

        // tuner: a low E string played 6 cents flat
        editor->showTuner (true);
        {
            const double sr = 48000.0, hz = 82.4069 * std::pow (2.0, -6.0 / 1200.0);
            std::vector<float> x (8192);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double t = (double) i / sr, w = 2 * 3.14159265358979323846 * hz * t;
                x[i] = (float) (0.2 * std::sin (w) + 0.25 * std::sin (2 * w + 0.4) + 0.12 * std::sin (3 * w + 1.1) + 0.06 * std::sin (4 * w));
            }
            editor->getTuner().analyse (x.data(), (int) x.size(), sr);
        }
        auto timg = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
        auto tf = outDir.getChildFile ("namknobs-tuner.png");
        tf.deleteFile();
        if (auto os = tf.createOutputStream()) png.writeImageToStream (timg, *os);
        std::printf ("wrote %s\n", tf.getFullPathName().toRawUTF8());
        editor->showTuner (false);
    }

    void playAudio()
    {
        // play a few seconds of a decaying guitar-like signal so meters + blend status are live
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int b = 0; b < 160; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const double t = (b * 512 + i) / 48000.0;
                const float v = (float) (0.3 * std::exp (-3.0 * std::fmod (t, 1.0)) * (std::sin (2 * 3.14159265358979323846 * 110 * t) + 0.5 * std::sin (2 * 3.14159265358979323846 * 165 * t)));
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            processor->processBlock (buf, midi);
        }
    }

    void snapshot()
    {
        // a musical EQ setting for the EQ page shot
        auto setE = [this] (const char* id, float v) {
            if (auto* prm = processor->getAPVTS().getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        setE (pid::eqBand[0][0], 1.0f); setE (pid::eqBand[0][1], 85.0f);
        setE (pid::eqBand[1][2], 2.0f);
        setE (pid::eqBand[2][1], 350.0f); setE (pid::eqBand[2][2], -2.5f);
        setE (pid::eqBand[3][1], 1800.0f); setE (pid::eqBand[3][2], 3.5f);
        setE (pid::eqBand[4][1], 3600.0f); setE (pid::eqBand[4][2], -4.0f); setE (pid::eqBand[4][3], 2.5f);
        setE (pid::eqBand[5][2], 1.5f);
        setE (pid::eqBand[6][0], 1.0f); setE (pid::eqBand[6][1], 9500.0f);
        playAudio();
        const char* names[] = { "amp", "tone-matrix-empty", "eq", "captures", "cabinet", "tube-harmonics", "library" };
        for (int t = 0; t < 7; ++t)
        {
            editor->showTab (t);
            editor->setSize (NamKnobsEditor::kWidth, NamKnobsEditor::kHeight);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile (juce::String ("namknobs-") + names[t] + ".png");
            f.deleteFile();
            juce::PNGImageFormat png;
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    }

    void finish()
    {
        // app icon (also used by the plugin bundle)
        {
            juce::Image icon (juce::Image::ARGB, 512, 512, true);
            juce::Graphics g (icon);
            ui::drawTolex (g, { 0, 0, 512, 512 });
            ui::drawFaceplate (g, { 56, 150, 400, 212 });
            g.setColour (ui::ink);
            g.setFont (ui::brand (118.0f));
            g.drawText ("N", juce::Rectangle<int> (56, 150, 400, 212), juce::Justification::centred);
            auto f = outDir.getChildFile ("Icon.png");
            f.deleteFile();
            juce::PNGImageFormat png;
            if (auto os = f.createOutputStream()) png.writeImageToStream (icon, *os);
        }
        editor.reset();
        processor.reset();
        quit();
    }

    void shutdown() override { editor.reset(); processor.reset(); }

private:
    juce::File outDir;
    std::string blendId;
    int phase = 0;
    std::unique_ptr<NamKnobsProcessor> processor;
    std::unique_ptr<NamKnobsEditor> editor;
};

START_JUCE_APPLICATION (ScreenshotApp)
