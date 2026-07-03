#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueCompressorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    struct EngineUiConfig
    {
        const char* title;
        std::array<const char*, 5> paramIds;
        std::array<const char*, 6> labels;
        juce::StringArray variants;
    };

    explicit MusiqueCompressorEditor(MusiqueCompressorProcessor&);
    ~MusiqueCompressorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;
    using ComboAttach = APVTS::ComboBoxAttachment;

    void timerCallback() override;
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);
    void loadPresets();
    void refreshPresetBox();
    void rebuildEngineUi(bool force = false);
    void bindEngineKnobs(int engineIndex);
    void rebuildVariantItems(int engineIndex);
    void applyVariantSelection(int engineIndex, int variantIndex);
    void updateHeaderStatus();
    int getCurrentEngineIndex() const;
    int getCurrentVariantIndex() const;
    juce::StringArray getAllPresetParameterIds() const;
    void storeCurrentABSlot();
    void recallABSlot(bool slotA);

    MusiqueCompressorProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::compressor };

    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn { "Bypass" };
    juce::TextButton monoBtn { "STEREO IN" };
    juce::TextButton statusBtn { "MODE" };
    juce::TextButton reductionBtn { "GR 0.0dB" };

    juce::TextButton prevBtn { "<" };
    juce::TextButton nextBtn { ">" };
    juce::TextButton saveBtn { "Save" };
    juce::TextButton abBtn { "A/B" };
    juce::ComboBox presetBox;
    juce::ComboBox engineBox;
    juce::ComboBox variantBox;

    juce::Slider knobs[6];
    juce::Label knobLabels[6];

    fx::MeterComponent inMeter, outMeter;
    juce::Slider outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent gainLED;

    std::array<std::unique_ptr<SliderAttach>, 5> engineKnobAtts;
    std::unique_ptr<SliderAttach> mixAtt, outAtt;
    std::unique_ptr<ComboAttach> engineAtt;
    std::unique_ptr<ButtonAttach> bypassAtt, monoAtt;
    std::shared_ptr<juce::Array<juce::var>> presets;

    int displayedEngine = -1;
    float animPhase = 0.0f;
    bool showingA = true;
    juce::ValueTree abStateA, abStateB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueCompressorEditor)
};
