#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include "FXAudioVisualState.h"

namespace dynamics
{
class CompressorEngine;
class LimiterEngine;
class GateEngine;
class MultibandEngine;
class TransientShaperEngine;
}

struct DynamicsSnapshot
{
    float primaryReductionDb = 0.0f;
    float secondaryReductionDb = 0.0f;
    std::array<float, 3> bandReductionDb { 0.0f, 0.0f, 0.0f };
    float transientAttackDelta = 0.0f;
    float transientSustainDelta = 0.0f;
};

class MusiqueCompressorProcessor : public juce::AudioProcessor
{
public:
    enum EngineIndex
    {
        compressor = 0,
        limiter,
        gateExpander,
        multiband,
        transientShaper,
        numEngines
    };

    MusiqueCompressorProcessor();
    ~MusiqueCompressorProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override
    {
#ifdef JucePlugin_Name
        return JucePlugin_Name;
#else
        return "Musique Dynamics";
#endif
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.12; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    DynamicsSnapshot getDynamicsSnapshot() const noexcept;
    float getCurrentGainReductionDb() const noexcept { return primaryReductionDb.load(std::memory_order_relaxed); }
    static juce::StringArray getAllParameterIds();
    static void normalisePresetObject(juce::var& preset);
    void applyPresetCompat(const juce::var& preset);

private:
    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> mixSmoothed;
    juce::SmoothedValue<float> outputSmoothed;
    double preparedSampleRate = 44100.0;
    int preparedBlockCapacity = 0;
    int lastEngineIndex = -1;

    std::unique_ptr<dynamics::CompressorEngine> compressorEngineState;
    std::unique_ptr<dynamics::LimiterEngine> limiterEngineState;
    std::unique_ptr<dynamics::GateEngine> gateEngineState;
    std::unique_ptr<dynamics::MultibandEngine> multibandEngineState;
    std::unique_ptr<dynamics::TransientShaperEngine> transientEngineState;

    std::atomic<float> primaryReductionDb { 0.0f };
    std::atomic<float> secondaryReductionDb { 0.0f };
    std::atomic<float> bandReductionLow { 0.0f };
    std::atomic<float> bandReductionMid { 0.0f };
    std::atomic<float> bandReductionHigh { 0.0f };
    std::atomic<float> transientAttackDelta { 0.0f };
    std::atomic<float> transientSustainDelta { 0.0f };

    void resetAllEngines();
    void clearSnapshot() noexcept;
    void storeSnapshot(float, float, const std::array<float, 3>&, float, float) noexcept;
    static void normaliseStateTree(juce::ValueTree& state);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueCompressorProcessor)
};
