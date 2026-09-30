// Engine tests. Dependency-free on purpose: `ctest` or run the binary directly.
#include "lili/Engine.h"
#include "lili/Wavetable.h"

#include <algorithm>
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
    check(s.rms > 1e-3, "hold 1234 drones without gates");
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
    p.tune[0] = 0.0f;
    check(std::fabs(lili::Engine::voiceFrequency(p, 0) - lili::mtof(-16.f) * mul) < 1e-3f, "voice 1 low end");
    p.tune[7] = 1.0f;
    check(std::fabs(lili::Engine::voiceFrequency(p, 7) / (lili::mtof(131.22f) * mul) - 1.0f) < 1e-4f,
          "voice 8 high end");
    p.quantize = true;
    p.tune[2] = 0.5f;
    const float q = lili::ftom(lili::Engine::voiceFrequency(p, 2));
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
        for (int v = 0; v < 8; ++v) {
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
        for (int v = 0; v < 8; ++v) {
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
    for (int f = 0; f <= 12; ++f) {
        for (int m = 0; m <= 8; ++m) {
            const float fam = static_cast<float>(f) * 0.25f;
            const float morph = static_cast<float>(m) * 0.125f;
            for (int i = 0; i < 256; ++i) {
                peak = std::max(peak, std::fabs(bank.read(fam, morph, static_cast<float>(i) / 256.0f, dt)));
            }
        }
    }
    check(peak <= 1.05f && peak > 0.5f, "wavetables stay normalised (peak " + std::to_string(peak) + ")");
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
    for (int v = 0; v < 8; ++v) {
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
    for (int v = 0; v < 8; ++v) {
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
    for (int v = 0; v < 8; ++v) {
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
        for (int v = 0; v < 8; v += 2) {
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
    for (int v = 0; v < 8; ++v) {
        e.setGate(v, true);
    }
    const auto s = render(e, static_cast<int>(kSr * 20));
    const auto& t = e.telemetry();
    check(s.finite && s.peak < 4.0f, "pollinator stays finite and bounded");
    check(t.lfoHzA == 0.0f && (t.lfoPhaseA == 0.0f || t.lfoPhaseA == 0.5f),
          "pollinator drives the leaf LEDs");
}

void testParamTableRoundTrip() {
    lili::Params p;
    for (size_t i = 0; i < lili::kNumParams; ++i) {
        lili::setParam(p, i, lili::kParamInfo[i].defaultValue);
    }
    const lili::Params d;
    check(p.tune == d.tune && p.fast == d.fast && p.source == d.source && p.volume == d.volume &&
              p.delaySource == d.delaySource && p.drive == d.drive,
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
        {"wave engine", testWaveEngine},
        {"seed silent without sample", testSeedSilentWithoutSample},
        {"seed follows tune", testSeedFollowsTune},
        {"seed bounded", testSeedBounded},
        {"bloom off is identical", testBloomOffIsIdentical},
        {"bloom wakes voices", testBloomWakesVoices},
        {"pollinator bounded", testPollinatorBounded},
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
