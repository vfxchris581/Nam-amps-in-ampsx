#include "NamModel.h"

#include <stdexcept>
#include <NAM/dsp.h>
#include <NAM/get_dsp.h>

namespace namified
{
NamModel::NamModel() = default;
NamModel::~NamModel() = default;

void NamModel::load (const std::filesystem::path& file)
{
    nam::dspData config;
    std::unique_ptr<nam::DSP> loaded;
    try
    {
        loaded = nam::get_dsp (file, config);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error ("Could not load NAM model '" + file.filename().string() + "': " + e.what());
    }
    if (loaded == nullptr)
        throw std::runtime_error ("Unsupported NAM model: " + file.filename().string());
    if (loaded->NumInputChannels() != 1 || loaded->NumOutputChannels() != 1)
        throw std::runtime_error ("Only mono-in/mono-out NAM models are supported: " + file.filename().string());

    architecture = config.architecture;
    const double sr = loaded->GetExpectedSampleRate();
    modelRate = sr > 0.0 ? sr : 48000.0; // NAM convention: unknown => 48 kHz
    prewarmSamples = loaded->GetPrewarmSamples();
    dsp = std::move (loaded);
}

void NamModel::prepare (double hostSampleRate, int maxBlockSize)
{
    if (dsp == nullptr) throw std::runtime_error ("NamModel::prepare called before load");
    hostRate = hostSampleRate;
    maxBlock = std::max (1, maxBlockSize);
    needsResampling = std::abs (hostRate - modelRate) > 0.5 && ! dsp->SupportsArbitrarySampleRate();

    if (! needsResampling)
    {
        modelMaxBlock = maxBlock;
        resampleLatency = 0;
        dsp->ResetAndPrewarm (hostRate, modelMaxBlock);
        return;
    }

    const double ratio = modelRate / hostRate;
    modelMaxBlock = (int) std::ceil (maxBlock * ratio) + 8;
    up.prepare (hostRate, modelRate, maxBlock);
    down.prepare (modelRate, hostRate, modelMaxBlock);

    // Total group delay in host samples, plus a small safety margin for the fractional phase.
    const double lat = up.latencyInInputSamples() + down.latencyInInputSamples() / ratio;
    resampleLatency = (int) std::ceil (lat) + 4;

    modelIn.allocate ((size_t) modelMaxBlock * 4 + 64);
    hostOut.allocate ((size_t) maxBlock * 4 + (size_t) resampleLatency + 64);
    scratchIn.assign ((size_t) modelMaxBlock, 0.0);
    scratchOut.assign ((size_t) modelMaxBlock, 0.0);

    dsp->ResetAndPrewarm (modelRate, modelMaxBlock);

    for (int i = 0; i < resampleLatency; ++i) hostOut.push (0.0);
}

void NamModel::process (const double* in, double* out, int n) noexcept
{
    if (dsp == nullptr) { std::fill (out, out + n, 0.0); return; }

    if (! needsResampling)
    {
        // NAM_SAMPLE is double (64-bit) in this build.
        double* inPtr = const_cast<double*> (in);
        double* outPtr = out;
        dsp->process (&inPtr, &outPtr, n);
        return;
    }

    up.process (in, n, modelIn);
    while (modelIn.size() > 0)
    {
        const int m = (int) std::min ((size_t) modelMaxBlock, modelIn.size());
        for (int i = 0; i < m; ++i) scratchIn[(size_t) i] = modelIn.pop();
        double* ip = scratchIn.data();
        double* op = scratchOut.data();
        dsp->process (&ip, &op, m);
        down.process (scratchOut.data(), m, hostOut);
    }
    for (int i = 0; i < n; ++i) out[i] = hostOut.pop();
}

bool NamModel::hasLoudness() const noexcept { return dsp && dsp->HasLoudness(); }
double NamModel::getLoudness() const noexcept { return dsp && dsp->HasLoudness() ? dsp->GetLoudness() : -18.0; }
bool NamModel::hasInputLevel() const noexcept { return dsp && const_cast<nam::DSP&> (*dsp).HasInputLevel(); }
double NamModel::getInputLevelDbu() const noexcept
{
    return hasInputLevel() ? const_cast<nam::DSP&> (*dsp).GetInputLevel() : 12.2;
}
} // namespace namified
