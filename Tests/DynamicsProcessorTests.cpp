#include "PluginProcessor.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <string>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const std::string& name)
    {
        ++checks;
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
        if (!condition)
            ++failures;
    }
};

void setParameter(MusiqueCompressorProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    if (parameter == nullptr)
    {
        std::cerr << "Missing parameter: " << id << '\n';
        std::exit(2);
    }
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameter(MusiqueCompressorProcessor& processor, const juce::String& id)
{
    if (auto* value = processor.getAPVTS().getRawParameterValue(id))
        return value->load();
    std::cerr << "Missing parameter: " << id << '\n';
    std::exit(2);
}

std::unique_ptr<MusiqueCompressorProcessor> makeProcessor(int channels = 2, int blockSize = 512)
{
    auto processor = std::make_unique<MusiqueCompressorProcessor>();
    processor->setPlayConfigDetails(channels, channels, 48000.0, blockSize);
    processor->prepareToPlay(48000.0, blockSize);
    return processor;
}

juce::AudioBuffer<float> makeSignal(int channels, int samples, float frequency = 220.0f)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        const float t = (float) sample / 48000.0f;
        const float left = 0.22f * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * t)
            + 0.05f * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * 2.3f * t);
        buffer.setSample(0, sample, left);
        if (channels > 1)
            buffer.setSample(1, sample, 0.18f * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * 1.37f * t + 0.7f));
    }
    return buffer;
}

void process(MusiqueCompressorProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

bool isFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

float maxAbs(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    return peak;
}

float diffEnergy(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float sum = 0.0f;
    const int channels = juce::jmin(a.getNumChannels(), b.getNumChannels());
    const int samples = juce::jmin(a.getNumSamples(), b.getNumSamples());
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            sum += std::abs(a.getSample(channel, sample) - b.getSample(channel, sample));
    return sum / (float) juce::jmax(1, channels * samples);
}

juce::ValueTree copyState(MusiqueCompressorProcessor& processor)
{
    juce::MemoryBlock data;
    processor.getStateInformation(data);
    auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), (int) data.getSize());
    if (xml == nullptr)
        std::exit(2);
    return juce::ValueTree::fromXml(*xml);
}

void loadState(MusiqueCompressorProcessor& processor, const juce::ValueTree& state)
{
    auto xml = state.createXml();
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    processor.setStateInformation(data.getData(), (int) data.getSize());
}

void removeParam(juce::ValueTree& state, const juce::String& id)
{
    for (int index = state.getNumChildren() - 1; index >= 0; --index)
    {
        auto child = state.getChild(index);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
            state.removeChild(index, nullptr);
    }
}

juce::File findFactoryBank()
{
    auto dir = juce::File::getCurrentWorkingDirectory();
    for (int depth = 0; depth < 8; ++depth)
    {
        const std::array<juce::File, 3> candidates {
            dir.getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("fx-dynamics").getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("FX").getChildFile("fx-dynamics").getChildFile("Presets").getChildFile("factory_bank.json")
        };
        for (const auto& candidate : candidates)
            if (candidate.existsAsFile())
                return candidate;

        const auto parent = dir.getParentDirectory();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

juce::Array<juce::var> loadFactoryPresets(juce::var* rootOut = nullptr)
{
    const auto file = findFactoryBank();
    if (!file.existsAsFile())
    {
        std::cerr << "factory_bank.json not found\n";
        std::exit(2);
    }

    auto json = juce::JSON::parse(file.loadFileAsString());
    if (rootOut != nullptr)
        *rootOut = json;
    if (auto* object = json.getDynamicObject())
        if (auto* presets = object->getProperty("presets").getArray())
            return *presets;
    return {};
}

void testBypassDryStrict(Runner& runner)
{
    auto processor = makeProcessor();
    auto buffer = makeSignal(2, 512);
    const auto dry = buffer;
    setParameter(*processor, "bypass", 1.0f);
    setParameter(*processor, "mono", 1.0f);
    setParameter(*processor, "output", -12.0f);
    setParameter(*processor, "mix", 100.0f);
    process(*processor, buffer);
    runner.expect(diffEnergy(buffer, dry) < 1.0e-7f, "bypass is dry-identical without mono/output/mix");
}

void testLayouts(Runner& runner)
{
    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    auto processor = makeProcessor();
    runner.expect(processor->isBusesLayoutSupported(monoLayout), "mono->mono layout is supported");
    runner.expect(processor->isBusesLayoutSupported(stereoLayout), "stereo->stereo layout is supported");

    auto mono = makeProcessor(1);
    auto monoBuffer = makeSignal(1, 512);
    process(*mono, monoBuffer);
    runner.expect(isFinite(monoBuffer), "mono processing remains finite");

    auto stereo = makeProcessor(2);
    auto stereoBuffer = makeSignal(2, 512);
    process(*stereo, stereoBuffer);
    runner.expect(isFinite(stereoBuffer), "stereo processing remains finite");
}

void testStateRoundTrip(Runner& runner)
{
    auto processor = makeProcessor();
    setParameter(*processor, "engine", 4.0f);
    setParameter(*processor, "variant", 2.0f);
    setParameter(*processor, "threshold", -23.0f);
    setParameter(*processor, "ratio", 7.0f);
    setParameter(*processor, "attack", 3.5f);
    setParameter(*processor, "release", 220.0f);
    setParameter(*processor, "makeup", 4.0f);
    setParameter(*processor, "limit_drive", 9.0f);
    setParameter(*processor, "gate_range", 37.0f);
    setParameter(*processor, "mb_glue", 66.0f);
    setParameter(*processor, "trans_attack", 52.0f);
    setParameter(*processor, "trans_sustain", -18.0f);
    setParameter(*processor, "mix", 73.0f);
    setParameter(*processor, "output", -3.0f);
    setParameter(*processor, "mono", 1.0f);

    const auto state = copyState(*processor);
    auto restored = makeProcessor();
    loadState(*restored, state);

    runner.expect(std::abs(getParameter(*restored, "engine") - 4.0f) < 0.001f, "state restores engine");
    runner.expect(std::abs(getParameter(*restored, "variant") - 2.0f) < 0.001f, "state restores variant");
    runner.expect(std::abs(getParameter(*restored, "threshold") + 23.0f) < 0.001f, "state restores threshold");
    runner.expect(std::abs(getParameter(*restored, "ratio") - 7.0f) < 0.001f, "state restores ratio");
    runner.expect(std::abs(getParameter(*restored, "trans_attack") - 52.0f) < 0.001f, "state restores transient attack");
    runner.expect(std::abs(getParameter(*restored, "mix") - 73.0f) < 0.001f, "state restores mix");
    runner.expect(std::abs(getParameter(*restored, "output") + 3.0f) < 0.001f, "state restores output");
    runner.expect(getParameter(*restored, "mono") > 0.5f, "state restores mono");
}

void testLegacyStateMigration(Runner& runner)
{
    auto processor = makeProcessor();
    setParameter(*processor, "rms_mode", 1.0f);
    auto state = copyState(*processor);
    removeParam(state, "engine");
    removeParam(state, "variant");
    removeParam(state, "bypass");
    removeParam(state, "mono");

    auto restored = makeProcessor();
    loadState(*restored, state);
    runner.expect((int) std::round(getParameter(*restored, "engine")) == 0, "legacy state injects compressor engine");
    runner.expect((int) std::round(getParameter(*restored, "variant")) == 1, "legacy state maps rms_mode to RMS variant");
    runner.expect(getParameter(*restored, "bypass") < 0.5f, "legacy state injects bypass false");
    runner.expect(getParameter(*restored, "mono") < 0.5f, "legacy state injects mono false");
}

void testLegacyPresetCompat(Runner& runner)
{
    auto processor = makeProcessor();
    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty("name", "LegacyDynamics");
    object->setProperty("mix", 47.0);
    object->setProperty("threshold", -22.0);
    object->setProperty("ratio", 99.0);
    object->setProperty("rms_mode", true);
    juce::var preset(object.get());
    processor->applyPresetCompat(preset);

    runner.expect((int) std::round(getParameter(*processor, "engine")) == 0, "legacy preset injects compressor engine");
    runner.expect((int) std::round(getParameter(*processor, "variant")) == 1, "legacy preset maps RMS variant");
    runner.expect(std::abs(getParameter(*processor, "mix") - 47.0f) < 0.001f, "legacy preset preserves present value");
    runner.expect(std::abs(getParameter(*processor, "ratio") - 20.0f) < 0.001f, "legacy preset clamps ratio");
    runner.expect(getParameter(*processor, "bypass") < 0.5f, "legacy preset injects bypass false");
}

void testFactoryPresets(Runner& runner)
{
    juce::var root;
    auto presets = loadFactoryPresets(&root);
    runner.expect(presets.size() == 18, "factory bank contains 18 presets");

    bool bankVersionOk = false;
    if (auto* object = root.getDynamicObject())
        bankVersionOk = object->hasProperty("bank_version") && (int) object->getProperty("bank_version") >= 2;
    runner.expect(bankVersionOk, "factory bank declares bank_version");

    std::set<int> engines;
    std::set<std::string> categories;
    bool allFinite = true;
    auto processor = makeProcessor();
    for (auto& preset : presets)
    {
        MusiqueCompressorProcessor::normalisePresetObject(preset);
        if (auto* object = preset.getDynamicObject())
        {
            engines.insert((int) object->getProperty("engine"));
            categories.insert(object->getProperty("category").toString().toStdString());
        }

        processor->applyPresetCompat(preset);
        auto buffer = makeSignal(2, 512);
        process(*processor, buffer);
        allFinite = allFinite && isFinite(buffer);
    }

    runner.expect(engines.size() == (size_t) MusiqueCompressorProcessor::numEngines, "factory bank covers all engines");
    runner.expect(categories.count("Compressor") > 0 && categories.count("Limiter") > 0
        && categories.count("Gate") > 0 && categories.count("Multiband") > 0
        && categories.count("Transient") > 0, "factory bank covers all categories");
    runner.expect(allFinite, "all factory presets process finite audio");
}

void testAllEnginesFinite(Runner& runner)
{
    bool allFinite = true;
    float largestPeak = 0.0f;

    for (int engine = 0; engine < MusiqueCompressorProcessor::numEngines; ++engine)
    {
        for (int variant = 0; variant < 3; ++variant)
        {
            auto processor = makeProcessor();
            setParameter(*processor, "engine", (float) engine);
            setParameter(*processor, "variant", (float) variant);
            setParameter(*processor, "mix", 100.0f);
            auto buffer = makeSignal(2, 512, 160.0f + (float) engine * 40.0f + (float) variant * 7.0f);
            process(*processor, buffer);
            allFinite = allFinite && isFinite(buffer);
            largestPeak = juce::jmax(largestPeak, maxAbs(buffer));
        }
    }

    runner.expect(allFinite, "all engines and variants remain finite");
    runner.expect(largestPeak < 8.0f, "all engines and variants remain bounded");
}

void testMonoProcessing(Runner& runner)
{
    auto processor = makeProcessor();
    setParameter(*processor, "mono", 1.0f);
    setParameter(*processor, "engine", 0.0f);
    setParameter(*processor, "mix", 100.0f);
    auto buffer = makeSignal(2, 512);
    process(*processor, buffer);

    float maxDiff = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        maxDiff = juce::jmax(maxDiff, std::abs(buffer.getSample(0, sample) - buffer.getSample(1, sample)));

    runner.expect(isFinite(buffer), "stereo mono mode remains finite");
    runner.expect(maxDiff < 0.0001f, "mono mode feeds coherent stereo channels");

    auto mono = makeProcessor(1);
    setParameter(*mono, "mono", 1.0f);
    auto monoBuffer = makeSignal(1, 512);
    process(*mono, monoBuffer);
    runner.expect(isFinite(monoBuffer), "host mono buffer remains finite");
}

void testMixZeroDry(Runner& runner)
{
    auto processor = makeProcessor();
    setParameter(*processor, "mix", 0.0f);
    setParameter(*processor, "output", -12.0f);
    setParameter(*processor, "engine", 4.0f);
    auto buffer = makeSignal(2, 512);
    const auto dry = buffer;
    process(*processor, buffer);
    runner.expect(diffEnergy(buffer, dry) < 1.0e-7f, "mix=0 is dry-identical without output trim");
}

void testRapidAutomationFinite(Runner& runner)
{
    auto processor = makeProcessor();
    bool allFinite = true;
    float largestPeak = 0.0f;

    for (int block = 0; block < 100; ++block)
    {
        setParameter(*processor, "engine", (float) (block % MusiqueCompressorProcessor::numEngines));
        setParameter(*processor, "variant", (float) (block % 3));
        setParameter(*processor, "threshold", -55.0f + (float) (block % 50));
        setParameter(*processor, "ratio", 1.0f + (float) (block % 19));
        setParameter(*processor, "attack", 0.1f + (float) ((block * 3) % 99));
        setParameter(*processor, "release", 5.0f + (float) ((block * 17) % 900));
        setParameter(*processor, "limit_drive", (float) ((block * 5) % 24));
        setParameter(*processor, "gate_range", (float) ((block * 7) % 80));
        setParameter(*processor, "mb_glue", (float) ((block * 11) % 100));
        setParameter(*processor, "trans_attack", -100.0f + (float) ((block * 9) % 200));
        setParameter(*processor, "mix", 5.0f + (float) ((block * 13) % 95));
        setParameter(*processor, "output", -12.0f + (float) (block % 24));

        auto buffer = makeSignal(2, 512, 140.0f + (float) block);
        process(*processor, buffer);
        allFinite = allFinite && isFinite(buffer);
        largestPeak = juce::jmax(largestPeak, maxAbs(buffer));
    }

    runner.expect(allFinite, "rapid automation remains finite");
    runner.expect(largestPeak < 16.0f, "rapid automation remains bounded");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    Runner runner;
    testBypassDryStrict(runner);
    testLayouts(runner);
    testStateRoundTrip(runner);
    testLegacyStateMigration(runner);
    testLegacyPresetCompat(runner);
    testFactoryPresets(runner);
    testAllEnginesFinite(runner);
    testMonoProcessing(runner);
    testMixZeroDry(runner);
    testRapidAutomationFinite(runner);

    std::cout << "Checks: " << runner.checks << ", Failures: " << runner.failures << '\n';
    return runner.failures == 0 ? 0 : 1;
}
