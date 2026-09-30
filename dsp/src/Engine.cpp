#include "lili/Engine.h"

#include <algorithm>
#include <cmath>

namespace lili {

namespace {

// Petal tuning range in MIDI notes: C1..C7.
constexpr float kTuneLowNote = 24.0f;
constexpr float kTuneSpan = 72.0f;

// Cross-mod partners per pair for Switch = 0 / Switch = 1 (see SPEC "Source matrix").
constexpr std::array<int, kNumPairs> kPartnerSwitchOff{1, 0, 3, 2};
constexpr std::array<int, kNumPairs> kPartnerSwitchOn{3, 0, 1, 2};

constexpr float kVoiceMix = 0.16f;
constexpr float kPulseWidth = 0.53125f;
constexpr float kWaveGain = 0.82f; // wavetables are RMS-normalised; this matches the classic pulse level
constexpr float kSeedGain = 0.8f;  // user samples are rarely at full scale

float leak(bool on) { return on ? 1.0f : 0.001f; }
int groupOf(int voice) { return voice / 4; }

float hyperLfoHz(float x) { return mtof(127.0f * x * x - 75.0f); }

// clip(1000 cos(2 pi p), -1, 1). The product only leaves +-1 within
// asin(1e-3) / 2pi ~ 1.6e-4 of the zero crossings, so skip cos elsewhere.
float squareOf(float phase) {
    constexpr float kEdge = 2.0e-4f;
    if (std::fabs(phase - 0.25f) > kEdge && std::fabs(phase - 0.75f) > kEdge) {
        return (phase < 0.25f || phase > 0.75f) ? 1.0f : -1.0f;
    }
    return clampf(1000.0f * std::cos(kTwoPi * phase), -1.0f, 1.0f);
}
float triangleOf(float phase) { return 4.0f * std::min(phase, 1.0f - phase) - 1.0f; }
float wrap01(float p) { return p - std::floor(p); }

float pow31(float t) {
    const float t2 = t * t;
    const float t4 = t2 * t2;
    const float t8 = t4 * t4;
    const float t16 = t8 * t8;
    return t16 * t8 * t4 * t2 * t;
}

} // namespace

int Engine::pitchSemitones(float x) {
    return static_cast<int>(std::lround(12.0f * std::log2(0.01f + 1.99f * x)));
}

float Engine::pitchMultiplier(float x) { return std::exp2(static_cast<float>(pitchSemitones(x)) / 12.0f); }

float Engine::spreadSemitones(float x) {
    const float u = 2.0f * x - 1.0f;
    return 12.0f * u * u * u; // fine detune near the centre, +-1 octave at the ends
}

float Engine::voiceFrequency(const Params& p, int voice) {
    const auto petal = static_cast<size_t>(voice / 2);
    float note = kTuneLowNote + p.tune[petal] * kTuneSpan;
    if (voice % 2 == 1) {
        note += spreadSemitones(p.spread[petal]); // oscillator B
    }
    float hz = mtof(note) * pitchMultiplier(p.pitch[static_cast<size_t>(groupOf(voice))]);
    if (p.quantize) {
        hz = mtof(std::floor(ftom(hz) + 0.5f));
    }
    return hz;
}

void Engine::prepare(double sampleRate, const EngineConfig& config) {
    sampleRate_ = static_cast<float>(sampleRate);
    invSampleRate_ = 1.0f / sampleRate_;
    config_ = config;

    for (auto& pr : pairs_) {
        pr.sensor.prepare(sampleRate_);
    }
    osc_.prepare(sampleRate_);
    bank_ = &WavetableBank::instance(); // built here, never on the audio thread
    grainLength_ = std::max(64, static_cast<int>(0.09f * sampleRate_));
    bloom_.prepare(sampleRate_);
    bloomRng_.seed(config_.seed * 31u + 7u);
    for (auto& b : bloomVoices_) { // every voice starts somewhere different in its walk
        b.from = {bloomRng_.next(), bloomRng_.next()};
        b.to = {bloomRng_.next(), bloomRng_.next()};
        b.t = 0.5f + 0.5f * bloomRng_.next();
    }
    for (size_t v = 0; v < seedVoices_.size(); ++v) {
        seedVoices_[v].rng.seed(config_.seed * 7919u + static_cast<uint32_t>(v) + 1u);
    }
    for (auto& t : table_) {
        t.prepare(sampleRate_);
    }
    freqCoef_ = Smoother::coefficient(sampleRate_);

    Noise rng(config_.seed * 2654435761u + 1u);
    for (auto& pr : pairs_) {
        pr.sharp.prepare(sampleRate_);
        pr.mod.prepare(sampleRate_);
        pr.tapFilter.setCutoff(3.0f, sampleRate_);
        pr.tap.setDelay(config_.legacyBlockFeedback ? 64 : 1);
        const float rate = 0.5f + 3.0f * static_cast<float>(rng.nextU32() % 1001u) / 1000.0f;
        pr.vibInc = rate * invSampleRate_;
    }
    for (auto& h : hold_) {
        h.prepare(sampleRate_);
    }

    lfoFreqA_.prepare(sampleRate_);
    lfoFreqB_.prepare(sampleRate_);

    const int maxDelaySamples = static_cast<int>(std::ceil((kMaxDelayMs + 100.0f) * 0.001f * sampleRate_));
    for (auto& d : delay_) {
        d.line.prepare(maxDelaySamples);
        d.timeMs.prepare(sampleRate_);
        d.depthMs.prepare(sampleRate_);
        d.hp.setCutoff(1.0f, sampleRate_);
        d.lp.setCutoff(4000.0f, sampleRate_);
        d.selfLp.setCutoff(689.0f, sampleRate_);
        d.comp.prepare(sampleRate_);
        d.expander.prepare(sampleRate_);
    }
    delayFeedback_.prepare(sampleRate_);
    delayMix_.prepare(sampleRate_);
    noise_.seed(config_.seed ^ 0x9e3779b9u);

    drive_.prepare(sampleRate_);
    distMix_.prepare(sampleRate_);
    volume_.prepare(sampleRate_);
    driveHp_.setCutoff(20.0f, sampleRate_);
    shapeHp_.setCutoff(10.0f, sampleRate_);
    totalFbHp_.setCutoff(3.0f, sampleRate_);
    totalFeedback_.setDelay(config_.legacyBlockFeedback ? 64 : 1);

    reset();
}

void Engine::reset() {
    for (auto& pr : pairs_) {
        pr.sensor.reset(0.0f);
        pr.gate = false;
    }
    osc_.reset(kPulseWidth);
    wavePhase_.fill(0.0f);
    bloomTune_.fill(1.0f);
    bloomTimbre_.fill(0.0f);
    bloomBreath_.fill(0.0f);
    bloomCountdown_ = 0;
    lorenzX_ = lorenzY_ = 1.0;
    lorenzZ_ = 20.0;
    for (auto& sv : seedVoices_) {
        sv.grains = {};
        sv.untilNext = 0;
        sv.next = 0;
    }
    for (auto& pr : pairs_) {
        pr.tapFilter.reset();
        pr.tap.reset();
        pr.vibPhase = 0.0f;
    }
    lfoPhaseA_ = lfoPhaseB_ = 0.0f;
    for (auto& d : delay_) {
        d.line.reset();
        d.hp.reset();
        d.lp.reset();
        d.selfLp.reset();
        d.comp.reset();
        d.expander.reset();
        d.lastWrite = 0.0f;
    }
    driveHp_.reset();
    shapeHp_.reset();
    totalFbHp_.reset();
    totalFeedback_.reset();
    snapOnNextParams_ = true;
}

void Engine::retriggerSensor(Pair& pair) {
    const float attackMs = pair.fast ? 100.0f : 200.0f;
    const float releaseMs = pair.fast ? 100.0f : 8000.0f;
    pair.sensor.rampTo(pair.gate ? 1.0f : 0.0f, pair.gate ? attackMs : releaseMs);
}

// Bloom: each voice follows two slow smoothstepped random walks. At bloom 0
// every factor is exactly neutral (x1, +0), so the engine is unchanged.
void Engine::updateBloom() {
    const float depth = bloom_.next();
    for (size_t v = 0; v < kNumVoices; ++v) {
        auto& b = bloomVoices_[v];
        b.t += bloomInc_;
        if (b.t >= 1.0f) {
            b.t -= std::floor(b.t);
            b.from = b.to;
            b.to = {bloomRng_.next(), bloomRng_.next()};
        }
        const float s = b.t * b.t * (3.0f - 2.0f * b.t);
        const float walk = b.from[0] + s * (b.to[0] - b.from[0]);
        const float breath = 0.5f + 0.5f * (b.from[1] + s * (b.to[1] - b.from[1]));
        const float wake = clampf((breath - 0.35f) / 0.4f, 0.0f, 1.0f); // asleep below 0.35, awake above 0.75
        bloomTune_[v] = depth > 0.0f ? std::exp2(depth * (40.0f / 1200.0f) * walk) : 1.0f;
        bloomTimbre_[v] = depth * 0.3f * walk;
        bloomBreath_[v] = depth * 0.6f * wake * wake * (3.0f - 2.0f * wake);
    }
}

void Engine::setSeed(int group, const SeedSample* sample) {
    if (group >= 0 && group < kNumGroups) {
        seeds_[static_cast<size_t>(group)].store(sample, std::memory_order_release);
    }
}

// Two Hann grains at 50% overlap (they sum to unity gain). Tune sets the
// playback rate against a C3 root, timbre (Sharp) the read position, and FM
// both bends the rate (via hz) and scatters where new grains start.
float Engine::seedPetal(SeedVoice& sv, const SeedSample& s, float hz, float timbre, float fm) {
    const auto frames = static_cast<double>(s.data.size());
    if (--sv.untilNext <= 0) {
        sv.untilNext = grainLength_ / 2;
        auto& grain = sv.grains[sv.next];
        sv.next ^= 1u;
        const double spread = 0.004 * sv.rng.next() + 0.02 * static_cast<double>(clampf(fm, -1.0f, 1.0f));
        const double start = static_cast<double>(timbre) + spread;
        grain.pos = (start - std::floor(start)) * frames;
        grain.age = 0;
        grain.active = true;
    }
    const double rate = static_cast<double>(std::fabs(hz) / kSeedRootHz * s.sampleRate * invSampleRate_);
    const float invLength = 1.0f / static_cast<float>(grainLength_);
    float out = 0.0f;
    for (auto& grain : sv.grains) {
        if (!grain.active) {
            continue;
        }
        const auto i0 = static_cast<size_t>(grain.pos);
        const size_t i1 = i0 + 1 < s.data.size() ? i0 + 1 : 0;
        const auto frac = static_cast<float>(grain.pos - static_cast<double>(i0));
        const float sample = s.data[i0] + frac * (s.data[i1] - s.data[i0]);
        const float window = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(grain.age) * invLength);
        out += sample * window;
        grain.pos += rate;
        grain.pos -= frames * std::floor(grain.pos / frames); // loop the sample
        if (++grain.age >= grainLength_) {
            grain.active = false;
        }
    }
    return kSeedGain * out;
}

void Engine::setGate(int petal, bool on) {
    if (petal < 0 || petal >= kNumPetals) {
        return;
    }
    auto& pr = pairs_[static_cast<size_t>(petal)];
    if (pr.gate == on) {
        return;
    }
    pr.gate = on;
    retriggerSensor(pr);
}

void Engine::setParams(const Params& p) {
    const auto target = [this](Smoother& s, float value) {
        if (snapOnNextParams_) {
            s.snap(value);
        } else {
            s.setTarget(value);
        }
    };

    for (int i = 0; i < kNumVoices; ++i) {
        const auto v = static_cast<size_t>(i);
        freqTarget_[v] = voiceFrequency(p, i);
        if (snapOnNextParams_) {
            freq_[v] = freqTarget_[v];
        }
    }
    for (size_t i = 0; i < kNumPairs; ++i) {
        auto& pr = pairs_[i];
        target(pr.sharp, p.sharp[i] * p.sharp[i]);
        const float m2 = p.mod[i] * p.mod[i];
        target(pr.mod, 2.0f * m2 * m2);
        pr.source = p.source[i];
        if (pr.fast != p.fast[i]) {
            pr.fast = p.fast[i];
            // Re-send the gate so a running ramp picks up the new time.
            retriggerSensor(pr);
        }
    }
    for (size_t g = 0; g < kNumGroups; ++g) {
        target(hold_[g], p.hold[g] * p.hold[g]);
        target(table_[g], p.table[g]);
        engine_[g] = p.engine[g];
    }
    crossSwitch_ = p.crossSwitch;
    totalFb_ = p.totalFb;
    vibrato_ = p.vibrato;

    target(lfoFreqA_, hyperLfoHz(p.lfoFreqA));
    target(lfoFreqB_, hyperLfoHz(p.lfoFreqB));
    lfoOr_ = p.lfoAndOr != 0;
    lfoLink_ = p.lfoLink;
    bee_ = p.bee;
    beeRho_ = 20.0f + 25.0f * p.lfoFreqB; // Freq B becomes "chaos" in Pollinator mode
    target(bloom_, p.bloom);
    // Drift: segment period from ~5 min (0) to ~8 s (1), exponentially.
    const float period = 300.0f * std::pow(8.0f / 300.0f, p.drift);
    bloomInc_ = static_cast<float>(kBloomBlock) / (period * sampleRate_);

    for (size_t k = 0; k < 2; ++k) {
        target(delay_[k].timeMs, 1.45125f * std::exp2(12.0f * p.delayTime[k]));
        target(delay_[k].depthMs, 10.0f * p.delayModDepth[k] * p.delayModDepth[k]);
    }
    const float fbx = p.delayFeedback;
    target(delayFeedback_, fbx * std::exp2(2.0f * fbx));
    target(delayMix_, p.delayMix);
    delaySource_ = p.delaySource;
    delayWaveform_ = p.delayWaveform;

    target(drive_, dbtorms(std::pow(3.0f, 2.0f * p.drive + 1.0f) + 3.0f + 100.0f));
    target(distMix_, p.distMix);
    target(volume_, p.volume * p.volume);

    snapOnNextParams_ = false;
}

void Engine::process(float* left, float* right, int numSamples) {
    auto& tm = telemetry_;
    for (int n = 0; n < numSamples; ++n) {
        const float y = processSample();
        left[n] = y;
        if (right != nullptr && right != left) {
            right[n] = y;
        }
    }

    if (bee_) {
        // Leaf LEDs flicker with the sign of the flight ("wings"): phase 0 = lit, 0.5 = dark.
        tm.lfoPhaseA = lorenzX_ > 0.0 ? 0.0f : 0.5f;
        tm.lfoPhaseB = lorenzY_ > 0.0 ? 0.0f : 0.5f;
        tm.lfoHzA = tm.lfoHzB = 0.0f;
    } else {
        tm.lfoPhaseA = lfoPhaseA_;
        tm.lfoPhaseB = lfoPhaseB_;
        tm.lfoHzA = lfoFreqA_.value();
        tm.lfoHzB = lfoFreqB_.value();
    }
    tm.delayMs = {delay_[0].timeMs.value(), delay_[1].timeMs.value()};
}

void Engine::clearTelemetryPeaks() {
    auto& tm = telemetry_;
    tm.pairPeak.fill(0.0f);
    tm.fmPeak.fill(0.0f);
    tm.delayPeak.fill(0.0f);
    tm.drivePeak = tm.outPeak = tm.totalFbPeak = 0.0f;
}

float Engine::processSample() {
    auto& tm = telemetry_;

    // --- Hyper LFO ---------------------------------------------------------
    const float fA = lfoFreqA_.next();
    const float fB = lfoFreqB_.next();
    const float sqA = squareOf(lfoPhaseA_);
    const float sqB = squareOf(lfoPhaseB_);
    const float triA = triangleOf(lfoPhaseA_);
    const float triB = triangleOf(lfoPhaseB_);
    lfoPhaseA_ = wrap01(lfoPhaseA_ + fA * invSampleRate_);
    const float fBEff = fB * (1.0f + (lfoLink_ ? 0.5f * sqA : 0.0f));
    lfoPhaseB_ = wrap01(lfoPhaseB_ + fBEff * invSampleRate_);

    float sqrLfo = lfoOr_ ? 0.5f * (sqA + sqB) : sqA * sqB;
    float delSqr = 0.5f * (sqA + sqB);
    float delTri = 0.5f * (triA + triB);
    if (bee_) {
        // Pollinator: Lorenz flight (sigma 10, beta 8/3). Freq A sets the speed,
        // Freq B the chaos (rho). Euler is stable here: dt <= ~0.003 per sample.
        const double dt = static_cast<double>(std::max(fA, 0.05f) * invSampleRate_) * 0.8;
        const double dx = 10.0 * (lorenzY_ - lorenzX_);
        const double dy = lorenzX_ * (static_cast<double>(beeRho_) - lorenzZ_) - lorenzY_;
        const double dz = lorenzX_ * lorenzY_ - (8.0 / 3.0) * lorenzZ_;
        lorenzX_ += dx * dt;
        lorenzY_ += dy * dt;
        lorenzZ_ += dz * dt;
        if (!std::isfinite(lorenzX_ + lorenzY_ + lorenzZ_) || std::fabs(lorenzZ_) > 1e3) {
            lorenzX_ = lorenzY_ = 1.0;
            lorenzZ_ = 20.0;
        }
        sqrLfo = std::tanh(static_cast<float>(lorenzX_) / 12.0f);
        delTri = std::tanh(static_cast<float>(lorenzY_) / 15.0f);
        delSqr = clampf(static_cast<float>(lorenzZ_) / 25.0f - 1.0f, -1.0f, 1.0f);
    }
    if (--bloomCountdown_ <= 0) {
        bloomCountdown_ = kBloomBlock;
        updateBloom();
    }

    // --- Voices ------------------------------------------------------------
    std::array<float, kNumPairs> tapsIn{};
    for (size_t i = 0; i < kNumPairs; ++i) {
        tapsIn[i] = pairs_[i].tap.read();
    }
    const float totalFbIn = totalFeedback_.read();
    const float lfoSource = sqrLfo * leak(!totalFb_) + totalFbIn * leak(totalFb_);

    std::array<float, kNumGroups> hold{};
    std::array<float, kNumGroups> family{};
    for (size_t g = 0; g < kNumGroups; ++g) {
        hold[g] = hold_[g].next();
        family[g] = table_[g].next() * static_cast<float>(WavetableBank::kFamilies - 1);
    }

    // Per-pair controls, broadcast to both voices of the pair.
    std::array<float, kNumPairs> pairFreqScale{};
    std::array<float, kNumPairs> pairSharp{};
    std::array<float, kNumPairs> pairFm{};
    alignas(16) std::array<float, kNumVoices> hz{};
    alignas(16) std::array<float, kNumVoices> pw{};
    for (size_t i = 0; i < kNumPairs; ++i) {
        auto& pr = pairs_[i];
        const float cross = tapsIn[static_cast<size_t>(kPartnerSwitchOff[i])] * leak(!crossSwitch_) +
                            tapsIn[static_cast<size_t>(kPartnerSwitchOn[i])] * leak(crossSwitch_);
        const float source = cross * leak(pr.source == 0) + lfoSource * leak(pr.source == 2);
        const float fmIndex = source * pr.mod.next();
        const float fmAmount = 1.0f + fmIndex;
        pairFm[i] = fmIndex;
        tm.fmPeak[i] = std::max(tm.fmPeak[i], std::fabs(fmIndex));
        pairSharp[i] = pr.sharp.next();

        const float vib = vibrato_ ? std::cos(kTwoPi * pr.vibPhase) : 0.0f;
        pr.vibPhase = wrap01(pr.vibPhase + pr.vibInc);
        pairFreqScale[i] = (1.0f + 0.005f * vib) * fmAmount;
        pw[2 * i] = pw[2 * i + 1] = kPulseWidth + 0.025f * vib;
    }
    for (size_t v = 0; v < kNumVoices; ++v) {
        freq_[v] += freqCoef_ * (freqTarget_[v] - freq_[v]);
        hz[v] = freq_[v] * pairFreqScale[v / 2] * bloomTune_[v];
    }

    alignas(16) std::array<float, kNumVoices> square{};
    alignas(16) std::array<float, kNumVoices> triangle{};
    osc_.process(hz.data(), pw.data(), square.data(), triangle.data());

    std::array<float, kNumPetals> petalLevel{};
    std::array<float, kNumPetals> petalThump{};
    for (size_t i = 0; i < kNumPetals; ++i) {
        const float l = pairs_[i].sensor.next();
        petalLevel[i] = l;
        petalThump[i] = (l > 0.0f && l < 1.0f) ? 0.5f * std::sin(kTwoPi * l) : 0.0f;
    }

    std::array<float, kNumPairs> pairOut{};
    for (size_t v = 0; v < kNumVoices; ++v) {
        const float sharp = clampf(pairSharp[v / 2] + bloomTimbre_[v], 0.0f, 1.0f);
        const size_t g = v / 4;
        float shaped = 0.0f;
        if (engine_[g] == PetalWave) {
            // Sharp is the morph through the table (its smoother holds x^2).
            const float dt = std::fabs(hz[v]) * invSampleRate_;
            shaped = kWaveGain * bank_->read(family[g], std::sqrt(sharp), wavePhase_[v], dt);
            wavePhase_[v] = wrap01(wavePhase_[v] + dt);
        } else if (engine_[g] == PetalSeed) {
            const SeedSample* seed = seeds_[g].load(std::memory_order_acquire);
            shaped = seed != nullptr && !seed->data.empty()
                         ? seedPetal(seedVoices_[v], *seed, hz[v], std::sqrt(sharp), pairFm[v / 2])
                         : 0.0f;
        } else {
            shaped = square[v] * sharp - triangle[v] * (1.0f - sharp);
        }
        // One sensor per petal: both oscillators share it, and only oscillator A carries the thump.
        const float l = petalLevel[v / 2];
        const float thump = v % 2 == 0 ? petalThump[v / 2] : 0.0f;
        const float gain = std::min(l * l + hold[v / 4] + bloomBreath_[v], 1.0f);
        tm.voiceGain[v] = gain;
        pairOut[v / 2] += (shaped + thump) * gain;
    }

    float voiceSum = 0.0f;
    for (size_t i = 0; i < kNumPairs; ++i) {
        pairs_[i].tap.write(pairs_[i].tapFilter.process(pairOut[i]));
        tm.pairPeak[i] = std::max(tm.pairPeak[i], std::fabs(pairOut[i]));
        voiceSum += pairOut[i];
    }
    const float in = voiceSum * kVoiceMix;

    // --- Dual mod delay ----------------------------------------------------
    const float inN = in + 0.001f * noise_.next();
    const float fb = delayFeedback_.next();
    const float lfoMod = delaySource_ == 2 ? (delayWaveform_ == 0 ? delTri : delSqr) : 0.0f;
    const float msToSamples = 0.001f * sampleRate_;
    float wetSum = 0.0f;
    for (size_t k = 0; k < delay_.size(); ++k) {
        auto& d = delay_[k];
        const float self = d.selfLp.process(0.5f * d.lastWrite);
        const float mod = lfoMod + (delaySource_ == 0 ? self : 0.0f);
        const float ms = d.timeMs.next() + d.depthMs.next() * mod;
        const float read = d.line.read(ms * msToSamples);
        const float loop = d.expander.process(d.comp.process(d.lp.process(d.hp.process(read))));
        float write = inN + fb * loop;
        if (config_.delaySafetySaturator) {
            write = 4.0f * std::tanh(0.25f * write);
        }
        d.line.push(write);
        d.lastWrite = write;
        tm.delayPeak[k] = std::max(tm.delayPeak[k], std::fabs(loop));
        wetSum += loop;
    }
    const float wet = tanhP(wetSum / std::max(fb, 1.5f));
    const float mix = delayMix_.next();
    const float delayed = 0.7f * in * (1.0f - mix) + mix * wet;

    // --- Drive / distortion / volume ---------------------------------------
    const float dG = drive_.next();
    const float t = tanhP(driveHp_.process(delayed * dG));
    const float shapedDist = shapeHp_.process(t + 0.25f * pow31(t)) / clampf(dG, 1.0f, 4.0f);
    const float dm = distMix_.next();
    const float mixed = delayed * (1.0f - dm) + (shapedDist + 0.1f * delayed) * dm;
    const float totalFb = totalFbHp_.process(tanhP(mixed));
    totalFeedback_.write(totalFb);

    const float out = mixed * volume_.next();
    tm.drivePeak = std::max(tm.drivePeak, std::fabs(shapedDist * dm));
    tm.totalFbPeak = std::max(tm.totalFbPeak, std::fabs(totalFb));
    tm.outPeak = std::max(tm.outPeak, std::fabs(out));
    return out;
}

void setParam(Params& p, std::size_t index, float value) {
    const bool on = value >= 0.5f;
    const int choice = static_cast<int>(std::lround(value));
    const auto at = [](size_t base, size_t idx) { return idx - base; };
    switch (index) {
    case P_TUNE_1:
    case P_TUNE_2:
    case P_TUNE_3:
    case P_TUNE_4: p.tune[at(P_TUNE_1, index)] = value; break;
    case P_SPREAD_1:
    case P_SPREAD_2:
    case P_SPREAD_3:
    case P_SPREAD_4: p.spread[at(P_SPREAD_1, index)] = value; break;
    case P_TIMBRE_1:
    case P_TIMBRE_2:
    case P_TIMBRE_3:
    case P_TIMBRE_4: p.sharp[at(P_TIMBRE_1, index)] = value; break;
    case P_MOD_1:
    case P_MOD_2:
    case P_MOD_3:
    case P_MOD_4: p.mod[at(P_MOD_1, index)] = value; break;
    case P_SOURCE_1:
    case P_SOURCE_2:
    case P_SOURCE_3:
    case P_SOURCE_4: p.source[at(P_SOURCE_1, index)] = choice; break;
    case P_FAST_1:
    case P_FAST_2:
    case P_FAST_3:
    case P_FAST_4: p.fast[at(P_FAST_1, index)] = on; break;
    case P_PITCH_12:
    case P_PITCH_34: p.pitch[at(P_PITCH_12, index)] = value; break;
    case P_HOLD_12:
    case P_HOLD_34: p.hold[at(P_HOLD_12, index)] = value; break;
    case P_ENGINE_12:
    case P_ENGINE_34: p.engine[at(P_ENGINE_12, index)] = choice; break;
    case P_TABLE_12:
    case P_TABLE_34: p.table[at(P_TABLE_12, index)] = value; break;
    case P_SWITCH: p.crossSwitch = on; break;
    case P_TOTAL_FB: p.totalFb = on; break;
    case P_VIBRATO: p.vibrato = on; break;
    case P_LFO_FREQ_A: p.lfoFreqA = value; break;
    case P_LFO_FREQ_B: p.lfoFreqB = value; break;
    case P_LFO_ANDOR: p.lfoAndOr = choice; break;
    case P_LFO_LINK: p.lfoLink = on; break;
    case P_DEL_TIME_1:
    case P_DEL_TIME_2: p.delayTime[at(P_DEL_TIME_1, index)] = value; break;
    case P_DEL_FEEDBACK: p.delayFeedback = value; break;
    case P_DEL_MIX: p.delayMix = value; break;
    case P_DEL_MOD_1:
    case P_DEL_MOD_2: p.delayModDepth[at(P_DEL_MOD_1, index)] = value; break;
    case P_DEL_SOURCE: p.delaySource = choice; break;
    case P_DEL_WAVEFORM: p.delayWaveform = choice; break;
    case P_DIST_DRIVE: p.drive = value; break;
    case P_DIST_MIX: p.distMix = value; break;
    case P_VOLUME: p.volume = value; break;
    case P_QUANTIZE: p.quantize = on; break;
    case P_BLOOM: p.bloom = value; break;
    case P_DRIFT: p.drift = value; break;
    case P_BEE: p.bee = on; break;
    case P_SENSOR_1:
    case P_SENSOR_2:
    case P_SENSOR_3:
    case P_SENSOR_4: p.latch[at(P_SENSOR_1, index)] = on; break;
    default: break;
    }
}

} // namespace lili
