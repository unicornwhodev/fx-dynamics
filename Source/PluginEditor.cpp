#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

namespace
{
const std::array<MusiqueCompressorEditor::EngineUiConfig, MusiqueCompressorProcessor::numEngines> kEngineConfigs { {
    {
        "COMPRESSOR",
        { "threshold", "ratio", "attack", "release", "makeup" },
        { "THRESHOLD", "RATIO", "ATTACK", "RELEASE", "MAKEUP", "MIX" },
        juce::StringArray { "Peak", "RMS", "Bus" }
    },
    {
        "LIMITER",
        { "limit_drive", "limit_ceiling", "limit_release", "limit_lookahead", "limit_softness" },
        { "DRIVE", "CEILING", "RELEASE", "LOOKAHEAD", "SOFT", "MIX" },
        juce::StringArray { "Clean", "Punch", "ClipSafe" }
    },
    {
        "GATE / EXPANDER",
        { "gate_threshold", "gate_range", "gate_attack", "gate_release", "gate_hold" },
        { "THRESHOLD", "RANGE", "ATTACK", "RELEASE", "HOLD", "MIX" },
        juce::StringArray { "Gate", "Expander", "Soft Gate" }
    },
    {
        "MULTIBAND",
        { "mb_low", "mb_mid", "mb_high", "mb_glue", "mb_recovery" },
        { "LOW", "MID", "HIGH", "GLUE", "RECOVERY", "MIX" },
        juce::StringArray { "Punch", "Glue", "Control" }
    },
    {
        "TRANSIENT",
        { "trans_attack", "trans_sustain", "trans_sensitivity", "trans_speed", "trans_clip" },
        { "ATTACK", "SUSTAIN", "SENSE", "SPEED", "CLIP", "MIX" },
        juce::StringArray { "Punch", "Snap", "Smooth" }
    }
} };

float getParamValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float fallback = 0.0f)
{
    if (auto* param = apvts.getRawParameterValue(id))
        return param->load();
    return fallback;
}

int getChoiceValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int fallback = 0)
{
    return (int) std::round(getParamValue(apvts, id, (float) fallback));
}

void setupKnob(juce::Slider& slider, juce::Label& label, const char* text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::label).withStyle("Bold")));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, fx::col::textMuted);
}

juce::String crossoverLabel(int variantIndex)
{
    switch (variantIndex)
    {
        case 1: return "180 Hz / 3.0 kHz";
        case 2: return "90 Hz / 1.6 kHz";
        default: return "120 Hz / 2.2 kHz";
    }
}
}

MusiqueCompressorEditor::MusiqueCompressorEditor(MusiqueCompressorProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    titleLabel.setText("DYNAMICS", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    auto setupButton = [&](juce::TextButton& button, bool toggle = false)
    {
        button.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
        button.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
        if (toggle)
            button.setClickingTogglesState(true);
        addAndMakeVisible(button);
    };

    setupButton(bypassBtn, true);
    setupButton(monoBtn, true);
    setupButton(statusBtn);
    setupButton(reductionBtn);
    setupButton(prevBtn);
    setupButton(nextBtn);
    setupButton(saveBtn);
    setupButton(abBtn);

    statusBtn.setInterceptsMouseClicks(false, false);
    reductionBtn.setInterceptsMouseClicks(false, false);
    monoBtn.setTooltip("Sum the input to mono before the active dynamics engine");
    statusBtn.setTooltip("Active engine mode and detector status");
    reductionBtn.setTooltip("Live gain reduction or transient delta readout");

    addAndMakeVisible(presetBox);
    addAndMakeVisible(engineBox);
    addAndMakeVisible(variantBox);
    engineBox.addItemList(juce::StringArray { "Compressor", "Limiter", "Gate/Expander", "Multiband", "Transient" }, 1);

    for (int index = 0; index < 6; ++index)
    {
        setupKnob(knobs[index], knobLabels[index], index == 5 ? "MIX" : "");
        knobs[index].setTooltip(index == 5 ? "Blend dry and processed signal" : "Active engine control");
        addAndMakeVisible(knobs[index]);
        addAndMakeVisible(knobLabels[index]);
    }

    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    outputSlider.setTooltip("Final output trim");
    addAndMakeVisible(outputSlider);

    gainLED.setAccent(fx::accent::compressor);
    addAndMakeVisible(gainLED);

    versionLabel.setText(juce::String("Musique Dynamics v") + JucePlugin_VersionString, juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    mixAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix", knobs[5]);
    outAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "output", outputSlider);
    engineAtt = std::make_unique<ComboAttach>(proc.getAPVTS(), "engine", engineBox);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    monoAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "mono", monoBtn);

    loadPresets();
    presetBox.setTextWhenNothingSelected("Manual State");
    presetBox.setSelectedItemIndex(-1, juce::dontSendNotification);

    abStateA = proc.getAPVTS().copyState();
    abStateB = abStateA.createCopy();
    showingA = true;

    presetBox.onChange = [this]
    {
        const int presetIndex = presetBox.getSelectedItemIndex();
        if (presets == nullptr || presetIndex < 0 || presetIndex >= presets->size())
            return;

        auto preset = presets->getReference(presetIndex);
        proc.applyPresetCompat(preset);
        abStateA = proc.getAPVTS().copyState();
        abStateB = abStateA.createCopy();
        showingA = true;
        abBtn.setButtonText("A/B");
        rebuildEngineUi(true);
    };

    prevBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index > 0)
            presetBox.setSelectedItemIndex(index - 1);
    };

    nextBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index < presetBox.getNumItems() - 1)
            presetBox.setSelectedItemIndex(index + 1);
    };

    saveBtn.onClick = [this]
    {
        const auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        if (fx::preset::saveUserPreset("fx-dynamics", name, getAllPresetParameterIds(), proc.getAPVTS()))
        {
            loadPresets();
            if (presetBox.getNumItems() > 0)
                presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    abBtn.onClick = [this]
    {
        storeCurrentABSlot();
        recallABSlot(!showingA);
    };

    engineBox.onChange = [this] { rebuildEngineUi(true); };
    variantBox.onChange = [this]
    {
        const int selection = variantBox.getSelectedItemIndex();
        if (selection >= 0)
            applyVariantSelection(getCurrentEngineIndex(), selection);
    };

    rebuildEngineUi(true);
    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueCompressorEditor::~MusiqueCompressorEditor()
{
    setLookAndFeel(nullptr);
}

void MusiqueCompressorEditor::loadPresets()
{
    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-dynamics"));
    for (auto& preset : *presets)
        MusiqueCompressorProcessor::normalisePresetObject(preset);
    refreshPresetBox();
}

void MusiqueCompressorEditor::refreshPresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    if (presets == nullptr || presets->isEmpty())
    {
        presetBox.addItem("Init", 1);
        presetBox.setSelectedId(1, juce::dontSendNotification);
        return;
    }

    int itemId = 1;
    for (auto& preset : *presets)
        if (auto* object = preset.getDynamicObject())
            presetBox.addItem(object->getProperty("name").toString(), itemId++);
}

int MusiqueCompressorEditor::getCurrentEngineIndex() const
{
    return juce::jlimit(0, MusiqueCompressorProcessor::numEngines - 1, getChoiceValue(proc.getAPVTS(), "engine"));
}

int MusiqueCompressorEditor::getCurrentVariantIndex() const
{
    const int variant = juce::jlimit(0, 2, getChoiceValue(proc.getAPVTS(), "variant"));
    if (getCurrentEngineIndex() != MusiqueCompressorProcessor::compressor)
        return variant;

    if (variant == 2)
        return 2;

    return getParamValue(proc.getAPVTS(), "rms_mode") > 0.5f ? 1 : 0;
}

juce::StringArray MusiqueCompressorEditor::getAllPresetParameterIds() const
{
    return MusiqueCompressorProcessor::getAllParameterIds();
}

void MusiqueCompressorEditor::rebuildEngineUi(bool force)
{
    const int engineIndex = getCurrentEngineIndex();
    if (!force && engineIndex == displayedEngine)
    {
        variantBox.setSelectedItemIndex(getCurrentVariantIndex(), juce::dontSendNotification);
        updateHeaderStatus();
        return;
    }

    displayedEngine = engineIndex;
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    for (int index = 0; index < 6; ++index)
        knobLabels[index].setText(config.labels[(size_t) index], juce::dontSendNotification);

    bindEngineKnobs(engineIndex);
    rebuildVariantItems(engineIndex);
    variantBox.setSelectedItemIndex(getCurrentVariantIndex(), juce::dontSendNotification);
    updateHeaderStatus();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueCompressorEditor::bindEngineKnobs(int engineIndex)
{
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    for (int index = 0; index < 5; ++index)
        engineKnobAtts[(size_t) index] = std::make_unique<SliderAttach>(proc.getAPVTS(), config.paramIds[(size_t) index], knobs[index]);
}

void MusiqueCompressorEditor::rebuildVariantItems(int engineIndex)
{
    variantBox.clear(juce::dontSendNotification);
    variantBox.addItemList(kEngineConfigs[(size_t) engineIndex].variants, 1);
}

void MusiqueCompressorEditor::applyVariantSelection(int engineIndex, int variantIndex)
{
    const int clampedVariant = juce::jlimit(0, 2, variantIndex);
    if (auto* variantParam = proc.getAPVTS().getParameter("variant"))
        variantParam->setValueNotifyingHost(variantParam->convertTo0to1((float) clampedVariant));

    if (engineIndex == MusiqueCompressorProcessor::compressor)
    {
        if (auto* rmsParam = proc.getAPVTS().getParameter("rms_mode"))
            rmsParam->setValueNotifyingHost(clampedVariant == 0 ? 0.0f : 1.0f);
    }

    rebuildEngineUi(true);
}

void MusiqueCompressorEditor::storeCurrentABSlot()
{
    const auto state = proc.getAPVTS().copyState();
    if (!abStateA.isValid())
    {
        abStateA = state;
        abStateB = state.createCopy();
        showingA = true;
        return;
    }

    if (showingA)
        abStateA = state;
    else
        abStateB = state;
}

void MusiqueCompressorEditor::recallABSlot(bool slotA)
{
    auto state = slotA ? abStateA : abStateB;
    if (!state.isValid())
        return;

    proc.getAPVTS().replaceState(state.createCopy());
    showingA = slotA;
    abBtn.setButtonText(showingA ? "A" : "B");
    rebuildEngineUi(true);
}

void MusiqueCompressorEditor::updateHeaderStatus()
{
    const auto snapshot = proc.getDynamicsSnapshot();
    const int engineIndex = getCurrentEngineIndex();
    const int variantIndex = getCurrentVariantIndex();
    const auto& config = kEngineConfigs[(size_t) engineIndex];
    const bool mono = getParamValue(proc.getAPVTS(), "mono") > 0.5f;

    monoBtn.setButtonText(mono ? "MONO IN" : "STEREO IN");

    juce::String statusText = config.variants[variantIndex].toUpperCase();
    juce::String reductionText = "GR " + juce::String(snapshot.primaryReductionDb, 1) + " dB";

    switch (engineIndex)
    {
        case MusiqueCompressorProcessor::compressor:
            statusText = variantIndex == 2 ? "BUS GLUE" : (variantIndex == 1 ? "RMS DET" : "PEAK DET");
            reductionText = "GR " + juce::String(snapshot.primaryReductionDb, 1) + " dB";
            break;

        case MusiqueCompressorProcessor::limiter:
            statusText = config.variants[variantIndex].toUpperCase() + " CEIL " + juce::String(getParamValue(proc.getAPVTS(), "limit_ceiling"), 1) + "dB";
            reductionText = snapshot.secondaryReductionDb > 0.05f
                ? "RED " + juce::String(snapshot.primaryReductionDb, 1) + "dB"
                : "SAFE " + juce::String(snapshot.primaryReductionDb, 1) + "dB";
            break;

        case MusiqueCompressorProcessor::gateExpander:
            statusText = config.variants[variantIndex].toUpperCase() + " HOLD " + juce::String(getParamValue(proc.getAPVTS(), "gate_hold"), 0) + "ms";
            reductionText = "ATTN " + juce::String(snapshot.primaryReductionDb, 1) + " dB";
            break;

        case MusiqueCompressorProcessor::multiband:
            statusText = config.variants[variantIndex].toUpperCase() + " " + crossoverLabel(variantIndex);
            reductionText = "L/M/H " + juce::String(snapshot.bandReductionDb[0], 1) + "/" + juce::String(snapshot.bandReductionDb[1], 1) + "/" + juce::String(snapshot.bandReductionDb[2], 1);
            break;

        case MusiqueCompressorProcessor::transientShaper:
        {
            statusText = config.variants[variantIndex].toUpperCase() + " SENSE " + juce::String(getParamValue(proc.getAPVTS(), "trans_sensitivity"), 0) + "%";
            const int attackPct = (int) std::round(snapshot.transientAttackDelta * 100.0f);
            const int sustainPct = (int) std::round(snapshot.transientSustainDelta * 100.0f);
            reductionText = "A " + juce::String(attackPct) + " S " + juce::String(sustainPct);
            break;
        }

        default:
            break;
    }

    statusBtn.setButtonText(statusText);
    reductionBtn.setButtonText(reductionText);
    statusBtn.setColour(juce::TextButton::buttonColourId, fx::accent::compressor.withAlpha(0.16f));
    statusBtn.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
    reductionBtn.setColour(juce::TextButton::buttonColourId,
        engineIndex == MusiqueCompressorProcessor::transientShaper
            ? fx::col::surfSecondary
            : fx::accent::compressor.withAlpha(0.20f));
    reductionBtn.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);

    gainLED.setOn(snapshot.primaryReductionDb > 0.45f
        || std::abs(snapshot.transientAttackDelta) > 0.08f
        || std::abs(snapshot.transientSustainDelta) > 0.08f);
}

void MusiqueCompressorEditor::timerCallback()
{
    rebuildEngineUi();

    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);

    const int engineIndex = getCurrentEngineIndex();
    float animationRate = 0.8f;
    switch (engineIndex)
    {
        case MusiqueCompressorProcessor::compressor:
            animationRate = 0.6f + getParamValue(proc.getAPVTS(), "ratio") * 0.07f;
            break;
        case MusiqueCompressorProcessor::limiter:
            animationRate = 1.0f + getParamValue(proc.getAPVTS(), "limit_drive") * 0.06f;
            break;
        case MusiqueCompressorProcessor::gateExpander:
            animationRate = 0.7f + getParamValue(proc.getAPVTS(), "gate_hold") * 0.01f;
            break;
        case MusiqueCompressorProcessor::multiband:
            animationRate = 0.7f + getParamValue(proc.getAPVTS(), "mb_recovery") * 0.015f;
            break;
        case MusiqueCompressorProcessor::transientShaper:
            animationRate = 0.9f + getParamValue(proc.getAPVTS(), "trans_speed") * 0.03f;
            break;
        default:
            break;
    }

    animPhase += animationRate * 0.015f;
    if (animPhase > juce::MathConstants<float>::twoPi * 8.0f)
        animPhase -= juce::MathConstants<float>::twoPi * 8.0f;

    updateHeaderStatus();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueCompressorEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& apvts = proc.getAPVTS();
    const auto snapshot = proc.getDynamicsSnapshot();
    const int engineIndex = getCurrentEngineIndex();
    const int variantIndex = getCurrentVariantIndex();
    const auto& config = kEngineConfigs[(size_t) engineIndex];

    const float left = (float) area.getX();
    const float top = (float) area.getY();
    const float width = (float) area.getWidth();
    const float height = (float) area.getHeight();

    auto drawBadge = [&](juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(0.16f));
        g.fillRoundedRectangle(rect, 8.0f);
        g.setColour(colour.withAlpha(0.62f));
        g.drawRoundedRectangle(rect, 8.0f, 1.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions {}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
    };

    drawBadge({ left + 22.0f, top + 16.0f, 120.0f, 22.0f }, config.variants[variantIndex].toUpperCase(), fx::accent::compressor);
    drawBadge({ left + 150.0f, top + 16.0f, 100.0f, 22.0f }, getParamValue(apvts, "mono") > 0.5f ? "INPUT MONO" : "INPUT STEREO", fx::col::textSecondary);

    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions {}.withHeight(11.0f).withStyle("Bold")));
    g.drawText(config.title, area.removeFromTop(28), juce::Justification::centredLeft);

    switch (engineIndex)
    {
        case MusiqueCompressorProcessor::compressor:
        {
            const float threshold = getParamValue(apvts, "threshold", -18.0f);
            const float ratio = getParamValue(apvts, "ratio", 4.0f);
            const float makeup = getParamValue(apvts, "makeup", 0.0f);
            const float grDb = snapshot.primaryReductionDb;
            const float pad = 40.0f;
            const float graphW = width - pad * 2.0f;
            const float graphH = height - pad * 2.0f;

            g.setColour(fx::col::textMuted);
            g.setFont(juce::Font(juce::FontOptions {}.withHeight(9.0f)));
            for (int db = -60; db <= 0; db += 12)
            {
                const float norm = (float) (db + 60) / 60.0f;
                const float x = left + pad + norm * graphW;
                const float y = top + height - pad - norm * graphH;
                g.setColour(fx::col::gridMinor);
                g.drawVerticalLine((int) x, top + pad, top + height - pad);
                g.drawHorizontalLine((int) y, left + pad, left + width - pad);
                g.setColour(fx::col::textMuted);
                g.drawText(juce::String(db), (int) (x - 14.0f), (int) (top + height - pad + 4.0f), 28, 12, juce::Justification::centred);
            }

            g.setColour(fx::col::gridMajor);
            g.drawLine(left + pad, top + height - pad, left + width - pad, top + pad, 1.0f);

            juce::Path transfer;
            for (int point = 0; point <= 200; ++point)
            {
                const float inputDb = -60.0f + (float) point * 60.0f / 200.0f;
                const float outputDb = juce::jlimit(-60.0f, 0.0f,
                    inputDb <= threshold ? inputDb + makeup : threshold + (inputDb - threshold) / ratio + makeup);
                const float x = left + pad + ((inputDb + 60.0f) / 60.0f) * graphW;
                const float y = top + height - pad - ((outputDb + 60.0f) / 60.0f) * graphH;
                if (point == 0)
                    transfer.startNewSubPath(x, y);
                else
                    transfer.lineTo(x, y);
            }

            g.setColour(fx::accent::compressor.withAlpha(0.9f));
            g.strokePath(transfer, juce::PathStrokeType(2.8f));

            const float thresholdX = left + pad + ((threshold + 60.0f) / 60.0f) * graphW;
            const float thresholdY = top + height - pad - ((threshold + 60.0f) / 60.0f) * graphH;
            g.setColour(fx::accent::compressor.withAlpha(0.35f));
            g.drawVerticalLine((int) thresholdX, top + pad, top + height - pad);
            g.setColour(fx::accent::compressor.withAlpha(0.25f * (0.7f + 0.3f * std::sin(animPhase))));
            g.fillEllipse(thresholdX - 14.0f, thresholdY - 14.0f, 28.0f, 28.0f);
            g.setColour(fx::accent::compressor);
            g.fillEllipse(thresholdX - 5.0f, thresholdY - 5.0f, 10.0f, 10.0f);

            const float grBarX = left + width - 28.0f;
            const float grBarY = top + pad;
            const float grBarH = graphH;
            g.setColour(fx::col::meterBg);
            g.fillRoundedRectangle(grBarX, grBarY, 14.0f, grBarH, 3.0f);
            const float grDisplay = juce::jlimit(0.0f, 1.0f, grDb / 18.0f) * grBarH;
            if (grDisplay > 1.0f)
            {
                g.setColour(fx::accent::compressor.withAlpha(0.8f));
                g.fillRoundedRectangle(grBarX, grBarY, 14.0f, grDisplay, 3.0f);
            }

            drawBadge({ left + width - 186.0f, top + 16.0f, 88.0f, 22.0f }, "GR " + juce::String(grDb, 1) + " dB", fx::accent::compressor);
            drawBadge({ left + width - 92.0f, top + 16.0f, 72.0f, 22.0f }, juce::String(ratio, 1) + ":1", fx::col::textSecondary);
            break;
        }

        case MusiqueCompressorProcessor::limiter:
        {
            const float ceiling = getParamValue(apvts, "limit_ceiling", -0.8f);
            const float lookahead = getParamValue(apvts, "limit_lookahead", 2.5f);
            const float softness = getParamValue(apvts, "limit_softness", 35.0f);
            const float grDb = snapshot.primaryReductionDb;
            const float pad = 40.0f;
            const float graphW = width - pad * 2.0f;
            const float graphH = height - pad * 2.0f;

            juce::Path curve;
            for (int point = 0; point <= 200; ++point)
            {
                const float inputDb = -36.0f + (float) point * 48.0f / 200.0f;
                const float knee = juce::jmap(softness / 100.0f, 0.2f, 3.5f);
                float outputDb = inputDb;
                if (inputDb > ceiling - knee)
                    outputDb = juce::jmap(juce::jlimit(0.0f, 1.0f, (inputDb - (ceiling - knee)) / juce::jmax(0.25f, knee)),
                                          inputDb,
                                          ceiling);
                outputDb = juce::jmin(outputDb, ceiling);
                const float x = left + pad + ((inputDb + 36.0f) / 48.0f) * graphW;
                const float y = top + height - pad - ((outputDb + 36.0f) / 48.0f) * graphH;
                if (point == 0)
                    curve.startNewSubPath(x, y);
                else
                    curve.lineTo(x, y);
            }

            g.setColour(fx::col::gridMajor);
            g.drawLine(left + pad, top + height - pad, left + width - pad, top + pad, 1.0f);
            g.setColour(fx::accent::compressor.withAlpha(0.82f));
            g.strokePath(curve, juce::PathStrokeType(2.6f));

            const float ceilingNorm = (ceiling + 36.0f) / 48.0f;
            const float ceilingY = top + height - pad - ceilingNorm * graphH;
            g.setColour(fx::accent::compressor.withAlpha(0.35f));
            g.drawHorizontalLine((int) ceilingY, left + pad, left + width - pad - 36.0f);
            drawBadge({ left + width - 228.0f, top + 16.0f, 116.0f, 22.0f }, "CEIL " + juce::String(ceiling, 1) + " dB", fx::accent::compressor);
            drawBadge({ left + width - 104.0f, top + 16.0f, 84.0f, 22.0f }, "LA " + juce::String(lookahead, 1) + " ms", fx::col::textSecondary);

            const float grBarX = left + width - 28.0f;
            const float grBarY = top + pad;
            g.setColour(fx::col::meterBg);
            g.fillRoundedRectangle(grBarX, grBarY, 14.0f, graphH, 3.0f);
            const float grDisplay = juce::jlimit(0.0f, 1.0f, grDb / 18.0f) * graphH;
            g.setColour(fx::accent::compressor.withAlpha(0.84f));
            g.fillRoundedRectangle(grBarX, grBarY, 14.0f, grDisplay, 3.0f);
            break;
        }

        case MusiqueCompressorProcessor::gateExpander:
        {
            const float threshold = getParamValue(apvts, "gate_threshold", -32.0f);
            const float range = getParamValue(apvts, "gate_range", 45.0f);
            const float attenuation = snapshot.primaryReductionDb;
            const float pad = 44.0f;
            const float graphW = width - pad * 2.0f;
            const float graphH = height - pad * 2.0f;

            juce::Path curve;
            for (int point = 0; point <= 200; ++point)
            {
                const float inputDb = -60.0f + (float) point * 60.0f / 200.0f;
                float outputDb = inputDb;
                if (variantIndex == 1)
                {
                    const float below = juce::jmax(0.0f, threshold - inputDb);
                    outputDb = inputDb - juce::jmin(range, below * 0.55f);
                }
                else if (variantIndex == 2)
                {
                    const float edge = threshold - 7.0f;
                    const float norm = juce::jlimit(0.0f, 1.0f, (inputDb - edge) / juce::jmax(0.25f, threshold - edge));
                    const float shaped = norm * norm * (3.0f - 2.0f * norm);
                    outputDb = inputDb - (1.0f - shaped) * range;
                }
                else if (inputDb < threshold)
                {
                    outputDb = inputDb - range;
                }

                const float x = left + pad + ((inputDb + 60.0f) / 60.0f) * graphW;
                const float y = top + height - pad - ((juce::jlimit(-60.0f, 0.0f, outputDb) + 60.0f) / 60.0f) * graphH;
                if (point == 0)
                    curve.startNewSubPath(x, y);
                else
                    curve.lineTo(x, y);
            }

            g.setColour(fx::col::gridMajor);
            g.drawLine(left + pad, top + height - pad, left + width - pad, top + pad, 1.0f);
            g.setColour(fx::accent::compressor.withAlpha(0.82f));
            g.strokePath(curve, juce::PathStrokeType(2.4f));
            const float thresholdX = left + pad + ((threshold + 60.0f) / 60.0f) * graphW;
            g.setColour(fx::accent::compressor.withAlpha(0.32f));
            g.drawVerticalLine((int) thresholdX, top + pad, top + height - pad);
            drawBadge({ left + width - 216.0f, top + 16.0f, 110.0f, 22.0f }, "RANGE " + juce::String(range, 0) + " dB", fx::accent::compressor);
            drawBadge({ left + width - 98.0f, top + 16.0f, 78.0f, 22.0f }, "ATTN " + juce::String(attenuation, 1), fx::col::textSecondary);
            break;
        }

        case MusiqueCompressorProcessor::multiband:
        {
            const float barBottom = top + height - 60.0f;
            const float barTop = top + 74.0f;
            const float barHeight = barBottom - barTop;
            const float barWidth = 92.0f;
            const float startX = left + 170.0f;
            const auto bands = snapshot.bandReductionDb;

            drawBadge({ left + width - 250.0f, top + 16.0f, 140.0f, 22.0f }, crossoverLabel(variantIndex), fx::accent::compressor);
            drawBadge({ left + width - 102.0f, top + 16.0f, 80.0f, 22.0f }, "GLUE " + juce::String(getParamValue(apvts, "mb_glue"), 0), fx::col::textSecondary);

            for (int band = 0; band < 3; ++band)
            {
                const float x = startX + band * 190.0f;
                const float value = juce::jlimit(0.0f, 1.0f, bands[(size_t) band] / 12.0f);
                const float filled = value * barHeight;
                g.setColour(fx::col::meterBg);
                g.fillRoundedRectangle(x, barTop, barWidth, barHeight, 6.0f);
                g.setColour(fx::accent::compressor.withAlpha(0.82f));
                g.fillRoundedRectangle(x, barBottom - filled, barWidth, filled, 6.0f);
                g.setColour(fx::col::textPrimary);
                g.setFont(juce::Font(juce::FontOptions {}.withHeight(12.0f).withStyle("Bold")));
                g.drawText(band == 0 ? "LOW" : (band == 1 ? "MID" : "HIGH"), (int) x, (int) (barBottom + 10.0f), (int) barWidth, 16, juce::Justification::centred);
                g.drawText(juce::String(bands[(size_t) band], 1) + " dB", (int) x, (int) (barTop - 20.0f), (int) barWidth, 16, juce::Justification::centred);
            }

            g.setColour(fx::col::textMuted);
            g.drawText("Macro three-band compression with fixed internal crossovers and linked glue",
                (int) (left + 44.0f), (int) (top + height - 28.0f), (int) (width - 88.0f), 16, juce::Justification::centred);
            break;
        }

        case MusiqueCompressorProcessor::transientShaper:
        {
            const float graphLeft = left + 34.0f;
            const float graphTop = top + 72.0f;
            const float graphWidth = width - 68.0f;
            const float graphHeight = height - 132.0f;
            const float midY = graphTop + graphHeight * 0.55f;
            const float attackDelta = snapshot.transientAttackDelta;
            const float sustainDelta = snapshot.transientSustainDelta;

            juce::Path inputEnv;
            juce::Path outputEnv;
            for (int point = 0; point <= (int) graphWidth; ++point)
            {
                const float t = (float) point / graphWidth;
                const float inputShape = std::exp(-t * 5.5f) * (0.35f + 0.65f * std::sin(t * juce::MathConstants<float>::pi));
                const float transientPulse = std::exp(-std::pow((t - 0.10f) * 14.0f, 2.0f)) * attackDelta;
                const float sustainLift = std::exp(-t * 2.2f) * sustainDelta * 0.8f;
                const float outputShape = juce::jlimit(-0.15f, 1.6f, inputShape + transientPulse + sustainLift);
                const float x = graphLeft + (float) point;
                const float inY = midY - inputShape * graphHeight * 0.48f;
                const float outY = midY - outputShape * graphHeight * 0.48f;
                if (point == 0)
                {
                    inputEnv.startNewSubPath(x, inY);
                    outputEnv.startNewSubPath(x, outY);
                }
                else
                {
                    inputEnv.lineTo(x, inY);
                    outputEnv.lineTo(x, outY);
                }
            }

            g.setColour(fx::col::gridMinor);
            g.drawHorizontalLine((int) midY, graphLeft, graphLeft + graphWidth);
            g.setColour(fx::col::textSecondary.withAlpha(0.7f));
            g.strokePath(inputEnv, juce::PathStrokeType(1.6f));
            g.setColour(fx::accent::compressor.withAlpha(0.88f));
            g.strokePath(outputEnv, juce::PathStrokeType(2.4f));

            drawBadge({ left + width - 236.0f, top + 16.0f, 110.0f, 22.0f }, "ATK " + juce::String((int) std::round(attackDelta * 100.0f)), fx::accent::compressor);
            drawBadge({ left + width - 118.0f, top + 16.0f, 96.0f, 22.0f }, "SUS " + juce::String((int) std::round(sustainDelta * 100.0f)), fx::col::textSecondary);
            g.setColour(fx::col::textMuted);
            g.drawText("Input envelope versus shaped output attack and sustain response",
                (int) graphLeft, (int) (graphTop + graphHeight + 12.0f), (int) graphWidth, 16, juce::Justification::centred);
            break;
        }

        default:
            break;
    }
}

void MusiqueCompressorEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::compressor);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12.0f, 10.0f, 40.0f, 40.0f), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int footerY = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float) getWidth() - 52.0f, (float) footerY + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "OUT", 80, 180);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueCompressorEditor::resized()
{
    titleLabel.setBounds(56, 10, 160, 40);
    bypassBtn.setBounds(getWidth() - 390, 16, 72, fx::dim::btnH);
    monoBtn.setBounds(getWidth() - 312, 16, 96, fx::dim::btnH);
    statusBtn.setBounds(getWidth() - 210, 16, 102, fx::dim::btnH);
    reductionBtn.setBounds(getWidth() - 100, 16, 82, fx::dim::btnH);

    const int presetY = fx::dim::headerH + 11;
    prevBtn.setBounds(164, presetY, 30, fx::dim::btnH);
    presetBox.setBounds(198, presetY, 220, fx::dim::btnH);
    nextBtn.setBounds(422, presetY, 30, fx::dim::btnH);
    engineBox.setBounds(462, presetY, 146, fx::dim::btnH);
    variantBox.setBounds(616, presetY, 136, fx::dim::btnH);
    saveBtn.setBounds(760, presetY, 56, fx::dim::btnH);
    abBtn.setBounds(822, presetY, 48, fx::dim::btnH);

    const int controlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int knobWidth = getWidth() / 6;
    const int knobY = controlTop + 14;
    for (int index = 0; index < 6; ++index)
    {
        const int x = index * knobWidth;
        knobs[index].setBounds(x + (knobWidth - 92) / 2, knobY, 92, 90);
        knobLabels[index].setBounds(x + (knobWidth - 120) / 2, knobY + 92, 120, 16);
    }

    const int footerY = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, footerY + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, footerY + 6, 20, fx::dim::footerH - 12);
    outputSlider.setBounds(80, footerY + 8, 180, 24);
    gainLED.setBounds(280, footerY + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 240, footerY + 8, 180, 24);
}
