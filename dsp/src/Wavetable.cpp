#include "lili/Wavetable.h"

#include "lili/Primitives.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace lili {

namespace {

using Complex = std::complex<double>;
constexpr double kPi = 3.141592653589793;
constexpr int kStride = WavetableBank::kSize + 1; // + wrap-around guard sample

// In-place iterative radix-2 FFT with a positive exponent (unscaled inverse).
void inverseFft(std::vector<Complex>& x) {
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(x[i], x[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * kPi / static_cast<double>(len);
        const Complex step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            Complex w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const Complex u = x[i + k];
                const Complex v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

// Deterministic per-(frame, harmonic) random numbers for the Moss family.
double hashUnit(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9e3779b9u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return static_cast<double>(h & 0xffffffu) / static_cast<double>(0x1000000);
}

bool isSparsePartial(int n) {
    // Bell-like: the fundamental, octave and the primes.
    if (n <= 2) {
        return true;
    }
    for (int d = 2; d * d <= n; ++d) {
        if (n % d == 0) {
            return false;
        }
    }
    return true;
}

// Amplitude and phase of harmonic n for a family at morph t in [0, 1].
void harmonic(int family, int frameIndex, double t, int n, double& amp, double& phase) {
    const double dn = static_cast<double>(n);
    phase = 0.0;
    switch (family) {
    case 0: // Stem: saw -> hollow (odd-only) pulse
        amp = (n % 2 == 1 ? 1.0 : 1.0 - t) / dn;
        break;
    case 1: { // Reed: a formant sweeping up the spectrum over a quiet body
        const double centre = 2.0 + t * 30.0;
        const double width = 1.5 + t * 4.0;
        const double d = (dn - centre) / width;
        amp = std::exp(-0.5 * d * d) + 0.25 / dn;
        break;
    }
    case 2: // Glass: sparse partials, brighter with t
        amp = isSparsePartial(n) ? 1.0 / std::pow(dn, 1.6 - 0.9 * t) : 0.0;
        break;
    default: { // Moss: seeded random spectra, each frame its own growth
        const auto f = static_cast<uint32_t>(frameIndex);
        const double r = hashUnit(f, static_cast<uint32_t>(n));
        amp = r * r / std::pow(dn, 0.85);
        phase = 2.0 * kPi * hashUnit(f + 101u, static_cast<uint32_t>(n));
        break;
    }
    }
}

} // namespace

const WavetableBank& WavetableBank::instance() {
    static const WavetableBank bank;
    return bank;
}

int WavetableBank::levelFor(float dt) {
    // Level L holds harmonics <= 1024 >> L, i.e. is alias-free up to dt = 2^L / kSize.
    const float x = dt * static_cast<float>(kSize);
    if (x <= 1.0f) {
        return 0;
    }
    return std::min(kLevels - 1, static_cast<int>(std::ceil(std::log2(x))));
}

WavetableBank::WavetableBank() {
    data_.assign(static_cast<size_t>(kFamilies * kFrames * kLevels * kStride), 0.0f);
    std::vector<Complex> spectrum(kSize);
    for (int fam = 0; fam < kFamilies; ++fam) {
        for (int fr = 0; fr < kFrames; ++fr) {
            const double t = static_cast<double>(fr) / (kFrames - 1);
            float norm = 1.0f; // from level 0, applied to every level so loudness doesn't jump with pitch
            for (int level = 0; level < kLevels; ++level) {
                const int maxHarmonic = (kSize / 2) >> level;
                std::fill(spectrum.begin(), spectrum.end(), Complex(0.0, 0.0));
                for (int n = 1; n <= maxHarmonic && n < kSize / 2; ++n) {
                    double amp = 0.0;
                    double phase = 0.0;
                    harmonic(fam, fr, t, n, amp, phase);
                    // x[k] = sum amp * sin(2 pi n k / N + phase)
                    const Complex c = std::polar(0.5 * amp, phase - kPi / 2.0);
                    spectrum[static_cast<size_t>(n)] = c;
                    spectrum[static_cast<size_t>(kSize - n)] = std::conj(c);
                }
                inverseFft(spectrum);
                float* dst = &data_[static_cast<size_t>(((fam * kFrames + fr) * kLevels + level) * kStride)];
                float peak = 0.0f;
                for (int k = 0; k < kSize; ++k) {
                    dst[k] = static_cast<float>(spectrum[static_cast<size_t>(k)].real());
                    peak = std::max(peak, std::fabs(dst[k]));
                }
                if (level == 0) {
                    norm = peak > 0.0f ? 1.0f / peak : 1.0f;
                }
                for (int k = 0; k < kSize; ++k) {
                    dst[k] *= norm;
                }
                dst[kSize] = dst[0];
            }
        }
    }
}

const float* WavetableBank::frame(int family, int frameIndex, int level) const {
    return &data_[static_cast<size_t>(((family * kFrames + frameIndex) * kLevels + level) * kStride)];
}

float WavetableBank::lookup(int family, int frameIndex, int level, float phase) const {
    const float* t = frame(family, frameIndex, level);
    const float pos = phase * static_cast<float>(kSize);
    const int i = std::min(static_cast<int>(pos), kSize - 1);
    const float f = pos - static_cast<float>(i);
    return t[i] + f * (t[i + 1] - t[i]);
}

float WavetableBank::read(float family, float morph, float phase, float dt) const {
    const int level = levelFor(dt);
    const float famPos = clampf(family, 0.0f, static_cast<float>(kFamilies - 1));
    const int f0 = std::min(static_cast<int>(famPos), kFamilies - 1);
    const int f1 = std::min(f0 + 1, kFamilies - 1);
    const float ff = famPos - static_cast<float>(f0);
    const float frPos = clampf(morph, 0.0f, 1.0f) * static_cast<float>(kFrames - 1);
    const int r0 = std::min(static_cast<int>(frPos), kFrames - 1);
    const int r1 = std::min(r0 + 1, kFrames - 1);
    const float rf = frPos - static_cast<float>(r0);

    const auto readFamily = [&](int fam) {
        const float a = lookup(fam, r0, level, phase);
        return rf > 1e-4f ? a + rf * (lookup(fam, r1, level, phase) - a) : a;
    };
    const float a = readFamily(f0);
    return ff > 1e-4f ? a + ff * (readFamily(f1) - a) : a;
}

} // namespace lili
