#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

// The rendered circuit-board editor. Art is pre-rendered in Blender (see
// docs/ART.md): a board image plus knob/toggle sprite strips, laid out from
// art/board.json so hit areas match the render exactly. Controls follow the
// parameters (and host automation); pads and LED meters follow engine telemetry.
class LiliEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer {
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
    void mouseExit(const juce::MouseEvent& e) override;
    void parentHierarchyChanged() override;

    // Seed samples: drop an audio file on the left (1·2) or right (3·4) half of the board.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    // OutMode: the clickable "MONO OUT" / "STEREO OUT" silkscreen (a two-state label).
    enum class Kind { Knob, Toggle3, Toggle2, Pad, OutMode };

    struct Control {
        Kind kind;
        juce::RangedAudioParameter* param;
        juce::Point<float> centre;  // board px
        bool upIsHigh = false;      // Toggle2: is "lever up" the parameter's 1.0?
        juce::String label;         // name for the hover readout (the silkscreen word, or its long form)
        juce::StringArray options;  // toggle positions' legends, top to bottom
        juce::Rectangle<float> hit; // OutMode: click area, board px
    };

    // A pre-rendered glow delta (tools/art/export_ui.py), kept only as runs of
    // pixels that actually light up, so per-frame cost follows what's glowing.
    struct GlowSpan {
        int y, x0, x1;
        size_t offset; // into GlowLayer::bgr
    };
    struct GlowLayer {
        juce::String name;
        std::vector<GlowSpan> spans;
        std::vector<uint8_t> bgr;    // delta pixels in the image's byte order
        float shown = 0.0f;          // level after release smoothing
        float drawn = 0.0f;          // level currently added into frame_
        juce::Rectangle<int> bounds; // in frame_ pixels (board px * 2)
    };

    void timerCallback() override;
    void loadLayout();
    void loadGlow();
    juce::Rectangle<int> updateGlow(float dt); // returns the changed area in frame_ pixels
    void repaintBoardArea(juce::Rectangle<float> boardArea);
    float glowTarget(const juce::String& name);
    float paramValue(const juce::String& id);
    Control* controlAt(juce::Point<float> boardPos);
    juce::Point<float> toBoard(juce::Point<float> p) const { return p / scale_; }
    int frameFor(const Control& c) const;
    void setNormalised(Control& c, float v);
    void drawSprite(juce::Graphics& g, const juce::Image& strip, int frames, int frame,
                    juce::Point<float> centre);
    juce::String readoutText(const Control& c) const;
    juce::Rectangle<float> readoutArea(const Control& c) const; // board px
    juce::Rectangle<float> spriteArea(const Control& c) const;  // what a value change repaints, board px
    void setReadout(Control* c);
    void applyOutMode(); // bakes the current MONO OUT / STEREO OUT patch into the board images
    void hideForSnapshot();

    LiliProcessor& processor_;
    juce::Image board_;
    juce::Image pristine_; // board as ARGB, to undo last frame's glow
    juce::Image frame_;    // board + glow: what paint() draws
    std::vector<GlowLayer> glow_;
    juce::Image knobStrip_;
    juce::Image toggleStrip_;
    juce::Image outModeStrip_;            // MONO OUT / STEREO OUT board patches, frame = stereo state
    juce::Rectangle<float> outModePatch_; // where that patch sits, board px
    Control* outMode_ = nullptr;          // null until the processor has a "stereo" parameter
    int outModeFrame_ = -1;               // frame currently in pristine_
    std::vector<Control> controls_;
    std::vector<juce::Point<float>> meters_; // centre of each pair's 5-LED meter, board px
    float meterPitch_ = 10.0f;
    std::array<juce::Rectangle<float>, 2> seedBoxes_; // GROUP 1·2 / 3·4 frames, board px (drop targets)
    lili::Telemetry telemetry_;
    std::vector<float> lastValues_;
    bool metersWereLit_ = false;
    float scale_ = 1.0f;
    int snapshotCountdown_ = 30; // timer ticks until the optional LILI_SNAPSHOT capture
    juce::String snapshotPath_;  // LILI_SNAPSHOT: render offscreen, save, quit (standalone)

    int dropGroup_ = -1; // group highlighted while an audio file is dragged over
    juce::Font mono_{juce::FontOptions{}};
    Control* readout_ = nullptr; // control whose value tag is shown (hovered or dragged)

    Control* dragging_ = nullptr;
    float dragStartY_ = 0.0f;
    float dragStartValue_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiliEditor)
};
