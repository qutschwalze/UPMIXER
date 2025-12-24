/*
==============================================================================
    PluginProcessor.cpp
==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CoherentUpmixAudioProcessor::CoherentUpmixAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::create5point1(), true)
                     .withOutput ("Output", juce::AudioChannelSet::create5point1(), true))
     , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#else
     : apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
}

CoherentUpmixAudioProcessor::~CoherentUpmixAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CoherentUpmixAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>("surroundBalance", "Surround Balance", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("lfeAmount", "LFE Amount", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -12.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("crossoverFreq", "Crossover Freq", juce::NormalisableRange<float> (40.0f, 200.0f, 1.0f, 0.5f), 80.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("dialogExtract", "Dialog Extract", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("centerComp", "Center Comp", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("surroundDelay", "Rear Delay (ms)", juce::NormalisableRange<float> (0.0f, 30.0f, 1.0f), 20.0f));

    juce::StringArray modes;
    modes.add("Coherent Upmix");
    modes.add("Neo:6 Mode");
    modes.add("Matrix Mode (PLII)");
    modes.add("Modern Transient");
    modes.add("Exact Downmix");
    modes.add("5.1 Pass-Through");  // ← NEU HINZUFÜGEN
    params.push_back(std::make_unique<juce::AudioParameterChoice>("processingMode", "Algorithm Mode", modes, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool>("loudnessBoost", "Loudness Boost", false));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String CoherentUpmixAudioProcessor::getName() const { return JucePlugin_Name; }
bool CoherentUpmixAudioProcessor::acceptsMidi() const { return false; }
bool CoherentUpmixAudioProcessor::producesMidi() const { return false; }
bool CoherentUpmixAudioProcessor::isMidiEffect() const { return false; }
double CoherentUpmixAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int CoherentUpmixAudioProcessor::getNumPrograms() { return 1; }
int CoherentUpmixAudioProcessor::getCurrentProgram() { return 0; }
void CoherentUpmixAudioProcessor::setCurrentProgram (int index) {}
const juce::String CoherentUpmixAudioProcessor::getProgramName (int index) { return {}; }
void CoherentUpmixAudioProcessor::changeProgramName (int index, const juce::String& newName) {}

//==============================================================================
void CoherentUpmixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec stereoSpec;
    stereoSpec.sampleRate = sampleRate;
    stereoSpec.maximumBlockSize = samplesPerBlock;
    stereoSpec.numChannels = 2;

    lowPassFilter.prepare (stereoSpec);
    lowPassFilter.setType (juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
    highPassFilter.prepare (stereoSpec);
    highPassFilter.setType (juce::dsp::LinkwitzRileyFilter<float>::Type::highpass);

    dialogFilter.prepare (stereoSpec);
    dialogFilter.reset();
    *dialogFilter.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass (sampleRate, 1500.0f, 0.7f);

    centerCompressor.prepare (stereoSpec);
    centerCompressor.reset();
    centerCompressor.setAttack (5.0f);
    centerCompressor.setRelease (100.0f);
    centerCompressor.setRatio (4.0f);

    juce::dsp::ProcessSpec surroundSpec = stereoSpec;
    surroundSpec.numChannels = 6;
    outputLimiter.prepare(surroundSpec);
    outputLimiter.reset();

    surroundDelayLine.prepare (stereoSpec);
    surroundDelayLine.reset();
    surroundDelayLine.setMaximumDelayInSamples (sampleRate * 1.0);

    neo6LowPass.prepare(stereoSpec);
    neo6LowPass.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::lowpass);
    neo6LowPass.setCutoffFrequency(3000.0f);
    neo6HighPass.prepare(stereoSpec);
    neo6HighPass.setType(juce::dsp::LinkwitzRileyFilter<float>::Type::highpass);
    neo6HighPass.setCutoffFrequency(3000.0f);

    fastEnvL = 0.0f; slowEnvL = 0.0f;
    fastEnvR = 0.0f; slowEnvR = 0.0f;
}

void CoherentUpmixAudioProcessor::releaseResources() {}

bool CoherentUpmixAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled()) return false;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo()) return true;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::create5point1()) return true;
    if (in == juce::AudioChannelSet::create5point1() && out == juce::AudioChannelSet::create5point1()) return true;

    return false;
}

void CoherentUpmixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto numSamples = buffer.getNumSamples();

    const int numInputChannels  = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    // Heuristik: Prüfen, ob auf den 5.1-Surround-Kanälen (C, LFE, Ls, Rs)
    // wirklich Inhalt vorhanden ist. Wenn nicht → als Stereo behandeln.
    bool hasTrue51Content = false;
    if (numInputChannels >= 6)
    {
        const float threshold = 1e-5f; // ggf. anpassen
        for (int ch = 2; ch < juce::jmin (6, numInputChannels); ++ch)
        {
            if (buffer.getRMSLevel (ch, 0, numSamples) > threshold)
            {
                hasTrue51Content = true;
                break;
            }
        }
    }
    // Mode EINMAL lesen, damit er überall verfügbar ist
    const int currentMode = (int) apvts.getRawParameterValue ("processingMode")->load();
    // Fall 1: Echter 5.1-Input (Energie auf einem der Kanäle 2..5) → Passthrough
    if (hasTrue51Content && numOutputChannels >= 6)
    {
        rmsLevelLeft.store   (buffer.getMagnitude (0, 0, numSamples));
        rmsLevelRight.store  (buffer.getMagnitude (1, 0, numSamples));
        rmsLevelCenter.store (buffer.getMagnitude (2, 0, numSamples));
        rmsLevelLFE.store    (buffer.getMagnitude (3, 0, numSamples));
        rmsLevelLs.store     (buffer.getMagnitude (4, 0, numSamples));
        rmsLevelRs.store     (buffer.getMagnitude (5, 0, numSamples));
        // Buffer nicht anfassen → echter 5.1-Stream geht unverändert durch
        return;
    }
    // Pass-Through Modus prüfen (NEU)
    // const int currentMode = (int) apvts.getRawParameterValue("processingMode")->load();
    if (currentMode == modePassThrough || (numInputChannels == 6 && hasTrue51Content && numOutputChannels == 6))
    {
        // Einfacher Pass-Through: Input direkt zu Output kopieren
        for (int ch = 0; ch < juce::jmin(numInputChannels, numOutputChannels); ++ch)
            buffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        
        // RMS für Meter aktualisieren
        rmsLevelLeft.store (buffer.getMagnitude(0, 0, numSamples));
        rmsLevelRight.store(buffer.getMagnitude(1, 0, numSamples));
        rmsLevelCenter.store(buffer.getMagnitude(2, 0, numSamples));
        rmsLevelLFE.store  (buffer.getMagnitude(3, 0, numSamples));
        rmsLevelLs.store   (buffer.getMagnitude(4, 0, numSamples));
        rmsLevelRs.store   (buffer.getMagnitude(5, 0, numSamples));
        return;
    }

    // Ab hier: Upmix-Zweig (Stereo → 5.1). Für Upmix brauchen wir 6 Ausgänge.
    if (numOutputChannels < 6)
        return;

    const float surroundBalance = apvts.getRawParameterValue ("surroundBalance")->load();
    const float lfeAmountDb     = apvts.getRawParameterValue ("lfeAmount")->load();
    const float crossoverHz     = apvts.getRawParameterValue ("crossoverFreq")->load();
    const float dialogExtract   = apvts.getRawParameterValue ("dialogExtract")->load();
    const float compAmount      = apvts.getRawParameterValue ("centerComp")->load();
    const bool  boostActive     = apvts.getRawParameterValue ("loudnessBoost")->load() > 0.5f;
    //const int   currentMode     = (int)*apvts.getRawParameterValue ("processingMode");

    juce::AudioBuffer<float> lpBuffer; lpBuffer.makeCopyOf (buffer, true);
    juce::AudioBuffer<float> hpBuffer; hpBuffer.makeCopyOf (buffer, true);
    juce::AudioBuffer<float> rawCopy;  rawCopy.makeCopyOf  (buffer, true);

    juce::dsp::AudioBlock<float> lpBlockFull (lpBuffer);
    juce::dsp::AudioBlock<float> hpBlockFull (hpBuffer);
    juce::dsp::AudioBlock<float> lpStereo = lpBlockFull.getSubsetChannelBlock (0, 2);
    juce::dsp::AudioBlock<float> hpStereo = hpBlockFull.getSubsetChannelBlock (0, 2);

    lowPassFilter.setCutoffFrequency (crossoverHz);
    highPassFilter.setCutoffFrequency (crossoverHz);

    juce::dsp::ProcessContextReplacing<float> lpContext (lpStereo);
    juce::dsp::ProcessContextReplacing<float> hpContext (hpStereo);
    lowPassFilter.process (lpContext);
    highPassFilter.process (hpContext);

    const float* lpL = lpBuffer.getReadPointer (0);
    const float* lpR = lpBuffer.getReadPointer (1);
    const float* hpL = hpBuffer.getReadPointer (0);
    const float* hpR = hpBuffer.getReadPointer (1);

    float* outL   = buffer.getWritePointer (0);
    float* outR   = buffer.getWritePointer (1);
    float* outC   = buffer.getWritePointer (2);
    float* outLFE = buffer.getWritePointer (3);
    float* outLs  = buffer.getWritePointer (4);
    float* outRs  = buffer.getWritePointer (5);

    if (currentMode == modeDownmix)
    {
        juce::FloatVectorOperations::clear (outC,   numSamples);
        juce::FloatVectorOperations::clear (outLFE, numSamples);
        juce::FloatVectorOperations::clear (outLs,  numSamples);
        juce::FloatVectorOperations::clear (outRs,  numSamples);

        const float* srcL = rawCopy.getReadPointer (0);
        const float* srcR = rawCopy.getReadPointer (1);
        juce::FloatVectorOperations::copy (outL, srcL, numSamples);
        juce::FloatVectorOperations::copy (outR, srcR, numSamples);
    }
    else
    {
        if (compAmount > 0.01f)
        {
            centerCompressor.setThreshold (-30.0f * compAmount);
            centerCompressor.setRatio (1.0f + (3.0f * compAmount));
        }

        juce::AudioBuffer<float> tmpOut;
        tmpOut.setSize (6, numSamples);
        tmpOut.clear();

        float* tL   = tmpOut.getWritePointer (0);
        float* tR   = tmpOut.getWritePointer (1);
        float* tC   = tmpOut.getWritePointer (2);
        float* tLFE = tmpOut.getWritePointer (3);
        float* tLs  = tmpOut.getWritePointer (4);
        float* tRs  = tmpOut.getWritePointer (5);

        const float lfeGain      = juce::Decibels::decibelsToGain (lfeAmountDb);
        const float surroundGain = 0.8f * surroundBalance;
        const float frontWeight  = 1.0f - surroundBalance;
        const float centerGain   = 0.5f * frontWeight;
        const float dialogBoost  = dialogExtract * 2.5f;

        if (currentMode == modeNeo6)
        {
            juce::AudioBuffer<float> bandLow;  bandLow.makeCopyOf (hpBuffer, true);
            juce::AudioBuffer<float> bandHigh; bandHigh.makeCopyOf (hpBuffer, true);

            juce::dsp::AudioBlock<float> blockLow  (bandLow);
            juce::dsp::AudioBlock<float> blockHigh (bandHigh);
            juce::dsp::AudioBlock<float> subLow  = blockLow.getSubsetChannelBlock  (0, 2);
            juce::dsp::AudioBlock<float> subHigh = blockHigh.getSubsetChannelBlock (0, 2);

            juce::dsp::ProcessContextReplacing<float> ctxLow  (subLow);
            juce::dsp::ProcessContextReplacing<float> ctxHigh (subHigh);
            neo6LowPass.process  (ctxLow);
            neo6HighPass.process (ctxHigh);

            const float cw = 1.0f - dialogExtract;

            processNeo6Band (bandLow.getReadPointer (0), bandLow.getReadPointer (1), numSamples,
                             tL, tR, tC, tLs, tRs, surroundGain, cw, steerStateLow);

            juce::AudioBuffer<float> highOut; highOut.setSize (6, numSamples); highOut.clear();
            processNeo6Band (bandHigh.getReadPointer (0), bandHigh.getReadPointer (1), numSamples,
                             highOut.getWritePointer (0), highOut.getWritePointer (1),
                             highOut.getWritePointer (2), highOut.getWritePointer (4), highOut.getWritePointer (5),
                             surroundGain, cw, steerStateHigh);

            for (int ch : { 0, 1, 2, 4, 5 })
                juce::FloatVectorOperations::add (tmpOut.getWritePointer (ch),
                                                  highOut.getReadPointer (ch),
                                                  numSamples);
        }
        else if (currentMode == modeProLogicII)
        {
            const float centerWidth        = 1.0f - dialogExtract;
            const float matrixSurroundBoost = 1.6f;

            for (int n = 0; n < numSamples; ++n)
            {
                float l   = hpL[n];
                float r   = hpR[n];
                float sum = (l + r) * 0.707f;
                float diff = (l - r) * 0.707f;

                tC[n] = sum;

                float surrL = (diff * 0.7f) + (l * 0.3f);
                float surrR = (-diff * 0.7f) + (r * 0.3f);
                tLs[n] = surrL * surroundGain * matrixSurroundBoost;
                tRs[n] = surrR * surroundGain * matrixSurroundBoost;

                float subtractionFactor = (1.0f - centerWidth) * 0.8f;
                tL[n] = l - (sum * subtractionFactor);
                tR[n] = r - (sum * subtractionFactor);
            }
        }
        else if (currentMode == modeTransient)
        {
            const float att = 0.9f;
            const float rel = 0.999f;

            for (int n = 0; n < numSamples; ++n)
            {
                float l = hpL[n];
                float r = hpR[n];

                float absL = std::abs (l);
                float absR = std::abs (r);

                if (absL > fastEnvL) fastEnvL = absL; else fastEnvL *= att;
                if (absL > slowEnvL) slowEnvL = absL; else slowEnvL = (slowEnvL * rel) + (absL * (1.0f - rel));
                if (absR > fastEnvR) fastEnvR = absR; else fastEnvR *= att;
                if (absR > slowEnvR) slowEnvR = absR; else slowEnvR = (slowEnvR * rel) + (absR * (1.0f - rel));

                float ratioL = (fastEnvL - slowEnvL); if (ratioL < 0.0f) ratioL = 0.0f;
                ratioL *= 4.0f; if (ratioL > 1.0f) ratioL = 1.0f;

                float ratioR = (fastEnvR - slowEnvR); if (ratioR < 0.0f) ratioR = 0.0f;
                ratioR *= 4.0f; if (ratioR > 1.0f) ratioR = 1.0f;

                float transWeightL = ratioL; float susWeightL = 1.0f - ratioL;
                float transWeightR = ratioR; float susWeightR = 1.0f - ratioR;

                float monoSum = (l + r) * 0.5f;

                tC[n] = monoSum * ((susWeightL + susWeightR) * 0.5f) * centerGain;
                if (dialogExtract > 0.0f)
                    tC[n] += monoSum * dialogExtract;

                tL[n]  = l * (transWeightL + (susWeightL * frontWeight));
                tR[n]  = r * (transWeightR + (susWeightR * frontWeight));
                tLs[n] = l * susWeightL * surroundBalance * 1.5f;
                tRs[n] = r * susWeightR * surroundBalance * 1.5f;
            }
        }
        else
        {
            juce::AudioBuffer<float> dialogBuffer;
            dialogBuffer.setSize (1, numSamples);

            auto* dW = dialogBuffer.getWritePointer (0);
            for (int i = 0; i < numSamples; ++i)
                dW[i] = 0.5f * (hpL[i] + hpR[i]);

            juce::dsp::AudioBlock<float> db (dialogBuffer);
            juce::dsp::ProcessContextReplacing<float> dbCtx (db);
            dialogFilter.process (dbCtx);

            const float* dR = dialogBuffer.getReadPointer (0);

            for (int n = 0; n < numSamples; ++n)
            {
                float l = hpL[n];
                float r = hpR[n];
                float monoMid = 0.5f * (l + r);

                tC[n]  = (centerGain * monoMid) + (dR[n] * dialogBoost);
                tLs[n] = l * surroundBalance;
                tRs[n] = r * surroundBalance;
                tL[n]  = l * frontWeight;
                tR[n]  = r * frontWeight;
            }
        }

        float delayMs      = apvts.getRawParameterValue ("surroundDelay")->load();
        float delaySamples = delayMs * (getSampleRate() / 1000.0f);
        surroundDelayLine.setDelay (delaySamples);

        juce::dsp::AudioBlock<float> fullBlock (tmpOut);
        juce::dsp::AudioBlock<float> surroundBlock = fullBlock.getSubsetChannelBlock (4, 2);
        juce::dsp::ProcessContextReplacing<float> delayCtx (surroundBlock);
        surroundDelayLine.process (delayCtx);

        if (compAmount > 0.01f)
        {
            juce::dsp::AudioBlock<float> centerBlock = fullBlock.getSubsetChannelBlock (2, 1);
            juce::dsp::ProcessContextReplacing<float> compCtx (centerBlock);
            centerCompressor.process (compCtx);
        }

        for (int n = 0; n < numSamples; ++n)
        {
            float monoBass = 0.5f * (lpL[n] + lpR[n]);
            outLFE[n] = monoBass * lfeGain;
            outL[n]   = tL[n] + lpL[n];
            outR[n]   = tR[n] + lpR[n];
            outC[n]   = tC[n];
            outLs[n]  = tLs[n];
            outRs[n]  = tRs[n];
        }
    }

    if (boostActive)
        buffer.applyGain (juce::Decibels::decibelsToGain (6.0f));

    juce::dsp::AudioBlock<float> outBlock (buffer);
    juce::dsp::ProcessContextReplacing<float> limitCtx (outBlock);
    outputLimiter.process (limitCtx);

    rmsLevelLeft.store   (buffer.getMagnitude (0, 0, numSamples));
    rmsLevelRight.store  (buffer.getMagnitude (1, 0, numSamples));
    rmsLevelCenter.store (buffer.getMagnitude (2, 0, numSamples));
    rmsLevelLFE.store    (buffer.getMagnitude (3, 0, numSamples));
    rmsLevelLs.store     (buffer.getMagnitude (4, 0, numSamples));
    rmsLevelRs.store     (buffer.getMagnitude (5, 0, numSamples));
}


//==============================================================================
bool CoherentUpmixAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* CoherentUpmixAudioProcessor::createEditor() { return new CoherentUpmixAudioProcessorEditor (*this); }

//==============================================================================
void CoherentUpmixAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void CoherentUpmixAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// HIER WAR DAS PROBLEM: Diese Funktion muss da sein!
//==============================================================================
void CoherentUpmixAudioProcessor::processNeo6Band(const float* inL, const float* inR, int numSamples,
                                                  float* outL, float* outR, float* outC,
                                                  float* outLs, float* outRs,
                                                  float surroundGain, float centerWidth,
                                                  float& steerState)
{
    const float alpha = 0.9995f;
    for (int n = 0; n < numSamples; ++n)
    {
        float l = inL[n];
        float r = inR[n];
        float sum = (l + r) * 0.707f;
        float diff = (l - r) * 0.707f;
        float absSum = std::abs(sum);
        float absDiff = std::abs(diff) + 0.0001f;
        float targetSteer = (absSum - absDiff) / (absSum + absDiff);
        
        steerState = (steerState * alpha) + (targetSteer * (1.0f - alpha));
        float smoothSteer = steerState;

        float cGain = 0.0f; float sGain = 0.0f; float lrGain = 1.0f;
        if (smoothSteer > 0.0f) { cGain = smoothSteer; lrGain = 1.0f - smoothSteer; sGain = 0.0f; }
        else { sGain = -smoothSteer; lrGain = 1.0f - sGain; cGain = 0.0f; }

        if (centerWidth > 0.0f && cGain > 0.0f) {
            float bleed = cGain * centerWidth;
            cGain -= bleed;
            lrGain += bleed;
        }

        outC[n] += sum * cGain;
        outLs[n] += diff * sGain * surroundGain;
        outRs[n] += -diff * sGain * surroundGain;
        outL[n] += l * lrGain;
        outR[n] += r * lrGain;
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CoherentUpmixAudioProcessor();
}
