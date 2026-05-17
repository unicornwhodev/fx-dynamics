#pragma once
#include <JuceHeader.h>
#include "FXAudioVisualState.h"

class MusiqueCompressorProcessor : public juce::AudioProcessor
{
public:
    MusiqueCompressorProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    float getCurrentGainReductionDb() const noexcept { return currentGainReductionDb.load(std::memory_order_relaxed); }
private:
    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> thresholdSmoothed, ratioSmoothed, attackSmoothed, releaseSmoothed, makeupSmoothed, mixSmoothed;
    std::atomic<float> currentGainReductionDb { 0.0f };
    float gainReductionEnvelopeDb = 0.0f;
    float detectorEnvelope = 0.0f;
    float detectorRmsPower = 0.0f;
    double preparedSampleRate = 44100.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueCompressorProcessor)
};
