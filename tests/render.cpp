// lili_render: offline render to a 32-bit float WAV, for listening and for
// comparing against renders of the reference Pd patch.
//
//   lili_render out.wav [seconds] [param=value ...] [gates=134]
//
// `param` is any ParamInfo id (e.g. delMix=0.6 source1=0); `gates` lists the
// petals (1-4) held for the whole render.
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

bool writeWav(const char* path, const std::vector<float>& mono, int sampleRate) {
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const auto dataBytes = static_cast<uint32_t>(mono.size() * sizeof(float));
    std::fwrite("RIFF", 1, 4, f);
    writeLe(f, 36 + dataBytes, 4);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLe(f, 16, 4);
    writeLe(f, 3, 2); // IEEE float
    writeLe(f, 1, 2);
    writeLe(f, static_cast<uint32_t>(sampleRate), 4);
    writeLe(f, static_cast<uint32_t>(sampleRate) * 4, 4);
    writeLe(f, 4, 2);
    writeLe(f, 32, 2);
    std::fwrite("data", 1, 4, f);
    writeLe(f, dataBytes, 4);
    std::fwrite(mono.data(), sizeof(float), mono.size(), f);
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.wav [seconds] [param=value ...] [gates=13]\n", argv[0]);
        return 2;
    }
    constexpr int kSampleRate = 48000;
    double seconds = 10.0;
    lili::Params params;
    std::string gates = "1";

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

    std::vector<float> out(static_cast<size_t>(seconds * kSampleRate));
    engine.process(out.data(), nullptr, static_cast<int>(out.size()));
    if (!writeWav(argv[1], out, kSampleRate)) {
        std::fprintf(stderr, "cannot write %s\n", argv[1]);
        return 1;
    }
    return 0;
}
