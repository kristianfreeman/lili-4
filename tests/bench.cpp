// lili_bench: realtime factor of the engine for a few representative patches.
#include "lili/Engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// `stereo`: render to two channels (the stereo image), else the mono engine.
double bench(const char* name, const lili::Params& p, int gates, bool stereo = false) {
    constexpr double kSr = 48000.0;
    constexpr int kBlock = 128;
    const char* env = std::getenv("LILI_BENCH_SECONDS");
    const int kSeconds = env != nullptr ? std::atoi(env) : 20;
    lili::Engine e;
    e.prepare(kSr);
    e.setParams(p);
    for (int v = 0; v < gates; ++v) {
        e.setGate(v, true);
    }
    std::vector<float> buf(kBlock);
    std::vector<float> right(kBlock);
    const int blocks = static_cast<int>(kSr) * kSeconds / kBlock;
    volatile float sink = 0.0f;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
        e.setParams(p);
        e.process(buf.data(), stereo ? right.data() : nullptr, kBlock);
        sink = sink + buf[0];
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double factor = kSeconds / secs;
    std::printf("%-22s %8.1fx realtime  (%.2f%% of one core)\n", name, factor, 100.0 / factor);
    return factor;
}

} // namespace

int main() {
    lili::Params idle;
    bench("idle", idle, 0);

    lili::Params drone;
    drone.hold = {0.6f, 0.6f};
    drone.vibrato = true;
    bench("hold drone", drone, 0);

    lili::Params full = drone;
    full.sharp.fill(0.7f);
    full.mod.fill(0.6f);
    full.source = {0, 2, 0, 2};
    full.delayMix = 0.6f;
    full.delayFeedback = 0.8f;
    full.delayModDepth = {0.5f, 0.5f};
    full.lfoLink = true;
    bench("all voices, FM, delay", full, 8);

    lili::Params wave = full;
    wave.engine = {lili::PetalWave, lili::PetalWave};
    wave.table = {0.35f, 0.8f};
    bench("wave engine, same patch", wave, 8);

    bench("stereo: hold drone", drone, 0, true);
    bench("stereo: all voices...", full, 8, true);
    bench("stereo: wave engine", wave, 8, true);
    return 0;
}
