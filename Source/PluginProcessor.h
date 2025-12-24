/*
==============================================================================
    PluginProcessor.h
==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class CoherentUpmixAudioProcessor  : public juce::AudioProcessor
{
public:
    // Enum für die Modi (öffentlich)
    // WICHTIG: Hier muss modeTransient enthalten sein!
    enum ProcessingMode
    {
        modeCoherent = 0,
        modeNeo6,
        modeProLogicII,
        modeTransient, // <-- NEU
        modeDownmix,
        modePassThrough  // ← NEU HINZUFÜGEN
    };

    //==============================================================================
    CoherentUpmixAudioProcessor();
    ~CoherentUpmixAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Öffentlicher Zugriff für Editor
    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
    
    // Metering Values (atomic für Thread-Safety)
    std::atomic<float> rmsLevelLeft { 0.0f };
    std::atomic<float> rmsLevelRight { 0.0f };
    std::atomic<float> rmsLevelCenter { 0.0f };
    std::atomic<float> rmsLevelLFE { 0.0f };
    std::atomic<float> rmsLevelLs { 0.0f };
    std::atomic<float> rmsLevelRs { 0.0f };

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Filter und DSP Objekte (WICHTIG: <float> explizit angeben)
    juce::dsp::LinkwitzRileyFilter<float> lowPassFilter;
    juce::dsp::LinkwitzRileyFilter<float> highPassFilter;
    
    // Neo:6 Filter
    juce::dsp::LinkwitzRileyFilter<float> neo6LowPass;
    juce::dsp::LinkwitzRileyFilter<float> neo6HighPass;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> dialogFilter;
    juce::dsp::Compressor<float> centerCompressor;
    juce::dsp::Limiter<float> outputLimiter;
    juce::dsp::DelayLine<float> surroundDelayLine { 96000 };

    // Helper für Neo:6
    void processNeo6Band(const float* inL, const float* inR, int numSamples,
                         float* outL, float* outR, float* outC,
                         float* outLs, float* outRs,
                         float surroundGain, float centerWidth,
                         float& steerState);

    float steerStateLow = 0.0f;
    float steerStateHigh = 0.0f;

    // WICHTIG: Variablen für Transient Mode hier deklarieren!
    float fastEnvL = 0.0f;
    float slowEnvL = 0.0f;
    float fastEnvR = 0.0f;
    float slowEnvR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoherentUpmixAudioProcessor)
};
