#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueCompressorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MusiqueCompressorEditor(MusiqueCompressorProcessor&);
    ~MusiqueCompressorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;

    void timerCallback() override;
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);

    MusiqueCompressorProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::compressor };

    // Header
    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn{"Bypass"}, monoBtn{"STEREO IN"}, modeBtn{"PEAK"}, autoBtn{"GR LIVE"};

    // Preset bar
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, saveBtn{"Save"}, abBtn{"A/B"};
    juce::ComboBox presetBox;

    // 6 knobs
    juce::Slider knobs[6];
    juce::Label knobLabels[6];

    // Footer
    fx::MeterComponent inMeter, outMeter;
    juce::Slider outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent grLED;

    // Visualization
    float grSmooth = 0.0f;
    float phase = 0.0f;

    // Attachments
    std::unique_ptr<SliderAttach> thrAtt, ratAtt, atkAtt, relAtt, mkAtt, mixAtt, outAtt;
    std::unique_ptr<ButtonAttach> bypassAtt, monoAtt, modeAtt;

    std::shared_ptr<juce::Array<juce::var>> presets;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueCompressorEditor)
};
