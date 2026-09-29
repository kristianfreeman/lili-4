#include "PluginEditor.h"

#include "LiliAssets.h"

namespace {

constexpr float kBoardW = 1120.0f;
constexpr float kBoardH = 800.0f;
constexpr float kPadRadius = 20.0f;
constexpr float kKnobRadius = 18.0f;   // grab radius around a knob centre, board px
constexpr float kToggleRadius = 16.0f; // grab radius around a toggle pivot
constexpr float kDragRange = 240.0f;   // board px of vertical drag for the full range

const juce::Colour kAmber{0xffffa640};
const juce::Colour kHot{0xffff4d7a};

juce::Image loadImage(const void* data, int size) { return juce::ImageCache::getFromMemory(data, size); }

float num(const juce::var& v) { return static_cast<float>(static_cast<double>(v)); }

} // namespace

LiliEditor::LiliEditor(LiliProcessor& owner) : AudioProcessorEditor(owner), processor_(owner) {
    board_ = loadImage(LiliAssets::board_png, LiliAssets::board_pngSize);
    knobStrip_ = loadImage(LiliAssets::knob_strip_png, LiliAssets::knob_strip_pngSize);
    toggleStrip_ = loadImage(LiliAssets::toggle_strip_png, LiliAssets::toggle_strip_pngSize);
    loadLayout();

    setResizable(true, true);
    setResizeLimits(784, 560, 1680, 1200);
    if (auto* constrainer = getConstrainer()) {
        constrainer->setFixedAspectRatio(static_cast<double>(kBoardW / kBoardH));
    }
    setSize(static_cast<int>(kBoardW), static_cast<int>(kBoardH));
    startTimerHz(30);
}

LiliEditor::~LiliEditor() { stopTimer(); }

void LiliEditor::loadLayout() {
    const auto layout = juce::JSON::parse(
        juce::String::createStringFromData(LiliAssets::board_json, LiliAssets::board_jsonSize));
    auto& state = processor_.state();
    const auto param = [&state](const juce::var& id) { return state.getParameter(id.toString()); };

    for (const auto& k : *layout["knobs"].getArray()) {
        if (auto* p = param(k[0])) {
            controls_.push_back({Kind::Knob, p, {num(k[1]) + 26.0f, num(k[2]) + 17.0f}});
        }
    }
    for (const auto& j : *layout["jumpers"].getArray()) {
        if (auto* p = param(j[0])) {
            const bool three = j[4].size() == 3;
            // Choice option 0 is "up": normalised 0.0 is up.
            controls_.push_back(
                {three ? Kind::Toggle3 : Kind::Toggle2, p, {num(j[1]) + 16.0f, num(j[2]) + 38.0f}});
        }
    }
    for (const auto& d : *layout["dips"].getArray()) {
        const auto& items = *d[3].getArray();
        for (int i = 0; i < items.size(); ++i) {
            if (auto* p = param(items[i][0])) {
                // On/off switches: "on" (1.0) is up.
                controls_.push_back({Kind::Toggle2,
                                     p,
                                     {num(d[0]) + 16.0f + 38.0f * static_cast<float>(i), num(d[1]) + 38.0f},
                                     true});
            }
        }
    }
    const auto& pads = *layout["pads"].getArray();
    for (int i = 0; i < pads.size(); ++i) {
        if (auto* p = param("sensor" + juce::String(i + 1))) {
            controls_.push_back({Kind::Pad, p, {num(pads[i][0]), num(pads[i][1])}});
        }
    }
    for (const auto& m : *layout["meters"].getArray()) {
        meters_.push_back({num(m[0]), num(m[1])});
    }
    lastValues_.assign(controls_.size(), -1.0f);
}

int LiliEditor::frameFor(const Control& c) const {
    const float v = c.param->getValue();
    switch (c.kind) {
    case Kind::Knob: {
        const int frames = knobStrip_.getHeight() / knobStrip_.getWidth();
        return juce::jlimit(0, frames - 1, juce::roundToInt(v * static_cast<float>(frames - 1)));
    }
    case Kind::Toggle3: return juce::jlimit(0, 2, juce::roundToInt(v * 2.0f));
    case Kind::Toggle2: return ((v >= 0.5f) == c.upIsHigh) ? 0 : 2;
    case Kind::Pad: return 0;
    }
    return 0;
}

void LiliEditor::drawSprite(juce::Graphics& g, const juce::Image& strip, int frames, int frame,
                            juce::Point<float> centre) {
    const int fw = strip.getWidth();
    const int fh = strip.getHeight() / frames;
    const float size = static_cast<float>(fw) * 0.5f * scale_; // strips are rendered at 2x board px
    const auto dest = juce::Rectangle<float>(size, size).withCentre(centre * scale_);
    g.drawImage(strip, juce::roundToInt(dest.getX()), juce::roundToInt(dest.getY()),
                juce::roundToInt(dest.getWidth()), juce::roundToInt(dest.getHeight()), 0, frame * fh, fw, fh);
}

void LiliEditor::paint(juce::Graphics& g) {
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(board_, getLocalBounds().toFloat());

    // Sounding voices warm their touch pads (the GPU pass will add real glow).
    int voice = 0;
    for (const auto& c : controls_) {
        if (c.kind != Kind::Pad) {
            continue;
        }
        const float gain = telemetry_.voiceGain[static_cast<size_t>(voice++)];
        if (gain > 0.01f) {
            const auto r = kPadRadius * 1.25f * scale_;
            juce::ColourGradient glow(kAmber.withAlpha(0.45f * gain), c.centre * scale_,
                                      kAmber.withAlpha(0.0f), c.centre * scale_ + juce::Point<float>(r, 0.0f),
                                      true);
            g.setGradientFill(glow);
            g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c.centre * scale_));
        }
    }

    // Pair level meters: 4 amber + 1 pink "hot" LED.
    constexpr std::array<float, 5> kSteps{0.02f, 0.08f, 0.2f, 0.45f, 0.9f};
    for (size_t k = 0; k < meters_.size() && k < telemetry_.pairPeak.size(); ++k) {
        const float level = telemetry_.pairPeak[k];
        for (size_t j = 0; j < kSteps.size(); ++j) {
            if (level < kSteps[j]) {
                break;
            }
            const auto colour = j == kSteps.size() - 1 ? kHot : kAmber;
            const auto centre =
                (meters_[k] + juce::Point<float>((static_cast<float>(j) - 2.0f) * 12.0f, 0.0f)) * scale_;
            g.setColour(colour.withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(14.0f * scale_, 14.0f * scale_).withCentre(centre));
            g.setColour(colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(5.2f * scale_, 6.4f * scale_).withCentre(centre),
                                   1.2f * scale_);
        }
    }

    for (const auto& c : controls_) {
        if (c.kind == Kind::Knob) {
            drawSprite(g, knobStrip_, knobStrip_.getHeight() / knobStrip_.getWidth(), frameFor(c), c.centre);
        } else if (c.kind != Kind::Pad) {
            drawSprite(g, toggleStrip_, 3, frameFor(c), c.centre);
        }
    }
}

void LiliEditor::resized() { scale_ = static_cast<float>(getWidth()) / kBoardW; }

void LiliEditor::timerCallback() {
    telemetry_ = processor_.takeTelemetry();

    // Debug aid: LILI_SNAPSHOT=/path/out.png saves the editor once, ~1 s after opening.
    if (snapshotCountdown_ > 0 && --snapshotCountdown_ == 0) {
        const auto path = juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT", {});
        if (path.isNotEmpty()) {
            juce::File file(path);
            file.deleteFile();
            juce::FileOutputStream out(file);
            juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds(), true, 2.0f),
                                                      out);
        }
    }
    bool changed = false;
    for (size_t i = 0; i < controls_.size(); ++i) {
        const float v = controls_[i].param->getValue();
        if (!juce::exactlyEqual(v, lastValues_[i])) {
            lastValues_[i] = v;
            changed = true;
        }
    }
    bool alive = false;
    for (const float gain : telemetry_.voiceGain) {
        alive = alive || gain > 0.001f;
    }
    if (changed || alive) {
        repaint();
    }
}

LiliEditor::Control* LiliEditor::controlAt(juce::Point<float> boardPos) {
    for (auto& c : controls_) {
        const float radius = c.kind == Kind::Knob  ? kKnobRadius
                             : c.kind == Kind::Pad ? kPadRadius
                                                   : kToggleRadius;
        if (c.centre.getDistanceFrom(boardPos) <= radius) {
            return &c;
        }
    }
    return nullptr;
}

void LiliEditor::setNormalised(Control& c, float v) {
    c.param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
}

void LiliEditor::mouseMove(const juce::MouseEvent& e) {
    const auto* c = controlAt(toBoard(e.position));
    setMouseCursor(c == nullptr            ? juce::MouseCursor::NormalCursor
                   : c->kind == Kind::Knob ? juce::MouseCursor::UpDownResizeCursor
                                           : juce::MouseCursor::PointingHandCursor);
}

void LiliEditor::mouseDown(const juce::MouseEvent& e) {
    const auto pos = toBoard(e.position);
    auto* c = controlAt(pos);
    if (c == nullptr) {
        return;
    }
    switch (c->kind) {
    case Kind::Knob:
        dragging_ = c;
        dragStartY_ = e.position.y;
        dragStartValue_ = c->param->getValue();
        c->param->beginChangeGesture();
        break;
    case Kind::Pad:
    case Kind::Toggle2:
        c->param->beginChangeGesture();
        setNormalised(*c, c->param->getValue() >= 0.5f ? 0.0f : 1.0f);
        c->param->endChangeGesture();
        break;
    case Kind::Toggle3: {
        // Click above the pivot throws the lever up one position, below throws it down.
        const int index = juce::roundToInt(c->param->getValue() * 2.0f) + (pos.y < c->centre.y ? -1 : 1);
        c->param->beginChangeGesture();
        setNormalised(*c, static_cast<float>(juce::jlimit(0, 2, index)) / 2.0f);
        c->param->endChangeGesture();
        break;
    }
    }
    repaint();
}

void LiliEditor::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ == nullptr) {
        return;
    }
    const float range = kDragRange * scale_ * (e.mods.isShiftDown() ? 5.0f : 1.0f);
    setNormalised(*dragging_, dragStartValue_ + (dragStartY_ - e.position.y) / range);
    repaint();
}

void LiliEditor::mouseUp(const juce::MouseEvent& /*e*/) {
    if (dragging_ != nullptr) {
        dragging_->param->endChangeGesture();
        dragging_ = nullptr;
    }
}

void LiliEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    auto* c = controlAt(toBoard(e.position));
    if (c != nullptr && c->kind == Kind::Knob) {
        c->param->beginChangeGesture();
        setNormalised(*c, c->param->getDefaultValue());
        c->param->endChangeGesture();
        repaint();
    }
}

void LiliEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    auto* c = controlAt(toBoard(e.position));
    if (c == nullptr || c->kind != Kind::Knob) {
        return;
    }
    const float step =
        (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * (e.mods.isShiftDown() ? 0.05f : 0.25f);
    c->param->beginChangeGesture();
    setNormalised(*c, c->param->getValue() + step);
    c->param->endChangeGesture();
    repaint();
}
