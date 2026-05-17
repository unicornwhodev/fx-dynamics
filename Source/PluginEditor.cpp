#include "PluginEditor.h"
#include "BinaryData.h"

MusiqueCompressorEditor::MusiqueCompressorEditor(MusiqueCompressorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    // Header
    titleLabel.setText("COMPRESSOR", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    auto setupHdrBtn = [&](juce::TextButton& b, bool toggle = false) {
        b.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
        b.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
        if (toggle) b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    setupHdrBtn(bypassBtn, true);
    setupHdrBtn(monoBtn, true);
    setupHdrBtn(modeBtn, true);
    setupHdrBtn(autoBtn);
    monoBtn.setTooltip("Sum the input to mono before the compressor detector and gain stage");
    modeBtn.setTooltip("Switch compressor detector mode between Peak and RMS");
    autoBtn.setTooltip("Displays live measured gain reduction from the DSP path");
    autoBtn.onClick = [] {};

    // Preset bar
    setupHdrBtn(prevBtn); setupHdrBtn(nextBtn); setupHdrBtn(saveBtn); setupHdrBtn(abBtn);
    addAndMakeVisible(presetBox);

    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-dynamics"));
    if (presets->isEmpty()) { presetBox.addItem("Init", 1); presetBox.setSelectedId(1); }
    else
    {
        int id = 1;
        for (auto& pv : *presets)
            if (auto* o = pv.getDynamicObject())
                presetBox.addItem(o->getProperty("name").toString(), id++);
        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
        fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(0));
    }
    presetBox.onChange = [this] {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i < presets->size()) fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(i));
    };
    prevBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i > 0) presetBox.setSelectedItemIndex(i - 1); };
    nextBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i < presetBox.getNumItems() - 1) presetBox.setSelectedItemIndex(i + 1); };
    saveBtn.onClick = [this] {
        auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        juce::StringArray ids {"threshold","ratio","attack","release","makeup","mix","output","rms_mode","bypass","mono"};
        if (fx::preset::saveUserPreset("fx-dynamics", name, ids, proc.getAPVTS()))
        {
            *presets = fx::preset::loadAllPresets("fx-dynamics");
            presetBox.clear();
            int id = 1;
            for (auto& pv : *presets)
                if (auto* o = pv.getDynamicObject()) presetBox.addItem(o->getProperty("name").toString(), id++);
            presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    // Knobs
    const char* labels[6] = {"THRESHOLD", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX"};
    for (int i = 0; i < 6; ++i)
    {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setText(labels[i], juce::dontSendNotification);
        knobLabels[i].setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::label).withStyle("Bold")));
        knobLabels[i].setJustificationType(juce::Justification::centred);
        knobLabels[i].setColour(juce::Label::textColourId, fx::col::textMuted);
        addAndMakeVisible(knobLabels[i]);
    }

    // Footer
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(outputSlider);
    grLED.setAccent(fx::accent::compressor);
    addAndMakeVisible(grLED);
    versionLabel.setText("Musique Compressor v1.0", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    // Attachments
    thrAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "threshold", knobs[0]);
    ratAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "ratio",     knobs[1]);
    atkAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "attack",    knobs[2]);
    relAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "release",   knobs[3]);
    mkAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "makeup",    knobs[4]);
    mixAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix",       knobs[5]);
    outAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "output",    outputSlider);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    monoAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "mono", monoBtn);
    modeAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "rms_mode", modeBtn);

    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueCompressorEditor::~MusiqueCompressorEditor() { setLookAndFeel(nullptr); }

void MusiqueCompressorEditor::timerCallback()
{
    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);

    phase += 0.04f;
    if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;

    const bool mono = proc.getAPVTS().getRawParameterValue("mono")->load() > 0.5f;
    const bool rmsMode = proc.getAPVTS().getRawParameterValue("rms_mode")->load() > 0.5f;
    monoBtn.setButtonText(mono ? "MONO IN" : "STEREO IN");
    modeBtn.setButtonText(rmsMode ? "RMS" : "PEAK");

    const float grDb = proc.getCurrentGainReductionDb();
    const float grTarget = juce::jlimit(0.0f, 1.0f, grDb / 18.0f);
    grSmooth += (grTarget - grSmooth) * 0.18f;
    grLED.setOn(grSmooth > 0.3f);
    autoBtn.setButtonText(grDb > 0.1f ? ("GR " + juce::String(grDb, 1) + "dB") : "GR LIVE");

    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueCompressorEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    float threshold = -18.0f, ratio = 4.0f, makeup = 0.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("threshold")) threshold = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("ratio")) ratio = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("makeup")) makeup = p->load();
    const bool mono = proc.getAPVTS().getRawParameterValue("mono")->load() > 0.5f;
    const bool rmsMode = proc.getAPVTS().getRawParameterValue("rms_mode")->load() > 0.5f;
    const float grDb = proc.getCurrentGainReductionDb();

    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float cx = (float)area.getX();
    const float cy = (float)area.getY();
    const float pad = 40.0f;
    const float graphW = w - 2.0f * pad;
    const float graphH = h - 2.0f * pad;

    // dB scales (-60 to 0 on both axes)
    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    for (int db = -60; db <= 0; db += 12)
    {
        float norm = (float)(db + 60) / 60.0f;
        // Input axis (bottom)
        float xPos = cx + pad + norm * graphW;
        g.drawText(juce::String(db), (int)(xPos - 14), (int)(cy + h - pad + 4), 28, 12, juce::Justification::centred);
        g.setColour(fx::col::gridMinor);
        g.drawVerticalLine((int)xPos, cy + pad, cy + h - pad);
        g.setColour(fx::col::textMuted);

        // Output axis (left)
        float yPos = cy + h - pad - norm * graphH;
        g.drawText(juce::String(db), (int)(cx + 4), (int)(yPos - 5), 30, 10, juce::Justification::centredRight);
        g.setColour(fx::col::gridMinor);
        g.drawHorizontalLine((int)yPos, cx + pad, cx + w - pad);
        g.setColour(fx::col::textMuted);
    }

    // Axis labels
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("INPUT (dB)", (int)(cx + pad), (int)(cy + h - 14), (int)graphW, 12, juce::Justification::centred);

    // 1:1 reference line (diagonal)
    g.setColour(fx::col::gridMajor);
    g.drawLine(cx + pad, cy + h - pad, cx + w - pad, cy + pad, 1.0f);

    auto drawBadge = [&](juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(0.16f));
        g.fillRoundedRectangle(rect, 8.0f);
        g.setColour(colour.withAlpha(0.6f));
        g.drawRoundedRectangle(rect, 8.0f, 1.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
    };

    drawBadge({ cx + w - 286.0f, cy + 14.0f, 92.0f, 22.0f }, mono ? "INPUT MONO" : "INPUT STEREO", fx::col::textSecondary);
    drawBadge({ cx + w - 186.0f, cy + 14.0f, 82.0f, 22.0f }, "GR " + juce::String(grDb, 1) + " dB", fx::accent::compressor);
    drawBadge({ cx + w - 96.0f, cy + 14.0f, 74.0f, 22.0f }, rmsMode ? "RMS 6 dB" : "PEAK 6 dB", fx::col::textSecondary);

    // Transfer curve
    juce::Path transferPath;
    bool started = false;
    for (int i = 0; i <= 200; ++i)
    {
        float inputDb = -60.0f + (float)i * 60.0f / 200.0f;
        float outputDb;

        if (inputDb <= threshold)
            outputDb = inputDb; // below threshold: 1:1
        else
            outputDb = threshold + (inputDb - threshold) / ratio; // above: compressed

        outputDb += makeup; // makeup gain
        outputDb = juce::jlimit(-60.0f, 0.0f, outputDb);

        float xNorm = (inputDb + 60.0f) / 60.0f;
        float yNorm = (outputDb + 60.0f) / 60.0f;
        float xPos = cx + pad + xNorm * graphW;
        float yPos = cy + h - pad - yNorm * graphH;

        if (!started) { transferPath.startNewSubPath(xPos, yPos); started = true; }
        else transferPath.lineTo(xPos, yPos);
    }

    g.setColour(fx::accent::compressor.withAlpha(0.9f));
    g.strokePath(transferPath, juce::PathStrokeType(2.8f));

    // Threshold indicator (vertical line)
    float thrNorm = (threshold + 60.0f) / 60.0f;
    float thrX = cx + pad + thrNorm * graphW;
    g.setColour(fx::accent::compressor.withAlpha(0.4f));
    g.drawVerticalLine((int)thrX, cy + pad, cy + h - pad);

    // Threshold dot (knee point)
    float thrY = cy + h - pad - thrNorm * graphH;
    float pulse = 0.7f + 0.3f * std::sin(phase);
    g.setColour(fx::accent::compressor.withAlpha(0.25f * pulse));
    g.fillEllipse(thrX - 14.0f, thrY - 14.0f, 28.0f, 28.0f);
    g.setColour(fx::accent::compressor);
    g.fillEllipse(thrX - 5.0f, thrY - 5.0f, 10.0f, 10.0f);

    // Threshold label
    g.setColour(fx::col::textSecondary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(juce::String((int)threshold) + " dB", (int)(thrX - 24), (int)(thrY - 20), 48, 14, juce::Justification::centred);

    // Gain Reduction meter (vertical bar on right side)
    float grBarX = cx + w - 28.0f;
    float grBarH = graphH;
    float grBarY = cy + pad;

    g.setColour(fx::col::meterBg);
    g.fillRoundedRectangle(grBarX, grBarY, 14.0f, grBarH, 3.0f);

    float grDisplay = grSmooth * grBarH;
    if (grDisplay > 1.0f)
    {
        g.setColour(fx::accent::compressor.withAlpha(0.8f));
        g.fillRoundedRectangle(grBarX, grBarY, 14.0f, grDisplay, 3.0f);
    }

    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText("GR", (int)grBarX - 2, (int)(grBarY - 14), 18, 12, juce::Justification::centred);

    // Ratio label
    g.setColour(fx::col::textSecondary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    juce::String ratioText = juce::String(ratio, 1) + ":1";
    g.drawText(ratioText, (int)(cx + w - 80), (int)(cy + h - pad - 22), 44, 16, juce::Justification::centred);
}

void MusiqueCompressorEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::compressor);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12, 10, 40, 40), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int fy = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float)getWidth() - 52.0f, (float)fy + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "OUT", 80, 180);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueCompressorEditor::resized()
{
    // Header
    titleLabel.setBounds(56, 10, 160, 40);
    bypassBtn.setBounds(getWidth() - 356, 16, 64, fx::dim::btnH);
    monoBtn.setBounds(getWidth() - 286, 16, 96, fx::dim::btnH);
    modeBtn.setBounds(getWidth() - 184, 16, 58, fx::dim::btnH);
    autoBtn.setBounds(getWidth() - 120, 16, 82, fx::dim::btnH);

    // Preset bar
    const int py = fx::dim::headerH + 11;
    prevBtn.setBounds(260, py, 30, fx::dim::btnH);
    presetBox.setBounds(294, py, 250, fx::dim::btnH);
    nextBtn.setBounds(548, py, 30, fx::dim::btnH);
    saveBtn.setBounds(590, py, 56, fx::dim::btnH);
    abBtn.setBounds(652, py, 48, fx::dim::btnH);

    // Knobs
    const int ctrlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int numKnobs = 6;
    const int kW = getWidth() / numKnobs;
    const int kY = ctrlTop + 14;
    for (int i = 0; i < numKnobs; ++i)
    {
        int x = i * kW;
        knobs[i].setBounds(x + (kW - 92) / 2, kY, 92, 90);
        knobLabels[i].setBounds(x + (kW - 120) / 2, kY + 92, 120, 16);
    }

    // Footer
    const int fy = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, fy + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, fy + 6, 20, fx::dim::footerH - 12);
    outputSlider.setBounds(80, fy + 8, 180, 24);
    grLED.setBounds(280, fy + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 220, fy + 8, 160, 24);
}
