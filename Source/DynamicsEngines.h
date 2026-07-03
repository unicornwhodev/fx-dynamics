#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dynamics
{
inline float dbToGain(float dB) noexcept
{
    return std::pow(10.0f, dB / 20.0f);
}

inline float safeGainToDb(float gain, float floorDb = -120.0f) noexcept
{
    return juce::Decibels::gainToDecibels(juce::jmax(gain, 1.0e-6f), floorDb);
}

inline float smoothingCoeffMs(float timeMs, double sampleRate) noexcept
{
    return std::exp(-1.0f / (juce::jmax(0.05f, timeMs) * 0.001f * (float) sampleRate));
}

inline float softClip(float input, float drive) noexcept
{
    const float clampedDrive = juce::jmax(1.0f, drive);
    return std::tanh(input * clampedDrive) / clampedDrive;
}

inline float readInterpolated(const std::array<std::vector<float>, 2>& buffer, int channel, int writePos, float delaySamples) noexcept
{
    const int bufferSize = (int) buffer[(size_t) channel].size();
    if (bufferSize <= 1)
        return 0.0f;

    float readPos = (float) writePos - juce::jlimit(1.0f, (float) (bufferSize - 2), delaySamples);
    while (readPos < 0.0f)
        readPos += (float) bufferSize;

    const int indexA = ((int) std::floor(readPos)) % bufferSize;
    const int indexB = (indexA + 1) % bufferSize;
    const float frac = readPos - (float) indexA;
    return juce::jmap(frac, buffer[(size_t) channel][(size_t) indexA], buffer[(size_t) channel][(size_t) indexB]);
}

inline float computeSoftKneeReductionDb(float envDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    if (ratio <= 1.0f)
        return 0.0f;

    const float halfKnee = kneeDb * 0.5f;
    if (envDb <= thresholdDb - halfKnee)
        return 0.0f;

    const float slope = 1.0f - 1.0f / ratio;
    if (envDb >= thresholdDb + halfKnee)
        return (envDb - thresholdDb) * slope;

    const float overDb = envDb - (thresholdDb - halfKnee);
    return slope * (overDb * overDb) / (2.0f * juce::jmax(0.5f, kneeDb));
}

class CompressorEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused(maximumBlockSize);

        preparedSampleRate = sampleRate;
        thresholdSmoothed.reset(sampleRate, 0.02);
        ratioSmoothed.reset(sampleRate, 0.02);
        attackSmoothed.reset(sampleRate, 0.03);
        releaseSmoothed.reset(sampleRate, 0.04);
        makeupSmoothed.reset(sampleRate, 0.02);

        thresholdSmoothed.setCurrentAndTargetValue(-18.0f);
        ratioSmoothed.setCurrentAndTargetValue(4.0f);
        attackSmoothed.setCurrentAndTargetValue(10.0f);
        releaseSmoothed.setCurrentAndTargetValue(120.0f);
        makeupSmoothed.setCurrentAndTargetValue(0.0f);
        reset();
    }

    void reset()
    {
        gainReductionEnvelopeDb = 0.0f;
        detectorEnvelope = 0.0f;
        detectorRmsPower = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer,
                 int variant,
                 float thresholdDb,
                 float ratioValue,
                 float attackMs,
                 float releaseMs,
                 float makeupDb,
                 bool rmsMode,
                 float& primaryReductionDb,
                 float& secondaryReductionDb)
    {
        thresholdSmoothed.setTargetValue(thresholdDb);
        ratioSmoothed.setTargetValue(ratioValue);
        attackSmoothed.setTargetValue(attackMs);
        releaseSmoothed.setTargetValue(releaseMs);
        makeupSmoothed.setTargetValue(makeupDb);

        const bool busVariant = variant == 2;
        const bool detectRms = busVariant || rmsMode;
        const int numSamples = buffer.getNumSamples();
        float detectorDb = -120.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float threshold = thresholdSmoothed.getNextValue();
            const float ratio = juce::jmax(1.0f, ratioSmoothed.getNextValue());
            const float attack = juce::jmax(0.1f, attackSmoothed.getNextValue());
            const float release = juce::jmax(5.0f, releaseSmoothed.getNextValue()) * (busVariant ? 1.55f : 1.0f);
            const float makeupGain = dbToGain(makeupSmoothed.getNextValue());
            const float kneeDb = busVariant ? 10.0f : 6.0f;
            const float attackCoeff = smoothingCoeffMs(attack, preparedSampleRate);
            const float releaseCoeff = smoothingCoeffMs(release, preparedSampleRate);

            const float inLeft = buffer.getSample(0, sample);
            const float inRight = buffer.getSample(1, sample);
            const float linkedPeak = juce::jmax(std::abs(inLeft), std::abs(inRight));

            if (detectRms)
            {
                const float linkedPower = 0.5f * (inLeft * inLeft + inRight * inRight);
                const float detectorValue = std::sqrt(juce::jmax(linkedPower, 0.0f));
                const float previousRms = std::sqrt(juce::jmax(detectorRmsPower, 0.0f));
                const float coeff = detectorValue > previousRms ? attackCoeff : releaseCoeff;
                detectorRmsPower = linkedPower + coeff * (detectorRmsPower - linkedPower);
                detectorEnvelope = 0.0f;
                detectorDb = safeGainToDb(std::sqrt(juce::jmax(detectorRmsPower, 0.0f)));
            }
            else
            {
                const float coeff = linkedPeak > detectorEnvelope ? attackCoeff : releaseCoeff;
                detectorEnvelope = linkedPeak + coeff * (detectorEnvelope - linkedPeak);
                detectorRmsPower = 0.0f;
                detectorDb = safeGainToDb(detectorEnvelope);
            }

            float reductionDb = computeSoftKneeReductionDb(detectorDb, threshold, ratio, kneeDb);
            if (busVariant)
                reductionDb *= 0.92f;

            gainReductionEnvelopeDb += (reductionDb > gainReductionEnvelopeDb ? 0.24f : 0.09f) * (reductionDb - gainReductionEnvelopeDb);
            const float gain = dbToGain(-reductionDb) * makeupGain;

            buffer.setSample(0, sample, inLeft * gain);
            buffer.setSample(1, sample, inRight * gain);
        }

        primaryReductionDb = gainReductionEnvelopeDb;
        secondaryReductionDb = detectorDb;
    }

private:
    juce::SmoothedValue<float> thresholdSmoothed, ratioSmoothed, attackSmoothed, releaseSmoothed, makeupSmoothed;
    float gainReductionEnvelopeDb = 0.0f;
    float detectorEnvelope = 0.0f;
    float detectorRmsPower = 0.0f;
    double preparedSampleRate = 44100.0;
};

class LimiterEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        preparedSampleRate = sampleRate;
        const int bufferSize = juce::jmax(8192, maximumBlockSize + (int) std::round(sampleRate * 0.05));
        delayBuffer[0].assign((size_t) bufferSize, 0.0f);
        delayBuffer[1].assign((size_t) bufferSize, 0.0f);

        driveSmoothed.reset(sampleRate, 0.02);
        ceilingSmoothed.reset(sampleRate, 0.02);
        releaseSmoothed.reset(sampleRate, 0.03);
        lookaheadSmoothed.reset(sampleRate, 0.03);
        softnessSmoothed.reset(sampleRate, 0.03);

        driveSmoothed.setCurrentAndTargetValue(4.0f);
        ceilingSmoothed.setCurrentAndTargetValue(-0.8f);
        releaseSmoothed.setCurrentAndTargetValue(60.0f);
        lookaheadSmoothed.setCurrentAndTargetValue(2.5f);
        softnessSmoothed.setCurrentAndTargetValue(35.0f);
        reset();
    }

    void reset()
    {
        writePos = 0;
        gainEnvelope = 1.0f;
        reductionEnvelopeDb = 0.0f;
        peakOverCeilingDb = 0.0f;
        std::fill(delayBuffer[0].begin(), delayBuffer[0].end(), 0.0f);
        std::fill(delayBuffer[1].begin(), delayBuffer[1].end(), 0.0f);
    }

    void process(juce::AudioBuffer<float>& buffer,
                 int variant,
                 float driveDb,
                 float ceilingDb,
                 float releaseMs,
                 float lookaheadMs,
                 float softnessPercent,
                 float& primaryReductionDb,
                 float& secondaryReductionDb)
    {
        driveSmoothed.setTargetValue(driveDb);
        ceilingSmoothed.setTargetValue(ceilingDb);
        releaseSmoothed.setTargetValue(releaseMs);
        lookaheadSmoothed.setTargetValue(lookaheadMs);
        softnessSmoothed.setTargetValue(softnessPercent);

        const int numSamples = buffer.getNumSamples();
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float preDrive = dbToGain(driveSmoothed.getNextValue()) * (variant == 2 ? 1.10f : 1.0f);
            const float ceilingGain = dbToGain(ceilingSmoothed.getNextValue());
            const float release = juce::jmax(5.0f, releaseSmoothed.getNextValue()) * (variant == 1 ? 0.75f : (variant == 2 ? 1.15f : 1.0f));
            const float lookaheadSamples = juce::jlimit(1.0f,
                                                        (float) (delayBuffer[0].size() - 2),
                                                        lookaheadSmoothed.getNextValue() * 0.001f * (float) preparedSampleRate);
            const float softness = juce::jlimit(0.0f, 1.0f, softnessSmoothed.getNextValue() / 100.0f);
            const float releaseCoeff = smoothingCoeffMs(release, preparedSampleRate);

            const float inputLeft = buffer.getSample(0, sample) * preDrive;
            const float inputRight = buffer.getSample(1, sample) * preDrive;
            delayBuffer[0][(size_t) writePos] = inputLeft;
            delayBuffer[1][(size_t) writePos] = inputRight;

            const float delayedLeft = readInterpolated(delayBuffer, 0, writePos, lookaheadSamples);
            const float delayedRight = readInterpolated(delayBuffer, 1, writePos, lookaheadSamples);
            const float futurePeak = juce::jmax(std::abs(inputLeft), std::abs(inputRight));
            const float shapedCeiling = ceilingGain * juce::jmap(softness, 1.0f, variant == 2 ? 0.82f : 0.92f);

            float targetGain = 1.0f;
            if (futurePeak > shapedCeiling)
                targetGain = shapedCeiling / juce::jmax(futurePeak, 1.0e-5f);

            if (targetGain < gainEnvelope)
                gainEnvelope = targetGain;
            else
                gainEnvelope = targetGain + releaseCoeff * (gainEnvelope - targetGain);

            float wetLeft = delayedLeft * gainEnvelope;
            float wetRight = delayedRight * gainEnvelope;

            const float clipDrive = 1.0f + softness * (variant == 0 ? 1.3f : (variant == 1 ? 0.7f : 2.3f));
            if (softness > 0.001f || variant == 2)
            {
                wetLeft = softClip(wetLeft, clipDrive);
                wetRight = softClip(wetRight, clipDrive);
            }

            wetLeft = juce::jlimit(-ceilingGain, ceilingGain, wetLeft);
            wetRight = juce::jlimit(-ceilingGain, ceilingGain, wetRight);
            buffer.setSample(0, sample, wetLeft);
            buffer.setSample(1, sample, wetRight);

            const float reductionDb = juce::jmax(0.0f, -safeGainToDb(gainEnvelope, -60.0f));
            reductionEnvelopeDb += (reductionDb > reductionEnvelopeDb ? 0.28f : 0.10f) * (reductionDb - reductionEnvelopeDb);

            const float overshootRatio = futurePeak / juce::jmax(ceilingGain, 1.0e-5f);
            const float overshootDb = overshootRatio > 1.0f ? safeGainToDb(overshootRatio, 0.0f) : 0.0f;
            peakOverCeilingDb += 0.18f * (overshootDb - peakOverCeilingDb);

            writePos = (writePos + 1) % (int) delayBuffer[0].size();
        }

        primaryReductionDb = reductionEnvelopeDb;
        secondaryReductionDb = peakOverCeilingDb;
    }

private:
    std::array<std::vector<float>, 2> delayBuffer;
    juce::SmoothedValue<float> driveSmoothed, ceilingSmoothed, releaseSmoothed, lookaheadSmoothed, softnessSmoothed;
    int writePos = 0;
    float gainEnvelope = 1.0f;
    float reductionEnvelopeDb = 0.0f;
    float peakOverCeilingDb = 0.0f;
    double preparedSampleRate = 44100.0;
};

class GateEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused(maximumBlockSize);

        preparedSampleRate = sampleRate;
        thresholdSmoothed.reset(sampleRate, 0.02);
        rangeSmoothed.reset(sampleRate, 0.02);
        attackSmoothed.reset(sampleRate, 0.03);
        releaseSmoothed.reset(sampleRate, 0.03);
        holdSmoothed.reset(sampleRate, 0.03);

        thresholdSmoothed.setCurrentAndTargetValue(-32.0f);
        rangeSmoothed.setCurrentAndTargetValue(45.0f);
        attackSmoothed.setCurrentAndTargetValue(5.0f);
        releaseSmoothed.setCurrentAndTargetValue(120.0f);
        holdSmoothed.setCurrentAndTargetValue(40.0f);
        reset();
    }

    void reset()
    {
        detectorEnvelope = 0.0f;
        gainEnvelope = 1.0f;
        reductionEnvelopeDb = 0.0f;
        gateOpen = true;
        holdCounter = 0;
    }

    void process(juce::AudioBuffer<float>& buffer,
                 int variant,
                 float thresholdDb,
                 float rangeDb,
                 float attackMs,
                 float releaseMs,
                 float holdMs,
                 float& primaryReductionDb,
                 float& secondaryReductionDb)
    {
        thresholdSmoothed.setTargetValue(thresholdDb);
        rangeSmoothed.setTargetValue(rangeDb);
        attackSmoothed.setTargetValue(attackMs);
        releaseSmoothed.setTargetValue(releaseMs);
        holdSmoothed.setTargetValue(holdMs);

        const int numSamples = buffer.getNumSamples();
        float detectorDb = -120.0f;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float threshold = thresholdSmoothed.getNextValue();
            const float range = juce::jlimit(0.0f, 80.0f, rangeSmoothed.getNextValue());
            const float attack = juce::jmax(0.1f, attackSmoothed.getNextValue());
            const float release = juce::jmax(5.0f, releaseSmoothed.getNextValue());
            const float hold = juce::jlimit(0.0f, 400.0f, holdSmoothed.getNextValue());
            const float attackCoeff = smoothingCoeffMs(attack, preparedSampleRate);
            const float releaseCoeff = smoothingCoeffMs(release, preparedSampleRate);
            const float linkedPeak = juce::jmax(std::abs(buffer.getSample(0, sample)), std::abs(buffer.getSample(1, sample)));

            const float detectorCoeff = linkedPeak > detectorEnvelope ? attackCoeff : releaseCoeff;
            detectorEnvelope = linkedPeak + detectorCoeff * (detectorEnvelope - linkedPeak);
            detectorDb = safeGainToDb(detectorEnvelope);

            const float hysteresisDb = variant == 1 ? 1.5f : 3.0f;
            if (detectorDb >= threshold)
            {
                gateOpen = true;
                holdCounter = (int) std::round(hold * 0.001f * (float) preparedSampleRate);
            }
            else if (holdCounter > 0)
            {
                --holdCounter;
            }
            else if (detectorDb < threshold - hysteresisDb)
            {
                gateOpen = false;
            }

            const float closedGain = dbToGain(-range);
            float targetGain = gateOpen ? 1.0f : closedGain;
            if (variant == 1)
            {
                const float belowDb = juce::jmax(0.0f, threshold - detectorDb);
                const float reductionDb = juce::jmin(range, belowDb * 0.55f);
                targetGain = dbToGain(-reductionDb);
            }
            else if (variant == 2)
            {
                const float closeStart = threshold - hysteresisDb - 6.0f;
                const float norm = juce::jlimit(0.0f, 1.0f, (detectorDb - closeStart) / juce::jmax(0.25f, threshold - closeStart));
                const float shaped = norm * norm * (3.0f - 2.0f * norm);
                targetGain = juce::jmap(shaped, closedGain, 1.0f);
            }

            if (targetGain > gainEnvelope)
                gainEnvelope = targetGain + attackCoeff * (gainEnvelope - targetGain);
            else
                gainEnvelope = targetGain + releaseCoeff * (gainEnvelope - targetGain);

            buffer.setSample(0, sample, buffer.getSample(0, sample) * gainEnvelope);
            buffer.setSample(1, sample, buffer.getSample(1, sample) * gainEnvelope);

            const float reductionDb = juce::jmax(0.0f, -safeGainToDb(gainEnvelope, -60.0f));
            reductionEnvelopeDb += (reductionDb > reductionEnvelopeDb ? 0.24f : 0.08f) * (reductionDb - reductionEnvelopeDb);
        }

        primaryReductionDb = reductionEnvelopeDb;
        secondaryReductionDb = juce::jmax(0.0f, thresholdDb - detectorDb);
    }

private:
    juce::SmoothedValue<float> thresholdSmoothed, rangeSmoothed, attackSmoothed, releaseSmoothed, holdSmoothed;
    float detectorEnvelope = 0.0f;
    float gainEnvelope = 1.0f;
    float reductionEnvelopeDb = 0.0f;
    bool gateOpen = true;
    int holdCounter = 0;
    double preparedSampleRate = 44100.0;
};

class MultibandEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        preparedSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maximumBlockSize, 1 };
        for (auto& filter : lowFilters)
        {
            filter.prepare(spec);
            filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            filter.setResonance(0.7071f);
        }
        for (auto& filter : highFilters)
        {
            filter.prepare(spec);
            filter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
            filter.setResonance(0.7071f);
        }

        lowSmoothed.reset(sampleRate, 0.03);
        midSmoothed.reset(sampleRate, 0.03);
        highSmoothed.reset(sampleRate, 0.03);
        glueSmoothed.reset(sampleRate, 0.03);
        recoverySmoothed.reset(sampleRate, 0.03);

        lowSmoothed.setCurrentAndTargetValue(50.0f);
        midSmoothed.setCurrentAndTargetValue(55.0f);
        highSmoothed.setCurrentAndTargetValue(45.0f);
        glueSmoothed.setCurrentAndTargetValue(40.0f);
        recoverySmoothed.setCurrentAndTargetValue(45.0f);
        reset();
    }

    void reset()
    {
        for (auto& filter : lowFilters)
            filter.reset();
        for (auto& filter : highFilters)
            filter.reset();
        for (auto& state : bands)
        {
            state.detectorEnvelope = 0.0f;
            state.gainReductionDb = 0.0f;
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 int variant,
                 float lowAmount,
                 float midAmount,
                 float highAmount,
                 float glueAmount,
                 float recoveryAmount,
                 std::array<float, 3>& bandReductionDb,
                 float& primaryReductionDb,
                 float& secondaryReductionDb)
    {
        lowSmoothed.setTargetValue(lowAmount);
        midSmoothed.setTargetValue(midAmount);
        highSmoothed.setTargetValue(highAmount);
        glueSmoothed.setTargetValue(glueAmount);
        recoverySmoothed.setTargetValue(recoveryAmount);

        float lowCrossover = 120.0f;
        float highCrossover = 2200.0f;
        if (variant == 1)
        {
            lowCrossover = 180.0f;
            highCrossover = 3000.0f;
        }
        else if (variant == 2)
        {
            lowCrossover = 90.0f;
            highCrossover = 1600.0f;
        }

        for (auto& filter : lowFilters)
            filter.setCutoffFrequency(lowCrossover);
        for (auto& filter : highFilters)
            filter.setCutoffFrequency(highCrossover);

        const int numSamples = buffer.getNumSamples();
        std::array<float, 3> lastReduction {};

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float lowNorm = lowSmoothed.getNextValue() / 100.0f;
            const float midNorm = midSmoothed.getNextValue() / 100.0f;
            const float highNorm = highSmoothed.getNextValue() / 100.0f;
            const float glueNorm = glueSmoothed.getNextValue() / 100.0f;
            const float recoveryNorm = recoverySmoothed.getNextValue() / 100.0f;

            const float inLeft = buffer.getSample(0, sample);
            const float inRight = buffer.getSample(1, sample);

            const float lowLeft = lowFilters[0].processSample(0, inLeft);
            const float lowRight = lowFilters[1].processSample(0, inRight);
            const float highLeft = highFilters[0].processSample(0, inLeft);
            const float highRight = highFilters[1].processSample(0, inRight);
            const float midLeft = inLeft - lowLeft - highLeft;
            const float midRight = inRight - lowRight - highRight;

            lastReduction[0] = processBand(0, lowLeft, lowRight, lowNorm, glueNorm, recoveryNorm);
            lastReduction[1] = processBand(1, midLeft, midRight, midNorm, glueNorm, recoveryNorm);
            lastReduction[2] = processBand(2, highLeft, highRight, highNorm, glueNorm, recoveryNorm);

            const float averageReduction = (lastReduction[0] + lastReduction[1] + lastReduction[2]) / 3.0f;
            const float link = juce::jlimit(0.0f, 0.72f, glueNorm * 0.72f);

            const float lowGain = dbToGain(-juce::jmap(link, lastReduction[0], averageReduction));
            const float midGain = dbToGain(-juce::jmap(link, lastReduction[1], averageReduction));
            const float highGain = dbToGain(-juce::jmap(link, lastReduction[2], averageReduction));
            const float glueTrim = 1.0f - juce::jlimit(0.0f, 0.10f, glueNorm * averageReduction * 0.008f);

            buffer.setSample(0, sample, (lowLeft * lowGain + midLeft * midGain + highLeft * highGain) * glueTrim);
            buffer.setSample(1, sample, (lowRight * lowGain + midRight * midGain + highRight * highGain) * glueTrim);
        }

        bandReductionDb = lastReduction;
        primaryReductionDb = (lastReduction[0] + lastReduction[1] + lastReduction[2]) / 3.0f;
        secondaryReductionDb = juce::jmax(lastReduction[0], juce::jmax(lastReduction[1], lastReduction[2]));
    }

private:
    struct BandState
    {
        float detectorEnvelope = 0.0f;
        float gainReductionDb = 0.0f;
    };

    float processBand(int bandIndex, float left, float right, float amount, float glue, float recovery)
    {
        auto& state = bands[(size_t) bandIndex];
        const float intensity = juce::jlimit(0.0f, 1.0f, amount);
        if (intensity <= 0.001f)
        {
            state.gainReductionDb += 0.08f * (0.0f - state.gainReductionDb);
            return state.gainReductionDb;
        }

        const float attackMs = (bandIndex == 0 ? 22.0f : (bandIndex == 1 ? 12.0f : 4.5f)) * juce::jmap(recovery, 1.15f, 0.75f);
        const float releaseMs = juce::jmap(recovery, 320.0f, 45.0f) * (bandIndex == 0 ? 1.25f : (bandIndex == 1 ? 1.0f : 0.75f));
        const float detectorCoeff = smoothingCoeffMs(attackMs, preparedSampleRate);
        const float recoveryCoeff = smoothingCoeffMs(releaseMs, preparedSampleRate);
        const float linkedPeak = juce::jmax(std::abs(left), std::abs(right));
        const float coeff = linkedPeak > state.detectorEnvelope ? detectorCoeff : recoveryCoeff;
        state.detectorEnvelope = linkedPeak + coeff * (state.detectorEnvelope - linkedPeak);

        const float envDb = safeGainToDb(state.detectorEnvelope, -80.0f);
        const float thresholdDb = juce::jmap(intensity, -6.0f, -32.0f);
        const float ratio = 1.0f + intensity * (bandIndex == 1 ? 7.0f : 5.5f) + glue * 1.3f;
        const float targetReductionDb = computeSoftKneeReductionDb(envDb, thresholdDb, ratio, 4.0f + glue * 4.0f);

        if (targetReductionDb > state.gainReductionDb)
            state.gainReductionDb = targetReductionDb + detectorCoeff * (state.gainReductionDb - targetReductionDb);
        else
            state.gainReductionDb = targetReductionDb + recoveryCoeff * (state.gainReductionDb - targetReductionDb);

        return state.gainReductionDb;
    }

    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> lowFilters;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> highFilters;
    std::array<BandState, 3> bands;
    juce::SmoothedValue<float> lowSmoothed, midSmoothed, highSmoothed, glueSmoothed, recoverySmoothed;
    double preparedSampleRate = 44100.0;
};

class TransientShaperEngine
{
public:
    void prepare(double sampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused(maximumBlockSize);

        preparedSampleRate = sampleRate;
        attackSmoothed.reset(sampleRate, 0.03);
        sustainSmoothed.reset(sampleRate, 0.03);
        sensitivitySmoothed.reset(sampleRate, 0.03);
        speedSmoothed.reset(sampleRate, 0.03);
        clipSmoothed.reset(sampleRate, 0.03);

        attackSmoothed.setCurrentAndTargetValue(25.0f);
        sustainSmoothed.setCurrentAndTargetValue(0.0f);
        sensitivitySmoothed.setCurrentAndTargetValue(50.0f);
        speedSmoothed.setCurrentAndTargetValue(50.0f);
        clipSmoothed.setCurrentAndTargetValue(35.0f);
        reset();
    }

    void reset()
    {
        fastEnvelope = 0.0f;
        slowEnvelope = 0.0f;
        transientAttackDelta = 0.0f;
        transientSustainDelta = 0.0f;
        clipActivityDb = 0.0f;
    }

    void process(juce::AudioBuffer<float>& buffer,
                 int variant,
                 float attackAmount,
                 float sustainAmount,
                 float sensitivityAmount,
                 float speedAmount,
                 float clipAmount,
                 float& primaryReductionDb,
                 float& secondaryReductionDb,
                 float& attackDeltaOut,
                 float& sustainDeltaOut)
    {
        attackSmoothed.setTargetValue(attackAmount);
        sustainSmoothed.setTargetValue(sustainAmount);
        sensitivitySmoothed.setTargetValue(sensitivityAmount);
        speedSmoothed.setTargetValue(speedAmount);
        clipSmoothed.setTargetValue(clipAmount);

        const int numSamples = buffer.getNumSamples();
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float attackControl = attackSmoothed.getNextValue() / 100.0f;
            const float sustainControl = sustainSmoothed.getNextValue() / 100.0f;
            const float sensitivity = sensitivitySmoothed.getNextValue() / 100.0f;
            const float speed = speedSmoothed.getNextValue() / 100.0f;
            const float clip = clipSmoothed.getNextValue() / 100.0f;

            const float fastMs = juce::jmap(speed, variant == 1 ? 0.25f : 0.5f, variant == 2 ? 7.5f : 4.5f);
            const float slowMs = juce::jmap(speed, variant == 2 ? 22.0f : 12.0f, variant == 1 ? 95.0f : 140.0f);
            const float fastCoeff = smoothingCoeffMs(fastMs, preparedSampleRate);
            const float slowCoeff = smoothingCoeffMs(slowMs, preparedSampleRate);

            const float inputLeft = buffer.getSample(0, sample);
            const float inputRight = buffer.getSample(1, sample);
            const float linked = 0.5f * (std::abs(inputLeft) + std::abs(inputRight));

            fastEnvelope = linked + fastCoeff * (fastEnvelope - linked);
            slowEnvelope = linked + slowCoeff * (slowEnvelope - linked);

            const float sensitivityScale = 0.65f + sensitivity * 0.85f;
            const float transient = juce::jmax(0.0f, fastEnvelope - slowEnvelope * sensitivityScale);
            const float sustain = juce::jmax(0.0f, slowEnvelope - fastEnvelope * 0.8f);

            const float variantAttackBias = variant == 0 ? 1.0f : (variant == 1 ? 1.3f : 0.85f);
            const float variantSustainBias = variant == 2 ? 1.2f : 1.0f;
            const float attackGain = transient * attackControl * 2.3f * variantAttackBias;
            const float sustainGain = sustain * sustainControl * 1.8f * variantSustainBias;
            const float gain = juce::jlimit(0.2f, 3.6f, 1.0f + attackGain + sustainGain);

            float wetLeft = inputLeft * gain;
            float wetRight = inputRight * gain;
            const float clipDrive = 1.0f + clip * (variant == 1 ? 3.2f : 2.1f);
            if (clip > 0.001f)
            {
                const float clippedLeft = softClip(wetLeft, clipDrive);
                const float clippedRight = softClip(wetRight, clipDrive);

                const float prePeak = juce::jmax(std::abs(wetLeft), std::abs(wetRight));
                const float postPeak = juce::jmax(std::abs(clippedLeft), std::abs(clippedRight));
                const float reductionDb = juce::jmax(0.0f, safeGainToDb(prePeak / juce::jmax(postPeak, 1.0e-5f), 0.0f));
                clipActivityDb += 0.20f * (reductionDb - clipActivityDb);

                wetLeft = clippedLeft;
                wetRight = clippedRight;
            }
            else
            {
                clipActivityDb += 0.12f * (0.0f - clipActivityDb);
            }

            buffer.setSample(0, sample, wetLeft);
            buffer.setSample(1, sample, wetRight);

            transientAttackDelta += 0.18f * (juce::jlimit(-1.0f, 1.0f, attackGain) - transientAttackDelta);
            transientSustainDelta += 0.18f * (juce::jlimit(-1.0f, 1.0f, sustainGain) - transientSustainDelta);
        }

        primaryReductionDb = clipActivityDb;
        secondaryReductionDb = 0.0f;
        attackDeltaOut = transientAttackDelta;
        sustainDeltaOut = transientSustainDelta;
    }

private:
    juce::SmoothedValue<float> attackSmoothed, sustainSmoothed, sensitivitySmoothed, speedSmoothed, clipSmoothed;
    float fastEnvelope = 0.0f;
    float slowEnvelope = 0.0f;
    float transientAttackDelta = 0.0f;
    float transientSustainDelta = 0.0f;
    float clipActivityDb = 0.0f;
    double preparedSampleRate = 44100.0;
};
} // namespace dynamics
