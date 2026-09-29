#pragma once

#include "lili/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

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

  private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidi(const juce::MidiMessage& msg);
    void updateGates();
    void publishTelemetry();

    juce::AudioProcessorValueTreeState state_;
    std::array<std::atomic<float>*, lili::kNumParams> raw_{};

    lili::Engine engine_;
    lili::Params params_;
    std::array<bool, lili::kNumVoices> midiHeld_{};
    uint32_t seed_ = 1;

    // Handoff to the editor. The audio thread only ever try-locks.
    juce::SpinLock telemetryLock_;
    lili::Telemetry telemetry_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiliProcessor)
};
