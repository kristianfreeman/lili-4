// Small DSP building blocks mirroring the Pd objects used by the reference
// patch. See docs/SPEC.md ("Filters", "Oscillator", ...) for the math.
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lili {

inline constexpr float kTwoPi = 6.283185307179586f;
inline constexpr float kLog2E = 1.4426950408889634f;

inline float clampf(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }

// 2^x; the polynomial is good to ~2e-7 relative, and fastExp() adds the float
// rounding of x * log2(e) (~3e-6 at |x| = 40). Branch-free so it vectorises
// inside the voice loop (std::exp does not).
inline float fastExp2(float x) {
    x = clampf(x, -126.0f, 126.0f);
    const float xi = std::nearbyint(x);
    const float f = x - xi; // [-0.5, 0.5]
    const float p =
        1.0f +
        f * (0.69314718f +
             f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
    const auto bits = static_cast<int32_t>((static_cast<int32_t>(xi) + 127) << 23);
    return p * std::bit_cast<float>(bits);
}

inline float fastExp(float x) { return fastExp2(x * kLog2E); }

// PolyBLEP-corrected naive saw at phase t with increment dt (0 < dt <= 0.5),
// written branch-free for vectorisation.
inline float blepSaw(float t, float dt, float invDt) {
    const float x1 = t * invDt;
    const float lo = t < dt ? x1 + x1 - x1 * x1 - 1.0f : 0.0f;
    const float x2 = (t - 1.0f) * invDt;
    const float hi = t > 1.0f - dt ? x2 * x2 + x2 + x2 + 1.0f : 0.0f;
    return 2.0f * t - 1.0f - lo - hi;
}

// Pd [mtof].
inline float mtof(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }
inline float ftom(float hz) { return hz > 0.0f ? 69.0f + 12.0f * std::log2(hz / 440.0f) : -1500.0f; }

// Pd [dbtorms]: 100 dB == unity, <= 0 dB == silence.
inline float dbtorms(float db) { return db <= 0.0f ? 0.0f : std::pow(10.0f, (db - 100.0f) * 0.05f); }

// The reference's [ma.tanh~]: Pade approximation on input clipped to +-3.
inline float tanhP(float x) {
    const float c = clampf(x, -3.0f, 3.0f);
    const float c2 = c * c;
    return c * (27.0f + c2) / (27.0f + 9.0f * c2);
}

// Pd [hip~] (Pd >= 0.44 normalised one-pole/one-zero high-pass).
class HighPass1 {
  public:
    void setCutoff(float hz, float sampleRate) {
        coef_ = clampf(1.0f - hz * kTwoPi / sampleRate, 0.0f, 1.0f);
        normal_ = 0.5f * (1.0f + coef_);
    }
    float process(float x) {
        const float w = x + coef_ * last_;
        const float y = normal_ * (w - last_);
        last_ = w;
        return y;
    }
    void reset() { last_ = 0.0f; }

  private:
    float coef_ = 1.0f;
    float normal_ = 1.0f;
    float last_ = 0.0f;
};

// Pd [lop~].
class LowPass1 {
  public:
    void setCutoff(float hz, float sampleRate) { coef_ = clampf(hz * kTwoPi / sampleRate, 0.0f, 1.0f); }
    float process(float x) {
        last_ += coef_ * (x - last_);
        return last_;
    }
    void reset() { last_ = 0.0f; }

  private:
    float coef_ = 1.0f;
    float last_ = 0.0f;
};

// Exponential parameter smoother standing in for the reference's
// [$1 23.22( -> [line~] ramps.
class Smoother {
  public:
    static constexpr float kTimeConstantMs = 5.0f; // ~1% settle in 23 ms

    static float coefficient(float sampleRate) {
        return 1.0f - std::exp(-1000.0f / (kTimeConstantMs * sampleRate));
    }
    void prepare(float sampleRate) { coef_ = coefficient(sampleRate); }
    void setTarget(float t) { target_ = t; }
    void snap(float v) { value_ = target_ = v; }
    float next() {
        value_ += coef_ * (target_ - value_);
        return value_;
    }
    float value() const { return value_; }
    float target() const { return target_; }

  private:
    float coef_ = 1.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

// Pd [line~]: linear segment reaching `target` after `ms`, from wherever it is.
class LinearRamp {
  public:
    void prepare(float sampleRate) { sampleRate_ = sampleRate; }
    void rampTo(float target, float ms) {
        target_ = target;
        remaining_ = std::max(1, static_cast<int>(std::lround(ms * 0.001f * sampleRate_)));
        inc_ = (target_ - value_) / static_cast<float>(remaining_);
    }
    float next() {
        if (remaining_ > 0) {
            value_ = --remaining_ == 0 ? target_ : value_ + inc_;
        }
        return value_;
    }
    float value() const { return value_; }
    bool isRamping() const { return remaining_ > 0; }
    void reset(float v = 0.0f) {
        value_ = target_ = v;
        remaining_ = 0;
    }

  private:
    float sampleRate_ = 44100.0f;
    float value_ = 0.0f;
    float target_ = 0.0f;
    float inc_ = 0.0f;
    int remaining_ = 0;
};

// xorshift32 white noise in [-1, 1] (Pd [noise~] equivalent).
class Noise {
  public:
    explicit Noise(uint32_t seed = 0x1234567u) : state_(seed ? seed : 0x1234567u) {}
    void seed(uint32_t s) { state_ = s ? s : 0x1234567u; }
    float next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<float>(static_cast<int32_t>(state_)) * (1.0f / 2147483648.0f);
    }
    uint32_t nextU32() {
        next();
        return state_;
    }

  private:
    uint32_t state_;
};

// N PolyBLEP pulse oscillators with the derived "triangle" ([os.triangle~]),
// stored structure-of-arrays so one call processes all lanes with SIMD.
template <int N> class OscBank {
  public:
    void prepare(float sampleRate) {
        invSampleRate_ = 1.0f / sampleRate;
        nyquist_ = 0.5f * sampleRate;
    }
    void reset(float pw) {
        for (int i = 0; i < N; ++i) {
            phase_[i] = 0.0f;
            pwHeld_[i] = clampf(pw, 0.0f, 1.0f);
            lp_[i] = 0.0f;
        }
    }

    // hz/pw in, square/triangle out; all arrays have N elements.
    void process(const float* __restrict hz, const float* __restrict pw, float* __restrict square,
                 float* __restrict triangle) {
        // Work on local copies so the compiler can prove nothing aliases.
        alignas(16) float phase[N];
        alignas(16) float held[N];
        alignas(16) float lp[N];
        std::copy_n(phase_, N, phase);
        std::copy_n(pwHeld_, N, held);
        std::copy_n(lp_, N, lp);

        const float invSr = invSampleRate_;
        const float nyquist = nyquist_;
        const float lpScale = -kTwoPi * invSr * kLog2E;
#if defined(__clang__)
#pragma clang loop vectorize(enable) vectorize_width(4) interleave_count(1)
#endif
        for (int i = 0; i < N; ++i) {
            const float absF = std::fabs(hz[i]);
            const float dt = absF * invSr;
            const float bdt = clampf(dt, 1e-9f, 0.5f);
            const float invDt = 1.0f / bdt;
            const float p = phase[i];
            const float h = held[i];
            float p2 = p + h;
            p2 = p2 >= 1.0f ? p2 - 1.0f : p2;
            const float pulse = blepSaw(p, bdt, invDt) - blepSaw(p2, bdt, invDt) + 2.0f * h - 1.0f;

            // One-pole low-pass of the pulse -> triangle; a = exp(-2 pi fc / SR).
            const float fc = clampf(absF * 0.25f, 100.0f, nyquist);
            const float a = fastExp2(fc * lpScale);
            lp[i] = (1.0f - a) * pulse + a * lp[i];

            const float next = p + dt;
            phase[i] = next - std::floor(next);
            held[i] = next >= 1.0f ? clampf(pw[i], 0.0f, 1.0f) : h; // [samphold~] latches on wrap

            square[i] = 0.59f * pulse;
            triangle[i] = 2.65f * lp[i] - 0.145f;
        }

        std::copy_n(phase, N, phase_);
        std::copy_n(held, N, pwHeld_);
        std::copy_n(lp, N, lp_);
    }

  private:
    float invSampleRate_ = 1.0f / 44100.0f;
    float nyquist_ = 22050.0f;
    alignas(16) float phase_[N] = {};
    alignas(16) float pwHeld_[N] = {};
    alignas(16) float lp_[N] = {};
};

// Delay line with Pd [vd~] 4-point interpolation.
class DelayLine {
  public:
    void prepare(int maxSamples) {
        int size = 1;
        while (size < maxSamples + 4) {
            size <<= 1;
        }
        buffer_.assign(static_cast<size_t>(size), 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    void push(float x) {
        buffer_[static_cast<size_t>(write_)] = x;
        write_ = (write_ + 1) & mask_;
    }
    // Read `delaySamples` back in time; 1.0 is the most recently pushed sample.
    float read(float delaySamples) const {
        const float d = clampf(delaySamples, 1.0f, static_cast<float>(mask_ - 3));
        const int di = static_cast<int>(d);
        const float frac = d - static_cast<float>(di);
        const int newest = write_ - di; // age == di
        const float a = at(newest + 1);
        const float b = at(newest);
        const float c = at(newest - 1);
        const float e = at(newest - 2);
        const float cminusb = c - b;
        return b + frac * (cminusb - 0.1666667f * (1.0f - frac) *
                                         ((e - a - 3.0f * cminusb) * frac + (e + 2.0f * a - 3.0f * b)));
    }

  private:
    float at(int i) const { return buffer_[static_cast<size_t>(i & mask_)]; }

    std::vector<float> buffer_;
    int mask_ = 0;
    int write_ = 0;
};

// Peak follower from heavylib [hv.envfollow] (musicdsp #97).
class EnvFollower {
  public:
    void set(float attackMs, float releaseMs, float sampleRate) {
        attack_ = coeff(attackMs, sampleRate);
        release_ = coeff(releaseMs, sampleRate);
    }
    float process(float x) {
        const float t = std::fabs(x);
        const float c = t > env_ ? attack_ : release_;
        env_ = t + c * (env_ - t);
        return env_;
    }
    void reset() { env_ = 0.0f; }

  private:
    static float coeff(float ms, float sampleRate) {
        return std::exp(-4.60517f / std::max(1.0f, sampleRate * ms * 0.001f));
    }
    float attack_ = 0.0f;
    float release_ = 0.0f;
    float env_ = 0.0f;
};

// The reference's [compressor~] (whose gain curve is inverted; see SPEC).
class RefCompressor {
  public:
    void prepare(float sampleRate) {
        follower_.set(2.5f, 2.5f, sampleRate);
        threshold_ = dbtorms(-12.0f + 100.0f);
    }
    void reset() { follower_.reset(); }
    float process(float x) {
        const float env = follower_.process(x);
        const float denom = threshold_ + (env - threshold_) * (1.0f / 5.0f);
        const float g = denom != 0.0f ? clampf(env / denom, 0.0f, 1.0f) : 0.0f;
        return x * tanhP(g);
    }

  private:
    EnvFollower follower_;
    float threshold_ = 0.25f;
};

// The reference's [expander~]: Hann-windowed RMS ([env~ 512]) + dB ramp.
class RefExpander {
  public:
    static constexpr int kWindow = 512;
    static constexpr int kPeriod = 256;

    void prepare(float sampleRate) {
        ramp_.prepare(sampleRate);
        float sum = 0.0f;
        for (int i = 0; i < kWindow; ++i) {
            window_[i] = 0.5f - 0.5f * std::cos(kTwoPi * (static_cast<float>(i) + 0.5f) / kWindow);
            sum += window_[i];
        }
        windowNorm_ = 1.0f / sum;
        reset();
    }
    void reset() {
        std::fill(std::begin(history_), std::end(history_), 0.0f);
        pos_ = 0;
        count_ = 0;
        ramp_.reset(0.0f);
        gain_ = 1.0f;
    }
    float process(float x) {
        history_[pos_] = x * x;
        pos_ = (pos_ + 1) & (kWindow - 1);
        if (++count_ == kPeriod) {
            count_ = 0;
            float acc = 0.0f;
            for (int i = 0; i < kWindow; ++i) {
                acc += window_[i] * history_[(pos_ + i) & (kWindow - 1)];
            }
            const float meanSq = acc * windowNorm_;
            const float levelDb = std::max(-100.0f, 10.0f * std::log10(meanSq + 1e-30f));
            const float gainDb = std::min(0.0f, (levelDb - kThresholdDb) * (1.0f - 1.0f / kRatio));
            ramp_.rampTo(gainDb, 11.0f);
        }
        const bool moving = ramp_.isRamping();
        const float db = ramp_.next();
        if (moving) {
            gain_ = fastExp2(db * 0.16609640f); // == dbtorms(db + 100); db is in [-32, 0]
        }
        return x * gain_;
    }

  private:
    static constexpr float kThresholdDb = -60.0f;
    static constexpr float kRatio = 5.0f;

    LinearRamp ramp_;
    float window_[kWindow] = {};
    float history_[kWindow] = {};
    float windowNorm_ = 1.0f;
    int pos_ = 0;
    int count_ = 0;
    float gain_ = 1.0f;
};

} // namespace lili
