// lili_render: offline render to a 32-bit float WAV, for listening and for
// comparing against renders of the reference Pd patch.
//
//   lili_render out.wav [seconds] [param=value ...] [gates=134] [channels=2]
//
// `param` is any ParamInfo id (e.g. delMix=0.6 source1=0); `gates` lists the
// petals (1-4) held for the whole render. The default is a mono file of the
// mono engine; `channels=2` renders the stereo output (`stereo=0` then gives
// the mono engine in both channels).
#include "lili/Engine.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void writeLe(std::FILE* f, uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        std::fputc(static_cast<int>((v >> (8 * i)) & 0xffu), f);
    }
}

// `samples` is interleaved when channels > 1.
bool writeWav(const char* path, const std::vector<float>& samples, int channels, int sampleRate) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const auto ch = static_cast<uint32_t>(channels);
    const auto dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    std::fwrite("RIFF", 1, 4, f);
    writeLe(f, 36 + dataBytes, 4);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLe(f, 16, 4);
    writeLe(f, 3, 2); // IEEE float
    writeLe(f, ch, 2);
    writeLe(f, static_cast<uint32_t>(sampleRate), 4);
    writeLe(f, static_cast<uint32_t>(sampleRate) * 4 * ch, 4);
    writeLe(f, 4 * ch, 2);
    writeLe(f, 32, 2);
    std::fwrite("data", 1, 4, f);
    writeLe(f, dataBytes, 4);
    std::fwrite(samples.data(), sizeof(float), samples.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav [seconds] [param=value ...] [gates=13] [channels=2]\n",
                     argv[0]);
        return 2;
    }
    constexpr int kSampleRate = 48000;
    double seconds = 10.0;
    lili::Params params;
    std::string gates = "1";
    int channels = 1;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto eq = arg.find('=');
        if (eq == std::string::npos) {
            seconds = std::atof(arg.c_str());
            continue;
        }
        const std::string key = arg.substr(0, eq);
        const std::string value = arg.substr(eq + 1);
        if (key == "gates") {
            gates = value;
            continue;
        }
        if (key == "channels") {
            channels = std::atoi(value.c_str()) == 2 ? 2 : 1;
            continue;
        }
        bool found = false;
        for (size_t p = 0; p < lili::kNumParams; ++p) {
            if (lili::kParamInfo[p].id == key) {
                lili::setParam(params, p, static_cast<float>(std::atof(value.c_str())));
                found = true;
            }
        }
        if (!found) {
            std::fprintf(stderr, "unknown parameter '%s'\n", key.c_str());
            return 2;
        }
    }

    lili::Engine engine;
    engine.prepare(kSampleRate);
    engine.setParams(params);
    for (const char c : gates) {
        if (c >= '1' && c <= '4') { // petals 1-4
            engine.setGate(c - '1', true);
        }
    }

    const auto frames = static_cast<size_t>(seconds * kSampleRate);
    std::vector<float> left(frames);
    std::vector<float> right(channels == 2 ? frames : 0);
    engine.process(left.data(), channels == 2 ? right.data() : nullptr, static_cast<int>(frames));
    std::vector<float> out = left;
    if (channels == 2) {
        out.resize(2 * frames);
        for (size_t i = 0; i < frames; ++i) {
            out[2 * i] = left[i];
            out[2 * i + 1] = right[i];
        }
    }
    if (!writeWav(argv[1], out, channels, kSampleRate)) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
