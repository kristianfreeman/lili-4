// LILI-4 engine: 4 petals of 2 oscillators (FM pairs), LFO pair / Pollinator, dual mod delay, drive.
// Framework-free; the plugin and the tests drive it directly.
#pragma once

#include "lili/Params.h"
#include "lili/Primitives.h"
#include "lili/Wavetable.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace lili {

struct EngineConfig {
    // Seeds the per-pair vibrato rates and the delay noise. The reference
    // seeds from wall-clock time; pass a random value for that behaviour.
    uint32_t seed = 1;
    // Delay the pair/total-feedback FM paths by one 64-sample Pd block
    // instead of one sample (for A/B against reference renders).
    bool legacyBlockFeedback = false;
    // Soft-limit the delay write signal so high feedback cannot run away.
    bool delaySafetySaturator = true;
};

// What the UI animates from. Peaks accumulate until clearTelemetryPeaks();
// the rest are the values at the end of the last process() call. Written
// only by the audio thread.
struct Telemetry {
    std::array<float, kNumVoices> voiceGain{}; // amplitude gain applied to each voice (0..1)
    std::array<float, kNumPairs> pairPeak{};   // peak |pair output|
    std::array<float, kNumPairs> fmPeak{};     // peak |FM index| (source * mod depth)
    float lfoPhaseA = 0.0f;
    float lfoPhaseB = 0.0f;
    float lfoHzA = 0.0f;
    float lfoHzB = 0.0f;
    std::array<float, 2> delayPeak{}; // peak |delay loop signal|
    std::array<float, 2> delayMs{};   // current delay times
    float drivePeak = 0.0f;           // peak |distortion wet| * mix
    float outPeak = 0.0f;
    float totalFbPeak = 0.0f;
};

// A mono sample for the Seed petal engine. The owner (the plugin) keeps it alive
// for as long as the engine may read it; the engine never frees it.
struct SeedSample {
    std::vector<float> data;
    float sampleRate = 48000.0f;
};

class Engine {
  public:
    static constexpr float kMaxDelayMs = 5944.0f;
    static constexpr float kSeedRootHz = 130.8128f; // C3: a voice at this pitch plays the sample as recorded

    void prepare(double sampleRate, const EngineConfig& config = {});
    void reset();

    // Control rate. Cheap enough to call once per audio block.
    void setParams(const Params& params);
    // Pad gate for petal 0..3 (sounds both of its oscillators).
    void setGate(int petal, bool on);
    bool gate(int petal) const { return pairs_[static_cast<size_t>(petal)].gate; }

    // Seed sample for a group (0 = petals 1·2, 1 = 3·4), or nullptr. Any thread; lock-free.
    void setSeed(int group, const SeedSample* sample);

    // Mono engine; `right` may be null or equal to `left`.
    void process(float* left, float* right, int numSamples);

    // Pitch of a voice before smoothing, vibrato and FM (for tests/UI).
    static float voiceFrequency(const Params& params, int voice);

    // Group Pitch knob: the reference's 0.01..2.0 multiplier, snapped to whole
    // semitones (x = 64/127 is exactly 0 st). Range -80..+12 st.
    static int pitchSemitones(float x);
    static float pitchMultiplier(float x);

    // Petal Spread: oscillator B's offset from A in semitones, 12 u^3 with u = 2x - 1
    // (fine detune near the centre, +-12 st at the ends).
    static float spreadSemitones(float x);

    // Audio-thread only; copy it out under the host's own synchronisation.
    // Peaks accumulate across process() calls until clearTelemetryPeaks().
    const Telemetry& telemetry() const { return telemetry_; }
    void clearTelemetryPeaks();

  private:
    // A fixed delay of 1 or 64 samples for the internal FM feedback paths.
    class FeedbackPath {
      public:
        void setDelay(int samples) { delay_ = samples; }
        float read() const { return buf_[static_cast<size_t>((write_ + kSize - delay_) & (kSize - 1))]; }
        void write(float x) {
            buf_[static_cast<size_t>(write_)] = x;
            write_ = (write_ + 1) & (kSize - 1);
        }
        void reset() {
            buf_.fill(0.0f);
            write_ = 0;
        }

      private:
        static constexpr int kSize = 64;
        std::array<float, kSize> buf_{};
        int write_ = 0;
        int delay_ = 1;
    };

    // A petal: two oscillators sharing one pad, envelope and FM routing.
    struct Pair {
        LinearRamp sensor;
        bool gate = false;
        Smoother sharp;
        Smoother mod;
        HighPass1 tapFilter;
        FeedbackPath tap;
        float vibPhase = 0.0f;
        float vibInc = 0.0f;
        bool fast = true;
        int source = 1;
    };

    struct DelayChannel {
        DelayLine line;
        Smoother timeMs;
        Smoother depthMs;
        HighPass1 hp;
        LowPass1 lp;
        LowPass1 selfLp;
        RefCompressor comp;
        RefExpander expander;
        float lastWrite = 0.0f;
    };

    void retriggerSensor(Pair& pair);
    float processSample();

    float sampleRate_ = 44100.0f;
    float invSampleRate_ = 1.0f / 44100.0f;
    EngineConfig config_;
    bool snapOnNextParams_ = true;

    OscBank<kNumVoices> osc_;
    // Garden engine: per-group petal source (docs/SPEC.md).
    std::array<int, kNumGroups> engine_{};
    std::array<Smoother, kNumGroups> table_{};
    alignas(16) std::array<float, kNumVoices> wavePhase_{};
    const WavetableBank* bank_ = nullptr;

    // Seed engine: per-voice two-grain clouds over the group's sample.
    struct Grain {
        double pos = 0.0; // read position in sample frames
        int age = 0;
        bool active = false;
    };
    struct SeedVoice {
        std::array<Grain, 2> grains{};
        int untilNext = 0;
        size_t next = 0;
        Noise rng;
    };
    float seedPetal(SeedVoice& sv, const SeedSample& s, float hz, float timbre, float fm);
    std::array<std::atomic<const SeedSample*>, kNumGroups> seeds_{};
    std::array<SeedVoice, kNumVoices> seedVoices_; // default-init: Noise's default ctor is explicit
    int grainLength_ = 4320;                       // output samples (90 ms)
    // Per-voice pitch smoothing, kept as flat arrays so it vectorises.
    alignas(16) std::array<float, kNumVoices> freq_{};
    alignas(16) std::array<float, kNumVoices> freqTarget_{};
    float freqCoef_ = 1.0f;
    std::array<Pair, kNumPairs> pairs_{};
    std::array<Smoother, kNumGroups> hold_{};

    bool crossSwitch_ = false;
    bool totalFb_ = false;
    bool vibrato_ = false;

    // Hyper LFO
    Smoother lfoFreqA_, lfoFreqB_;
    float lfoPhaseA_ = 0.0f;
    float lfoPhaseB_ = 0.0f;
    bool lfoOr_ = false;
    bool lfoLink_ = false;

    // Pollinator: a Lorenz attractor standing in for the Hyper LFO.
    bool bee_ = false;
    float beeRho_ = 28.0f;
    double lorenzX_ = 1.0, lorenzY_ = 1.0, lorenzZ_ = 20.0;

    // Bloom: per-voice slow value noise, evaluated every kBloomBlock samples.
    static constexpr int kBloomBlock = 32;
    struct BloomVoice {
        std::array<float, 2> from{}; // [0] tune/timbre walk, [1] breath walk, both in [-1, 1]
        std::array<float, 2> to{};
        float t = 0.0f;
    };
    void updateBloom();
    Smoother bloom_;
    float bloomInc_ = 0.0f; // segment progress per bloom block
    int bloomCountdown_ = 0;
    Noise bloomRng_;
    std::array<BloomVoice, kNumVoices> bloomVoices_{};
    std::array<float, kNumVoices> bloomTune_{};   // frequency multiplier
    std::array<float, kNumVoices> bloomTimbre_{}; // added to Sharp
    std::array<float, kNumVoices> bloomBreath_{}; // added to the voice gain

    // Delay
    std::array<DelayChannel, 2> delay_{};
    Smoother delayFeedback_, delayMix_;
    int delaySource_ = 2;
    int delayWaveform_ = 0;
    Noise noise_;

    // Master
    Smoother drive_, distMix_, volume_;
    HighPass1 driveHp_, shapeHp_, totalFbHp_;
    FeedbackPath totalFeedback_;

    Telemetry telemetry_;
};

} // namespace lili
