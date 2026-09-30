// Procedural "botanical" wavetables for the Wave petal engine.
// See docs/PLAN-garden.md: four families x 8 morph frames x 2048 samples, each
// frame band-limited per octave (mip levels) so FM can't alias.
#pragma once

#include <vector>

namespace lili {

class WavetableBank {
  public:
    static constexpr int kFamilies = 4; // Stem, Reed, Glass, Moss
    static constexpr int kFrames = 8;
    static constexpr int kSize = 2048;
    static constexpr int kLevels = 11; // level L keeps harmonics <= 1024 >> L

    // Built once, on first use (thread-safe static init). Call it from
    // prepare() so the audio thread never pays for construction.
    static const WavetableBank& instance();

    // Mip level whose highest harmonic stays below Nyquist for phase increment dt.
    static int levelFor(float dt);

    // One band-limited sample. family in [0, kFamilies-1] crossfades
    // neighbouring families; morph in [0, 1] crossfades neighbouring frames.
    float read(float family, float morph, float phase, float dt) const;

    // Raw access for tests: kSize + 1 samples (last one wraps the first).
    const float* frame(int family, int frame, int level) const;

  private:
    WavetableBank();
    float lookup(int family, int frame, int level, float phase) const;

    std::vector<float> data_;
};

} // namespace lili
