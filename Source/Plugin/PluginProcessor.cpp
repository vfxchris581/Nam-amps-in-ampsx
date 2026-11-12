#include "PluginProcessor.h"
#include "GUI/PluginEditor.h"
#include "ChannelManager/ChannelManager.h"

namespace namvx
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
                        if (unit == "ms") return juce::String (v, 2) + " ms";
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

juce::File defaultLibraryRoot()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/Nam VX");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Nam VX");
   #endif
}

} // namespace

NamVXProcessor::NamVXProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts (*this, nullptr, "NAMVX", createLayout())
{
    init (defaultLibraryRoot());
}

NamVXProcessor::NamVXProcessor (const juce::File& root)
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts (*this, nullptr, "NAMVX", createLayout())
{
    init (root);
}

void NamVXProcessor::init (const juce::File& root)
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
    for (int i = 0; i < 4; ++i) rp.blendMix[i] = raw (pid::blendMix[i]);
    for (int b = 0; b < 5; ++b) for (int m = 0; m < 4; ++m) rp.toneSource[b][m] = raw (pid::toneSource[b][m]);
    apvts.addParameterListener (pid::channel, this);
    chain.prepare (48000.0, 512);
    setStatus (library->getAmps().empty() ? "Drop NAM captures of one amp to build your first virtual amplifier"
                                          : "Ready");
    if (! library->getAmps().empty()) loadAmp (library->getAmps().front().id);
    startTimerHz (10);
}

NamVXProcessor::~NamVXProcessor()
{
    stopTimer();
    apvts.removeParameterListener (pid::channel, this);
    workers.removeAllJobs (true, 20000);
}

bool NamVXProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

void NamVXProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool changed = sampleRate != preparedRate || samplesPerBlock > preparedBlock;
    preparedRate = sampleRate;
    preparedBlock = std::max (samplesPerBlock, preparedBlock);
    chain.prepare (sampleRate, std::max (64, preparedBlock));
    chain.setOversampling ((SignalChain::OversamplingQuality) options.oversampling, options.linearPhase);
    if (changed || chain.hasAmp.load() == false) rebuildRuntime();
    setLatencySamples (chain.getLatencySamples());
}

void NamVXProcessor::snapshotParameters (EngineParameters& p) const noexcept
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
    for (int i = 0; i < 4; ++i) p.blendMix[(size_t) i] = f (rp.blendMix[i]);
    for (int b = 0; b < 5; ++b) for (int m = 0; m < 4; ++m) p.toneSource[(size_t) b][(size_t) m] = f (rp.toneSource[b][m]);
}

template <typename T>
void NamVXProcessor::processAny (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    EngineParameters p;
    snapshotParameters (p);
    chain.setParameters (p);
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    chain.process (buffer.getArrayOfWritePointers(), std::min (numIn, buffer.getNumChannels()),
                   std::min (numOut, buffer.getNumChannels()), buffer.getNumSamples());
}

void NamVXProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) { processAny (b); }
void NamVXProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { processAny (b); }

juce::AudioProcessorEditor* NamVXProcessor::createEditor() { return new NamVXEditor (*this); }

// ---- amps -----------------------------------------------------------------------------------
const AmpDefinition* NamVXProcessor::getCurrentAmp() const { return library->find (currentAmpId.toStdString()); }

int NamVXProcessor::getCurrentChannel() const
{
    const auto* a = getCurrentAmp();
    const int ch = (int) rp.channel->load();
    return a ? juce::jlimit (0, std::max (0, (int) a->channels.size() - 1), ch) : 0;
}

void NamVXProcessor::setStatus (const juce::String& s)
{
    { std::lock_guard<std::mutex> l (statusLock); status = s; }
    sendChangeMessage();
}

juce::String NamVXProcessor::getStatus() const
{
    std::lock_guard<std::mutex> l (statusLock);
    return status;
}

void NamVXProcessor::loadAmp (const juce::String& ampId)
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

void NamVXProcessor::rebuildRuntime()
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

void NamVXProcessor::updateAmp (const AmpDefinition& def, bool rebuild)
{
    library->save (def);
    if (def.id == currentAmpId.toStdString() && rebuild) rebuildRuntime();
    sendChangeMessage();
}

void NamVXProcessor::deleteAmp (const juce::String& ampId)
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

void NamVXProcessor::importFiles (const juce::StringArray& paths)
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

std::vector<PendingCapture> NamVXProcessor::getPending() const { return pending; }

void NamVXProcessor::assignPending (int index, const juce::String& make, const juce::String& model, const juce::String& channel)
{
    if (index < 0 || index >= (int) pending.size() || make.trim().isEmpty() || model.trim().isEmpty()) return;
    const auto id = library->assignPending (pending[(size_t) index], make.trim().toStdString(), model.trim().toStdString(),
                                            channel.trim().toStdString());
    pending.erase (pending.begin() + index);
    loadAmp (id);
}

void NamVXProcessor::discardPending (int index)
{
    if (index >= 0 && index < (int) pending.size()) pending.erase (pending.begin() + index);
    sendChangeMessage();
}

void NamVXProcessor::createBlend (const juce::String& name, const juce::StringArray& namFiles)
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
            for (int i = 0; i < 4; ++i)
                if (auto* prm = apvts.getParameter (pid::blendMix[i])) prm->setValueNotifyingHost (1.0f);
            for (int b = 0; b < 5; ++b)
                for (int m = 0; m < 4; ++m)
                    if (auto* prm = apvts.getParameter (pid::toneSource[b][m])) prm->setValueNotifyingHost (1.0f);
            loadAmp (id);
            setStatus ("Blend \"" + juce::String (blendName) + "\" created - set the mix on the Amp page");
        });
    });
}

void NamVXProcessor::reanalyseCurrentAmp()
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
juce::String NamVXProcessor::loadImpulseResponse (int slot, const juce::File& f)
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

void NamVXProcessor::clearImpulseResponse (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    chain.getCabinet().slot (slot).clear();
    irPaths[slot] = {};
    chain.getCabinet().updateAlignment();
    sendChangeMessage();
}

void NamVXProcessor::setEngineOptions (const EngineOptions& o)
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
void NamVXProcessor::parameterChanged (const juce::String& id, float v)
{
    if (id == pid::channel) { pendingChannel = (int) v; triggerAsyncUpdate(); }
}

void NamVXProcessor::handleAsyncUpdate()
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

void NamVXProcessor::timerCallback()
{
    chain.collectGarbage();
    if (getLatencySamples() != chain.getLatencySamples()) setLatencySamples (chain.getLatencySamples());
}

// ---- state + presets -------------------------------------------------------------------------------
void NamVXProcessor::getStateInformation (juce::MemoryBlock& dest)
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

void NamVXProcessor::setStateInformation (const void* data, int size)
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

bool NamVXProcessor::savePreset (const juce::String& name)
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

bool NamVXProcessor::loadPreset (const juce::File& f)
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
} // namespace namvx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new namvx::NamVXProcessor(); }
