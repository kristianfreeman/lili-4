#include "PluginEditor.h"

#include "LiliAssets.h"

namespace {

constexpr float kBoardW = 1000.0f; // art/board.json "size"
constexpr float kBoardH = 424.0f;
constexpr float kPadRadius = 18.0f;
constexpr float kKnobRadius = 16.0f;   // grab radius around a knob centre, board px
constexpr float kToggleRadius = 14.0f; // grab radius around a toggle pivot
constexpr float kDragRange = 240.0f;   // board px of vertical drag for the full range

const juce::Colour kAmber{0xffffa640};
const juce::Colour kHot{0xffff4d7a};

juce::Image loadImage(const void* data, int size) { return juce::ImageCache::getFromMemory(data, size); }

float num(const juce::var& v) { return static_cast<float>(static_cast<double>(v)); }

juce::Rectangle<float> rect(const juce::var& v) { return {num(v[0]), num(v[1]), num(v[2]), num(v[3])}; }

// The readout name: an entry's optional long name at `index`, else its silkscreen word.
juce::String nameOf(const juce::var& entry, int index, int labelIndex) {
    return entry.size() > index ? entry[index].toString() : entry[labelIndex].toString();
}

} // namespace

LiliEditor::LiliEditor(LiliProcessor& owner) : AudioProcessorEditor(owner), processor_(owner) {
    board_ = loadImage(LiliAssets::board_png, LiliAssets::board_pngSize);
    knobStrip_ = loadImage(LiliAssets::knob_strip_png, LiliAssets::knob_strip_pngSize);
    toggleStrip_ = loadImage(LiliAssets::toggle_strip_png, LiliAssets::toggle_strip_pngSize);
    outModeStrip_ = loadImage(LiliAssets::outmode_strip_png, LiliAssets::outmode_strip_pngSize);
    mono_ = juce::Font(juce::FontOptions(juce::Typeface::createSystemTypefaceFor(
        LiliAssets::IBMPlexMonoMedium_ttf, LiliAssets::IBMPlexMonoMedium_ttfSize)));
    pristine_ = board_.convertedToFormat(juce::Image::ARGB);
    frame_ = pristine_.createCopy();
    loadLayout();
    loadGlow();
    applyOutMode();
    snapshotPath_ = juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT", {});

    // Debug aid for snapshots: LILI_SNAPSHOT_SENSORS=13 latches petals 1 and 3
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
    // LILI_SNAPSHOT_SEED=/path.wav loads a Seed sample into group 3·4.
    const auto seedPath = juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT_SEED", {});
    if (juce::File::isAbsolutePath(seedPath)) {
        processor_.loadSeed(1, juce::File(seedPath));
    }
    // LILI_SNAPSHOT_PARAMS="hold12=1,volume=0" sets normalised parameter values.
    for (const auto& kv : juce::StringArray::fromTokens(
             juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT_PARAMS", {}), ",", {})) {
        if (auto* p = processor_.state().getParameter(kv.upToFirstOccurrenceOf("=", false, false).trim())) {
            p->setValueNotifyingHost(kv.fromFirstOccurrenceOf("=", false, false).getFloatValue());
        }
    }
    const auto readoutId = juce::SystemStats::getEnvironmentVariable("LILI_SNAPSHOT_READOUT", {});
    for (auto& c : controls_) {
        if (readoutId.isNotEmpty() && c.param->getParameterID() == readoutId) {
            readout_ = &c; // show this control's value tag in the snapshot
        }
    }

    setResizable(true, true);
    setResizeLimits(700, 297, 2000, 848);
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

    const auto strings = [](const juce::var& list) {
        juce::StringArray out;
        for (const auto& o : *list.getArray()) {
            out.add(o.toString());
        }
        return out;
    };
    // Coordinates are control centres in board px; see art/board.json.
    for (const auto& k : *layout["knobs"].getArray()) { // [id, x, y, label, value, (name)]
        if (auto* p = param(k[0])) {
            controls_.push_back({Kind::Knob, p, {num(k[1]), num(k[2])}, false, nameOf(k, 5, 3), {}, {}});
        }
    }
    for (const auto& j : *layout["jumpers"].getArray()) { // [id, x, y, label, options, sel, (name)]
        if (auto* p = param(j[0])) {
            const auto options = strings(j[4]);
            // Choice option 0 is "up": normalised 0.0 is up.
            const auto kind = options.size() == 3 ? Kind::Toggle3 : Kind::Toggle2;
            controls_.push_back({kind, p, {num(j[1]), num(j[2])}, false, nameOf(j, 6, 3), options, {}});
        }
    }
    for (const auto& d : *layout["dips"].getArray()) { // [id, x, y, label, legends, on, (name)]
        if (auto* p = param(d[0])) {
            // On/off switches: "on" (1.0) is up; legends, if any, read top to bottom.
            controls_.push_back(
                {Kind::Toggle2, p, {num(d[1]), num(d[2])}, true, nameOf(d, 6, 3), strings(d[4]), {}});
        }
    }
    const auto& pads = *layout["pads"].getArray(); // [x, y, name]; pad i plays petal i
    for (int i = 0; i < pads.size(); ++i) {
        if (auto* p = param("sensor" + juce::String(i + 1))) {
            controls_.push_back(
                {Kind::Pad, p, {num(pads[i][0]), num(pads[i][1])}, false, pads[i][2].toString(), {}, {}});
        }
    }
    const auto& om = layout["outMode"];
    outModePatch_ = rect(om["patch"]);
    if (auto* p = param(om["id"])) {
        const auto hit = rect(om["hit"]);
        controls_.push_back(
            {Kind::OutMode, p, hit.getCentre(), true, om["name"].toString(), strings(om["texts"]), hit});
    }
    for (const auto& m : *layout["meters"].getArray()) {
        meters_.push_back({num(m[0]), num(m[1])});
    }
    meterPitch_ = num(layout.getProperty("meterPitch", 10.0));
    for (const auto& b : *layout["boxes"].getArray()) { // [x, y, w, h, title]
        const auto title = b[4].toString();
        if (title.startsWith("GROUP")) {
            seedBoxes_[title.endsWith("4") ? 1 : 0] = rect(b);
        }
    }
    for (auto& c : controls_) {
        if (c.kind == Kind::OutMode) {
            outMode_ = &c; // controls_ is complete: pointers into it stay valid
        }
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
    if (name.startsWith("petal")) {
        // The louder of the petal's two oscillators (Bloom breathes them separately).
        // Perceptual: a Hold drone at gain 0.36 (-9 dB) is clearly audible and should
        // read as lit, not 36% glow. sqrt keeps full notes full and lifts quiet drones.
        const auto p = static_cast<size_t>(name.getTrailingIntValue());
        return std::sqrt(std::max(t.voiceGain[2 * p], t.voiceGain[2 * p + 1]));
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
        static const char* const ids[] = {"source1", "source2", "source3", "source4"};
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
        // The LFO LEDs blink with the real LFO squares; too fast to blink -> steady glow.
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
    case Kind::OutMode: return v >= 0.5f ? 1 : 0;
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
    constexpr std::array<float, 5> kSteps{0.08f, 0.25f, 0.5f, 0.8f, 1.2f}; // one oscillator ~0.9, both ~1.4
    for (size_t k = 0; k < meters_.size() && k < telemetry_.pairPeak.size(); ++k) {
        const float level = telemetry_.pairPeak[k];
        for (size_t j = 0; j < kSteps.size(); ++j) {
            if (level < kSteps[j]) {
                break;
            }
            const auto colour = j == kSteps.size() - 1 ? kHot : kAmber;
            const float dx = (static_cast<float>(j) - 2.0f) * meterPitch_;
            const auto centre = (meters_[k] + juce::Point<float>(dx, 0.0f)) * scale_;
            // Bloom: the LED lights the board around it.
            const float r = 9.0f * scale_;
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.55f), centre, colour.withAlpha(0.0f),
                                                   centre.translated(r, 0.0f), true));
            g.fillEllipse(juce::Rectangle<float>(2.0f * r, 2.0f * r).withCentre(centre));
            g.setColour(colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(5.4f * scale_, 4.2f * scale_).withCentre(centre),
                                   1.0f * scale_);
        }
    }

    for (const auto& c : controls_) {
        if (c.kind == Kind::Knob) {
            drawSprite(g, knobStrip_, knobStrip_.getHeight() / knobStrip_.getWidth(), frameFor(c), c.centre);
        } else if (c.kind == Kind::Toggle2 || c.kind == Kind::Toggle3) {
            drawSprite(g, toggleStrip_, 3, frameFor(c), c.centre);
        }
    }

    // While an audio file is dragged over the board, outline the half it would load into.
    if (dropGroup_ >= 0) {
        const auto half = getLocalBounds().toFloat().withWidth(static_cast<float>(getWidth()) * 0.5f);
        const auto area = (dropGroup_ == 0 ? half : half.withX(half.getRight())).reduced(6.0f * scale_);
        g.setColour(kAmber.withAlpha(0.12f));
        g.fillRoundedRectangle(area, 12.0f * scale_);
        g.setColour(kAmber);
        g.drawRoundedRectangle(area, 12.0f * scale_, 2.0f * scale_);
        g.setFont(mono_.withHeight(14.0f * scale_));
        // Between the petal row and the group row.
        g.drawText(juce::String::fromUTF8(dropGroup_ == 0 ? "SEED 1\xc2\xb7"
                                                            "2"
                                                          : "SEED 3\xc2\xb7"
                                                            "4"),
                   area.withTrimmedTop(area.getHeight() * 0.45f), juce::Justification::centredTop, false);
        // and the group the sample will play in
        g.drawRect(seedBoxes_[static_cast<size_t>(dropGroup_)] * scale_, 2.0f * scale_);
    }

    // Value tag for the hovered or dragged control, in the silkscreen face.
    if (readout_ != nullptr) {
        const auto area = readoutArea(*readout_) * scale_;
        g.setColour(juce::Colour(0xf00a0f0c));
        g.fillRoundedRectangle(area, 3.0f * scale_);
        g.setColour(kAmber.withAlpha(0.8f));
        g.drawRoundedRectangle(area, 3.0f * scale_, 1.0f * scale_);
        g.setColour(kAmber);
        g.setFont(mono_.withHeight(11.0f * scale_));
        g.drawText(readoutText(*readout_), area, juce::Justification::centred, false);
    }
}

void LiliEditor::resized() { scale_ = static_cast<float>(getWidth()) / kBoardW; }

juce::String LiliEditor::readoutText(const Control& c) const {
    const auto id = c.param->getParameterID();
    const float v = c.param->getValue();
    juce::String value;
    if (c.kind == Kind::Knob) {
        if (id.startsWith("tune")) {
            // Exactly what the engine plays: tune curve x group pitch multiplier.
            lili::Params p;
            auto& state = processor_.state();
            for (size_t i = 0; i < lili::kNumParams; ++i) {
                const auto& info = lili::kParamInfo[i];
                if (const auto* raw =
                        state.getRawParameterValue(juce::String(info.id.data(), info.id.size()))) {
                    lili::setParam(p, i, raw->load());
                }
            }
            const float hz =
                lili::Engine::voiceFrequency(p, 2 * (id.getTrailingIntValue() - 1)); // oscillator A
            value = hz < 1000.0f ? juce::String(hz, hz < 100.0f ? 1 : 0) + " Hz"
                                 : juce::String(hz / 1000.0f, 2) + " kHz";
        } else if (id.startsWith("spread")) {
            const float st = lili::Engine::spreadSemitones(v);
            const juce::String sign = st >= 0.0f ? "+" : "";
            value = std::fabs(st) < 1.0f ? sign + juce::String(juce::roundToInt(st * 100.0f)) + " c"
                                         : sign + juce::String(st, 1) + " st";
        } else if (id.startsWith("pitch")) {
            const int st = lili::Engine::pitchSemitones(v); // quantised to semitones
            value = (st > 0 ? "+" : "") + juce::String(st) + " st";
        } else if (id.startsWith("lfoFreq")) {
            const auto* bee = processor_.state().getParameter("bee");
            const bool pollinator = bee != nullptr && bee->getValue() >= 0.5f;
            const float hz = lili::mtof(127.0f * v * v - 75.0f);
            if (pollinator && id == "lfoFreqB") {
                value =
                    "CHAOS " + juce::String::fromUTF8("\xcf\x81") + "=" + juce::String(20.0f + 25.0f * v, 1);
            } else {
                value = (pollinator ? "FLIGHT " : "") + juce::String(hz, hz < 1.0f ? 2 : 1) + " Hz";
            }
        } else if (id.startsWith("table")) {
            static const char* const names[] = {"STEM", "REED", "GLASS", "MOSS"};
            const float f = v * 3.0f;
            const int i0 = std::min(2, static_cast<int>(f));
            const float frac = f - static_cast<float>(i0);
            value = frac < 0.1f ? juce::String(names[i0])
                    : frac > 0.9f
                        ? juce::String(names[i0 + 1])
                        : juce::String(names[i0]) + juce::String::fromUTF8("\xe2\x80\xba") + names[i0 + 1];
        } else if (id == "drift") {
            const float period = 300.0f * std::pow(8.0f / 300.0f, v);
            value = period >= 60.0f ? "~" + juce::String(period / 60.0f, 1) + " min"
                                    : "~" + juce::String(juce::roundToInt(period)) + " s";
        } else if (id.startsWith("delTime")) {
            const float ms = 1.45125f * std::exp2(12.0f * v);
            value = ms < 1000.0f ? juce::String(ms, ms < 10.0f ? 1 : 0) + " ms"
                                 : juce::String(ms / 1000.0f, 2) + " s";
        } else if (id == "delFeedback") {
            value = juce::String(v * std::exp2(2.0f * v), 2);
        } else {
            value = juce::String(juce::roundToInt(v * 100.0f)) + "%";
        }
    } else if (c.kind == Kind::OutMode) {
        value = v >= 0.5f ? "STEREO" : "MONO";
    } else if (!c.options.isEmpty()) {
        // Legends read top to bottom: the position the lever is thrown to.
        const int index = c.kind == Kind::Toggle3 ? juce::roundToInt(v * 2.0f) : (frameFor(c) == 0 ? 0 : 1);
        value = c.options[juce::jlimit(0, c.options.size() - 1, index)];
        if (id.startsWith("engine") && index == lili::PetalSeed) {
            const auto name = processor_.seedName(id == "engine12" ? 0 : 1);
            value += ": " + (name.isNotEmpty() ? name : juce::String("drop an audio file"));
        }
    } else if (c.kind == Kind::Pad) {
        value = v >= 0.5f ? "LATCHED" : "OFF";
    } else {
        value = v >= 0.5f ? "ON" : "OFF";
    }
    return c.label + juce::String::fromUTF8(" \xc2\xb7 ") + value;
}

juce::Rectangle<float> LiliEditor::readoutArea(const Control& c) const {
    const auto text = readoutText(c);
    const float w = juce::GlyphArrangement::getStringWidth(mono_.withHeight(11.0f), text) + 16.0f;
    auto area = juce::Rectangle<float>(w, 20.0f);
    if (c.kind == Kind::OutMode) {
        // At the board's top edge: show it to the left of the label.
        area = area.withCentre({c.hit.getX() - w * 0.5f - 6.0f, c.hit.getCentreY()});
    } else {
        const float above = c.kind == Kind::Knob ? 30.0f : 26.0f;
        area = area.withCentre(c.centre.translated(0.0f, -above - 10.0f));
    }
    // Keep it on the board (controls near the edges).
    return area.constrainedWithin(juce::Rectangle<float>(kBoardW, kBoardH).reduced(4.0f));
}

juce::Rectangle<float> LiliEditor::spriteArea(const Control& c) const {
    if (c.kind == Kind::OutMode) {
        return outModePatch_.expanded(2.0f);
    }
    return juce::Rectangle<float>(130.0f, 130.0f).withCentre(c.centre); // sprite + shadow
}

void LiliEditor::applyOutMode() {
    if (!outModeStrip_.isValid()) {
        return;
    }
    const int frame = outMode_ != nullptr ? frameFor(*outMode_) : 0; // mono until "stereo" exists
    if (frame == outModeFrame_) {
        return;
    }
    outModeFrame_ = frame;
    // Silkscreen is part of the board, under the glow: put the patch into the clean board, then
    // rebuild frame_ from it (every lit layer is re-added on the next updateGlow).
    const int fw = outModeStrip_.getWidth();
    const int fh = outModeStrip_.getHeight() / 2;
    const auto d = (outModePatch_ * 2.0f).toNearestInt(); // board images are 2x board px
    {
        juce::Graphics g(pristine_);
        const int x = d.getX(), y = d.getY(), w = d.getWidth(), h = d.getHeight();
        g.drawImage(board_, x, y, w, h, x, y, w, h);
        g.drawImage(outModeStrip_, x, y, w, h, 0, frame * fh, fw, fh);
    }
    frame_ = pristine_.createCopy();
    for (auto& layer : glow_) {
        layer.drawn = 0.0f;
    }
    repaintBoardArea(outModePatch_);
}

void LiliEditor::parentHierarchyChanged() {
    if (snapshotPath_.isNotEmpty()) {
        hideForSnapshot();
    }
}

void LiliEditor::hideForSnapshot() {
    // The standalone window would flash up on the user's screen: keep it fully transparent.
    if (auto* top = getTopLevelComponent(); top != this && top->getAlpha() > 0.0f) {
        top->setAlpha(0.0f);
    }
}

void LiliEditor::setReadout(Control* c) {
    if (c == readout_) {
        return;
    }
    if (readout_ != nullptr) {
        repaintBoardArea(readoutArea(*readout_));
    }
    readout_ = c;
    if (readout_ != nullptr) {
        repaintBoardArea(readoutArea(*readout_));
    }
}

void LiliEditor::timerCallback() {
    telemetry_ = processor_.takeTelemetry();

    // Debug aid: LILI_SNAPSHOT=/path/out.png renders the editor offscreen ~1 s after opening, saves
    // it and quits the standalone app; its window stays invisible throughout.
    if (snapshotPath_.isNotEmpty()) {
        hideForSnapshot();
    }
    if (snapshotCountdown_ > 0 && --snapshotCountdown_ == 0 && snapshotPath_.isNotEmpty()) {
        juce::File file(snapshotPath_);
        file.deleteFile();
        {
            juce::FileOutputStream out(file);
            const auto image = createComponentSnapshot(getLocalBounds(), true, 2.0f);
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
        if (juce::JUCEApplicationBase::isStandaloneApp()) {
            juce::JUCEApplicationBase::quit();
        }
    }
    // Repaint only what changed.
    for (size_t i = 0; i < controls_.size(); ++i) {
        const float v = controls_[i].param->getValue();
        if (!juce::exactlyEqual(v, lastValues_[i])) {
            lastValues_[i] = v;
            if (&controls_[i] == outMode_) {
                applyOutMode();
            }
            repaintBoardArea(spriteArea(controls_[i]));
            if (&controls_[i] == readout_) {
                // the tag's text (and width) changed too, e.g. from automation
                repaintBoardArea(readoutArea(controls_[i]).withSizeKeepingCentre(260.0f, 24.0f));
            }
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
            repaintBoardArea(juce::Rectangle<float>(5.0f * meterPitch_ + 12.0f, 16.0f).withCentre(m));
        }
    }
    metersWereLit_ = metersLit;
}

LiliEditor::Control* LiliEditor::controlAt(juce::Point<float> boardPos) {
    for (auto& c : controls_) {
        if (c.kind == Kind::OutMode) {
            if (c.hit.contains(boardPos)) {
                return &c;
            }
            continue;
        }
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

bool LiliEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    const auto wildcards = processor_.formats().getWildcardForAllFormats();
    for (const auto& f : files) {
        if (juce::File(f).hasFileExtension(wildcards.removeCharacters("*"))) {
            return true;
        }
    }
    return false;
}

void LiliEditor::fileDragEnter(const juce::StringArray& /*files*/, int x, int /*y*/) {
    dropGroup_ = x < getWidth() / 2 ? 0 : 1;
    repaint();
}

void LiliEditor::fileDragMove(const juce::StringArray& files, int x, int y) {
    const int group = x < getWidth() / 2 ? 0 : 1;
    if (group != dropGroup_) {
        fileDragEnter(files, x, y);
    }
}

void LiliEditor::fileDragExit(const juce::StringArray& /*files*/) {
    dropGroup_ = -1;
    repaint();
}

void LiliEditor::filesDropped(const juce::StringArray& files, int x, int /*y*/) {
    dropGroup_ = -1;
    const int group = x < getWidth() / 2 ? 0 : 1;
    for (const auto& f : files) {
        if (processor_.loadSeed(group, juce::File(f))) {
            // Switch that group to the Seed engine so the drop is heard straight away.
            if (auto* engine = processor_.state().getParameter(group == 0 ? "engine12" : "engine34")) {
                engine->beginChangeGesture();
                engine->setValueNotifyingHost(engine->convertTo0to1(static_cast<float>(lili::PetalSeed)));
                engine->endChangeGesture();
            }
            break;
        }
    }
    repaint();
}

void LiliEditor::mouseExit(const juce::MouseEvent& /*e*/) {
    if (dragging_ == nullptr && snapshotPath_.isEmpty()) { // a snapshot keeps LILI_SNAPSHOT_READOUT
        setReadout(nullptr);
    }
}

void LiliEditor::mouseMove(const juce::MouseEvent& e) {
    if (snapshotPath_.isNotEmpty()) {
        return; // the (invisible) snapshot window ignores the pointer
    }
    auto* c = controlAt(toBoard(e.position));
    setReadout(c);
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
    case Kind::OutMode:
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

void LiliEditor::mouseUp(const juce::MouseEvent& e) {
    if (dragging_ != nullptr) {
        dragging_->param->endChangeGesture();
        dragging_ = nullptr;
    }
    setReadout(controlAt(toBoard(e.position))); // the drag may have ended off the knob
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
