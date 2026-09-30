// Parameter model shared by the engine, the plugin and the tests.
// Continuous parameters are normalised x in [0, 1] (the reference's v / 127);
// choices are small integers. See docs/SPEC.md "Parameters".
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace lili {

inline constexpr int kNumVoices = 8;
inline constexpr int kNumPairs = 4;
inline constexpr int kNumGroups = 2;
inline constexpr int kFirstSensorNote = 36; // C1 in Live's naming

struct Params {
    std::array<bool, kNumPairs> fast{true, true, true, true};
    std::array<float, kNumVoices> tune{32 / 127.f, 64 / 127.f, 32 / 127.f, 64 / 127.f,
                                       32 / 127.f, 64 / 127.f, 32 / 127.f, 64 / 127.f};
    std::array<float, kNumPairs> sharp{};
    std::array<float, kNumPairs> mod{};
    std::array<int, kNumPairs> source{1, 1, 1, 1}; // 0 = other pair, 1 = off, 2 = LFO/total FB
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
    // Latched sensor pads (the reference GUI's click-to-hold). Hosts OR these
    // with MIDI gates; the engine itself only sees gates.
    std::array<bool, kNumVoices> latch{};

    // Garden engine (docs/PLAN-garden.md). Defaults reproduce the classic engine.
    std::array<int, kNumGroups> engine{};  // 0 = classic, 1 = wave, 2 = seed
    std::array<float, kNumGroups> table{}; // wavetable family scan, 0..1 over Stem/Reed/Glass/Moss
    float bloom = 0.0f;                    // generative growth depth (0 = off)
    float drift = 0.4f;                    // bloom speed: 0 = ~5 min cycles, 1 = ~8 s
    bool bee = false;                      // Pollinator (Lorenz) replaces the Hyper LFO
};

enum PetalEngine : int { PetalClassic = 0, PetalWave = 1, PetalSeed = 2 };

enum class ParamKind { Continuous, Toggle, Choice };

struct ParamInfo {
    std::string_view id;   // stable host-facing id; never rename
    std::string_view name; // display name (matches the reference)
    ParamKind kind;
    float defaultValue; // x for continuous, 0/1 for toggles, index for choices
    std::array<std::string_view, 3> choices;
    int numChoices;
};

// Stable, ordered parameter list. Append only; hosts store automation by id.
enum ParamIndex : std::size_t {
    P_FAST_12,
    P_FAST_34,
    P_FAST_56,
    P_FAST_78,
    P_TUNE_1,
    P_TUNE_2,
    P_TUNE_3,
    P_TUNE_4,
    P_TUNE_5,
    P_TUNE_6,
    P_TUNE_7,
    P_TUNE_8,
    P_SHARP_12,
    P_SHARP_34,
    P_SHARP_56,
    P_SHARP_78,
    P_MOD_12,
    P_MOD_34,
    P_MOD_56,
    P_MOD_78,
    P_SOURCE_12,
    P_SOURCE_34,
    P_SOURCE_56,
    P_SOURCE_78,
    P_PITCH_1234,
    P_PITCH_5678,
    P_HOLD_1234,
    P_HOLD_5678,
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
    P_SENSOR_1,
    P_SENSOR_2,
    P_SENSOR_3,
    P_SENSOR_4,
    P_SENSOR_5,
    P_SENSOR_6,
    P_SENSOR_7,
    P_SENSOR_8,
    P_ENGINE_1234,
    P_ENGINE_5678,
    P_TABLE_1234,
    P_TABLE_5678,
    P_BLOOM,
    P_DRIFT,
    P_BEE,
    kNumParams
};

// clang-format off
inline constexpr std::array<ParamInfo, kNumParams> kParamInfo{{
    {"fast12", "Fast 12", ParamKind::Toggle, 1, {}, 0},
    {"fast34", "Fast 34", ParamKind::Toggle, 1, {}, 0},
    {"fast56", "Fast 56", ParamKind::Toggle, 1, {}, 0},
    {"fast78", "Fast 78", ParamKind::Toggle, 1, {}, 0},
    {"tune1", "Tune 1", ParamKind::Continuous, 32 / 127.f, {}, 0},
    {"tune2", "Tune 2", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"tune3", "Tune 3", ParamKind::Continuous, 32 / 127.f, {}, 0},
    {"tune4", "Tune 4", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"tune5", "Tune 5", ParamKind::Continuous, 32 / 127.f, {}, 0},
    {"tune6", "Tune 6", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"tune7", "Tune 7", ParamKind::Continuous, 32 / 127.f, {}, 0},
    {"tune8", "Tune 8", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"sharp12", "Sharp 12", ParamKind::Continuous, 0, {}, 0},
    {"sharp34", "Sharp 34", ParamKind::Continuous, 0, {}, 0},
    {"sharp56", "Sharp 56", ParamKind::Continuous, 0, {}, 0},
    {"sharp78", "Sharp 78", ParamKind::Continuous, 0, {}, 0},
    {"mod12", "Mod 12", ParamKind::Continuous, 0, {}, 0},
    {"mod34", "Mod 34", ParamKind::Continuous, 0, {}, 0},
    {"mod56", "Mod 56", ParamKind::Continuous, 0, {}, 0},
    {"mod78", "Mod 78", ParamKind::Continuous, 0, {}, 0},
    {"source12", "Source 12", ParamKind::Choice, 1, {"34", "Off", "LFO/FB"}, 3},
    {"source34", "Source 34", ParamKind::Choice, 1, {"12", "Off", "LFO/FB"}, 3},
    {"source56", "Source 56", ParamKind::Choice, 1, {"78", "Off", "LFO/FB"}, 3},
    {"source78", "Source 78", ParamKind::Choice, 1, {"56", "Off", "LFO/FB"}, 3},
    {"pitch1234", "Pitch 1234", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"pitch5678", "Pitch 5678", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"hold1234", "Hold 1234", ParamKind::Continuous, 0, {}, 0},
    {"hold5678", "Hold 5678", ParamKind::Continuous, 0, {}, 0},
    {"switch", "Switch", ParamKind::Toggle, 0, {}, 0},
    {"totalFb", "Total FB", ParamKind::Toggle, 0, {}, 0},
    {"vibrato", "Vibrato", ParamKind::Toggle, 0, {}, 0},
    {"lfoFreqA", "LFO Freq A", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"lfoFreqB", "LFO Freq B", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"lfoAndOr", "LFO AND/OR", ParamKind::Choice, 0, {"AND", "OR", ""}, 2},
    {"lfoLink", "LFO Link", ParamKind::Toggle, 0, {}, 0},
    {"delTime1", "Delay Time 1", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delTime2", "Delay Time 2", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delFeedback", "Delay Feedback", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"delMix", "Delay Mix", ParamKind::Continuous, 0, {}, 0},
    {"delMod1", "Delay Mod 1", ParamKind::Continuous, 0, {}, 0},
    {"delMod2", "Delay Mod 2", ParamKind::Continuous, 0, {}, 0},
    {"delSource", "Delay Mod Source", ParamKind::Choice, 2, {"Self", "Off", "LFO"}, 3},
    {"delWaveform", "Delay Mod Waveform", ParamKind::Choice, 0, {"Triangle", "Square", ""}, 2},
    {"distDrive", "Drive", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"distMix", "Distortion Mix", ParamKind::Continuous, 64 / 127.f, {}, 0},
    {"volume", "Volume", ParamKind::Continuous, 1, {}, 0},
    {"quantize", "Quantize Pitch", ParamKind::Toggle, 0, {}, 0},
    {"sensor1", "Sensor 1", ParamKind::Toggle, 0, {}, 0},
    {"sensor2", "Sensor 2", ParamKind::Toggle, 0, {}, 0},
    {"sensor3", "Sensor 3", ParamKind::Toggle, 0, {}, 0},
    {"sensor4", "Sensor 4", ParamKind::Toggle, 0, {}, 0},
    {"sensor5", "Sensor 5", ParamKind::Toggle, 0, {}, 0},
    {"sensor6", "Sensor 6", ParamKind::Toggle, 0, {}, 0},
    {"sensor7", "Sensor 7", ParamKind::Toggle, 0, {}, 0},
    {"sensor8", "Sensor 8", ParamKind::Toggle, 0, {}, 0},
    {"engine1234", "Engine 1234", ParamKind::Choice, 0, {"Classic", "Wave", "Seed"}, 3},
    {"engine5678", "Engine 5678", ParamKind::Choice, 0, {"Classic", "Wave", "Seed"}, 3},
    {"table1234", "Table 1234", ParamKind::Continuous, 0, {}, 0},
    {"table5678", "Table 5678", ParamKind::Continuous, 0, {}, 0},
    {"bloom", "Bloom", ParamKind::Continuous, 0, {}, 0},
    {"drift", "Drift", ParamKind::Continuous, 0.4f, {}, 0},
    {"bee", "Pollinator", ParamKind::Toggle, 0, {}, 0},
}};
// clang-format on

// Writes one parameter (value in the units described by ParamInfo).
void setParam(Params& p, std::size_t index, float value);

} // namespace lili
