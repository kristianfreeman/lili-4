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
    pristine_ = board_.convertedToFormat(juce::Image::ARGB);
    frame_ = pristine_.createCopy();
    loadLayout();
    loadGlow();

    // Debug aid for snapshots: LILI_SNAPSHOT_SENSORS=136 latches sensors 1, 3, 6
    // and mutes the output (telemetry is measured before the volume stage).
    const auto demo = juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT_SENSORS", {});
    if (demo.isNotEmpty()) {
        for (const auto ch : demo) {
            if (auto* p = processor_.state().getParameter("sensor" + juce::String::charToString(ch))) {
                p->setValueNotifyingHost(1.0f);
            }
        }
        if (auto* vol = processor_.state().getParameter("volume")) {
            vol->setValueNotifyingHost(0.0f);
        }
    }

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

void LiliEditor::loadGlow() {
    const auto manifest = juce::JSON::parse(
        juce::String::createStringFromData(LiliAssets::glow_json, LiliAssets::glow_jsonSize));
    const auto* rects = manifest["rects"].getDynamicObject();
    if (rects == nullptr) {
        return;
    }
    constexpr int kSignificant = 2; // deltas below this (of 255) are invisible
    for (const auto& prop : rects->getProperties()) {
        const auto name = prop.name.toString();
        int size = 0;
        const auto* data = LiliAssets::getNamedResource((name + "_png").toRawUTF8(), size);
        if (data == nullptr) {
            continue;
        }
        const auto img = juce::ImageCache::getFromMemory(data, size);
        const int rx = static_cast<int>(prop.value[0]);
        const int ry = static_cast<int>(prop.value[1]);
        const juce::Image::BitmapData src(img, juce::Image::BitmapData::readOnly);
        GlowLayer layer;
        layer.name = name;
        for (int y = 0; y < img.getHeight(); ++y) {
            int start = -1;
            for (int x = 0; x <= img.getWidth(); ++x) {
                const auto c = x < img.getWidth() ? src.getPixelColour(x, y) : juce::Colour();
                const bool lit =
                    x < img.getWidth() && std::max({c.getRed(), c.getGreen(), c.getBlue()}) >= kSignificant;
                if (lit) {
                    if (start < 0) {
                        start = x;
                        layer.spans.push_back({ry + y, rx + x, rx + x, layer.bgr.size()});
                    }
                    // byte order of a JUCE ARGB pixel in memory (little-endian): b, g, r, a
                    layer.bgr.insert(layer.bgr.end(), {c.getBlue(), c.getGreen(), c.getRed()});
                    layer.spans.back().x1 = rx + x + 1;
                } else {
                    start = -1;
                }
            }
        }
        layer.bounds = {rx, ry, img.getWidth(), img.getHeight()};
        glow_.push_back(std::move(layer));
    }
}

float LiliEditor::paramValue(const juce::String& id) {
    const auto* p = processor_.state().getParameter(id);
    return p != nullptr ? p->getValue() : 0.0f;
}

float LiliEditor::glowTarget(const juce::String& name) {
    const auto& t = telemetry_;
    const auto clamp01 = [](float v) { return juce::jlimit(0.0f, 1.0f, v); };
    if (name.startsWith("voice")) {
        return t.voiceGain[static_cast<size_t>(name.getTrailingIntValue())];
    }
    if (name == "mix") {
        return clamp01((t.pairPeak[0] + t.pairPeak[1] + t.pairPeak[2] + t.pairPeak[3]) * 0.8f);
    }
    if (name.startsWith("delay")) {
        const auto k = static_cast<size_t>(name.getTrailingIntValue());
        return clamp01(t.delayPeak[k] * 4.0f) * (paramValue("delMix") > 0.01f ? 1.0f : 0.35f);
    }
    if (name.startsWith("xmod")) {
        // Lit when a pair on that side takes its partner as FM source (Source option 0).
        const size_t a = name.endsWith("0") ? 0 : 2;
        static const char* const ids[] = {"source12", "source34", "source56", "source78"};
        float level = 0.0f;
        for (size_t p = a; p < a + 2; ++p) {
            if (juce::roundToInt(paramValue(ids[p]) * 2.0f) == 0) {
                level = std::max(level, t.fmPeak[p] * 1.5f);
            }
        }
        return clamp01(level);
    }
    if (name == "totalfb") {
        return paramValue("totalFb") >= 0.5f ? clamp01(t.totalFbPeak * 2.0f) : 0.0f;
    }
    if (name.startsWith("lfo")) {
        // The leaf LEDs blink with the real LFO squares; too fast to blink -> steady glow.
        const bool b = name.endsWith("1");
        const float hz = b ? t.lfoHzB : t.lfoHzA;
        const float phase = b ? t.lfoPhaseB : t.lfoPhaseA;
        return hz > 12.0f ? 0.6f : (std::cos(juce::MathConstants<float>::twoPi * phase) > 0.0f ? 1.0f : 0.0f);
    }
    if (name == "stamens") {
        return clamp01(t.outPeak * 1.2f);
    }
    return 0.0f;
}

juce::Rectangle<int> LiliEditor::updateGlow(float dt) {
    const float decay = std::exp(-dt / 0.15f); // instant attack, 150 ms release
    juce::Rectangle<int> dirty;
    for (auto& layer : glow_) {
        layer.shown = std::max(glowTarget(layer.name), layer.shown * decay);
        if (layer.shown < 0.004f) {
            layer.shown = 0.0f;
        }
        if (std::abs(layer.shown - layer.drawn) > 0.004f) {
            dirty = dirty.getUnion(layer.bounds);
        }
    }
    if (dirty.isEmpty()) {
        return {};
    }

    const juce::Image::BitmapData dst(frame_, juce::Image::BitmapData::readWrite);
    const juce::Image::BitmapData orig(pristine_, juce::Image::BitmapData::readOnly);
    // Undo last frame's glow (layers overlap, so restore everything before adding).
    for (const auto& layer : glow_) {
        if (layer.drawn > 0.0f) {
            for (const auto& s : layer.spans) {
                std::memcpy(dst.getPixelPointer(s.x0, s.y), orig.getPixelPointer(s.x0, s.y),
                            static_cast<size_t>((s.x1 - s.x0) * dst.pixelStride));
            }
        }
    }
    for (auto& layer : glow_) {
        layer.drawn = layer.shown;
        if (layer.shown <= 0.0f) {
            continue;
        }
        const int q = juce::roundToInt(layer.shown * 256.0f);
        for (const auto& s : layer.spans) {
            auto* d = dst.getPixelPointer(s.x0, s.y);
            const auto* g = layer.bgr.data() + s.offset;
            for (int x = s.x0; x < s.x1; ++x, d += dst.pixelStride, g += 3) {
                for (int c = 0; c < 3; ++c) {
                    d[c] = static_cast<uint8_t>(std::min(255, d[c] + ((g[c] * q) >> 8)));
                }
            }
        }
    }
    return dirty;
}

void LiliEditor::repaintBoardArea(juce::Rectangle<float> boardArea) {
    repaint((boardArea * scale_).expanded(2.0f).getSmallestIntegerContainer());
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
    g.drawImage(frame_, getLocalBounds().toFloat()); // board + audio-driven glow (updateGlow)

    // Pair level meters: 4 amber + 1 pink "hot" LED.
    constexpr std::array<float, 5> kSteps{0.08f, 0.25f, 0.5f, 0.8f, 1.2f}; // one voice ~0.9, both ~1.4
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
    // Repaint only what changed: a full-board repaint at 30 fps costs ~15% of a core.
    for (size_t i = 0; i < controls_.size(); ++i) {
        const float v = controls_[i].param->getValue();
        if (!juce::exactlyEqual(v, lastValues_[i])) {
            lastValues_[i] = v;
            repaintBoardArea(
                juce::Rectangle<float>(130.0f, 130.0f).withCentre(controls_[i].centre)); // sprite + shadow
        }
    }
    const auto glowArea = updateGlow(1.0f / 30.0f);
    if (!glowArea.isEmpty()) {
        repaintBoardArea(glowArea.toFloat() * 0.5f); // frame_ is 2x board px
    }
    bool metersLit = false;
    for (const float peak : telemetry_.pairPeak) {
        metersLit = metersLit || peak > 0.02f;
    }
    if (metersLit || metersWereLit_) {
        for (const auto& m : meters_) {
            repaintBoardArea(juce::Rectangle<float>(76.0f, 20.0f).withCentre(m));
        }
    }
    metersWereLit_ = metersLit;
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
