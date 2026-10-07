// Engine tests. Dependency-free on purpose: `ctest` or run the binary directly.
#include "lili/Engine.h"
#include "lili/Wavetable.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

struct Stats {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Stats render(lili::Engine& e, int samples, std::vector<float>* capture = nullptr) {
    Stats s;
    std::vector<float> buf(512);
    double sumSq = 0.0;
    for (int done = 0; done < samples; done += 512) {
        const int n = std::min(512, samples - done);
        e.process(buf.data(), nullptr, n);
        for (int i = 0; i < n; ++i) {
            const float x = buf[static_cast<size_t>(i)];
            s.finite = s.finite && std::isfinite(x);
            s.peak = std::max(s.peak, std::fabs(x));
            sumSq += static_cast<double>(x) * x;
            if (capture != nullptr) {
                capture->push_back(x);
            }
        }
    }
    s.rms = std::sqrt(sumSq / samples);
    return s;
}

constexpr double kSr = 48000.0;

void testSilentWhenIdle() {
    lili::Engine e;
    e.prepare(kSr);
    e.setParams({});
    const auto s = render(e, 48000);
    check(s.finite, "idle output finite");
    check(s.peak < 1e-4f, "idle output silent (peak " + std::to_string(s.peak) + ")");
}

void testGateProducesSound() {
    lili::Engine e;
    e.prepare(kSr);
    e.setParams({});
    e.setGate(0, true);
    const auto s = render(e, 48000);
    check(s.finite, "gated output finite");
    check(s.rms > 1e-3, "gated voice audible (rms " + std::to_string(s.rms) + ")");
    check(s.peak < 2.0f, "gated voice bounded");
}

void testReleaseDecays() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.fast[0] = true; // 100 ms release
    e.setParams(p);
    e.setGate(0, true);
    render(e, 24000);
    e.setGate(0, false);
    render(e, 9600); // 200 ms
    const auto tail = render(e, 4800);
    check(tail.peak < 1e-3f, "fast release reaches silence (peak " + std::to_string(tail.peak) + ")");
}

void testHoldDrones() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.hold = {1.0f, 0.0f};
    e.setParams(p);
    render(e, 4800);
    const auto s = render(e, 48000);
    check(s.rms > 1e-3, "hold 1·2 drones without gates");
}

void testVoiceFrequencyTable() {
    lili::Params p;
    p.pitch = {64 / 127.f, 64 / 127.f};
    const float mul = lili::Engine::pitchMultiplier(64 / 127.f);
    check(std::fabs(mul - 1.0f) < 1e-6f, "default group pitch is exactly unity");
    check(lili::Engine::pitchSemitones(1.0f) == 12, "pitch knob top = +12 st");
    for (int i = 0; i <= 100; ++i) {
        const float semis = 12.0f * std::log2(lili::Engine::pitchMultiplier(static_cast<float>(i) / 100.0f));
        check(std::fabs(semis - std::round(semis)) < 1e-3f, "group pitch lands on whole semitones");
    }
    // Petal tune spans C1..C7; oscillator A (even voices) is the petal's pitch.
    p.tune[0] = 0.0f;
    check(std::fabs(lili::Engine::voiceFrequency(p, 0) / lili::mtof(24.f) - 1.0f) < 1e-5f,
          "petal 1 low end is C1");
    p.tune[3] = 1.0f;
    check(std::fabs(lili::Engine::voiceFrequency(p, 6) / lili::mtof(96.f) - 1.0f) < 1e-5f,
          "petal 4 high end is C7");
    // Spread: oscillator B sits 12 u^3 semitones from A.
    check(std::fabs(lili::Engine::spreadSemitones(0.5f)) < 1e-6f, "spread centre is unison");
    check(std::fabs(lili::Engine::spreadSemitones(1.0f) - 12.0f) < 1e-5f, "spread top is +1 octave");
    check(std::fabs(lili::Engine::spreadSemitones(0.0f) + 12.0f) < 1e-5f, "spread bottom is -1 octave");
    p.spread[1] = 1.0f;
    const float ratio = lili::Engine::voiceFrequency(p, 3) / lili::Engine::voiceFrequency(p, 2);
    check(std::fabs(ratio - 2.0f) < 1e-4f, "petal 2 oscillator B an octave above A at full spread");
    const float cents = 1200.0f * lili::Engine::spreadSemitones(lili::Params{}.spread[0]) / 12.0f;
    check(cents > 0.5f && cents < 5.0f,
          "default spread is a gentle detune (" + std::to_string(cents) + " c)");
    p.quantize = true;
    p.tune[2] = 0.5f;
    const float q = lili::ftom(lili::Engine::voiceFrequency(p, 4));
    check(std::fabs(q - std::round(q)) < 1e-3f, "quantize snaps to semitones");
}

void testOscillatorPitch() {
    lili::OscBank<1> osc;
    osc.prepare(static_cast<float>(kSr));
    osc.reset(0.5f);
    const float hz = 220.0f;
    const float pw = 0.5f;
    float sq = 0.0f;
    float tri = 0.0f;
    int crossings = 0;
    osc.process(&hz, &pw, &sq, &tri);
    float prev = tri;
    for (int i = 0; i < 48000; ++i) {
        osc.process(&hz, &pw, &sq, &tri);
        if (prev < 0.0f && tri >= 0.0f) {
            ++crossings;
        }
        prev = tri;
    }
    check(std::abs(crossings - 220) <= 1, "oscillator runs at 220 Hz (" + std::to_string(crossings) + ")");
}

void testFastExp() {
    float worst = 0.0f;
    for (int i = -40000; i <= 10000; ++i) {
        const float x = static_cast<float>(i) * 0.001f;
        const float ref = std::exp(x);
        worst = std::max(worst, std::fabs(lili::fastExp(x) - ref) / ref);
    }
    check(worst < 5e-6f, "fastExp relative error " + std::to_string(worst));
}

void testDelayLineInterpolation() {
    lili::DelayLine d;
    d.prepare(1024);
    for (int i = 0; i < 100; ++i) {
        d.push(static_cast<float>(i));
    }
    // Most recent is 99 at delay 1; a ramp must interpolate exactly.
    check(std::fabs(d.read(1.0f) - 99.0f) < 1e-4f, "delay 1 = newest");
    check(std::fabs(d.read(10.0f) - 90.0f) < 1e-4f, "delay 10");
    check(std::fabs(d.read(10.25f) - 89.75f) < 1e-3f, "fractional delay");
}

void testDeterministicWithSeed() {
    std::vector<float> a;
    std::vector<float> b;
    for (auto* out : {&a, &b}) {
        lili::Engine e;
        lili::EngineConfig cfg;
        cfg.seed = 42;
        e.prepare(kSr, cfg);
        lili::Params p;
        p.vibrato = true;
        p.delayMix = 0.5f;
        e.setParams(p);
        for (int v = 0; v < lili::kNumPetals; ++v) {
            e.setGate(v, true);
        }
        render(e, 9600, out);
    }
    check(a == b, "same seed renders identically");
}

// Everything at the extremes, including runaway delay feedback and total FB.
void testStressExtremes() {
    for (const bool legacy : {false, true}) {
        lili::Engine e;
        lili::EngineConfig cfg;
        cfg.legacyBlockFeedback = legacy;
        e.prepare(kSr, cfg);
        lili::Params p;
        p.sharp.fill(1.0f);
        p.mod.fill(1.0f);
        p.source = {0, 2, 0, 2};
        p.hold = {1.0f, 1.0f};
        p.pitch = {1.0f, 1.0f};
        p.tune.fill(1.0f);
        p.totalFb = true;
        p.vibrato = true;
        p.lfoFreqA = p.lfoFreqB = 1.0f;
        p.lfoLink = true;
        p.delayTime = {0.0f, 0.3f};
        p.delayFeedback = 1.0f;
        p.delayMix = 1.0f;
        p.delayModDepth = {1.0f, 1.0f};
        p.delaySource = 0;
        p.drive = 1.0f;
        p.distMix = 1.0f;
        e.setParams(p);
        for (int v = 0; v < lili::kNumPetals; ++v) {
            e.setGate(v, true);
        }
        const auto s = render(e, static_cast<int>(kSr * 10));
        const std::string tag = legacy ? " (legacy)" : "";
        check(s.finite, "extremes stay finite" + tag);
        check(s.peak < 4.0f, "extremes bounded (peak " + std::to_string(s.peak) + ")" + tag);
    }
}

void testTelemetry() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.source[0] = 2; // pair 12 FM'd by the LFO
    p.mod[0] = 1.0f;
    e.setParams(p);
    e.setGate(0, true);
    render(e, 24000);
    e.clearTelemetryPeaks();
    render(e, 512);
    const auto& t = e.telemetry();
    check(t.voiceGain[0] > 0.99f, "telemetry: gated voice at full gain");
    check(t.voiceGain[4] == 0.0f, "telemetry: idle voice at zero gain");
    check(t.pairPeak[0] > 0.01f && t.pairPeak[2] < 1e-6f, "telemetry: pair peaks follow the gate");
    check(t.fmPeak[0] > 0.5f, "telemetry: FM depth reported for LFO-modulated pair");
    check(t.outPeak > 0.01f, "telemetry: output peak");
    check(std::fabs(t.lfoHzA - lili::mtof(127.f * (64 / 127.f) * (64 / 127.f) - 75.f)) < 0.01f,
          "telemetry: LFO A rate");
}

// Magnitude of harmonic k in one wavetable cycle (direct DFT).
double harmonicMagnitude(const float* t, int k) {
    const int n = lili::WavetableBank::kSize;
    double re = 0.0;
    double im = 0.0;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * 3.141592653589793 * k * i / n;
        re += t[i] * std::cos(a);
        im -= t[i] * std::sin(a);
    }
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

void testWavetableBandLimited() {
    const auto& bank = lili::WavetableBank::instance();
    double worst = 0.0;
    for (int fam = 0; fam < lili::WavetableBank::kFamilies; ++fam) {
        for (int level = 0; level < lili::WavetableBank::kLevels; ++level) {
            const int limit = (lili::WavetableBank::kSize / 2) >> level;
            const float* t = bank.frame(fam, 3, level);
            for (int k = limit + 1; k < std::min(limit + 24, lili::WavetableBank::kSize / 2); ++k) {
                worst = std::max(worst, harmonicMagnitude(t, k));
            }
        }
    }
    check(worst < 1e-4, "wavetable mips have no energy above their limit (" + std::to_string(worst) + ")");
    // The level chosen for a pitch must keep its top harmonic below Nyquist.
    for (const float hz : {50.0f, 440.0f, 2000.0f, 9000.0f}) {
        const float dt = hz / 48000.0f;
        const int limit = (lili::WavetableBank::kSize / 2) >> lili::WavetableBank::levelFor(dt);
        check(static_cast<float>(limit) * dt <= 0.5f,
              "mip level alias-free at " + std::to_string(hz) + " Hz");
    }
}

void testWavetablePitchAndRange() {
    const auto& bank = lili::WavetableBank::instance();
    const float dt = 220.0f / 48000.0f;
    float phase = 0.0f;
    int crossings = 0;
    float prev = bank.read(0.0f, 0.0f, phase, dt);
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        phase += dt;
        phase -= std::floor(phase);
        const float x = bank.read(0.0f, 0.0f, phase, dt);
        crossings += (prev < 0.0f && x >= 0.0f) ? 1 : 0;
        prev = x;
    }
    check(std::abs(crossings - 220) <= 1, "wavetable runs at 220 Hz (" + std::to_string(crossings) + ")");
    // Every family and morph frame sits at the RMS of a unit sine, with a bounded crest.
    float lowRms = 1e9f;
    float highRms = 0.0f;
    for (int f = 0; f < lili::WavetableBank::kFamilies; ++f) {
        for (int m = 0; m < lili::WavetableBank::kFrames; ++m) {
            const float morph = static_cast<float>(m) / static_cast<float>(lili::WavetableBank::kFrames - 1);
            double energy = 0.0;
            for (int i = 0; i < 1024; ++i) {
                const float x = bank.read(static_cast<float>(f), morph, static_cast<float>(i) / 1024.0f, dt);
                energy += static_cast<double>(x) * x;
                peak = std::max(peak, std::fabs(x));
            }
            const auto rms = static_cast<float>(std::sqrt(energy / 1024.0));
            lowRms = std::min(lowRms, rms);
            highRms = std::max(highRms, rms);
        }
    }
    check(lowRms > 0.6f && highRms < 0.8f, "wavetables share one loudness (rms " + std::to_string(lowRms) +
                                               ".." + std::to_string(highRms) + ")");
    check(peak < 5.0f, "wavetable crest stays bounded (peak " + std::to_string(peak) + ")");
}

void testWavetableFundamental() {
    // Every frame, and every point between frames and families, keeps a real fundamental: at least
    // 0.55 of a unit sine's amplitude (-5.2 dB), so no table is all overtones and morphing can't cancel it.
    const auto& bank = lili::WavetableBank::instance();
    const float dt = 65.0f / 48000.0f;
    constexpr int kN = lili::WavetableBank::kSize;
    std::vector<float> cycle(kN + 1);
    double weakest = 1e9;
    std::string where;
    for (int f2 = 0; f2 <= 2 * (lili::WavetableBank::kFamilies - 1); ++f2) {
        for (int m2 = 0; m2 <= 2 * (lili::WavetableBank::kFrames - 1); ++m2) {
            const float family = 0.5f * static_cast<float>(f2);
            const float morph =
                static_cast<float>(m2) / static_cast<float>(2 * (lili::WavetableBank::kFrames - 1));
            for (int i = 0; i < kN; ++i) {
                cycle[static_cast<size_t>(i)] = bank.read(family, morph, static_cast<float>(i) / kN, dt);
            }
            const double h1 = harmonicMagnitude(cycle.data(), 1);
            if (h1 < weakest) {
                weakest = h1;
                where = "family " + std::to_string(family) + " morph " + std::to_string(morph);
            }
        }
    }
    check(weakest > 0.55,
          "wavetables keep their fundamental (weakest " + std::to_string(weakest) + " at " + where + ")");
}

// RMS of petal 1 alone (dry, no drive) after the attack.
double petalRms(const lili::Params& base, float tune, int engine, float table, float timbre) {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p = base;
    p.distMix = 0.0f;
    p.spread[0] = 0.5f; // unison, so the two oscillators don't beat during the measurement
    p.tune[0] = tune;
    p.engine[0] = engine;
    p.table[0] = table;
    p.sharp[0] = timbre;
    e.setParams(p);
    e.setGate(0, true);
    render(e, 24000);
    return render(e, 48000).rms;
}

void testWaveTracksClassicLevel() {
    // The Classic triangle is louder in the low register (its 100 Hz smoothing floor); the Wave body must
    // follow it there, and sit at the same level higher up.
    const lili::Params base;
    for (const float tune : {0.0f, 12 / 72.f, 24 / 72.f, 48 / 72.f}) {
        const double classic = petalRms(base, tune, lili::PetalClassic, 0.0f, 0.0f);
        for (const float table : {0.0f, 1 / 3.f, 2 / 3.f, 1.0f}) {
            const double wave = petalRms(base, tune, lili::PetalWave, table, 0.5f);
            const double db = 20.0 * std::log10(wave / classic);
            check(db > -4.5 && db < 2.0, "wave level tracks classic at C" +
                                             std::to_string(1 + static_cast<int>(tune * 6)) + ", table " +
                                             std::to_string(table) + " (" + std::to_string(db) + " dB)");
        }
    }
}

void testWaveEngine() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.engine = {lili::PetalWave, lili::PetalWave};
    p.table = {0.4f, 0.9f};
    p.sharp = {0.3f, 0.6f, 0.9f, 0.1f};
    p.mod = {0.8f, 0.0f, 0.5f, 0.0f};
    p.source = {0, 1, 2, 1};
    e.setParams(p);
    for (int v = 0; v < lili::kNumPetals; ++v) {
        e.setGate(v, true);
    }
    const auto s = render(e, 48000);
    check(s.finite && s.rms > 1e-3 && s.peak < 2.0f, "wave engine is audible, finite and bounded");
}

lili::SeedSample sineSeed(float hz, float seconds) {
    lili::SeedSample s;
    s.sampleRate = 48000.0f;
    s.data.resize(static_cast<size_t>(seconds * s.sampleRate));
    for (size_t i = 0; i < s.data.size(); ++i) {
        s.data[i] = std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) / s.sampleRate);
    }
    return s;
}

void testSeedSilentWithoutSample() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.engine = {lili::PetalSeed, lili::PetalSeed};
    e.setParams(p);
    for (int v = 0; v < lili::kNumPetals; ++v) {
        e.setGate(v, true);
    }
    render(e, 9600); // let the touch thump pass
    const auto s = render(e, 24000);
    check(s.finite && s.peak < 1e-3f, "seed voices are silent until a sample is loaded");
}

void testSeedFollowsTune() {
    // A C3 sine played by a voice sounds at that voice's own frequency.
    const auto seed = sineSeed(lili::Engine::kSeedRootHz, 2.0f);
    lili::Engine e;
    e.prepare(kSr);
    e.setSeed(0, &seed);
    lili::Params p;
    p.engine = {lili::PetalSeed, lili::PetalClassic};
    p.distMix = 0.0f; // keep the output linear
    p.tune[0] = 0.6f;
    e.setParams(p);
    e.setGate(0, true);
    render(e, 24000);
    std::vector<float> out;
    render(e, 48000, &out);
    int crossings = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        crossings += (out[i - 1] < 0.0f && out[i] >= 0.0f) ? 1 : 0;
    }
    const float expected = lili::Engine::voiceFrequency(p, 0);
    check(std::fabs(static_cast<float>(crossings) - expected) < 0.05f * expected,
          "seed pitch follows Tune (" + std::to_string(crossings) + " vs " + std::to_string(expected) +
              " Hz)");
}

void testSeedBounded() {
    const auto seed = sineSeed(220.0f, 0.5f);
    lili::Engine e;
    e.prepare(kSr);
    e.setSeed(0, &seed);
    e.setSeed(1, &seed);
    lili::Params p;
    p.engine = {lili::PetalSeed, lili::PetalSeed};
    p.mod.fill(1.0f);
    p.source = {0, 2, 0, 2};
    p.sharp = {0.0f, 0.4f, 0.99f, 1.0f};
    p.tune.fill(1.0f);
    p.pitch = {1.0f, 1.0f};
    e.setParams(p);
    for (int v = 0; v < lili::kNumPetals; ++v) {
        e.setGate(v, true);
    }
    const auto s = render(e, 96000);
    check(s.finite && s.peak < 4.0f && s.rms > 1e-3, "seed engine bounded under full FM, extreme tune");
}

void testBloomOffIsIdentical() {
    // Bloom 0 must not change the sound at all, whatever Drift says.
    std::vector<float> a;
    std::vector<float> b;
    for (auto* out : {&a, &b}) {
        lili::Engine e;
        e.prepare(kSr);
        lili::Params p;
        p.drift = out == &a ? 0.4f : 1.0f;
        p.sharp = {0.2f, 0.5f, 0.7f, 0.9f};
        e.setParams(p);
        for (int v = 0; v < lili::kNumPetals; v += 2) {
            e.setGate(v, true);
        }
        render(e, 24000, out);
    }
    check(a == b, "bloom 0 is bit-identical to the classic engine");
}

void testBloomWakesVoices() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.bloom = 1.0f;
    p.drift = 1.0f; // ~8 s cycles
    e.setParams(p);
    const auto s = render(e, static_cast<int>(kSr * 20));
    check(s.finite && s.rms > 1e-3 && s.peak < 2.0f, "bloom breathes voices awake without any gates");
}

void testPollinatorBounded() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p;
    p.bee = true;
    p.lfoFreqA = 1.0f; // fastest flight
    p.lfoFreqB = 1.0f; // most chaos
    p.source = {2, 2, 2, 2};
    p.mod.fill(1.0f);
    p.delaySource = 2;
    p.delayModDepth = {1.0f, 1.0f};
    p.delayMix = 0.5f;
    e.setParams(p);
    for (int v = 0; v < lili::kNumPetals; ++v) {
        e.setGate(v, true);
    }
    const auto s = render(e, static_cast<int>(kSr * 20));
    const auto& t = e.telemetry();
    check(s.finite && s.peak < 4.0f, "pollinator stays finite and bounded");
    check(t.lfoHzA == 0.0f && (t.lfoPhaseA == 0.0f || t.lfoPhaseA == 0.5f),
          "pollinator drives the leaf LEDs");
}

// --- Stereo ----------------------------------------------------------------

// Renders in 512-sample blocks into L and R (appended).
void renderStereo(lili::Engine& e, int samples, std::vector<float>& left, std::vector<float>& right) {
    std::vector<float> l(512);
    std::vector<float> r(512);
    for (int done = 0; done < samples; done += 512) {
        const int n = std::min(512, samples - done);
        e.process(l.data(), r.data(), n);
        left.insert(left.end(), l.begin(), l.begin() + n);
        right.insert(right.end(), r.begin(), r.begin() + n);
    }
}

// A patch that touches every stage: FM, Pollinator, Bloom, echo with self-mod, drive.
lili::Params busyPatch() {
    lili::Params p;
    p.engine = {lili::PetalClassic, lili::PetalWave};
    p.table = {0.0f, 0.5f};
    p.sharp = {0.3f, 0.6f, 0.2f, 0.8f};
    p.mod = {0.6f, 0.3f, 0.5f, 0.4f};
    p.source = {0, 2, 2, 0};
    p.spread = {0.5f, 0.6f, 0.45f, 0.7f};
    p.vibrato = true;
    p.bee = true;
    p.bloom = 0.5f;
    p.delayMix = 0.6f;
    p.delayFeedback = 0.6f;
    p.delayTime = {0.55f, 0.62f};
    p.delayModDepth = {0.4f, 0.3f};
    p.delaySource = 0;
    p.drive = 0.7f;
    return p;
}

void startPetals(lili::Engine& e, const lili::Params& p) {
    e.setParams(p);
    for (int v = 0; v < lili::kNumPetals; ++v) {
        e.setGate(v, true);
    }
}

void testMonoModeIsTheMonoEngine() {
    // Stereo off must be the reference's mono engine in both channels, bit for bit. (Against the
    // engine before stereo existed: see docs/SPEC.md "Stereo"; checked with lili_render renders.)
    std::vector<float> mono;
    std::vector<float> left;
    std::vector<float> right;
    std::vector<float> stereoIntoMono;
    for (int run = 0; run < 3; ++run) {
        lili::Engine e;
        e.prepare(kSr);
        lili::Params p = busyPatch();
        p.stereo = run == 2; // run 2: stereo on, but a mono output
        startPetals(e, p);
        if (run == 1) {
            renderStereo(e, 96000, left, right);
        } else {
            render(e, 96000, run == 0 ? &mono : &stereoIntoMono);
        }
    }
    check(left == mono, "stereo off: left is the mono engine");
    check(right == mono, "stereo off: right is the mono engine");
    check(stereoIntoMono == mono, "a mono output renders the mono engine even with stereo on");
}

// Energy of x after a 4th-order low-pass at `hz` (two cascaded Butterworth biquads).
double lowEnergy(const std::vector<float>& x, double hz, size_t skip) {
    const double k = std::tan(3.141592653589793 * hz / kSr);
    const double q = 1.0 / std::sqrt(2.0);
    const double norm = 1.0 / (1.0 + k / q + k * k);
    const double b0 = k * k * norm;
    const double a1 = 2.0 * (k * k - 1.0) * norm;
    const double a2 = (1.0 - k / q + k * k) * norm;
    std::array<double, 4> z{}; // two stages, transposed direct form II
    double sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double y = x[i];
        for (size_t st = 0; st < 2; ++st) {
            const double in = y;
            y = b0 * in + z[2 * st];
            z[2 * st] = 2.0 * b0 * in - a1 * y + z[2 * st + 1];
            z[2 * st + 1] = b0 * in - a2 * y;
        }
        if (i >= skip) {
            sum += y * y;
        }
    }
    return sum;
}

double energy(const std::vector<float>& x, size_t skip) {
    double sum = 0.0;
    for (size_t i = skip; i < x.size(); ++i) {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return sum;
}

void testStereoFoldsDownToMono() {
    // (L + R) / 2 must keep the mono engine's low end and loudness: no cancellation, no thinning.
    lili::Params def;
    lili::Params echo;
    echo.delayMix = 0.65f;
    echo.delayFeedback = 0.6f;
    echo.delayModDepth = {0.35f, 0.25f};
    echo.delayTime = {0.56f, 0.63f};
    echo.vibrato = true;
    lili::Params wave;
    wave.engine = {lili::PetalWave, lili::PetalWave};
    wave.sharp = {0.3f, 0.5f, 0.2f, 0.6f};
    const std::array<std::pair<const char*, lili::Params>, 4> patches{
        {{"default", def}, {"echo", echo}, {"wave", wave}, {"busy", busyPatch()}}};
    for (const auto& [name, patch] : patches) {
        std::vector<float> mono;
        std::vector<float> left;
        std::vector<float> right;
        for (const bool stereo : {false, true}) {
            lili::Engine e;
            e.prepare(kSr);
            lili::Params p = patch;
            p.stereo = stereo;
            startPetals(e, p);
            if (stereo) {
                renderStereo(e, static_cast<int>(kSr * 6), left, right);
            } else {
                render(e, static_cast<int>(kSr * 6), &mono);
            }
        }
        std::vector<float> mid(mono.size());
        std::vector<float> side(mono.size());
        for (size_t i = 0; i < mid.size(); ++i) {
            mid[i] = 0.5f * (left[i] + right[i]);
            side[i] = 0.5f * (left[i] - right[i]);
        }
        const size_t skip = static_cast<size_t>(kSr); // past the attack
        const double lowDb = 10.0 * std::log10(lowEnergy(mid, 150.0, skip) / lowEnergy(mono, 150.0, skip));
        const double allDb = 10.0 * std::log10(energy(mid, skip) / energy(mono, skip));
        const double powerDb =
            10.0 * std::log10(0.5 * (energy(left, skip) + energy(right, skip)) / energy(mono, skip));
        const double sideDb = 10.0 * std::log10(energy(side, skip) / energy(mid, skip));
        const double sideLowDb = 10.0 * std::log10(lowEnergy(side, 80.0, skip) / lowEnergy(mid, 80.0, skip));
        const std::string tag = std::string(" (") + name + ")";
        check(std::fabs(lowDb) < 1.0,
              "fold-down keeps the lows below 150 Hz" + tag + ": " + std::to_string(lowDb) + " dB");
        check(std::fabs(allDb) < 1.0,
              "fold-down keeps the level" + tag + ": " + std::to_string(allDb) + " dB");
        check(std::fabs(powerDb) < 1.0,
              "no loudness jump into stereo" + tag + ": " + std::to_string(powerDb) + " dB");
        check(sideDb > -25.0, "stereo is actually wide" + tag + ": side " + std::to_string(sideDb) + " dB");
        check(sideLowDb < -20.0,
              "bass stays centred" + tag + ": side below 80 Hz " + std::to_string(sideLowDb) + " dB");
    }
}

void testStereoToggleIsSmooth() {
    // Switching mid-drone crossfades: the first sample after the switch is where the old mode would
    // have been, and after the ramp the output is the new mode (exactly L = R once back in mono).
    const int pre = 48000;
    const int ramp = static_cast<int>(kSr * lili::Engine::kWidthRampMs / 1000.0) + 512;
    for (const bool toStereo : {false, true}) {
        std::vector<float> keepL;
        std::vector<float> keepR;
        std::vector<float> flipL;
        std::vector<float> flipR;
        for (const bool flip : {false, true}) {
            lili::Engine e;
            e.prepare(kSr);
            lili::Params p; // a smooth drone (triangles, no drive) with echo, so any click would stand out
            p.distMix = 0.0f;
            p.delayMix = 0.5f;
            p.delayFeedback = 0.5f;
            p.delayTime = {0.45f, 0.5f};
            p.stereo = !toStereo;
            startPetals(e, p);
            auto& l = flip ? flipL : keepL;
            auto& r = flip ? flipR : keepR;
            renderStereo(e, pre, l, r);
            if (flip) {
                p.stereo = toStereo;
                e.setParams(p);
            }
            renderStereo(e, 2 * ramp + 4800, l, r);
        }
        const auto at = static_cast<size_t>(pre);
        const std::string tag = toStereo ? " (into stereo)" : " (into mono)";
        float firstStep = 0.0f;
        for (size_t i = at; i < at + 4; ++i) {
            firstStep = std::max({firstStep, std::fabs(flipL[i] - keepL[i]), std::fabs(flipR[i] - keepR[i])});
        }
        check(firstStep < 2e-3f, "switch starts from the old image" + tag + ": " + std::to_string(firstStep));
        // Largest sample-to-sample jump during the ramp vs. the steady state either side of it
        // (the old mode before the switch, the new mode after the ramp).
        const auto jump = [&](size_t from, size_t to) {
            float j = 0.0f;
            for (size_t i = from; i < to; ++i) {
                j = std::max({j, std::fabs(flipL[i] - flipL[i - 1]), std::fabs(flipR[i] - flipR[i - 1])});
            }
            return j;
        };
        const auto span = static_cast<size_t>(ramp);
        const float during = jump(at, at + span);
        const float steady = std::max(jump(at - span, at), jump(at + span, at + 2 * span));
        check(during < 1.25f * steady,
              "switch doesn't click" + tag + ": " + std::to_string(during) + " vs " + std::to_string(steady));
        bool settled = true;
        double sideSum = 0.0;
        for (size_t i = at + static_cast<size_t>(ramp); i < flipL.size(); ++i) {
            settled = settled && (toStereo || flipL[i] == flipR[i]);
            sideSum += std::fabs(flipL[i] - flipR[i]);
        }
        check(settled && (toStereo == (sideSum > 1.0)), "switch settles into the new mode" + tag);
    }
}

void testStereoBounded() {
    lili::Engine e;
    e.prepare(kSr);
    lili::Params p = busyPatch();
    p.sharp.fill(1.0f);
    p.mod.fill(1.0f);
    p.hold = {1.0f, 1.0f};
    p.totalFb = true;
    p.delayFeedback = 1.0f;
    p.delayMix = 1.0f;
    p.delayModDepth = {1.0f, 1.0f};
    p.drive = 1.0f;
    p.distMix = 1.0f;
    startPetals(e, p);
    std::vector<float> l;
    std::vector<float> r;
    renderStereo(e, static_cast<int>(kSr * 10), l, r);
    bool ok = true;
    for (size_t i = 0; i < l.size(); ++i) {
        ok = ok && std::isfinite(l[i]) && std::isfinite(r[i]) && std::fabs(l[i]) < 4.0f &&
             std::fabs(r[i]) < 4.0f;
    }
    check(ok, "stereo stays finite and bounded at the extremes");
}

void testParamTableRoundTrip() {
    lili::Params p;
    for (size_t i = 0; i < lili::kNumParams; ++i) {
        lili::setParam(p, i, lili::kParamInfo[i].defaultValue);
    }
    const lili::Params d;
    check(p.tune == d.tune && p.fast == d.fast && p.source == d.source && p.volume == d.volume &&
              p.delaySource == d.delaySource && p.drive == d.drive && p.stereo == d.stereo,
          "ParamInfo defaults match Params defaults");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"silent when idle", testSilentWhenIdle},
        {"gate produces sound", testGateProducesSound},
        {"release decays", testReleaseDecays},
        {"hold drones", testHoldDrones},
        {"voice frequency table", testVoiceFrequencyTable},
        {"oscillator pitch", testOscillatorPitch},
        {"fast exp accuracy", testFastExp},
        {"delay interpolation", testDelayLineInterpolation},
        {"deterministic with seed", testDeterministicWithSeed},
        {"stress extremes", testStressExtremes},
        {"telemetry", testTelemetry},
        {"wavetable band-limited", testWavetableBandLimited},
        {"wavetable pitch and range", testWavetablePitchAndRange},
        {"wavetable fundamental", testWavetableFundamental},
        {"wave tracks classic level", testWaveTracksClassicLevel},
        {"wave engine", testWaveEngine},
        {"seed silent without sample", testSeedSilentWithoutSample},
        {"seed follows tune", testSeedFollowsTune},
        {"seed bounded", testSeedBounded},
        {"bloom off is identical", testBloomOffIsIdentical},
        {"bloom wakes voices", testBloomWakesVoices},
        {"pollinator bounded", testPollinatorBounded},
        {"mono mode is the mono engine", testMonoModeIsTheMonoEngine},
        {"stereo folds down to mono", testStereoFoldsDownToMono},
        {"stereo toggle is smooth", testStereoToggleIsSmooth},
        {"stereo bounded", testStereoBounded},
        {"param table round trip", testParamTableRoundTrip},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        fn();
        std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", name);
    }
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
