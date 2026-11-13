#include "PluginProcessor.h"
#include "GUI/PluginEditor.h"
#include "ChannelManager/ChannelManager.h"

namespace namknobs
{
namespace
{
juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : parameterSpecs())
    {
        const juce::ParameterID id { s.id, 1 };
        switch (s.kind)
        {
            case ParamKind::Float:
            {
                juce::NormalisableRange<float> r ((float) s.min, (float) s.max);
                if (s.skewCentre > 0) r.setSkewForCentre ((float) s.skewCentre);
                auto attrs = juce::AudioParameterFloatAttributes().withLabel (s.unit)
                    .withStringFromValueFunction ([unit = s.unit, mx = s.max] (float v, int) {
                        if (unit == "dB") return juce::String (v, 1) + " dB";
                        if (unit == "ms") return juce::String (v, v < 10.0f ? 2 : 0) + " ms";
                        if (unit == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                                                              : juce::String (juce::roundToInt (v)) + " Hz";
                        if (unit == "Q") return juce::String (v, 2);
                        if (mx <= 1.0001) return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                        return juce::String (v, 1);
                    });
                layout.add (std::make_unique<juce::AudioParameterFloat> (id, s.name, r, (float) s.def, attrs));
                break;
            }
            case ParamKind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, s.name, s.def > 0.5));
                break;
            case ParamKind::Choice:
            {
                juce::StringArray c;
                for (auto& x : s.choices) c.add (x);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, c, (int) s.def));
                break;
            }
        }
    }
    return layout;
}

juce::File appDataFolder (const juce::String& name)
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/" + name);
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (name);
   #endif
}

juce::File defaultLibraryRoot()
{
    const auto root = appDataFolder ("Nam Knobs");
    if (root.getChildFile ("Library").isDirectory()) return root;

    // First launch after a rename: bring the previous library (amps, blends, captures, presets) along.
    for (auto old : { "Nam VX", "Nam0", "Namified" })
    {
        const auto oldRoot = appDataFolder (old);
        if (! oldRoot.getChildFile ("Library").isDirectory()) continue;
        root.createDirectory();
        if (! oldRoot.getChildFile ("Library").copyDirectoryTo (root.getChildFile ("Library"))) break;
        // stored capture / IR paths point into the old folder: re-point them
        const auto from = oldRoot.getFullPathName(), to = root.getFullPathName();
        auto escaped = [] (const juce::String& s) { return s.replace ("\\", "\\\\"); };
        for (auto& f : root.getChildFile ("Library").findChildFiles (juce::File::findFiles, true, "*.json"))
        {
            const auto text = f.loadFileAsString();
            const auto fixed = text.replace (from, to).replace (escaped (from), escaped (to));
            if (fixed != text) f.replaceWithText (fixed);
        }
        break;
    }
    return root;
}

} // namespace

NamKnobsProcessor::NamKnobsProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts (*this, nullptr, "NAMKNOBS", createLayout())
{
    init (defaultLibraryRoot());
}

NamKnobsProcessor::NamKnobsProcessor (const juce::File& root)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts (*this, nullptr, "NAMKNOBS", createLayout())
{
    init (root);
}

void NamKnobsProcessor::init (const juce::File& root)
{
    library = std::make_unique<AmpLibrary> (root.getChildFile ("Library").getFullPathName().toStdString());
    presets = std::make_unique<PresetManager> (juce::File (library->presetsDir().string()),
                                               juce::File (library->cabPresetsDir().string()));
    for (int k = 0; k < kNumControls; ++k) knobParams[(size_t) k] = raw (controlId ((Control) k));
    rp = { raw (pid::channel), raw (pid::input), raw (pid::output), raw (pid::preTubeOn), raw (pid::preTubeDrive),
           raw (pid::preTubeMix), raw (pid::powerAmpOn), raw (pid::tubeType), raw (pid::bias), raw (pid::sag),
           raw (pid::nfb), raw (pid::transformer), raw (pid::mismatch), raw (pid::speaker), raw (pid::harmOn),
           raw (pid::harmAmount), raw (pid::harmEvenOdd), raw (pid::harmTouch), raw (pid::harmAttack),
           raw (pid::harmLowDef), raw (pid::harmHighDetail), raw (pid::cabOn), raw (pid::cabStereo),
           raw (pid::cabAutoAlign), raw (pid::cabBlend), raw (pid::cabLevel), raw (pid::ir1On), raw (pid::ir1Level),
           raw (pid::ir1Pan), raw (pid::ir1Invert), raw (pid::ir1Delay), raw (pid::ir2On), raw (pid::ir2Level),
           raw (pid::ir2Pan), raw (pid::ir2Invert), raw (pid::ir2Delay), {} };
    rp.matrixAuto = raw (pid::matrixAuto);
    rp.levelMatch = raw (pid::levelMatch);
    for (int t = 0; t < 5; ++t) rp.toneTrim[t] = raw (pid::toneTrim[t]);
    rp.gateOn = raw (pid::gateOn); rp.gateThreshold = raw (pid::gateThreshold); rp.gateRelease = raw (pid::gateRelease);
    rp.phaseMode = raw (pid::phaseMode);
    rp.eqOn = raw (pid::eqOn); rp.eqPost = raw (pid::eqPost); rp.eqOut = raw (pid::eqOut);
    for (int bd = 0; bd < 7; ++bd)
        for (int k = 0; k < 5; ++k)
            rp.eqBand[bd][k] = *pid::eqBand[bd][k] ? raw (pid::eqBand[bd][k]) : nullptr;
    for (int i = 0; i < kBlendSlots; ++i) rp.slotInvert[i] = raw (pid::slotInvert[i]);
    for (int i = 0; i < kBlendSlots; ++i) rp.blendMix[i] = raw (pid::blendMix[i]);
    for (int b = 0; b < 5; ++b) for (int m = 0; m < kBlendSlots; ++m) rp.toneSource[b][m] = raw (pid::toneSource[b][m]);
    apvts.addParameterListener (pid::channel, this);
    chain.prepare (48000.0, 512);
    setStatus (library->getAmps().empty() ? "Drop NAM captures of one amp to build your first virtual amplifier"
                                          : "Ready");
    if (! library->getAmps().empty()) loadAmp (library->getAmps().front().id);
    startTimerHz (10);
}

NamKnobsProcessor::~NamKnobsProcessor()
{
    stopTimer();
    apvts.removeParameterListener (pid::channel, this);
    workers.removeAllJobs (true, 20000);
}

bool NamKnobsProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void NamKnobsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool changed = sampleRate != preparedRate || samplesPerBlock > preparedBlock;
    preparedRate = sampleRate;
    preparedBlock = std::max (samplesPerBlock, preparedBlock);
    chain.prepare (sampleRate, std::max (64, preparedBlock));
    chain.setOversampling ((SignalChain::OversamplingQuality) options.oversampling, options.linearPhase);
    if (changed || chain.hasAmp.load() == false) rebuildRuntime();
    setLatencySamples (chain.getLatencySamples());
}

void NamKnobsProcessor::snapshotParameters (EngineParameters& p) const noexcept
{
    for (int k = 0; k < kNumControls; ++k) p.knobs[(size_t) k] = knobParams[(size_t) k]->load();
    auto b = [] (std::atomic<float>* a) { return a->load() > 0.5f; };
    auto f = [] (std::atomic<float>* a) { return (double) a->load(); };
    p.channel = (int) rp.channel->load();
    p.inputDb = f (rp.input); p.outputDb = f (rp.output);
    p.preTubeOn = b (rp.preTubeOn); p.preTubeDrive = f (rp.preTubeDrive); p.preTubeMix = f (rp.preTubeMix);
    p.powerAmpOn = b (rp.powerAmpOn); p.tubeType = (int) rp.tubeType->load();
    p.bias = f (rp.bias); p.sag = f (rp.sag); p.nfb = f (rp.nfb); p.transformer = f (rp.transformer);
    p.mismatch = f (rp.mismatch); p.speaker = f (rp.speaker);
    p.harmonicsOn = b (rp.harmOn);
    p.harm.amount = f (rp.harmAmount); p.harm.evenOdd = f (rp.harmEvenOdd); p.harm.touch = f (rp.harmTouch);
    p.harm.attackPreserve = f (rp.harmAttack); p.harm.lowDefinition = f (rp.harmLowDef); p.harm.highDetail = f (rp.harmHighDetail);
    p.cabOn = b (rp.cabOn); p.cabStereo = b (rp.cabStereo); p.cabAutoAlign = b (rp.cabAutoAlign);
    p.cabBlend = f (rp.cabBlend); p.cabLevelDb = f (rp.cabLevel);
    p.ir1 = { b (rp.ir1On), f (rp.ir1Level), f (rp.ir1Pan), f (rp.ir1Delay), b (rp.ir1Invert) };
    p.ir2 = { b (rp.ir2On), f (rp.ir2Level), f (rp.ir2Pan), f (rp.ir2Delay), b (rp.ir2Invert) };
    p.matrixAuto = b (rp.matrixAuto);
    p.levelMatch = b (rp.levelMatch);
    for (int t = 0; t < 5; ++t) p.toneTrimDb[(size_t) t] = f (rp.toneTrim[t]);
    p.gateOn = b (rp.gateOn); p.gateThresholdDb = f (rp.gateThreshold); p.gateReleaseMs = f (rp.gateRelease);
    p.phaseMode = (int) rp.phaseMode->load();
    p.eq.enabled = b (rp.eqOn);
    p.eq.postCab = rp.eqPost->load() > 0.5f;
    p.eq.outputDb = f (rp.eqOut);
    for (int bd = 0; bd < 7; ++bd)
    {
        auto& band = p.eq.band[(size_t) bd];
        band.on = b (rp.eqBand[bd][0]);
        band.freq = f (rp.eqBand[bd][1]);
        if (rp.eqBand[bd][2]) band.gainDb = f (rp.eqBand[bd][2]);
        if (rp.eqBand[bd][3]) band.q = f (rp.eqBand[bd][3]);
        if (rp.eqBand[bd][4]) band.slope = (int) rp.eqBand[bd][4]->load();
    }
    for (int i = 0; i < kBlendSlots; ++i) p.slotInvert[(size_t) i] = b (rp.slotInvert[i]);
    for (int i = 0; i < kBlendSlots; ++i) p.blendMix[(size_t) i] = f (rp.blendMix[i]);
    for (int b = 0; b < 5; ++b) for (int m = 0; m < kBlendSlots; ++m) p.toneSource[(size_t) b][(size_t) m] = f (rp.toneSource[b][m]);
}

template <typename T>
void NamKnobsProcessor::processAny (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    EngineParameters p;
    snapshotParameters (p);
    chain.setParameters (p);
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    chain.process (buffer.getArrayOfWritePointers(), std::min (numIn, buffer.getNumChannels()),
                   std::min (numOut, buffer.getNumChannels()), buffer.getNumSamples());
}

void NamKnobsProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { processAny (b); }
void NamKnobsProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processAny (b); }

juce::AudioProcessorEditor* NamKnobsProcessor::createEditor() { return new NamKnobsEditor (*this); }

// ---- amps -----------------------------------------------------------------------------------
const AmpDefinition* NamKnobsProcessor::getCurrentAmp() const { return library->find (currentAmpId.toStdString()); }

int NamKnobsProcessor::getCurrentChannel() const
{
    const auto* a = getCurrentAmp();
    const int ch = (int) rp.channel->load();
    return a ? juce::jlimit (0, std::max (0, (int) a->channels.size() - 1), ch) : 0;
}

void NamKnobsProcessor::setStatus (const juce::String& s)
{
    { std::lock_guard<std::mutex> l (statusLock); status = s; }
    sendChangeMessage();
}

juce::String NamKnobsProcessor::getStatus() const
{
    std::lock_guard<std::mutex> l (statusLock);
    return status;
}

void NamKnobsProcessor::loadAmp (const juce::String& ampId)
{
    const auto* a = library->find (ampId.toStdString());
    if (a == nullptr) return;
    currentAmpId = ampId;
    // restore the channel's saved knobs
    lastChannel = getCurrentChannel();
    restoringChannelKnobs = true;
    const auto& knobs = a->channels.empty() ? KnobCoords { 5, 5, 5, 5, 5, 5, 5, 5 } : a->channels[(size_t) lastChannel].savedKnobs;
    for (int k = 0; k < kNumControls; ++k)
        if (auto* prm = apvts.getParameter (controlId ((Control) k)))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) knobs[(size_t) k]));
    restoringChannelKnobs = false;
    if (! a->ir1Path.empty() && irPaths[0].isEmpty()) loadImpulseResponse (0, juce::File (a->ir1Path));
    if (! a->ir2Path.empty() && irPaths[1].isEmpty()) loadImpulseResponse (1, juce::File (a->ir2Path));
    rebuildRuntime();
    sendChangeMessage();
}

void NamKnobsProcessor::rebuildRuntime()
{
    const auto* a = getCurrentAmp();
    if (a == nullptr) { chain.publishRuntime (nullptr); return; }
    const AmpDefinition def = *a;
    const double sr = preparedRate > 0 ? preparedRate : 48000.0;
    const int block = std::max (64, preparedBlock > 0 ? preparedBlock : 512);
    RuntimeOptions ro;
    ro.maxActiveCaptures = options.maxActive;
    ro.loudnessCompensation = options.loudnessComp;
    ro.alwaysOnModels = options.alwaysOn;
    const int gen = ++buildGeneration;
    setStatus ("Loading " + juce::String (def.name) + " (" + juce::String (def.totalCaptures()) + " captures)...");

    workers.addJob ([this, def, sr, block, ro, gen] {
        std::vector<std::string> errors;
        auto rt = AmpRuntime::build (def, sr, block, ro, errors);
        if (gen != buildGeneration.load()) return; // superseded by a newer request
        chain.publishRuntime (std::move (rt));
        juce::String msg = juce::String (def.name) + " ready - " + juce::String (def.totalCaptures()) + " captures, "
                           + juce::String ((int) def.channels.size()) + (def.channels.size() == 1 ? " channel" : " channels");
        if (! errors.empty()) msg << "  (" << (int) errors.size() << " failed: " << juce::String (errors.front()) << ")";
        setStatus (msg);
    });
}

void NamKnobsProcessor::updateAmp (const AmpDefinition& def, bool rebuild)
{
    library->save (def);
    if (def.id == currentAmpId.toStdString() && rebuild) rebuildRuntime();
    sendChangeMessage();
}

void NamKnobsProcessor::deleteAmp (const juce::String& ampId)
{
    library->remove (ampId.toStdString());
    if (ampId == currentAmpId)
    {
        currentAmpId = {};
        chain.publishRuntime (nullptr);
        if (! library->getAmps().empty()) loadAmp (library->getAmps().front().id);
    }
    sendChangeMessage();
}

void NamKnobsProcessor::importFiles (const juce::StringArray& paths)
{
    if (importing.exchange (true)) return;
    std::vector<std::filesystem::path> files;
    for (auto& p : paths) files.emplace_back (p.toStdString());
    const auto root = library->getRoot();

    workers.addJob ([this, files, root] {
        AmpLibrary worker (root); // separate instance: the UI keeps using the main one meanwhile
        auto res = worker.importFiles (files, [this] (int done, int total, const std::string& f) {
            if (total > 0 && ! f.empty())
                setStatus ("Analysing capture " + juce::String (done + 1) + " of " + juce::String (total) + ": " + juce::String (f));
            return true;
        });
        juce::MessageManager::callAsync ([this, res] {
            library->reload();
            for (auto& p : res.pending) pending.push_back (p);
            juce::String msg = "Imported " + juce::String (res.imported) + " capture" + (res.imported == 1 ? "" : "s");
            if (res.duplicates > 0) msg << ", " << res.duplicates << " already in library";
            if (! res.pending.empty()) msg << ", " << (int) res.pending.size() << " need manufacturer/model (Captures tab)";
            if (! res.errors.empty()) msg << ", " << (int) res.errors.size() << " failed: " << juce::String (res.errors.front());
            importing = false;
            if (! res.ampIds.empty()) loadAmp (res.ampIds.front());
            setStatus (msg);
        });
    });
}

std::vector<PendingCapture> NamKnobsProcessor::getPending() const { return pending; }

void NamKnobsProcessor::assignPending (int index, const juce::String& make, const juce::String& model, const juce::String& channel)
{
    if (index < 0 || index >= (int) pending.size() || make.trim().isEmpty() || model.trim().isEmpty()) return;
    const auto id = library->assignPending (pending[(size_t) index], make.trim().toStdString(), model.trim().toStdString(),
                                            channel.trim().toStdString());
    pending.erase (pending.begin() + index);
    loadAmp (id);
}

void NamKnobsProcessor::discardPending (int index)
{
    if (index >= 0 && index < (int) pending.size()) pending.erase (pending.begin() + index);
    sendChangeMessage();
}

void NamKnobsProcessor::createBlend (const juce::String& name, const juce::StringArray& namFiles)
{
    if (importing.exchange (true)) return;
    std::vector<std::filesystem::path> files;
    for (auto& f : namFiles) files.emplace_back (f.toStdString());
    const auto root = library->getRoot();
    const auto blendName = name.toStdString();
    setStatus ("Analysing models for blend \"" + name + "\"...");
    workers.addJob ([this, files, root, blendName] {
        AmpLibrary worker (root);
        std::string error;
        const auto id = worker.createBlend (blendName, files, error);
        juce::MessageManager::callAsync ([this, id, error, blendName] {
            importing = false;
            library->reload();
            if (id.empty()) { setStatus ("Blend not created: " + juce::String (error)); return; }
            for (int i = 0; i < kBlendSlots; ++i)
                if (auto* prm = apvts.getParameter (pid::blendMix[i])) prm->setValueNotifyingHost (1.0f);
            for (int b = 0; b < 5; ++b)
                for (int m = 0; m < kBlendSlots; ++m)
                    if (auto* prm = apvts.getParameter (pid::toneSource[b][m])) prm->setValueNotifyingHost (1.0f);
            if (auto* prm = apvts.getParameter (pid::matrixAuto)) prm->setValueNotifyingHost (1.0f); // knobs blend the captures
            loadAmp (id);
            setStatus ("Blend \"" + juce::String (blendName) + "\" created - the amp knobs blend the models");
        });
    });
}

void NamKnobsProcessor::loadBlendSlot (int slot, const juce::File& namFile)
{
    if (importing.exchange (true)) return;
    const auto* amp = getCurrentAmp();
    const std::string ampId = (amp != nullptr && amp->isBlend) ? amp->id : std::string();
    const auto root = library->getRoot();
    const auto file = std::filesystem::path (namFile.getFullPathName().toStdString());
    setStatus ("Analysing " + namFile.getFileNameWithoutExtension() + " for slot " + juce::String (slot + 1) + "...");
    workers.addJob ([this, slot, ampId, root, file] {
        AmpLibrary worker (root);
        std::string error;
        const auto id = worker.setBlendSlot (ampId, slot, file, "New Blend", error);
        juce::MessageManager::callAsync ([this, id, error, slot, ampId] {
            importing = false;
            library->reload();
            if (id.empty()) { setStatus ("Slot " + juce::String (slot + 1) + " not loaded: " + juce::String (error)); return; }
            if (ampId.empty()) // fresh blend: start every knob of the new slot up, others at their defaults
                for (int m = 0; m < kBlendSlots; ++m)
                {
                    if (auto* prm = apvts.getParameter (pid::blendMix[m])) prm->setValueNotifyingHost (1.0f);
                    for (int b = 0; b < 5; ++b)
                        if (auto* prm = apvts.getParameter (pid::toneSource[b][m])) prm->setValueNotifyingHost (1.0f);
                }
            if (ampId.empty())
                if (auto* prm = apvts.getParameter (pid::matrixAuto)) prm->setValueNotifyingHost (1.0f); // knobs blend the captures
            loadAmp (id);
            setStatus ("Slot " + juce::String (slot + 1) + " loaded");
        });
    });
}

void NamKnobsProcessor::clearBlendSlot (int slot)
{
    const auto* amp = getCurrentAmp();
    if (amp == nullptr || ! amp->isBlend) return;
    if (library->clearBlendSlot (amp->id, slot)) { rebuildRuntime(); sendChangeMessage(); }
}

void NamKnobsProcessor::clearAllBlendSlots()
{
    const auto* amp = getCurrentAmp();
    if (amp == nullptr || ! amp->isBlend) return;
    if (library->clearAllBlendSlots (amp->id))
    {
        rebuildRuntime();
        setStatus ("All loaders emptied");
        sendChangeMessage();
    }
}

void NamKnobsProcessor::deleteAllAmps()
{
    std::vector<std::string> ids;
    for (auto& a : library->getAmps()) ids.push_back (a.id);
    for (auto& id : ids) library->remove (id);
    currentAmpId = {};
    chain.publishRuntime (nullptr);
    setStatus ("Library cleared - drop NAM captures to build a new amp");
    sendChangeMessage();
}

void NamKnobsProcessor::reanalyseCurrentAmp()
{
    const auto* a = getCurrentAmp();
    if (a == nullptr) return;
    AmpDefinition def = *a;
    setStatus ("Re-analysing " + juce::String (def.name) + "...");
    workers.addJob ([this, def]() mutable {
        for (auto& ch : def.channels)
        {
            for (auto& c : ch.captures)
            {
                try { NamModel m; m.load (c.path); c.analysis = ProfileAnalyzer::analyse (m); }
                catch (...) {}
            }
            ChannelManager::refreshEstimates (ch);
        }
        juce::MessageManager::callAsync ([this, def] { updateAmp (def); });
    });
}

// ---- impulse responses -------------------------------------------------------------------------
juce::String NamKnobsProcessor::loadImpulseResponse (int slot, const juce::File& f)
{
    slot = juce::jlimit (0, 1, slot);
    const auto err = chain.getCabinet().slot (slot).loadFile (f);
    if (err.isEmpty())
    {
        irPaths[slot] = f.getFullPathName();
        chain.getCabinet().updateAlignment();
        setStatus ("IR " + juce::String (slot + 1) + ": " + f.getFileNameWithoutExtension());
    }
    else
        setStatus (err);
    sendChangeMessage();
    return err;
}

void NamKnobsProcessor::clearImpulseResponse (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    chain.getCabinet().slot (slot).clear();
    irPaths[slot] = {};
    chain.getCabinet().updateAlignment();
    sendChangeMessage();
}

void NamKnobsProcessor::setEngineOptions (const EngineOptions& o)
{
    const bool osChanged = o.oversampling != options.oversampling || o.linearPhase != options.linearPhase;
    const bool rtChanged = o.maxActive != options.maxActive || o.loudnessComp != options.loudnessComp || o.alwaysOn != options.alwaysOn;
    options = o;
    if (osChanged)
    {
        suspendProcessing (true);
        chain.setOversampling ((SignalChain::OversamplingQuality) options.oversampling, options.linearPhase);
        suspendProcessing (false);
        setLatencySamples (chain.getLatencySamples());
    }
    if (rtChanged) rebuildRuntime();
    sendChangeMessage();
}

// ---- channel memory ------------------------------------------------------------------------------
void NamKnobsProcessor::parameterChanged (const juce::String& id, float v)
{
    if (id == pid::channel) { pendingChannel = (int) v; triggerAsyncUpdate(); }
}

void NamKnobsProcessor::handleAsyncUpdate()
{
    const int newCh = pendingChannel.exchange (-1);
    if (newCh < 0 || restoringChannelKnobs) return;
    const auto* a = getCurrentAmp();
    if (a == nullptr) { lastChannel = newCh; return; }
    const int target = juce::jlimit (0, std::max (0, (int) a->channels.size() - 1), newCh);
    if (target == lastChannel) return;

    AmpDefinition def = *a;
    if (lastChannel < (int) def.channels.size())
        for (int k = 0; k < kNumControls; ++k) def.channels[(size_t) lastChannel].savedKnobs[(size_t) k] = knobParams[(size_t) k]->load();
    library->save (def); // remember the knobs of the channel we leave (no runtime rebuild needed)

    restoringChannelKnobs = true;
    for (int k = 0; k < kNumControls; ++k)
        if (auto* prm = apvts.getParameter (controlId ((Control) k)))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) def.channels[(size_t) target].savedKnobs[(size_t) k]));
    restoringChannelKnobs = false;
    lastChannel = target;
    sendChangeMessage();
}

void NamKnobsProcessor::timerCallback()
{
    chain.collectGarbage();
    if (getLatencySamples() != chain.getLatencySamples()) setLatencySamples (chain.getLatencySamples());
}

// ---- state + presets -------------------------------------------------------------------------------
void NamKnobsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("ampId", currentAmpId, nullptr);
    state.setProperty ("ir1", irPaths[0], nullptr);
    state.setProperty ("ir2", irPaths[1], nullptr);
    state.setProperty ("oversampling", options.oversampling, nullptr);
    state.setProperty ("linearPhase", options.linearPhase, nullptr);
    state.setProperty ("maxActive", options.maxActive, nullptr);
    state.setProperty ("loudnessComp", options.loudnessComp, nullptr);
    state.setProperty ("alwaysOn", options.alwaysOn, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void NamKnobsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.hasType (apvts.state.getType())) return;
        chain.requestDip();
        apvts.replaceState (tree);
        EngineOptions o;
        o.oversampling = juce::jlimit (1, 3, (int) tree.getProperty ("oversampling", 2));
        o.linearPhase = (bool) tree.getProperty ("linearPhase", false);
        o.maxActive = juce::jlimit (1, 6, (int) tree.getProperty ("maxActive", 3));
        o.loudnessComp = (double) tree.getProperty ("loudnessComp", 1.0);
        o.alwaysOn = (bool) tree.getProperty ("alwaysOn", false);
        currentPresetName = tree.getProperty ("presetName").toString();
        for (int s = 0; s < 2; ++s)
        {
            const juce::File f (tree.getProperty (s == 0 ? "ir1" : "ir2").toString());
            if (f.existsAsFile()) loadImpulseResponse (s, f);
            else if (irPaths[s].isNotEmpty()) clearImpulseResponse (s);
        }
        const auto amp = tree.getProperty ("ampId").toString();
        lastChannel = getCurrentChannel();
        if (amp.isNotEmpty() && library->find (amp.toStdString()) != nullptr && amp != currentAmpId)
        {
            currentAmpId = amp;
            rebuildRuntime();
        }
        setEngineOptions (o);
        sendChangeMessage();
    }
}

bool NamKnobsProcessor::savePreset (const juce::String& name)
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    const bool ok = presets->savePreset (name, tree);
    if (ok) currentPresetName = name;
    sendChangeMessage();
    return ok;
}

bool NamKnobsProcessor::loadPreset (const juce::File& f)
{
    auto tree = presets->loadPreset (f);
    if (! tree.isValid()) return false;
    tree.setProperty ("presetName", f.getFileNameWithoutExtension(), nullptr);
    if (auto xml = tree.createXml())
    {
        juce::MemoryBlock mb;
        copyXmlToBinary (*xml, mb);
        setStateInformation (mb.getData(), (int) mb.getSize());
        return true;
    }
    return false;
}
} // namespace namknobs

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new namknobs::NamKnobsProcessor(); }
