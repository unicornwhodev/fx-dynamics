#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace { static float dbToGain(float dB){ return std::pow(10.0f,dB/20.0f);} }

MusiqueCompressorProcessor::MusiqueCompressorProcessor()
: AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
  parameters(*this, nullptr, "MusiqueCompressor", createParameterLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueCompressorProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("threshold","Threshold",-60.0f,0.0f,-18.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("ratio","Ratio",1.0f,20.0f,4.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("attack","Attack",0.1f,100.0f,10.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("release","Release",5.0f,1000.0f,120.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("makeup","Makeup",0.0f,24.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix","Mix",0.0f,100.0f,100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output","Output",-24.0f,12.0f,0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("rms_mode","RMS Mode",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("bypass","Bypass",false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("mono","Mono",false));
    return {p.begin(), p.end()};
}

void MusiqueCompressorProcessor::prepareToPlay(double sr, int bs)
{
    preparedSampleRate = sr;
    wetBuffer.setSize(2, bs, false, false, true);
    thresholdSmoothed.reset(sr, 0.02);
    ratioSmoothed.reset(sr, 0.02);
    attackSmoothed.reset(sr, 0.03);
    releaseSmoothed.reset(sr, 0.04);
    makeupSmoothed.reset(sr, 0.02);
    mixSmoothed.reset(sr, 0.02);

    thresholdSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("threshold")->load());
    ratioSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("ratio")->load());
    attackSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("attack")->load());
    releaseSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("release")->load());
    makeupSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("makeup")->load());
    mixSmoothed.setCurrentAndTargetValue(parameters.getRawParameterValue("mix")->load() / 100.0f);
    currentGainReductionDb.store(0.0f, std::memory_order_relaxed);
    gainReductionEnvelopeDb = 0.0f;
    detectorEnvelope = 0.0f;
    detectorRmsPower = 0.0f;
}

bool MusiqueCompressorProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{ return l.getMainInputChannelSet()==juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo(); }

void MusiqueCompressorProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(b);
    const int numSamples = b.getNumSamples();
    if (numSamples <= 0)
        return;

    if (*parameters.getRawParameterValue("mono") > 0.5f)
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (b.getSample(0, i) + b.getSample(1, i));
            b.setSample(0, i, m);
            b.setSample(1, i, m);
        }

    if (*parameters.getRawParameterValue("bypass") > 0.5f)
    {
        currentGainReductionDb.store(0.0f, std::memory_order_relaxed);
        gainReductionEnvelopeDb = 0.0f;
        detectorEnvelope = 0.0f;
        detectorRmsPower = 0.0f;
        b.applyGain(dbToGain(*parameters.getRawParameterValue("output")));
        visualState.captureOutput(b);
        return;
    }

    thresholdSmoothed.setTargetValue(parameters.getRawParameterValue("threshold")->load());
    ratioSmoothed.setTargetValue(parameters.getRawParameterValue("ratio")->load());
    attackSmoothed.setTargetValue(parameters.getRawParameterValue("attack")->load());
    releaseSmoothed.setTargetValue(parameters.getRawParameterValue("release")->load());
    makeupSmoothed.setTargetValue(parameters.getRawParameterValue("makeup")->load());
    mixSmoothed.setTargetValue(parameters.getRawParameterValue("mix")->load() / 100.0f);

    const float out = dbToGain(*parameters.getRawParameterValue("output"));

    const bool mixAtZero = mixSmoothed.getCurrentValue() <= 0.0001f && !mixSmoothed.isSmoothing();
    if (mixAtZero)
    {
        currentGainReductionDb.store(0.0f, std::memory_order_relaxed);
        gainReductionEnvelopeDb = 0.0f;
        detectorEnvelope = 0.0f;
        detectorRmsPower = 0.0f;
        b.applyGain(out);
        visualState.captureOutput(b);
        return;
    }

    const bool rmsMode = *parameters.getRawParameterValue("rms_mode") > 0.5f;

    const bool fullyWet = mixSmoothed.getCurrentValue() >= 0.999f
        && mixSmoothed.getTargetValue() >= 0.999f
        && !mixSmoothed.isSmoothing();

    if (! fullyWet)
    {
        wetBuffer.setSize(2, numSamples, false, false, true);
        wetBuffer.makeCopyOf(b, true);
    }

    constexpr int chunkSize = 32;
    for (int offset = 0; offset < numSamples; offset += chunkSize)
    {
        const int samplesThisChunk = juce::jmin(chunkSize, numSamples - offset);
        const float threshold = thresholdSmoothed.skip(samplesThisChunk);
        const float ratio = juce::jmax(1.0f, ratioSmoothed.skip(samplesThisChunk));
        const float attack = juce::jmax(0.1f, attackSmoothed.skip(samplesThisChunk));
        const float release = juce::jmax(5.0f, releaseSmoothed.skip(samplesThisChunk));
        const float makeup = makeupSmoothed.skip(samplesThisChunk);
        const float mix = mixSmoothed.skip(samplesThisChunk);
        const float makeupGain = dbToGain(makeup);
        constexpr float softKneeWidthDb = 6.0f;
        const float halfKneeDb = softKneeWidthDb * 0.5f;
        const float attackCoeff = std::exp(-1.0f / (0.001f * attack * (float) preparedSampleRate));
        const float releaseCoeff = std::exp(-1.0f / (0.001f * release * (float) preparedSampleRate));
        float chunkMaxReductionDb = 0.0f;

        double inputEnergy = 0.0;
        double wetEnergy = 0.0;
        if (fullyWet)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < samplesThisChunk; ++i)
                {
                    const float sample = b.getSample(ch, offset + i);
                    inputEnergy += (double) sample * (double) sample;
                }

            for (int i = 0; i < samplesThisChunk; ++i)
            {
                const int sampleIndex = offset + i;
                const float inL = b.getSample(0, sampleIndex);
                const float inR = b.getSample(1, sampleIndex);
                const float linkedPeak = juce::jmax(std::abs(inL), std::abs(inR));
                float envDb = -120.0f;
                if (rmsMode)
                {
                    const float linkedPower = 0.5f * (inL * inL + inR * inR);
                    const float detectorValue = std::sqrt(juce::jmax(linkedPower, 0.0f));
                    const float coeff = detectorValue > std::sqrt(juce::jmax(detectorRmsPower, 0.0f)) ? attackCoeff : releaseCoeff;
                    detectorRmsPower = linkedPower + coeff * (detectorRmsPower - linkedPower);
                    envDb = juce::Decibels::gainToDecibels(std::sqrt(juce::jmax(detectorRmsPower, 0.0f)), -120.0f);
                    detectorEnvelope = 0.0f;
                }
                else
                {
                    const float coeff = linkedPeak > detectorEnvelope ? attackCoeff : releaseCoeff;
                    detectorEnvelope = linkedPeak + coeff * (detectorEnvelope - linkedPeak);
                    envDb = juce::Decibels::gainToDecibels(detectorEnvelope, -120.0f);
                    detectorRmsPower = 0.0f;
                }

                float reductionDb = 0.0f;
                if (envDb > threshold - halfKneeDb)
                {
                    if (envDb >= threshold + halfKneeDb)
                    {
                        reductionDb = (envDb - threshold) * (1.0f - 1.0f / ratio);
                    }
                    else
                    {
                        const float overDb = envDb - (threshold - halfKneeDb);
                        reductionDb = (1.0f - 1.0f / ratio) * (overDb * overDb) / (2.0f * softKneeWidthDb);
                    }
                }

                chunkMaxReductionDb = juce::jmax(chunkMaxReductionDb, reductionDb);
                const float gain = juce::Decibels::decibelsToGain(-reductionDb) * makeupGain * out;
                b.setSample(0, sampleIndex, inL * gain);
                b.setSample(1, sampleIndex, inR * gain);
                wetEnergy += (double) (inL * juce::Decibels::decibelsToGain(-reductionDb)) * (double) (inL * juce::Decibels::decibelsToGain(-reductionDb));
                wetEnergy += (double) (inR * juce::Decibels::decibelsToGain(-reductionDb)) * (double) (inR * juce::Decibels::decibelsToGain(-reductionDb));
            }

            const float inRms = std::sqrt((float) (inputEnergy / (double) (samplesThisChunk * 2)));
            const float wetRms = std::sqrt((float) (wetEnergy / (double) (samplesThisChunk * 2)));
            const float grDb = juce::jmax(0.0f,
                juce::jmax(chunkMaxReductionDb,
                    juce::Decibels::gainToDecibels(inRms, -80.0f) - juce::Decibels::gainToDecibels(wetRms, -80.0f)));
            gainReductionEnvelopeDb += (grDb > gainReductionEnvelopeDb ? 0.22f : 0.08f) * (grDb - gainReductionEnvelopeDb);
        }
        else
        {
            double inputChunkEnergy = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < samplesThisChunk; ++i)
                {
                    const float sample = wetBuffer.getSample(ch, offset + i);
                    inputChunkEnergy += (double) sample * (double) sample;
                }

            for (int i = 0; i < samplesThisChunk; ++i)
            {
                const int sampleIndex = offset + i;
                const float wetInL = wetBuffer.getSample(0, sampleIndex);
                const float wetInR = wetBuffer.getSample(1, sampleIndex);
                const float linkedPeak = juce::jmax(std::abs(wetInL), std::abs(wetInR));
                float envDb = -120.0f;
                if (rmsMode)
                {
                    const float linkedPower = 0.5f * (wetInL * wetInL + wetInR * wetInR);
                    const float detectorValue = std::sqrt(juce::jmax(linkedPower, 0.0f));
                    const float coeff = detectorValue > std::sqrt(juce::jmax(detectorRmsPower, 0.0f)) ? attackCoeff : releaseCoeff;
                    detectorRmsPower = linkedPower + coeff * (detectorRmsPower - linkedPower);
                    envDb = juce::Decibels::gainToDecibels(std::sqrt(juce::jmax(detectorRmsPower, 0.0f)), -120.0f);
                    detectorEnvelope = 0.0f;
                }
                else
                {
                    const float coeff = linkedPeak > detectorEnvelope ? attackCoeff : releaseCoeff;
                    detectorEnvelope = linkedPeak + coeff * (detectorEnvelope - linkedPeak);
                    envDb = juce::Decibels::gainToDecibels(detectorEnvelope, -120.0f);
                    detectorRmsPower = 0.0f;
                }

                float reductionDb = 0.0f;
                if (envDb > threshold - halfKneeDb)
                {
                    if (envDb >= threshold + halfKneeDb)
                    {
                        reductionDb = (envDb - threshold) * (1.0f - 1.0f / ratio);
                    }
                    else
                    {
                        const float overDb = envDb - (threshold - halfKneeDb);
                        reductionDb = (1.0f - 1.0f / ratio) * (overDb * overDb) / (2.0f * softKneeWidthDb);
                    }
                }

                chunkMaxReductionDb = juce::jmax(chunkMaxReductionDb, reductionDb);
                const float compressedGain = juce::Decibels::decibelsToGain(-reductionDb);
                const float wetCompressedL = wetInL * compressedGain;
                const float wetCompressedR = wetInR * compressedGain;
                wetEnergy += (double) wetCompressedL * (double) wetCompressedL;
                wetEnergy += (double) wetCompressedR * (double) wetCompressedR;

                const float wetWithMakeupL = wetCompressedL * makeupGain;
                const float wetWithMakeupR = wetCompressedR * makeupGain;
                const float dryL = b.getSample(0, sampleIndex);
                const float dryR = b.getSample(1, sampleIndex);
                b.setSample(0, sampleIndex, (dryL * (1.0f - mix) + wetWithMakeupL * mix) * out);
                b.setSample(1, sampleIndex, (dryR * (1.0f - mix) + wetWithMakeupR * mix) * out);
            }

            const float inRms = std::sqrt((float) (inputChunkEnergy / (double) (samplesThisChunk * 2)));
            const float wetRms = std::sqrt((float) (wetEnergy / (double) (samplesThisChunk * 2)));
            const float grDb = juce::jmax(0.0f,
                juce::jmax(chunkMaxReductionDb,
                    juce::Decibels::gainToDecibels(inRms, -80.0f) - juce::Decibels::gainToDecibels(wetRms, -80.0f)));
            gainReductionEnvelopeDb += (grDb > gainReductionEnvelopeDb ? 0.22f : 0.08f) * (grDb - gainReductionEnvelopeDb);
        }
    }

    currentGainReductionDb.store(gainReductionEnvelopeDb, std::memory_order_relaxed);

    visualState.captureOutput(b);
}

void MusiqueCompressorProcessor::getStateInformation(juce::MemoryBlock& d){ auto s=parameters.copyState(); std::unique_ptr<juce::XmlElement> x(s.createXml()); copyXmlToBinary(*x,d);} 
void MusiqueCompressorProcessor::setStateInformation(const void* data,int size){ std::unique_ptr<juce::XmlElement> x(getXmlFromBinary(data,size)); if (x && x->hasTagName(parameters.state.getType())) parameters.replaceState(juce::ValueTree::fromXml(*x)); }

juce::AudioProcessorEditor* MusiqueCompressorProcessor::createEditor(){ return new MusiqueCompressorEditor(*this);} 
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){ return new MusiqueCompressorProcessor(); }
