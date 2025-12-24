/*
==============================================================================
    PluginEditor.h
==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour::fromString("ff1e1e1e"));
        setColour(juce::Slider::thumbColourId, juce::Colour::fromString("ff00b5ff"));
        setColour(juce::Slider::backgroundColourId, juce::Colour::fromString("ff111111"));
        setColour(juce::Slider::trackColourId, juce::Colour::fromString("ff00b5ff").withAlpha(0.3f));
        setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
        setColour(juce::TextButton::buttonColourId, juce::Colour::fromString("ff2b2b2b"));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromString("ff00b5ff"));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromString("ff121212"));
        setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.1f));
        setColour(juce::ComboBox::arrowColourId, juce::Colours::white.withAlpha(0.8f));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour::fromString("ff181818"));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        auto trackWidth = 4.0f;
        auto centerX = (float)x + (float)width * 0.5f;
        auto trackRect = juce::Rectangle<float>(centerX - trackWidth * 0.5f, (float)y, trackWidth, (float)height);

        g.setColour(findColour(juce::Slider::backgroundColourId));
        g.fillRoundedRectangle(trackRect, 2.0f);
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(trackRect, 2.0f, 1.0f);

        auto thumbWidth = 24.0f;
        auto thumbHeight = 14.0f;
        auto thumbY = sliderPos - (thumbHeight * 0.5f);
        juce::Rectangle<float> thumbBounds(centerX - thumbWidth * 0.5f, thumbY, thumbWidth, thumbHeight);

        g.setColour(juce::Colour::fromString("ff333333"));
        g.fillRoundedRectangle(thumbBounds, 2.0f);
        g.setColour(findColour(juce::Slider::thumbColourId));
        g.fillRect(thumbBounds.getX(), thumbBounds.getCentreY() - 1.0f, thumbBounds.getWidth(), 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(thumbBounds, 2.0f, 1.0f);
    }
};

//==============================================================================
class ProfessionalMeter : public juce::Component
{
public:
    ProfessionalMeter(juce::String name) : labelText(name) {}

    void setLevel(float linearLevel)
    {
        float db = juce::Decibels::gainToDecibels(linearLevel, -60.0f);
        targetLevel = (db + 60.0f) / 66.0f;
        if (targetLevel < 0.0f) targetLevel = 0.0f;
        if (targetLevel > 1.0f) targetLevel = 1.0f;
        currentLevel = 0.7f * currentLevel + 0.3f * targetLevel;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto labelArea = area.removeFromBottom(15);
        g.setColour(juce::Colours::grey);
        g.setFont(10.0f);
        g.drawText(labelText, labelArea, juce::Justification::centred, false);

        auto meterArea = area.reduced(4, 0);
        g.setColour(juce::Colour::fromString("ff0a0a0a"));
        g.fillRoundedRectangle(meterArea, 2.0f);

        if (currentLevel > 0.001f)
        {
            float height = meterArea.getHeight() * currentLevel;
            auto barBounds = meterArea.removeFromBottom(height);
            juce::ColourGradient grad(
                juce::Colour::fromString("ff005f7f"), barBounds.getBottomLeft(),
                juce::Colour::fromString("ff00d5ff"), barBounds.getTopLeft(), false);
            grad.addColour(0.8, juce::Colour::fromString("ff00ffff"));
            grad.addColour(0.95, juce::Colours::white);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(barBounds, 2.0f);
        }
    }

private:
    juce::String labelText;
    float currentLevel = 0.0f;
    float targetLevel = 0.0f;
};

//==============================================================================
class CoherentUpmixAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    CoherentUpmixAudioProcessorEditor (CoherentUpmixAudioProcessor&);
    ~CoherentUpmixAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    CoherentUpmixAudioProcessor& audioProcessor;
    ModernLookAndFeel modernLook;

    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& name);
    void loadPreset(int id);

    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    juce::Slider surroundBalanceSlider;
    juce::Slider lfeAmountSlider;
    juce::Slider dialogExtractSlider;
    juce::Slider centerCompSlider;
    juce::Slider delaySlider;

    juce::Label surroundBalanceLabel;
    juce::Label lfeAmountLabel;
    juce::Label dialogExtractLabel;
    juce::Label centerCompLabel;
    juce::Label delayLabel;

    juce::ComboBox modeSelector;
    juce::Label modeLabel;

    juce::TextButton loudnessButton;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> surroundBalanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfeAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dialogExtractAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> centerCompAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loudnessAttachment;

    // Meter werden im Constructor initialisiert
    ProfessionalMeter meterL;
    ProfessionalMeter meterR;
    ProfessionalMeter meterC;
    ProfessionalMeter meterLFE;
    ProfessionalMeter meterLs;
    ProfessionalMeter meterRs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoherentUpmixAudioProcessorEditor)
};
