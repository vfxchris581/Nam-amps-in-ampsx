// Renders the real Nam VX editor (every tab) to PNG files with a demo library built from the
// NeuralAmpModelerCore example models. Used for README/documentation previews and as a UI smoke
// test.  Usage:  NamVXScreenshot <output-dir>

#include <fstream>
#include <json.hpp>
#include "Plugin/PluginProcessor.h"
#include "GUI/PluginEditor.h"

namespace
{
using namespace namvx;

void writeDemoCapture (const juce::File& dir, const juce::String& file, const juce::String& name)
{
    std::ifstream in (juce::File (NAMVX_EXAMPLE_MODELS).getChildFile ("wavenet.nam").getFullPathName().toStdString());
    nlohmann::json j; in >> j;
    j["metadata"]["gear_make"] = "Demo Amplification";
    j["metadata"]["gear_model"] = "Lead 100";
    j["metadata"]["name"] = name.toStdString();
    j["metadata"]["modeled_by"] = "Nam VX demo";
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
    const juce::String getApplicationName() override { return "NamVXScreenshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& cmd) override
    {
        outDir = juce::File::getCurrentWorkingDirectory().getChildFile (cmd.isEmpty() ? "screenshots" : cmd.unquoted());
        outDir.createDirectory();
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("namvx_screenshot");
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
            writeDemoCapture (bl, "Plexi 1959 Crunch", "Plexi 1959 Crunch");
            writeDemoCapture (bl, "Recto Modern Lead", "Recto Modern Lead");
            writeDemoCapture (bl, "Deluxe Clean Edge", "Deluxe Clean Edge");
            std::string err;
            blendId = lib.createBlend ("Plexi x Recto x Deluxe",
                                       { bl.getChildFile ("Plexi 1959 Crunch.nam").getFullPathName().toStdString(),
                                         bl.getChildFile ("Recto Modern Lead.nam").getFullPathName().toStdString(),
                                         bl.getChildFile ("Deluxe Clean Edge.nam").getFullPathName().toStdString() }, err);
        }
        processor = std::make_unique<NamVXProcessor> (root);
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
        editor.reset (dynamic_cast<NamVXEditor*> (processor->createEditor()));
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
                const float mix[] = { 0.7f, 0.3f, 0.5f, 1.0f };
                for (int i = 0; i < 4; ++i)
                    if (auto* prm = processor->getAPVTS().getParameter (pid::blendMix[i])) prm->setValueNotifyingHost (mix[i]);
                startTimer (3000); // runtime build + UI refresh
                return;
            }
            finish();
            return;
        }
        if (phase == 2) { ++phase; playAudio(); startTimer (400); return; }
        // phase 3: blend shots
        auto shot = [this] (int tab, const char* file) {
            editor->showTab (tab);
            editor->setSize (NamVXEditor::kWidth, NamVXEditor::kHeight);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile (file);
            f.deleteFile();
            juce::PNGImageFormat png;
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        shot (0, "namvx-blend.png");
        shot (4, "namvx-library.png");
        finish();
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
        const char* names[] = { "amp", "captures", "cabinet", "tube-harmonics", "library" };
        for (int t = 0; t < 5; ++t)
        {
            editor->showTab (t);
            editor->setSize (NamVXEditor::kWidth, NamVXEditor::kHeight);
            auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.5f);
            auto f = outDir.getChildFile (juce::String ("namvx-") + names[t] + ".png");
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
    std::unique_ptr<NamVXProcessor> processor;
    std::unique_ptr<NamVXEditor> editor;
};

START_JUCE_APPLICATION (ScreenshotApp)
