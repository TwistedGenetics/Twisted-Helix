// HELIX for Akai standalone MPCs: the MPC Plugin SDK adapter (MPC OS 3.10.1+, Gen1 32-bit and Gen2 64-bit).
//
// This file is the whole MPC-specific layer: it implements helix::IPlatform on top of mpc::Plugin and forwards
// audio, MIDI, parameters and state to HelixMpcApp (platform-independent, tested on the desktop).
//
// Signal flow: MIDI (pads / keys / sequencer) -> HelixMpcApp (pad browse) -> helix::Engine
//              (2 wavetable osc + sub + noise -> 2 filters -> amp; 4 env, 4 LFO, 2 performers, 16-slot matrix)
//              -> FX chain -> both outputs.
#include "mpc_plugin.h"
#include "HelixMpcApp.h"
#include <memory>

using namespace helix;

static mpc::Param toSdkParam(int index)
{
    const ParamDef& d = mpcParamDef(index);
    mpc::Param p;
    switch (d.kind)
    {
        case ParamKind::Bool: p = mpc::boolParam(d.id, d.name, d.def >= 0.5f); break;
        case ParamKind::Choice: p = mpc::choiceParam(d.id, d.name, *d.choices, (int)d.def); break;
        case ParamKind::Int: p = mpc::intParam(d.id, d.name, (int)d.min, (int)d.max, (int)d.def, d.unit); break;
        case ParamKind::Log: p = mpc::logParam(d.id, d.name, d.min, d.max, d.def, d.unit); break;
        case ParamKind::Squared: p = mpc::floatParam(d.id, d.name, d.min, d.max, d.def, d.unit, 1, 0.5f); break;
        default: p = mpc::floatParam(d.id, d.name, d.min, d.max, d.def, d.unit, 2); break;
    }
    if (mpcIsFiller(index) || index == MP_CPU) p.automatable = false;
    return p;
}

class HelixMpc : public mpc::Plugin, public IPlatform
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "HELIX";
        i.vendor = "Twisted Genetics";
        i.uniqueId = MPC_FOURCC('T', 'g', 'H', 'x');   // == "uid" in plugin.json
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        i.tailSamples = 0;
        return i;
    }

    HelixMpc() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < kMpcNumParams; ++i) addParam(toSdkParam(i));
        // Factory presets as programs (the MPC's preset menu). Plain values cover every parameter; the app
        // applies the rest (tables, LFO shapes) when it sees the program change.
        const auto& f = factoryPresets();
        for (size_t k = 0; k < f.size(); ++k)
        {
            std::vector<float> v((size_t)kMpcNumParams);
            for (int i = 0; i < kMpcNumParams; ++i) v[(size_t)i] = i < kNumParams ? f[k].params[i] : mpcParamDef(i).def;
            v[(size_t)MP_PRESET] = (float)k;
            addProgram(f[k].name.c_str(), v);
        }
        app_ = std::make_unique<HelixMpcApp>(*this);
        for (int i = 0; i < kMpcNumParams; ++i) app_->setParam(i, get(i));
        midi_.reserve(1024);
    }

    ~HelixMpc() override { app_.reset(); }

    // ---- mpc::Plugin
    void prepare(double sampleRate, int maxBlockSize) override { app_->prepare(sampleRate, maxBlockSize); }
    void reset() override {}
    void parameterChanged(int index, float plain) override { if (app_) app_->setParam(index, plain); }
    bool formatValue(int index, float plain, std::string& out) override { return app_ && app_->formatValue(index, plain, out); }

    void process(mpc::ProcessContext& ctx) override
    {
        MidiMsg buf[kMaxMidi];
        int n = 0;
        for (int i = 0; i < ctx.numMidiEvents && n < kMaxMidi; ++i)
        {
            const mpc::MidiEvent& e = ctx.midi[i];
            MidiMsg m;
            m.offset = e.sampleOffset;
            m.data[0] = e.data[0]; m.data[1] = e.data[1]; m.data[2] = e.data[2];
            buf[n++] = m;
        }
        Transport tr;
        tr.valid = ctx.transport.valid && ctx.transport.tempoValid;
        tr.playing = ctx.transport.playing;
        tr.bpm = ctx.transport.bpm;
        tr.ppq = ctx.transport.ppqValid ? ctx.transport.ppqPos : 0.0;
        float* l = ctx.numOutputs > 0 ? ctx.outputs[0] : nullptr;
        float* r = ctx.numOutputs > 1 ? ctx.outputs[1] : l;
        if (!l) return;
        app_->process(l, r, ctx.numFrames, buf, n, tr);
    }

    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const std::string s = app_->saveState();
        out.assign(s.begin(), s.end());
    }
    void loadExtraState(const uint8_t* data, size_t size) override { app_->loadState(std::string((const char*)data, size)); }

    // ---- IPlatform
    const char* platformName() const override { return "MPC (Plugin SDK)"; }
    std::string contentDir() const override { return mpc::contentFolder(); }
    void notifyParameter(int index, float plainValue) override
    {
        set(index, plainValue);            // framework value (the host reads this) -> parameterChanged -> app
        notifyHostParameterChanged(index); // the screen follows
    }
    void requestDisplayRefresh() override { requestDisplayUpdate(); }
    int currentProgram() const override { return mpc::Plugin::currentProgram(); }

private:
    static constexpr int kMaxMidi = 1024;
    std::unique_ptr<HelixMpcApp> app_;
    std::vector<MidiMsg> midi_;
};

MPC_PLUGIN_ENTRY(HelixMpc)
