// Engine tests. Dependency-free on purpose: `ctest` or run the binary directly.
#include "lili/Engine.h"

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
