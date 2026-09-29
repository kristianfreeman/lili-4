#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// The rendered circuit-board editor. Art is pre-rendered in Blender (see
// docs/ART.md): a board image plus knob/toggle sprite strips, laid out from
// art/board.json so hit areas match the render exactly. Controls follow the
// parameters (and host automation); pads and LED meters follow engine telemetry.
class LiliEditor final : public juce::AudioProcessorEditor, private juce::Timer {
  public:
    explicit LiliEditor(LiliProcessor& owner);
    ~LiliEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    enum class Kind { Knob, Toggle3, Toggle2, Pad };

    struct Control {
        Kind kind;
        juce::RangedAudioParameter* param;
        juce::Point<float> centre; // board px
        bool upIsHigh = false;     // Toggle2: is "lever up" the parameter's 1.0?
    };

    void timerCallback() override;
    void loadLayout();
    Control* controlAt(juce::Point<float> boardPos);
    juce::Point<float> toBoard(juce::Point<float> p) const { return p / scale_; }
    int frameFor(const Control& c) const;
    void setNormalised(Control& c, float v);
    void drawSprite(juce::Graphics& g, const juce::Image& strip, int frames, int frame,
                    juce::Point<float> centre);

    LiliProcessor& processor_;
    juce::Image board_;
    juce::Image knobStrip_;
    juce::Image toggleStrip_;
    std::vector<Control> controls_;
    std::vector<juce::Point<float>> meters_; // centre of each pair's 5-LED meter, board px
    lili::Telemetry telemetry_;
    std::vector<float> lastValues_;
    float scale_ = 1.0f;
    int snapshotCountdown_ = 30; // timer ticks until the optional LILI_SNAPSHOT capture

    Control* dragging_ = nullptr;
    float dragStartY_ = 0.0f;
    float dragStartValue_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiliEditor)
};
