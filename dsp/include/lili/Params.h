// Parameter model shared by the engine, the plugin and the tests.
// Continuous parameters are normalised x in [0, 1]; choices are small integers.
// LILI-4: four petals of two oscillators each, grouped 1·2 and 3·4
// (docs/PLAN-lili4.md, docs/SPEC.md).
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace lili {

inline constexpr int kNumPetals = 4;
inline constexpr int kNumPairs = kNumPetals;                               // a petal is a pair of oscillators
inline constexpr int kNumVoices = 2 * kNumPetals;                          // oscillators
inline constexpr int kNumGroups = 2;                                       // petals 1·2 and 3·4
inline constexpr std::array<int, kNumPetals> kSensorNotes{36, 38, 40, 41}; // C1 D1 E1 F1 (Live naming)

struct Params {
    // Tune: C1..C7 (MIDI 24..96). Defaults C2, G2, C3, G3.
    std::array<float, kNumPetals> tune{12 / 72.f, 19 / 72.f, 24 / 72.f, 31 / 72.f};
    // Spread: oscillator B = A * 2^(12 u^3 / 12), u = 2x - 1; 0.56 is about +2 cents.
    std::array<float, kNumPetals> spread{0.56f, 0.56f, 0.56f, 0.56f};
    std::array<float, kNumPetals> sharp{}; // "Timbre": pulse shape / wavetable morph / seed position
    std::array<float, kNumPetals> mod{};
    std::array<int, kNumPetals> source{1, 1, 1, 1}; // 0 = partner petal, 1 = off, 2 = LFO/total FB
    std::array<bool, kNumPetals> fast{true, true, true, true};
    std::array<float, kNumGroups> pitch{64 / 127.f, 64 / 127.f};
    std::array<float, kNumGroups> hold{};
    bool crossSwitch = false;
    bool totalFb = false;
    bool vibrato = false;
    float lfoFreqA = 64 / 127.f;
    float lfoFreqB = 64 / 127.f;
    int lfoAndOr = 0; // 0 = AND, 1 = OR
    bool lfoLink = false;
    std::array<float, 2> delayTime{64 / 127.f, 64 / 127.f};
    float delayFeedback = 64 / 127.f;
    float delayMix = 0.0f;
    std::array<float, 2> delayModDepth{};
    int delaySource = 2;   // 0 = self, 1 = off, 2 = LFO
    int delayWaveform = 0; // 0 = triangle, 1 = square
    float drive = 64 / 127.f;
    float distMix = 64 / 127.f;
    float volume = 1.0f;
    bool quantize = false;
    // Latched petal pads (click-to-hold). Hosts OR these with MIDI gates.
    std::array<bool, kNumPetals> latch{};

    // Garden engine (docs/PLAN-garden.md). Defaults are the classic engine.
    std::array<int, kNumGroups> engine{};  // 0 = classic, 1 = wave, 2 = seed
    std::array<float, kNumGroups> table{}; // wavetable family scan, 0..1 over Stem/Reed/Glass/Moss
    float bloom = 0.0f;                    // generative growth depth (0 = off)
    float drift = 0.4f;                    // bloom speed: 0 = ~5 min cycles, 1 = ~8 s
    bool bee = false;                      // Pollinator (Lorenz) replaces the LFO pair
};

enum PetalEngine : int { PetalClassic = 0, PetalWave = 1, PetalSeed = 2 };

enum class ParamKind { Continuous, Toggle, Choice };

struct ParamInfo {
    std::string_view id;   // stable host-facing id; never rename
    std::string_view name; // display name
    ParamKind kind;
    float defaultValue; // x for continuous, 0/1 for toggles, index for choices
    std::array<std::string_view, 3> choices;
    int numChoices;
};

// Stable, ordered parameter list. Append only from here on; hosts store automation by id.
enum ParamIndex : std::size_t {
    P_TUNE_1,
    P_TUNE_2,
    P_TUNE_3,
    P_TUNE_4,
    P_SPREAD_1,
    P_SPREAD_2,
    P_SPREAD_3,
    P_SPREAD_4,
    P_TIMBRE_1,
    P_TIMBRE_2,
    P_TIMBRE_3,
    P_TIMBRE_4,
    P_MOD_1,
    P_MOD_2,
    P_MOD_3,
    P_MOD_4,
    P_SOURCE_1,
    P_SOURCE_2,
    P_SOURCE_3,
    P_SOURCE_4,
    P_FAST_1,
    P_FAST_2,
    P_FAST_3,
    P_FAST_4,
    P_PITCH_12,
    P_PITCH_34,
    P_HOLD_12,
    P_HOLD_34,
    P_ENGINE_12,
    P_ENGINE_34,
    P_TABLE_12,
    P_TABLE_34,
    P_SWITCH,
    P_TOTAL_FB,
    P_VIBRATO,
    P_LFO_FREQ_A,
    P_LFO_FREQ_B,
    P_LFO_ANDOR,
    P_LFO_LINK,
    P_DEL_TIME_1,
    P_DEL_TIME_2,
    P_DEL_FEEDBACK,
    P_DEL_MIX,
    P_DEL_MOD_1,
    P_DEL_MOD_2,
    P_DEL_SOURCE,
    P_DEL_WAVEFORM,
    P_DIST_DRIVE,
    P_DIST_MIX,
    P_VOLUME,
    P_QUANTIZE,
    P_BLOOM,
    P_DRIFT,
    P_BEE,
    P_SENSOR_1,
    P_SENSOR_2,
    P_SENSOR_3,
    P_SENSOR_4,
    kNumParams
};

// clang-format off
inline constexpr std::array<ParamInfo, kNumParams> kParamInfo{{
    {"tune1", "Tune 1", ParamKind::Continuous, 12 / 72.f, {}, 0},
    {"tune2", "Tune 2", ParamKind::Continuous, 19 / 72.f, {}, 0},
    {"tune3", "Tune 3", ParamKind::Continuous, 24 / 72.f, {}, 0},
    {"tune4", "Tune 4", ParamKind::Continuous, 31 / 72.f, {}, 0},
    {"spread1", "Spread 1", ParamKind::Continuous, 0.56f, {}, 0},
    {"spread2", "Spread 2", ParamKind::Continuous, 0.56f, {}, 0},
    {"spread3", "Spread 3", ParamKind::Continuous, 0.56f, {}, 0},
    {"spread4", "Spread 4", ParamKind::Continuous, 0.56f, {}, 0},
    {"timbre1", "Timbre 1", ParamKind::Continuous, 0, {}, 0},
    {"timbre2", "Timbre 2", ParamKind::Continuous, 0, {}, 0},
    {"timbre3", "Timbre 3", ParamKind::Continuous, 0, {}, 0},
    {"timbre4", "Timbre 4", ParamKind::Continuous, 0, {}, 0},
    {"mod1", "Mod 1", ParamKind::Continuous, 0, {}, 0},
    {"mod2", "Mod 2", ParamKind::Continuous, 0, {}, 0},
    {"mod3", "Mod 3", ParamKind::Continuous, 0, {}, 0},
    {"mod4", "Mod 4", ParamKind::Continuous, 0, {}, 0},
    {"source1", "Source 1", ParamKind::Choice, 1, {"2", "Off", "LFO/FB"}, 3},
    {"source2", "Source 2", ParamKind::Choice, 1, {"1", "Off", "LFO/FB"}, 3},
    {"source3", "Source 3", ParamKind::Choice, 1, {"4", "Off", "LFO/FB"}, 3},
    {"source4", "Source 4", ParamKind::Choice, 1, {"3", "Off", "LFO/FB"}, 3},
    {"fast1", "Fast 1", ParamKind::Toggle, 1, {}, 0},
    {"fast2", "Fast 2", ParamKind::Toggle, 1, {}, 0},
    {"fast3", "Fast 3", ParamKind::Toggle, 1, {}, 0},
    {"fast4", "Fast 4", ParamKind::Toggle, 1, {}, 0},
    {"pitch12", "Pitch 1-2", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"pitch34", "Pitch 3-4", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"hold12", "Hold 1-2", ParamKind::Continuous, 0, {}, 0},
    {"hold34", "Hold 3-4", ParamKind::Continuous, 0, {}, 0},
    {"engine12", "Engine 1-2", ParamKind::Choice, 0, {"Classic", "Wave", "Seed"}, 3},
    {"engine34", "Engine 3-4", ParamKind::Choice, 0, {"Classic", "Wave", "Seed"}, 3},
    {"table12", "Table 1-2", ParamKind::Continuous, 0, {}, 0},
    {"table34", "Table 3-4", ParamKind::Continuous, 0, {}, 0},
    {"switch", "Switch", ParamKind::Toggle, 0, {}, 0},
    {"totalFb", "Total FB", ParamKind::Toggle, 0, {}, 0},
    {"vibrato", "Vibrato", ParamKind::Toggle, 0, {}, 0},
    {"lfoFreqA", "LFO A", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"lfoFreqB", "LFO B", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"lfoAndOr", "LFO Logic", ParamKind::Choice, 0, {"AND", "OR", ""}, 2},
    {"lfoLink", "LFO Link", ParamKind::Toggle, 0, {}, 0},
    {"delTime1", "Echo Time 1", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delTime2", "Echo Time 2", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delFeedback", "Echo Feedback", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delMix", "Echo Mix", ParamKind::Continuous, 0, {}, 0},
    {"delMod1", "Echo Mod 1", ParamKind::Continuous, 0, {}, 0},
    {"delMod2", "Echo Mod 2", ParamKind::Continuous, 0, {}, 0},
    {"delSource", "Echo Mod Source", ParamKind::Choice, 2, {"Self", "Off", "LFO"}, 3},
    {"delWaveform", "Echo Mod Wave", ParamKind::Choice, 0, {"Triangle", "Square", ""}, 2},
    {"distDrive", "Drive", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"distMix", "Drive Mix", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"volume", "Volume", ParamKind::Continuous, 1, {}, 0},
    {"quantize", "Quantize Pitch", ParamKind::Toggle, 0, {}, 0},
    {"bloom", "Bloom", ParamKind::Continuous, 0, {}, 0},
    {"drift", "Drift", ParamKind::Continuous, 0.4f, {}, 0},
    {"bee", "Pollinator", ParamKind::Toggle, 0, {}, 0},
    {"sensor1", "Petal 1", ParamKind::Toggle, 0, {}, 0},
    {"sensor2", "Petal 2", ParamKind::Toggle, 0, {}, 0},
    {"sensor3", "Petal 3", ParamKind::Toggle, 0, {}, 0},
    {"sensor4", "Petal 4", ParamKind::Toggle, 0, {}, 0},
}};
// clang-format on

// Writes one parameter (value in the units described by ParamInfo).
void setParam(Params& p, std::size_t index, float value);

} // namespace lili
