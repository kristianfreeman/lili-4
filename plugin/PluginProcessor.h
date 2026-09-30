#pragma once

#include "lili/Engine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

class LiliProcessor final : public juce::AudioProcessor {
  public:
    LiliProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return state_; }

    // Message thread: the telemetry gathered since the previous call.
    lili::Telemetry takeTelemetry();

    // Message thread: load an audio file as the Seed sample for a group
    // (0 = 1234, 1 = 5678). Returns false if it can't be read.
    bool loadSeed(int group, const juce::File& file);
    juce::String seedName(int group) const;
    juce::AudioFormatManager& formats() { return formats_; }

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi(const juce::MidiMessage& msg);
    void updateGates();
    void publishTelemetry();

    juce::AudioProcessorValueTreeState state_;
    std::array<std::atomic<float>*, lili::kNumParams> raw_{};

    lili::Engine engine_;
    lili::Params params_;
    std::array<bool, lili::kNumPetals> midiHeld_{};
    uint32_t seed_ = 1;

    // Handoff to the editor. The audio thread only ever try-locks.
    juce::SpinLock telemetryLock_;
    lili::Telemetry telemetry_;

    // Seed samples. Every sample ever loaded stays alive for the processor's
    // lifetime, so the audio thread can never read freed memory.
    juce::AudioFormatManager formats_;
    std::vector<std::unique_ptr<lili::SeedSample>> seedStore_;
    std::array<juce::File, lili::kNumGroups> seedFiles_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiliProcessor)
};
