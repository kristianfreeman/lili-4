#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace {

juce::String toJuce(std::string_view s) { return juce::String(s.data(), s.size()); }

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LiliProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& info : lili::kParamInfo) {
        const juce::ParameterID id{toJuce(info.id), info.version};
        const auto name = toJuce(info.name);
        switch (info.kind) {
        case lili::ParamKind::Continuous:
            // Explicit range: the (min, max, default) constructor quantises to 0.01 steps, which
            // made Tune move in ~1.1-semitone jumps and snapped the 64/127 defaults to 0.50.
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                id, name, juce::NormalisableRange<float>(0.0f, 1.0f), info.defaultValue));
            break;
        case lili::ParamKind::Toggle:
            layout.add(std::make_unique<juce::AudioParameterBool>(id, name, info.defaultValue >= 0.5f));
            break;
        case lili::ParamKind::Choice: {
            juce::StringArray choices;
            for (int c = 0; c < info.numChoices; ++c) {
                choices.add(toJuce(info.choices[static_cast<size_t>(c)]));
            }
            layout.add(std::make_unique<juce::AudioParameterChoice>(id, name, choices,
                                                                    static_cast<int>(info.defaultValue)));
            break;
        }
        }
    }
    return layout;
}

LiliProcessor::LiliProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "LILI8", createLayout()) {
    for (size_t i = 0; i < lili::kNumParams; ++i) {
        raw_[i] = state_.getRawParameterValue(toJuce(lili::kParamInfo[i].id));
    }
    // The reference seeds its per-pair vibrato rates from the clock, so every
    // instance wobbles differently. Keep that.
    seed_ = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
    formats_.registerBasicFormats();
}

bool LiliProcessor::loadSeed(int group, const juce::File& file) {
    if (group < 0 || group >= lili::kNumGroups) {
        return false;
    }
    const std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0) {
        return false;
    }
    constexpr double kMaxSeconds = 120.0;
    const auto frames = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * kMaxSeconds)));
    const int channels = static_cast<int>(std::max(1u, reader->numChannels));
    juce::AudioBuffer<float> buffer(channels, frames);
    reader->read(&buffer, 0, frames, 0, true, true);

    auto sample = std::make_unique<lili::SeedSample>();
    sample->sampleRate = static_cast<float>(reader->sampleRate);
    sample->data.assign(static_cast<size_t>(frames), 0.0f);
    for (int ch = 0; ch < channels; ++ch) { // mix to mono
        juce::FloatVectorOperations::addWithMultiply(sample->data.data(), buffer.getReadPointer(ch),
                                                     1.0f / static_cast<float>(channels), frames);
    }
    engine_.setSeed(group, sample.get());
    seedStore_.push_back(std::move(sample)); // kept alive: the audio thread may still read older ones
    seedFiles_[static_cast<size_t>(group)] = file;
    return true;
}

juce::String LiliProcessor::seedName(int group) const {
    return group >= 0 && group < lili::kNumGroups ? seedFiles_[static_cast<size_t>(group)].getFileName()
                                                  : juce::String();
}

bool LiliProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void LiliProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    lili::EngineConfig config;
    config.seed = seed_;
    engine_.prepare(sampleRate, config);
    midiHeld_.fill(false);
}

void LiliProcessor::handleMidi(const juce::MidiMessage& msg) {
    if (msg.isNoteOnOrOff()) {
        // C1, D1, E1, F1 play petals 1-4.
        const auto it = std::find(lili::kSensorNotes.begin(), lili::kSensorNotes.end(), msg.getNoteNumber());
        if (it != lili::kSensorNotes.end()) {
            midiHeld_[static_cast<size_t>(it - lili::kSensorNotes.begin())] = msg.isNoteOn();
        }
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
        midiHeld_.fill(false);
    }
}

void LiliProcessor::updateGates() {
    for (int petal = 0; petal < lili::kNumPetals; ++petal) {
        const auto i = static_cast<size_t>(petal);
        engine_.setGate(petal, midiHeld_[i] || params_.latch[i]);
    }
}

void LiliProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    const juce::ScopedNoDenormals noDenormals;

    for (size_t i = 0; i < lili::kNumParams; ++i) {
        lili::setParam(params_, i, raw_[i]->load(std::memory_order_relaxed));
    }
    engine_.setParams(params_);
    updateGates();

    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer(0);
    // A mono bus (right == nullptr) always gets the mono engine, whatever "stereo" says.
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // Split the block at MIDI events for sample-accurate sensor gates.
    int pos = 0;
    for (const auto meta : midi) {
        const int at = juce::jlimit(pos, numSamples, meta.samplePosition);
        if (at > pos) {
            engine_.process(left + pos, right != nullptr ? right + pos : nullptr, at - pos);
            pos = at;
        }
        handleMidi(meta.getMessage());
        updateGates();
    }
    if (pos < numSamples) {
        engine_.process(left + pos, right != nullptr ? right + pos : nullptr, numSamples - pos);
    }
    publishTelemetry();
}

void LiliProcessor::publishTelemetry() {
    const juce::SpinLock::ScopedTryLockType lock(telemetryLock_);
    if (!lock.isLocked()) {
        return; // the editor is reading; keep accumulating and try next block
    }
    const auto& src = engine_.telemetry();
    auto& dst = telemetry_;
    const auto maxInto = [](auto& into, const auto& from) {
        for (size_t i = 0; i < into.size(); ++i) {
            into[i] = std::max(into[i], from[i]);
        }
    };
    maxInto(dst.pairPeak, src.pairPeak);
    maxInto(dst.fmPeak, src.fmPeak);
    maxInto(dst.delayPeak, src.delayPeak);
    dst.drivePeak = std::max(dst.drivePeak, src.drivePeak);
    dst.outPeak = std::max(dst.outPeak, src.outPeak);
    dst.totalFbPeak = std::max(dst.totalFbPeak, src.totalFbPeak);
    dst.voiceGain = src.voiceGain;
    dst.lfoPhaseA = src.lfoPhaseA;
    dst.lfoPhaseB = src.lfoPhaseB;
    dst.lfoHzA = src.lfoHzA;
    dst.lfoHzB = src.lfoHzB;
    dst.delayMs = src.delayMs;
    engine_.clearTelemetryPeaks();
}

lili::Telemetry LiliProcessor::takeTelemetry() {
    const juce::SpinLock::ScopedLockType lock(telemetryLock_);
    auto out = telemetry_;
    telemetry_.pairPeak.fill(0.0f);
    telemetry_.fmPeak.fill(0.0f);
    telemetry_.delayPeak.fill(0.0f);
    telemetry_.drivePeak = telemetry_.outPeak = telemetry_.totalFbPeak = 0.0f;
    return out;
}

juce::AudioProcessorEditor* LiliProcessor::createEditor() { return new LiliEditor(*this); }

void LiliProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto tree = state_.copyState();
    tree.setProperty("seed", static_cast<juce::int64>(seed_), nullptr);
    tree.setProperty("seedFile12", seedFiles_[0].getFullPathName(), nullptr);
    tree.setProperty("seedFile34", seedFiles_[1].getFullPathName(), nullptr);
    if (const auto xml = tree.createXml()) {
        copyXmlToBinary(*xml, destData);
    }
}

void LiliProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(state_.state.getType())) {
            auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasProperty("seed")) {
                // A saved set keeps its vibrato character; applied on next prepare.
                seed_ = static_cast<uint32_t>(static_cast<juce::int64>(tree.getProperty("seed")));
            }
            // Seed samples are referenced by path; a missing file just leaves that group silent.
            for (int g = 0; g < lili::kNumGroups; ++g) {
                const juce::String path = tree.getProperty(g == 0 ? "seedFile12" : "seedFile34").toString();
                if (juce::File::isAbsolutePath(path) && juce::File(path).existsAsFile()) {
                    loadSeed(g, juce::File(path));
                }
            }
            state_.replaceState(tree);
            // A state saved before a parameter existed (e.g. "stereo") gets that parameter's
            // default, not whatever this instance happened to be set to.
            for (const auto& info : lili::kParamInfo) {
                const auto id = toJuce(info.id);
                if (!tree.getChildWithProperty("id", id).isValid()) {
                    if (auto* param = state_.getParameter(id)) {
                        param->setValueNotifyingHost(param->getDefaultValue());
                    }
                }
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiliProcessor(); }
