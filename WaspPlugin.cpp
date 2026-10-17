// =============================================================================
//  WaspPlugin.cpp  -  DSP and processor for the Wasp reconstruction
// =============================================================================

#include "WaspPlugin.h"

#include <algorithm>
#include <cmath>

namespace wasp {

// =============================================================================
//  Wavetables
// =============================================================================

void Wavetables::build()
{
    for (int i = 0; i <= kSineTableSize; ++i)
        sine[(size_t) i] = std::sin(juce::MathConstants<float>::twoPi
                                    * (float) i / (float) kSineTableSize);

    juce::Random rng(0x57A5B00B);   // deterministic
    for (auto& n : noise)
        n = rng.nextFloat() * 2.0f - 1.0f;
}

float Wavetables::fetchSine(float phase) const
{
    const float f = phase * (float) kSineTableSize;
    const int   i = juce::jlimit(0, kSineTableSize - 1, (int) f);
    const float frac = f - (float) i;
    return sine[(size_t) i] + frac * (sine[(size_t) i + 1] - sine[(size_t) i]);
}

float Wavetables::fetchNoise(float phase) const
{
    const int i = juce::jlimit(0, kNoiseTableSize - 1,
                               (int) (phase * (float) kNoiseTableSize));
    return noise[(size_t) i];
}

// =============================================================================
//  Oscillator
// =============================================================================

void Oscillator::reset() { phase = 0.0f; }

float Oscillator::computeSample(const Wavetables& tables, float ph) const
{
    switch (shape)
    {
        case WaveShape::Saw:   return 2.0f * ph - 1.0f;          // naive, aliased (period-correct)
        case WaveShape::Pulse: return ph < pw ? 1.0f : -1.0f;
        case WaveShape::Sine:  return tables.fetchSine(ph);
        case WaveShape::Noise: return tables.fetchNoise(ph);
    }
    return 0.0f;
}

float Oscillator::tick(const Wavetables& tables, float note_hz, float sr,
                       float fm_offset_hz, float pitch_offset_semis)
{
    const float semis = coarse * 12.0f + fine + pitch_offset_semis;
    const float f     = note_hz * std::pow(2.0f, semis / 12.0f) + fm_offset_hz;
    const float inc   = juce::jlimit(-0.5f, 0.5f, f / sr);

    phase += inc;
    phase -= std::floor(phase);            // handles negative increments (FM)

    return computeSample(tables, phase);
}

// =============================================================================
//  Biquad (RBJ cookbook, direct form II transposed)
// =============================================================================

void Biquad::reset() { z1 = z2 = 0.0f; }

void Biquad::setLowpass(float fc, float q, float sr)
{
    const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
    const float cw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = (1.0f - cw) * 0.5f * inv;
    b1 = (1.0f - cw) * inv;
    b2 = b0;
    a1 = -2.0f * cw * inv;
    a2 = (1.0f - alpha) * inv;
}

void Biquad::setHighpass(float fc, float q, float sr)
{
    const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
    const float cw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = (1.0f + cw) * 0.5f * inv;
    b1 = -(1.0f + cw) * inv;
    b2 = b0;
    a1 = -2.0f * cw * inv;
    a2 = (1.0f - alpha) * inv;
}

void Biquad::setBandpass(float fc, float q, float sr)
{
    const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
    const float cw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = alpha * inv;
    b1 = 0.0f;
    b2 = -alpha * inv;
    a1 = -2.0f * cw * inv;
    a2 = (1.0f - alpha) * inv;
}

void Biquad::setNotch(float fc, float q, float sr)
{
    const float w0 = juce::MathConstants<float>::twoPi * fc / sr;
    const float cw = std::cos(w0), alpha = std::sin(w0) / (2.0f * q);
    const float inv = 1.0f / (1.0f + alpha);
    b0 = inv;
    b1 = -2.0f * cw * inv;
    b2 = inv;
    a1 = -2.0f * cw * inv;
    a2 = (1.0f - alpha) * inv;
}

float Biquad::process(float x)
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

// =============================================================================
//  FilterSection
// =============================================================================

void FilterSection::reset() { stageA.reset(); stageB.reset(); }

void FilterSection::configure(float cutoff_hz, float resonance, float sr,
                              float note_hz, bool kb_track)
{
    if (kb_track)
        cutoff_hz *= note_hz / 261.63f;

    cutoff_hz = juce::jlimit(20.0f, sr * 0.45f, cutoff_hz);

    constexpr float butterworth = 0.7071f;
    const float q      = 0.5f + resonance * 9.5f;     // resonance 0..1 -> Q 0.5..10
    const float notchQ = juce::jmax(1.0f, q);

    // 24 dB types: resonance lives in stage A only, so it is not squared
    // by the cascade.
    switch (type)
    {
        case FilterType::LP:
            stageA.setLowpass(cutoff_hz, q, sr);
            stageB.setLowpass(cutoff_hz, butterworth, sr);
            break;
        case FilterType::LP_Notch:
            stageA.setLowpass(cutoff_hz, q, sr);
            stageB.setNotch(cutoff_hz * 1.5f, notchQ, sr);
            break;
        case FilterType::LP_Fat:
            stageA.setLowpass(cutoff_hz,         q * 0.8f, sr);
            stageB.setLowpass(cutoff_hz * 1.05f, q * 0.8f, sr);
            break;
        case FilterType::DblNotch:
            stageA.setNotch(cutoff_hz,        notchQ, sr);
            stageB.setNotch(cutoff_hz * 1.5f, notchQ, sr);
            break;
        case FilterType::BP:
            stageA.setBandpass(cutoff_hz, q, sr);
            stageB.setBandpass(cutoff_hz, q, sr);
            break;
        case FilterType::HP:
            stageA.setHighpass(cutoff_hz, q, sr);
            stageB.setHighpass(cutoff_hz, butterworth, sr);
            break;
    }
}

float FilterSection::process(float x)
{
    float y = stageA.process(x);
    if (type == FilterType::LP_Fat)
        y = std::tanh(y * 1.2f);
    return stageB.process(y);
}

// =============================================================================
//  Lfo
// =============================================================================

void Lfo::reset() { phase = 0.0f; }

void Lfo::advance(int numSamples, float sr)
{
    phase += speed_hz * (float) numSamples / sr;
    phase -= std::floor(phase);
}

float Lfo::value(const Wavetables& tables) const
{
    switch (shape)
    {
        case WaveShape::Saw:   return 2.0f * phase - 1.0f;
        case WaveShape::Pulse: return phase < 0.5f ? 1.0f : -1.0f;   // square
        case WaveShape::Sine:  return tables.fetchSine(phase);
        case WaveShape::Noise: return tables.fetchNoise(phase);
    }
    return 0.0f;
}

// =============================================================================
//  Adsr (linear segments)
// =============================================================================

void Adsr::noteOn()
{
    stage = Stage::Attack;     // value is kept: no click when a voice is stolen
}

void Adsr::noteOff(float sr)
{
    if (stage == Stage::Idle || stage == Stage::Release)
        return;

    if (value <= 0.0f)
    {
        value = 0.0f;
        stage = Stage::Idle;
        return;
    }
    release_step = value / juce::jmax(1.0f, release * sr);
    stage = Stage::Release;
}

bool Adsr::isIdle() const { return stage == Stage::Idle; }

float Adsr::tick(float sr)
{
    switch (stage)
    {
        case Stage::Idle:
            value = 0.0f;
            break;

        case Stage::Attack:
            value += 1.0f / juce::jmax(1.0f, attack * sr);
            if (value >= 1.0f) { value = 1.0f; stage = Stage::Decay; }
            break;

        case Stage::Decay:
            value -= (1.0f - sustain) / juce::jmax(1.0f, decay * sr);
            if (value <= sustain)
            {
                value = sustain;
                stage = sustain <= 0.0001f ? Stage::Idle : Stage::Sustain;
            }
            break;

        case Stage::Sustain:
            value = sustain;
            break;

        case Stage::Release:
            value -= release_step;
            if (value <= 0.0f) { value = 0.0f; stage = Stage::Idle; }
            break;
    }
    return value;
}

// =============================================================================
//  Distortion
// =============================================================================

void Distortion::reset() { lp_state = 0.0f; }

float Distortion::process(float x)
{
    const float clipped = std::tanh(x * (1.0f + drive * 12.0f));

    const float coeff = 0.05f + tone * 0.9f;
    lp_state += coeff * (clipped - lp_state);

    return lp_state * (1.0f - tone) + (clipped - lp_state) * tone;
}

// =============================================================================
//  Voice
// =============================================================================

void Voice::noteOn(float hz, int midiNote, float vel, uint32_t id)
{
    note_hz     = hz;
    note_number = midiNote;
    velocity    = vel;
    note_id     = id;
    active      = true;
    held        = true;

    osc1.reset(); osc1b.reset(); osc2.reset(); osc3.reset();
    filter.reset();
    amp_adsr.noteOn();
    filter_adsr.noteOn();
}

void Voice::noteOff(float sr)
{
    held = false;
    amp_adsr.noteOff(sr);
    filter_adsr.noteOff(sr);
}

void Voice::render(juce::AudioBuffer<float>& output,
                   const Wavetables& tables,
                   const Params& p,
                   const Adsr& amp_env_params,
                   const Adsr& filt_env_params,
                   float sr)
{
    if (!active)
        return;

    amp_adsr.attack  = amp_env_params.attack;
    amp_adsr.decay   = amp_env_params.decay;
    amp_adsr.sustain = amp_env_params.sustain;
    amp_adsr.release = amp_env_params.release;

    filter_adsr.attack  = filt_env_params.attack;
    filter_adsr.decay   = filt_env_params.decay;
    filter_adsr.sustain = filt_env_params.sustain;
    filter_adsr.release = filt_env_params.release;

    const float duty = juce::jlimit(0.01f, 0.99f, 0.5f - p.pw * 0.45f);

    osc1.shape = osc1b.shape = p.osc1_shape;
    osc1.coarse = osc1b.coarse = p.osc1_coarse;
    osc1.fine   = osc1b.fine   = p.osc1_fine;
    osc1.pw = osc1b.pw = duty;

    osc2.shape = p.osc2_shape;
    osc2.coarse = p.osc2_coarse;
    osc2.fine   = p.osc2_fine;
    osc2.pw     = duty;

    osc3.shape  = p.osc3_shape;
    osc3.coarse = -1.0f;                // sub-bass, one octave down
    osc3.fine   = 0.0f;
    osc3.pw     = 0.5f;

    filter.type = p.filter_type;

    const int numSamples = output.getNumSamples();
    auto* left  = output.getWritePointer(0);
    auto* right = output.getNumChannels() > 1 ? output.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float amp_env  = amp_adsr.tick(sr);
        const float filt_env = filter_adsr.tick(sr);

        const float pitch1 = p.lfo1_pitch + p.lfo2_pitch;
        const float pitch2 = p.lfo1_pitch;

        const float osc1_out = osc1.tick(tables, note_hz, sr, 0.0f, pitch1);

        const float fm_offset = p.fm_enabled
            ? osc1_out * p.fm_amount * note_hz * 4.0f : 0.0f;

        const float osc2_out = osc2.tick(tables, note_hz, sr, fm_offset, pitch2);

        float osc1_final = osc1_out;
        if (p.ring_mod)
            osc1_final *= osc2_out;

        const float mix = juce::jlimit(0.0f, 1.0f, p.mix + p.lfo2_mix);
        float v = osc1_final * (1.0f - mix) + osc2_out * mix;

        if (p.dual_voice)
        {
            const float detuned = osc1b.tick(tables, note_hz * 1.005f, sr, 0.0f, pitch1);
            v = (v + detuned) * 0.5f;
        }

        if (p.osc3_amount > 0.001f)
            v += osc3.tick(tables, note_hz, sr) * p.osc3_amount;

        if ((i % kFilterUpdateInterval) == 0)
        {
            const float cutoff = p.cutoff_hz
                * std::pow(2.0f, filt_env * p.env_amount * 4.0f + p.lfo1_filter * 2.0f);
            filter.configure(cutoff, p.resonance, sr, note_hz, p.kb_track);
        }
        const float filtered = filter.process(v);

        const float s = filtered * amp_env * velocity * (1.0f + p.lfo2_amp * 0.5f);

        left[i] += s;
        if (right != nullptr)
            right[i] += s;
    }

    if (amp_adsr.isIdle())
        active = false;
}

// =============================================================================
//  WaspProcessor
// =============================================================================

WaspProcessor::WaspProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createLayout())
{
}

void WaspProcessor::prepareToPlay(double sampleRate, int /*blockSize*/)
{
    sample_rate = (float) sampleRate;
    tables.build();

    for (auto& v : voices)
    {
        v = Voice();
    }
    distortion.reset();
    lfo1.reset();
    lfo2.reset();
    next_note_id = 1;
}

void WaspProcessor::releaseResources() {}

bool WaspProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void WaspProcessor::renderVoices(juce::AudioBuffer<float>& buffer, int start, int num,
                                 const Voice::Params& vp, const Adsr& amp, const Adsr& filt)
{
    if (num <= 0)
        return;

    juce::AudioBuffer<float> segment(buffer.getArrayOfWritePointers(),
                                     buffer.getNumChannels(), start, num);

    for (auto& v : voices)
        if (v.active)
            v.render(segment, tables, vp, amp, filt, sample_rate);
}

void WaspProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    // ---- Block parameters -------------------------------------------------
    Voice::Params vp;
    readVoiceParams(vp);

    Adsr amp;
    amp.attack  = param(pid::amp_att);
    amp.decay   = param(pid::amp_dec);
    amp.sustain = param(pid::amp_sus);
    amp.release = param(pid::amp_rel);

    Adsr filt;
    if (param(pid::link_adsr) > 0.5f)
    {
        filt = amp;
    }
    else
    {
        filt.attack  = param(pid::filt_att);
        filt.decay   = param(pid::filt_dec);
        filt.sustain = param(pid::filt_sus);
        filt.release = param(pid::filt_rel);
    }

    // ---- LFOs (one value per block) ---------------------------------------
    lfo1.shape    = (WaveShape) (int) param(pid::lfo1_shape);
    lfo1.speed_hz = param(pid::lfo1_speed);
    lfo2.shape    = (WaveShape) (int) param(pid::lfo2_shape);
    lfo2.speed_hz = param(pid::lfo2_speed);

    const float l1 = lfo1.value(tables) * param(pid::lfo1_amount);
    const float l2 = lfo2.value(tables) * param(pid::lfo2_amount);
    lfo1.advance(numSamples, sample_rate);
    lfo2.advance(numSamples, sample_rate);

    switch ((int) param(pid::lfo1_target))
    {
        case 0: vp.lfo1_pitch  = l1 * 12.0f; break;
        case 1: vp.lfo1_filter = l1;         break;
        case 2: vp.lfo1_pw     = l1 * 0.25f; break;
    }
    switch ((int) param(pid::lfo2_target))
    {
        case 0: vp.lfo2_pitch = l2 * 12.0f; break;
        case 1: vp.lfo2_mix   = l2;         break;
        case 2: vp.lfo2_amp   = l2;         break;
    }
    vp.pw = juce::jlimit(0.0f, 1.0f, vp.pw + vp.lfo1_pw);

    // ---- Sample-accurate MIDI + rendering ---------------------------------
    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = juce::jlimit(0, numSamples, meta.samplePosition);
        if (evPos > pos)
        {
            renderVoices(buffer, pos, evPos - pos, vp, amp, filt);
            pos = evPos;
        }

        const auto msg = meta.getMessage();
        if      (msg.isNoteOn())                              handleNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())                             handleNoteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())  allNotesOff();
    }
    renderVoices(buffer, pos, numSamples - pos, vp, amp, filt);

    // ---- Distortion (mono voices -> process left, copy to the rest) -------
    if (param(pid::dist_on) > 0.5f && buffer.getNumChannels() > 0)
    {
        distortion.drive = param(pid::dist_drive);
        distortion.tone  = param(pid::dist_tone);

        auto* l = buffer.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            l[i] = distortion.process(l[i]);

        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
    }

    buffer.applyGain(0.7f);
}

// ---- metadata ---------------------------------------------------------------

bool WaspProcessor::hasEditor() const               { return true; }
const juce::String WaspProcessor::getName() const   { return "Wasp Replica"; }
bool WaspProcessor::acceptsMidi() const             { return true; }
bool WaspProcessor::producesMidi() const            { return false; }
bool WaspProcessor::isMidiEffect() const            { return false; }
double WaspProcessor::getTailLengthSeconds() const  { return 0.0; }
int  WaspProcessor::getNumPrograms()                { return 1; }
int  WaspProcessor::getCurrentProgram()             { return 0; }
void WaspProcessor::setCurrentProgram(int)          {}
const juce::String WaspProcessor::getProgramName(int) { return "Default"; }
void WaspProcessor::changeProgramName(int, const juce::String&) {}

// ---- state ------------------------------------------------------------------

void WaspProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void WaspProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ---- parameter layout -------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout WaspProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addFloat = [&](const char* id, const char* name, float lo, float hi,
                        float def, float step = 0.01f, float skew = 1.0f)
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, name,
            juce::NormalisableRange<float>(lo, hi, step, skew), def));
    };
    auto addChoice = [&](const char* id, const char* name,
                         const juce::StringArray& choices, int def)
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ id, 1 }, name, choices, def));
    };
    auto addBool = [&](const char* id, const char* name, bool def)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ id, 1 }, name, def));
    };

    // Filter
    addFloat (pid::cutoff,     "Cutoff",     20.0f, 20000.0f, 2000.0f, 1.0f, 0.3f);
    addFloat (pid::resonance,  "Resonance",  0.0f, 1.0f, 0.0f);
    addFloat (pid::env_amount, "Env Amount", 0.0f, 1.0f, 0.0f);
    addChoice(pid::filter_type, "Filter Type",
              { "LP", "LP+NOTCH", "LP FAT", "DBL NOTCH", "BP", "HP" }, 0);
    addBool  (pid::kb_track,   "KB Track",   false);

    // OSC1 / OSC2
    addFloat (pid::osc1_coarse, "OSC1 Coarse", -3.0f, 3.0f, 0.0f, 1.0f);
    addFloat (pid::osc1_fine,   "OSC1 Fine",   -1.0f, 1.0f, 0.0f);
    addChoice(pid::osc1_shape,  "OSC1 Shape", { "SAW", "PULSE", "SIN", "NOISE" }, 0);

    addFloat (pid::osc2_coarse, "OSC2 Coarse", -3.0f, 3.0f, 0.0f, 1.0f);
    addFloat (pid::osc2_fine,   "OSC2 Fine",   -1.0f, 1.0f, 0.0f);
    addChoice(pid::osc2_shape,  "OSC2 Shape", { "SAW", "PULSE", "SIN", "NOISE" }, 0);

    // OSC3 (sub)
    addChoice(pid::osc3_shape,  "OSC3 Shape", { "SAW", "SQR" }, 1);
    addFloat (pid::osc3_amount, "OSC3 Amount", 0.0f, 1.0f, 0.0f);

    // Mix / routing
    addFloat (pid::mix,        "Mix",         0.0f, 1.0f, 0.5f);
    addBool  (pid::ring_mod,   "Ring Mod",    false);
    addBool  (pid::dual_voice, "Dual Voice",  false);
    addBool  (pid::fm,         "FM",          false);
    addFloat (pid::fm_amount,  "FM Amount",   0.0f, 1.0f, 0.3f);
    addFloat (pid::pw,         "Pulse Width", 0.0f, 1.0f, 0.0f);

    // LFO1
    addChoice(pid::lfo1_shape,  "LFO1 Shape",  { "SAW", "SQR", "SIN", "NOISE" }, 2);
    addChoice(pid::lfo1_target, "LFO1 Target", { "OSC1+2", "FILTER", "PW" }, 0);
    addBool  (pid::lfo1_sync,   "LFO1 Sync",   false);   // reserved (host sync not yet implemented)
    addBool  (pid::lfo1_reset,  "LFO1 Reset",  false);
    addFloat (pid::lfo1_amount, "LFO1 Amount", 0.0f, 1.0f, 0.0f);
    addFloat (pid::lfo1_speed,  "LFO1 Speed",  0.01f, 20.0f, 1.0f, 0.01f, 0.4f);

    // LFO2
    addChoice(pid::lfo2_shape,  "LFO2 Shape",  { "SAW", "SQR", "SIN", "NOISE" }, 2);
    addChoice(pid::lfo2_target, "LFO2 Target", { "OSC1", "MIX", "AMP" }, 0);
    addBool  (pid::lfo2_sync,   "LFO2 Sync",   false);   // reserved
    addBool  (pid::lfo2_reset,  "LFO2 Reset",  false);
    addFloat (pid::lfo2_amount, "LFO2 Amount", 0.0f, 1.0f, 0.0f);
    addFloat (pid::lfo2_speed,  "LFO2 Speed",  0.01f, 20.0f, 1.0f, 0.01f, 0.4f);

    // Envelopes
    addFloat(pid::amp_att, "Amp Attack",  0.001f, 5.0f, 0.01f, 0.001f, 0.4f);
    addFloat(pid::amp_dec, "Amp Decay",   0.001f, 5.0f, 0.20f, 0.001f, 0.4f);
    addFloat(pid::amp_sus, "Amp Sustain", 0.0f,   1.0f, 0.70f);
    addFloat(pid::amp_rel, "Amp Release", 0.001f, 5.0f, 0.30f, 0.001f, 0.4f);

    addFloat(pid::filt_att, "Filter Attack",  0.001f, 5.0f, 0.01f, 0.001f, 0.4f);
    addFloat(pid::filt_dec, "Filter Decay",   0.001f, 5.0f, 0.20f, 0.001f, 0.4f);
    addFloat(pid::filt_sus, "Filter Sustain", 0.0f,   1.0f, 0.70f);
    addFloat(pid::filt_rel, "Filter Release", 0.001f, 5.0f, 0.30f, 0.001f, 0.4f);

    addBool(pid::link_adsr, "Link ADSR", false);

    // Distortion
    addBool (pid::dist_on,    "Distortion", false);
    addFloat(pid::dist_drive, "Dist Drive", 0.0f, 1.0f, 0.5f);
    addFloat(pid::dist_tone,  "Dist Tone",  0.0f, 1.0f, 0.5f);

    return layout;
}

void WaspProcessor::readVoiceParams(Voice::Params& p)
{
    p.osc1_coarse = param(pid::osc1_coarse);
    p.osc1_fine   = param(pid::osc1_fine);
    p.osc1_shape  = (WaveShape) (int) param(pid::osc1_shape);

    p.osc2_coarse = param(pid::osc2_coarse);
    p.osc2_fine   = param(pid::osc2_fine);
    p.osc2_shape  = (WaveShape) (int) param(pid::osc2_shape);

    p.osc3_shape  = (WaveShape) (int) param(pid::osc3_shape);   // 0 Saw, 1 Pulse
    p.osc3_amount = param(pid::osc3_amount);

    p.mix        = param(pid::mix);
    p.ring_mod   = param(pid::ring_mod)   > 0.5f;
    p.dual_voice = param(pid::dual_voice) > 0.5f;
    p.fm_enabled = param(pid::fm)         > 0.5f;
    p.fm_amount  = param(pid::fm_amount);
    p.pw         = param(pid::pw);

    p.filter_type = (FilterType) (int) param(pid::filter_type);
    p.cutoff_hz   = param(pid::cutoff);
    p.resonance   = param(pid::resonance);
    p.env_amount  = param(pid::env_amount);
    p.kb_track    = param(pid::kb_track) > 0.5f;

    p.lfo1_pitch = p.lfo1_filter = p.lfo1_pw = 0.0f;
    p.lfo2_pitch = p.lfo2_mix = p.lfo2_amp = 0.0f;
}

// ---- notes --------------------------------------------------------------------

void WaspProcessor::handleNoteOn(int note, float velocity)
{
    const float hz = 440.0f * std::pow(2.0f, (float) (note - 69) / 12.0f);

    Voice* target = nullptr;
    for (auto& v : voices)
        if (!v.active) { target = &v; break; }

    if (target == nullptr)
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.note_id < target->note_id)
                target = &v;
    }

    target->noteOn(hz, note, velocity, next_note_id++);

    if (param(pid::lfo1_reset) > 0.5f) lfo1.reset();
    if (param(pid::lfo2_reset) > 0.5f) lfo2.reset();
}

void WaspProcessor::handleNoteOff(int note)
{
    for (auto& v : voices)
        if (v.active && v.held && v.note_number == note)
            v.noteOff(sample_rate);
}

void WaspProcessor::allNotesOff()
{
    for (auto& v : voices)
        if (v.active)
            v.noteOff(sample_rate);
}

} // namespace wasp

// =============================================================================
//  JUCE plugin factory
// =============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wasp::WaspProcessor();
}
