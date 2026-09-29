#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace {

juce::String toJuce(std::string_view s) { return juce::String(s.data(), s.size()); }

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LiliProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& info : lili::kParamInfo) {
        const juce::ParameterID id{toJuce(info.id), 1};
        const auto name = toJuce(info.name);
        switch (info.kind) {
        case lili::ParamKind::Continuous:
            layout.add(std::make_unique<juce::AudioParameterFloat>(id, name, 0.0f, 1.0f, info.defaultValue));
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
        const int voice = msg.getNoteNumber() - lili::kFirstSensorNote;
        if (voice >= 0 && voice < lili::kNumVoices) {
            midiHeld_[static_cast<size_t>(voice)] = msg.isNoteOn();
        }
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
        midiHeld_.fill(false);
    }
}

void LiliProcessor::updateGates() {
    for (int v = 0; v < lili::kNumVoices; ++v) {
        const auto i = static_cast<size_t>(v);
        engine_.setGate(v, midiHeld_[i] || params_.latch[i]);
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
}

juce::AudioProcessorEditor* LiliProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

void LiliProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto tree = state_.copyState();
    tree.setProperty("seed", static_cast<juce::int64>(seed_), nullptr);
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
            state_.replaceState(tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LiliProcessor(); }
