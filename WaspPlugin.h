// =============================================================================
//  WaspPlugin.h  -  DSP + processor interface for the Wasp reconstruction
//
//  Waveform sets (the original had NO triangle):
//    OSC1 / OSC2 : Saw, Pulse (with PWM), Sine, Noise
//    OSC3        : Saw, Pulse (50%)
//    LFO1 / LFO2 : Saw, Square (50%), Sine, Noise
// =============================================================================

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace wasp {

constexpr int kMaxVoices            = 32;
constexpr int kSineTableSize        = 4096;
constexpr int kNoiseTableSize       = 4096;
constexpr int kFilterUpdateInterval = 8;   // samples between filter coeff updates

// Parameter IDs - keep stable for preset compatibility.
namespace pid {
    constexpr auto cutoff      = "cutoff";
    constexpr auto resonance   = "resonance";
    constexpr auto env_amount  = "env_amount";
    constexpr auto filter_type = "filter_type";
    constexpr auto kb_track    = "kb_track";

    constexpr auto osc1_coarse = "osc1_coarse";
    constexpr auto osc1_fine   = "osc1_fine";
    constexpr auto osc1_shape  = "osc1_shape";

    constexpr auto osc2_coarse = "osc2_coarse";
    constexpr auto osc2_fine   = "osc2_fine";
    constexpr auto osc2_shape  = "osc2_shape";

    constexpr auto osc3_shape  = "osc3_shape";
    constexpr auto osc3_amount = "osc3_amount";

    constexpr auto mix         = "mix";
    constexpr auto ring_mod    = "ring_mod";
    constexpr auto dual_voice  = "dual_voice";
    constexpr auto fm          = "fm";
    constexpr auto fm_amount   = "fm_amount";
    constexpr auto pw          = "pw";

    constexpr auto lfo1_shape  = "lfo1_shape";
    constexpr auto lfo1_target = "lfo1_target";
    constexpr auto lfo1_sync   = "lfo1_sync";
    constexpr auto lfo1_reset  = "lfo1_reset";
    constexpr auto lfo1_amount = "lfo1_amount";
    constexpr auto lfo1_speed  = "lfo1_speed";

    constexpr auto lfo2_shape  = "lfo2_shape";
    constexpr auto lfo2_target = "lfo2_target";
    constexpr auto lfo2_sync   = "lfo2_sync";
    constexpr auto lfo2_reset  = "lfo2_reset";
    constexpr auto lfo2_amount = "lfo2_amount";
    constexpr auto lfo2_speed  = "lfo2_speed";

    constexpr auto amp_att     = "amp_att";
    constexpr auto amp_dec     = "amp_dec";
    constexpr auto amp_sus     = "amp_sus";
    constexpr auto amp_rel     = "amp_rel";

    constexpr auto filt_att    = "filt_att";
    constexpr auto filt_dec    = "filt_dec";
    constexpr auto filt_sus    = "filt_sus";
    constexpr auto filt_rel    = "filt_rel";

    constexpr auto link_adsr   = "link_adsr";

    constexpr auto dist_on     = "dist_on";
    constexpr auto dist_drive  = "dist_drive";
    constexpr auto dist_tone   = "dist_tone";
} // namespace pid

// Choice indices map directly onto these values.
enum class WaveShape : int { Saw = 0, Pulse = 1, Sine = 2, Noise = 3 };

enum class FilterType : int {
    LP = 0, LP_Notch = 1, LP_Fat = 2, DblNotch = 3, BP = 4, HP = 5
};

// =============================================================================
struct Wavetables
{
    std::array<float, kSineTableSize + 1> sine {};   // +1 guard for interpolation
    std::array<float, kNoiseTableSize>    noise {};

    void  build();
    float fetchSine (float phase) const;   // phase in [0,1)
    float fetchNoise(float phase) const;   // phase in [0,1)
};

// =============================================================================
struct Oscillator
{
    WaveShape shape  = WaveShape::Saw;
    float coarse     = 0.0f;   // octaves
    float fine       = 0.0f;   // semitones
    float phase      = 0.0f;
    float pw         = 0.5f;   // duty cycle (Pulse only)

    void  reset();
    float tick(const Wavetables& tables, float note_hz, float sr,
               float fm_offset_hz = 0.0f, float pitch_offset_semis = 0.0f);
    float computeSample(const Wavetables& tables, float ph) const;
};

// =============================================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void  reset();
    void  setLowpass (float fc, float q, float sr);
    void  setHighpass(float fc, float q, float sr);
    void  setBandpass(float fc, float q, float sr);
    void  setNotch   (float fc, float q, float sr);
    float process(float x);
};

struct FilterSection
{
    FilterType type = FilterType::LP;
    Biquad stageA, stageB;

    void  reset();
    void  configure(float cutoff_hz, float resonance, float sr,
                    float note_hz, bool kb_track);
    float process(float x);
};

// =============================================================================
struct Lfo
{
    WaveShape shape    = WaveShape::Sine;
    float     phase    = 0.0f;
    float     speed_hz = 1.0f;

    void  reset();
    void  advance(int numSamples, float sr);
    float value(const Wavetables& tables) const;
};

// =============================================================================
struct Adsr
{
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    Stage stage = Stage::Idle;
    float value = 0.0f;
    float release_step = 0.0f;

    float attack  = 0.01f;
    float decay   = 0.20f;
    float sustain = 0.70f;
    float release = 0.30f;

    void  noteOn();
    void  noteOff(float sr);
    bool  isIdle() const;
    float tick(float sr);
};

// =============================================================================
struct Distortion
{
    float drive = 0.5f, tone = 0.5f, lp_state = 0.0f;

    void  reset();
    float process(float x);
};

// =============================================================================
struct Voice
{
    Oscillator osc1, osc1b, osc2, osc3;     // osc1b = dual-voice detuned copy
    Adsr amp_adsr, filter_adsr;
    FilterSection filter;

    float    note_hz     = 0.0f;
    float    velocity    = 1.0f;
    int      note_number = -1;
    uint32_t note_id     = 0;
    bool     active      = false;
    bool     held        = false;

    struct Params
    {
        float osc1_coarse = 0.0f, osc1_fine = 0.0f;
        WaveShape osc1_shape = WaveShape::Saw;

        float osc2_coarse = 0.0f, osc2_fine = 0.0f;
        WaveShape osc2_shape = WaveShape::Saw;

        WaveShape osc3_shape  = WaveShape::Pulse;
        float     osc3_amount = 0.0f;

        float mix = 0.5f;
        bool  ring_mod = false, dual_voice = false, fm_enabled = false;
        float fm_amount = 0.0f;
        float pw = 0.0f;                       // 0..1 (0 = 50% duty)

        FilterType filter_type = FilterType::LP;
        float cutoff_hz = 1000.0f, resonance = 0.0f, env_amount = 0.0f;
        bool  kb_track = false;

        float lfo1_pitch = 0.0f, lfo1_filter = 0.0f, lfo1_pw = 0.0f;
        float lfo2_pitch = 0.0f, lfo2_mix = 0.0f,    lfo2_amp = 0.0f;
    };

    void noteOn (float hz, int midiNote, float vel, uint32_t id);
    void noteOff(float sr);

    // Adds this voice into `output` (stereo, summed).
    void render(juce::AudioBuffer<float>& output,
                const Wavetables& tables,
                const Params& p,
                const Adsr& amp_env_params,
                const Adsr& filt_env_params,
                float sr);
};

// =============================================================================
class WaspProcessor : public juce::AudioProcessor
{
public:
    WaspProcessor();

    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;   // defined in WaspEditor.cpp
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool   acceptsMidi() const override;
    bool   producesMidi() const override;
    bool   isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void readVoiceParams(Voice::Params& p);
    void renderVoices(juce::AudioBuffer<float>& buffer, int start, int num,
                      const Voice::Params& vp, const Adsr& amp, const Adsr& filt);

    void handleNoteOn (int note, float velocity);
    void handleNoteOff(int note);
    void allNotesOff();

    float param(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

    Wavetables tables;
    std::array<Voice, kMaxVoices> voices;
    Lfo lfo1, lfo2;
    Distortion distortion;

    float    sample_rate  = 44100.0f;
    uint32_t next_note_id = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaspProcessor)
};

} // namespace wasp
