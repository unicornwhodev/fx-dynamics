#include "PluginProcessor.h"
#if ! MUSIQUE_DYNAMICS_DSP_TESTS
#include "PluginEditor.h"
#endif
#include "DynamicsEngines.h"
#include "FXComponents.h"
#include <cmath>

namespace
{
float getRaw(const juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback = 0.0f)
{
    if (auto* value = apvts.getRawParameterValue(id))
        return value->load();
    return fallback;
}

void setParam(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

bool stateHasParameter(const juce::ValueTree& state, const juce::String& id)
{
    for (int index = 0; index < state.getNumChildren(); ++index)
    {
        const auto child = state.getChild(index);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
            return true;
    }
    return false;
}

void ensureStateParamValue(juce::AudioProcessorValueTreeState& apvts,
                           const juce::ValueTree& state,
                           const juce::String& id,
                           float value)
{
    if (!stateHasParameter(state, id))
        setParam(apvts, id, value);
}

void setPresetDefault(juce::DynamicObject& object, const juce::Identifier& id, const juce::var& value)
{
    if (!object.hasProperty(id))
        object.setProperty(id, value);
}

float clampPresetFloat(juce::DynamicObject& object, const juce::Identifier& id, float minimum, float maximum)
{
    const auto clamped = juce::jlimit(minimum, maximum, (float) object.getProperty(id));
    object.setProperty(id, (double) clamped);
    return clamped;
}

int clampPresetInt(juce::DynamicObject& object, const juce::Identifier& id, int minimum, int maximum)
{
    const auto clamped = juce::jlimit(minimum, maximum, (int) std::round((float) object.getProperty(id)));
    object.setProperty(id, clamped);
    return clamped;
}
}

juce::StringArray MusiqueCompressorProcessor::getAllParameterIds()
{
    return {
        "engine","variant",
        "threshold","ratio","attack","release","makeup","rms_mode",
        "limit_drive","limit_ceiling","limit_release","limit_lookahead","limit_softness",
        "gate_threshold","gate_range","gate_attack","gate_release","gate_hold",
        "mb_low","mb_mid","mb_high","mb_glue","mb_recovery",
        "trans_attack","trans_sustain","trans_sensitivity","trans_speed","trans_clip",
        "mix","output","bypass","mono"
    };
}

MusiqueCompressorProcessor::MusiqueCompressorProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MusiqueCompressor", createParameterLayout()),
      compressorEngineState(std::make_unique<dynamics::CompressorEngine>()),
      limiterEngineState(std::make_unique<dynamics::LimiterEngine>()),
      gateEngineState(std::make_unique<dynamics::GateEngine>()),
      multibandEngineState(std::make_unique<dynamics::MultibandEngine>()),
      transientEngineState(std::make_unique<dynamics::TransientShaperEngine>())
{
}

MusiqueCompressorProcessor::~MusiqueCompressorProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueCompressorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine",
        juce::StringArray { "Compressor", "Limiter", "Gate/Expander", "Multiband", "Transient" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("variant", "Variant",
        juce::StringArray { "Variant A", "Variant B", "Variant C" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("threshold", "Threshold", -60.0f, 0.0f, -18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ratio", "Ratio", 1.0f, 20.0f, 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("attack", "Attack", 0.1f, 100.0f, 10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("release", "Release", 5.0f, 1000.0f, 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("makeup", "Makeup", 0.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("rms_mode", "RMS Mode", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("limit_drive", "Limit Drive", 0.0f, 24.0f, 4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("limit_ceiling", "Limit Ceiling", -12.0f, 0.0f, -0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("limit_release", "Limit Release", 5.0f, 400.0f, 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("limit_lookahead", "Limit Lookahead", 0.1f, 15.0f, 2.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("limit_softness", "Limit Softness", 0.0f, 100.0f, 35.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("gate_threshold", "Gate Threshold", -70.0f, 0.0f, -32.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gate_range", "Gate Range", 0.0f, 80.0f, 45.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gate_attack", "Gate Attack", 0.1f, 100.0f, 5.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gate_release", "Gate Release", 5.0f, 800.0f, 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gate_hold", "Gate Hold", 0.0f, 250.0f, 40.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("mb_low", "Multiband Low", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mb_mid", "Multiband Mid", 0.0f, 100.0f, 55.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mb_high", "Multiband High", 0.0f, 100.0f, 45.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mb_glue", "Multiband Glue", 0.0f, 100.0f, 40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mb_recovery", "Multiband Recovery", 0.0f, 100.0f, 45.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("trans_attack", "Transient Attack", -100.0f, 100.0f, 25.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trans_sustain", "Transient Sustain", -100.0f, 100.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trans_sensitivity", "Transient Sensitivity", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trans_speed", "Transient Speed", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("trans_clip", "Transient Clip", 0.0f, 100.0f, 35.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -24.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("mono", "Mono", false));

    return { params.begin(), params.end() };
}

void MusiqueCompressorProcessor::normalisePresetObject(juce::var& preset)
{
    auto* object = preset.getDynamicObject();
    if (object == nullptr)
        return;

    const bool legacyRms = object->hasProperty("rms_mode") && (bool) object->getProperty("rms_mode");

    setPresetDefault(*object, "engine", 0);
    setPresetDefault(*object, "variant", legacyRms ? 1 : 0);
    setPresetDefault(*object, "threshold", -18.0);
    setPresetDefault(*object, "ratio", 4.0);
    setPresetDefault(*object, "attack", 10.0);
    setPresetDefault(*object, "release", 120.0);
    setPresetDefault(*object, "makeup", 0.0);
    setPresetDefault(*object, "rms_mode", false);
    setPresetDefault(*object, "limit_drive", 4.0);
    setPresetDefault(*object, "limit_ceiling", -0.8);
    setPresetDefault(*object, "limit_release", 60.0);
    setPresetDefault(*object, "limit_lookahead", 2.5);
    setPresetDefault(*object, "limit_softness", 35.0);
    setPresetDefault(*object, "gate_threshold", -32.0);
    setPresetDefault(*object, "gate_range", 45.0);
    setPresetDefault(*object, "gate_attack", 5.0);
    setPresetDefault(*object, "gate_release", 120.0);
    setPresetDefault(*object, "gate_hold", 40.0);
    setPresetDefault(*object, "mb_low", 50.0);
    setPresetDefault(*object, "mb_mid", 55.0);
    setPresetDefault(*object, "mb_high", 45.0);
    setPresetDefault(*object, "mb_glue", 40.0);
    setPresetDefault(*object, "mb_recovery", 45.0);
    setPresetDefault(*object, "trans_attack", 25.0);
    setPresetDefault(*object, "trans_sustain", 0.0);
    setPresetDefault(*object, "trans_sensitivity", 50.0);
    setPresetDefault(*object, "trans_speed", 50.0);
    setPresetDefault(*object, "trans_clip", 35.0);
    setPresetDefault(*object, "mix", 100.0);
    setPresetDefault(*object, "output", 0.0);
    setPresetDefault(*object, "bypass", false);
    setPresetDefault(*object, "mono", false);

    const int engine = clampPresetInt(*object, "engine", 0, numEngines - 1);
    const bool rmsMode = object->hasProperty("rms_mode") && (bool) object->getProperty("rms_mode");
    const int fallbackVariant = engine == compressor && rmsMode ? 1 : 0;
    if (!object->hasProperty("variant"))
        object->setProperty("variant", fallbackVariant);
    clampPresetInt(*object, "variant", 0, 2);

    clampPresetFloat(*object, "threshold", -60.0f, 0.0f);
    clampPresetFloat(*object, "ratio", 1.0f, 20.0f);
    clampPresetFloat(*object, "attack", 0.1f, 100.0f);
    clampPresetFloat(*object, "release", 5.0f, 1000.0f);
    clampPresetFloat(*object, "makeup", 0.0f, 24.0f);
    clampPresetFloat(*object, "limit_drive", 0.0f, 24.0f);
    clampPresetFloat(*object, "limit_ceiling", -12.0f, 0.0f);
    clampPresetFloat(*object, "limit_release", 5.0f, 400.0f);
    clampPresetFloat(*object, "limit_lookahead", 0.1f, 15.0f);
    clampPresetFloat(*object, "limit_softness", 0.0f, 100.0f);
    clampPresetFloat(*object, "gate_threshold", -70.0f, 0.0f);
    clampPresetFloat(*object, "gate_range", 0.0f, 80.0f);
    clampPresetFloat(*object, "gate_attack", 0.1f, 100.0f);
    clampPresetFloat(*object, "gate_release", 5.0f, 800.0f);
    clampPresetFloat(*object, "gate_hold", 0.0f, 250.0f);
    clampPresetFloat(*object, "mb_low", 0.0f, 100.0f);
    clampPresetFloat(*object, "mb_mid", 0.0f, 100.0f);
    clampPresetFloat(*object, "mb_high", 0.0f, 100.0f);
    clampPresetFloat(*object, "mb_glue", 0.0f, 100.0f);
    clampPresetFloat(*object, "mb_recovery", 0.0f, 100.0f);
    clampPresetFloat(*object, "trans_attack", -100.0f, 100.0f);
    clampPresetFloat(*object, "trans_sustain", -100.0f, 100.0f);
    clampPresetFloat(*object, "trans_sensitivity", 0.0f, 100.0f);
    clampPresetFloat(*object, "trans_speed", 0.0f, 100.0f);
    clampPresetFloat(*object, "trans_clip", 0.0f, 100.0f);
    clampPresetFloat(*object, "mix", 0.0f, 100.0f);
    clampPresetFloat(*object, "output", -24.0f, 12.0f);
}

void MusiqueCompressorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockCapacity = juce::jmax(juce::jmax(1, samplesPerBlock), 8192);
    wetBuffer.setSize(2, preparedBlockCapacity, false, false, false);

    mixSmoothed.reset(sampleRate, 0.025);
    outputSmoothed.reset(sampleRate, 0.025);
    mixSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("mix")->load() / 100.0f);
    outputSmoothed.setCurrentAndTargetValue(dynamics::dbToGain(parameters.getRawParameterValue("output")->load()));

    compressorEngineState->prepare(sampleRate, preparedBlockCapacity);
    limiterEngineState->prepare(sampleRate, preparedBlockCapacity);
    gateEngineState->prepare(sampleRate, preparedBlockCapacity);
    multibandEngineState->prepare(sampleRate, preparedBlockCapacity);
    transientEngineState->prepare(sampleRate, preparedBlockCapacity);

    lastEngineIndex = -1;
    clearSnapshot();
}

void MusiqueCompressorProcessor::releaseResources()
{
    resetAllEngines();
    wetBuffer.setSize(0, 0);
    preparedBlockCapacity = 0;
}

bool MusiqueCompressorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

juce::AudioProcessorParameter* MusiqueCompressorProcessor::getBypassParameter() const
{
    return const_cast<juce::AudioProcessorValueTreeState&>(parameters).getParameter("bypass");
}

void MusiqueCompressorProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);
    clearSnapshot();
    visualState.captureOutput(buffer);
}

void MusiqueCompressorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (getRaw(parameters, "bypass") > 0.5f)
    {
        processBlockBypassed(buffer, midiMessages);
        return;
    }

    const float rawMix = juce::jlimit(0.0f, 100.0f, getRaw(parameters, "mix", 100.0f));
    if (rawMix <= 0.0001f)
    {
        clearSnapshot();
        visualState.captureOutput(buffer);
        return;
    }

    if (numSamples > wetBuffer.getNumSamples())
    {
        preparedBlockCapacity = numSamples;
        wetBuffer.setSize(2, preparedBlockCapacity, false, false, false);
    }

    wetBuffer.clear(0, 0, numSamples);
    wetBuffer.clear(1, 0, numSamples);

    const bool hasStereo = numChannels > 1;
    const bool mono = getRaw(parameters, "mono") > 0.5f;
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left = buffer.getSample(0, sample);
        float right = hasStereo ? buffer.getSample(1, sample) : left;
        if (mono)
            left = right = 0.5f * (left + right);

        wetBuffer.setSample(0, sample, left);
        wetBuffer.setSample(1, sample, right);
    }

    const int engine = juce::jlimit(0, numEngines - 1, (int) std::round(getRaw(parameters, "engine")));
    const int rawVariant = juce::jlimit(0, 2, (int) std::round(getRaw(parameters, "variant")));
    if (engine != lastEngineIndex)
    {
        resetAllEngines();
        lastEngineIndex = engine;
    }

    float primary = 0.0f;
    float secondary = 0.0f;
    std::array<float, 3> bandReduction {};
    float transientAttack = 0.0f;
    float transientSustain = 0.0f;

    switch (engine)
    {
        case compressor:
        {
            const bool rmsMode = getRaw(parameters, "rms_mode") > 0.5f;
            const int compressorVariant = rawVariant == 2 ? 2 : (rmsMode ? 1 : 0);
            compressorEngineState->process(
                wetBuffer,
                compressorVariant,
                getRaw(parameters, "threshold", -18.0f),
                getRaw(parameters, "ratio", 4.0f),
                getRaw(parameters, "attack", 10.0f),
                getRaw(parameters, "release", 120.0f),
                getRaw(parameters, "makeup"),
                rmsMode,
                primary,
                secondary);
            break;
        }

        case limiter:
            limiterEngineState->process(
                wetBuffer,
                rawVariant,
                getRaw(parameters, "limit_drive", 4.0f),
                getRaw(parameters, "limit_ceiling", -0.8f),
                getRaw(parameters, "limit_release", 60.0f),
                getRaw(parameters, "limit_lookahead", 2.5f),
                getRaw(parameters, "limit_softness", 35.0f),
                primary,
                secondary);
            break;

        case gateExpander:
            gateEngineState->process(
                wetBuffer,
                rawVariant,
                getRaw(parameters, "gate_threshold", -32.0f),
                getRaw(parameters, "gate_range", 45.0f),
                getRaw(parameters, "gate_attack", 5.0f),
                getRaw(parameters, "gate_release", 120.0f),
                getRaw(parameters, "gate_hold", 40.0f),
                primary,
                secondary);
            break;

        case multiband:
            multibandEngineState->process(
                wetBuffer,
                rawVariant,
                getRaw(parameters, "mb_low", 50.0f),
                getRaw(parameters, "mb_mid", 55.0f),
                getRaw(parameters, "mb_high", 45.0f),
                getRaw(parameters, "mb_glue", 40.0f),
                getRaw(parameters, "mb_recovery", 45.0f),
                bandReduction,
                primary,
                secondary);
            break;

        case transientShaper:
            transientEngineState->process(
                wetBuffer,
                rawVariant,
                getRaw(parameters, "trans_attack", 25.0f),
                getRaw(parameters, "trans_sustain"),
                getRaw(parameters, "trans_sensitivity", 50.0f),
                getRaw(parameters, "trans_speed", 50.0f),
                getRaw(parameters, "trans_clip", 35.0f),
                primary,
                secondary,
                transientAttack,
                transientSustain);
            break;

        default:
            break;
    }

    mixSmoothed.setTargetValue(rawMix / 100.0f);
    outputSmoothed.setTargetValue(dynamics::dbToGain(getRaw(parameters, "output")));

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float mix = juce::jlimit(0.0f, 1.0f, mixSmoothed.getNextValue());
        const float output = outputSmoothed.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float dry = buffer.getSample(channel, sample);
            const float wet = wetBuffer.getSample(channel == 0 ? 0 : 1, sample);
            buffer.setSample(channel, sample, (dry * (1.0f - mix) + wet * mix) * output);
        }
    }

    storeSnapshot(primary, secondary, bandReduction, transientAttack, transientSustain);
    visualState.captureOutput(buffer);
}

juce::AudioProcessorEditor* MusiqueCompressorProcessor::createEditor()
{
#if MUSIQUE_DYNAMICS_DSP_TESTS
    return nullptr;
#else
    return new MusiqueCompressorEditor(*this);
#endif
}

void MusiqueCompressorProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    normaliseStateTree(state);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destination);
}

void MusiqueCompressorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    normaliseStateTree(state);
    parameters.replaceState(state);
    ensureStateParamValue(parameters, state, "engine", 0.0f);
    ensureStateParamValue(parameters, state, "variant", 0.0f);
    ensureStateParamValue(parameters, state, "bypass", 0.0f);
    ensureStateParamValue(parameters, state, "mono", 0.0f);
    lastEngineIndex = -1;
}

DynamicsSnapshot MusiqueCompressorProcessor::getDynamicsSnapshot() const noexcept
{
    return {
        primaryReductionDb.load(std::memory_order_relaxed),
        secondaryReductionDb.load(std::memory_order_relaxed),
        {
            bandReductionLow.load(std::memory_order_relaxed),
            bandReductionMid.load(std::memory_order_relaxed),
            bandReductionHigh.load(std::memory_order_relaxed)
        },
        transientAttackDelta.load(std::memory_order_relaxed),
        transientSustainDelta.load(std::memory_order_relaxed)
    };
}

void MusiqueCompressorProcessor::resetAllEngines()
{
    if (compressorEngineState != nullptr)
        compressorEngineState->reset();
    if (limiterEngineState != nullptr)
        limiterEngineState->reset();
    if (gateEngineState != nullptr)
        gateEngineState->reset();
    if (multibandEngineState != nullptr)
        multibandEngineState->reset();
    if (transientEngineState != nullptr)
        transientEngineState->reset();
    clearSnapshot();
}

void MusiqueCompressorProcessor::clearSnapshot() noexcept
{
    std::array<float, 3> zeroBands {};
    storeSnapshot(0.0f, 0.0f, zeroBands, 0.0f, 0.0f);
}

void MusiqueCompressorProcessor::storeSnapshot(float primary,
                                               float secondary,
                                               const std::array<float, 3>& bandReduction,
                                               float transientAttack,
                                               float transientSustain) noexcept
{
    primaryReductionDb.store(primary, std::memory_order_relaxed);
    secondaryReductionDb.store(secondary, std::memory_order_relaxed);
    bandReductionLow.store(bandReduction[0], std::memory_order_relaxed);
    bandReductionMid.store(bandReduction[1], std::memory_order_relaxed);
    bandReductionHigh.store(bandReduction[2], std::memory_order_relaxed);
    transientAttackDelta.store(transientAttack, std::memory_order_relaxed);
    transientSustainDelta.store(transientSustain, std::memory_order_relaxed);
}

void MusiqueCompressorProcessor::normaliseStateTree(juce::ValueTree& state)
{
    auto findParamChild = [&state](const juce::String& id) -> juce::ValueTree
    {
        for (int index = 0; index < state.getNumChildren(); ++index)
        {
            auto child = state.getChild(index);
            if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
                return child;
        }
        return {};
    };

    auto readValue = [&state, &findParamChild](const juce::String& id, float fallback) -> float
    {
        if (auto child = findParamChild(id); child.isValid())
            return (float) child.getProperty("value", fallback);
        if (state.hasProperty(id))
            return (float) state.getProperty(id, fallback);
        return fallback;
    };

    auto writeValue = [&state, &findParamChild](const juce::String& id, float value)
    {
        auto child = findParamChild(id);
        if (!child.isValid())
        {
            child = juce::ValueTree("PARAM");
            child.setProperty("id", id, nullptr);
            state.addChild(child, -1, nullptr);
        }
        child.setProperty("value", value, nullptr);
        if (state.hasProperty(id))
            state.removeProperty(id, nullptr);
    };

    const bool rmsMode = readValue("rms_mode", 0.0f) > 0.5f;
    const int engineValue = juce::jlimit(0, numEngines - 1, (int) std::round(readValue("engine", 0.0f)));
    const int fallbackVariant = engineValue == compressor && rmsMode ? 1 : 0;
    const int variantValue = juce::jlimit(0, 2, (int) std::round(readValue("variant", (float) fallbackVariant)));

    writeValue("engine", (float) engineValue);
    writeValue("variant", (float) variantValue);
    writeValue("bypass", readValue("bypass", 0.0f) > 0.5f ? 1.0f : 0.0f);
    writeValue("mono", readValue("mono", 0.0f) > 0.5f ? 1.0f : 0.0f);
}

void MusiqueCompressorProcessor::applyPresetCompat(const juce::var& preset)
{
    auto normalised = preset;
    normalisePresetObject(normalised);
    fx::preset::applyToAPVTS(parameters, normalised);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueCompressorProcessor();
}
