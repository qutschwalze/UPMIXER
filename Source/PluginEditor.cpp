/*
==============================================================================
    PluginEditor.cpp
==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CoherentUpmixAudioProcessorEditor::CoherentUpmixAudioProcessorEditor (CoherentUpmixAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      meterL("L"), meterR("R"), meterC("C"), meterLFE("LFE"), meterLs("Ls"), meterRs("Rs")
{
    setLookAndFeel(&modernLook);

    // --- SLIDERS ---
    setupSlider(surroundBalanceSlider, surroundBalanceLabel, "SURROUND");
    setupSlider(lfeAmountSlider, lfeAmountLabel, "LFE");
    setupSlider(dialogExtractSlider, dialogExtractLabel, "DIALOG");
    setupSlider(centerCompSlider, centerCompLabel, "C-COMP");
    setupSlider(delaySlider, delayLabel, "DELAY");

    // --- MODE SELECTION ---
    addAndMakeVisible(modeSelector);
    // WICHTIG: IDs 1-basiert passt zum Attachment (Parameter 0 -> ID 1)
    modeSelector.addItem("Coherent Upmix", 1);
    modeSelector.addItem("Neo6 Mode", 2);
    modeSelector.addItem("Matrix Mode PLII", 3);
    modeSelector.addItem("Modern Transient", 4);  // War bisher "Exact Downmix"
    modeSelector.addItem("Exact Downmix", 5);     // Verschoben
    modeSelector.addItem("5.1 Pass-Through", 6);  // ← NEU HINZUFÜGEN
    
    modeLabel.setText("ALGORITHM", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    modeLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    modeLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    modeLabel.attachToComponent(&modeSelector, false);
    addAndMakeVisible(modeLabel);

    // --- LOUDNESS ---
    loudnessButton.setButtonText("Loudness +6dB");
    loudnessButton.setClickingTogglesState(true);
    addAndMakeVisible(loudnessButton);

    // --- PRESETS ---
    addAndMakeVisible(presetSelector);
    presetSelector.addItem("Default (Neutral)", 1);
    presetSelector.addItem("Action Movie (Bass+)", 2);
    presetSelector.addItem("Interview (Center+)", 3);
    presetSelector.addItem("Ambient (Surround+)", 4);
    
    // Callback definieren
    presetSelector.onChange = [this] { loadPreset(presetSelector.getSelectedId()); };
    
    // FIX: Preset-Anzeige auf "Default" setzen, ABER NICHT LADEN!
    // Wenn wir hier Notification senden würden, würde er sofort alles auf Default zurücksetzen.
    presetSelector.setText("Select Preset...", juce::dontSendNotification);

    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centredRight);
    presetLabel.attachToComponent(&presetSelector, true);
    addAndMakeVisible(presetLabel);

    // --- METERS ---
    addAndMakeVisible(meterL);
    addAndMakeVisible(meterR);
    addAndMakeVisible(meterC);
    addAndMakeVisible(meterLFE);
    addAndMakeVisible(meterLs);
    addAndMakeVisible(meterRs);

    // --- ATTACHMENTS ---
    // Attachments müssen NACH dem Setup der Komponenten erstellt werden.
    // Sie synchronisieren sofort den UI-Status mit dem Parameter-Status.
    auto& vts = audioProcessor.getValueTreeState();

    surroundBalanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "surroundBalance", surroundBalanceSlider);
    lfeAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "lfeAmount", lfeAmountSlider);
    dialogExtractAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "dialogExtract", dialogExtractSlider);
    centerCompAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "centerComp", centerCompSlider);
    delayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(vts, "surroundDelay", delaySlider);
    
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(vts, "processingMode", modeSelector);
    loudnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(vts, "loudnessBoost", loudnessButton);

    setSize (800, 450);
    startTimerHz(60);
}

CoherentUpmixAudioProcessorEditor::~CoherentUpmixAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// ... Rest der Datei (paint, resized, setupSlider, timerCallback) bleibt exakt gleich ...
// Nur loadPreset Funktion ist hier nochmal zur Vollständigkeit, falls du Änderungen prüfen willst:

void CoherentUpmixAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (findColour(juce::ResizableWindow::backgroundColourId));
    juce::Rectangle<float> headerArea (0, 0, getWidth(), 50);
    g.setColour(juce::Colour::fromString("ff181818"));
    g.fillRect(headerArea);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawHorizontalLine(50, 0.0f, (float)getWidth());
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(juce::FontOptions("Roboto", 22.0f, juce::Font::bold)));
    g.drawText("COHERENT UPMIX 5.1", 20, 0, 300, 50, juce::Justification::centredLeft);
    g.setColour(findColour(juce::Slider::thumbColourId));
    g.setFont(14.0f);
    g.drawText("PRO EDITION", 230, 0, 100, 50, juce::Justification::centredLeft);
    auto area = getLocalBounds().toFloat();
    area.removeFromTop(60);
    area.removeFromBottom(60);
    auto rightArea = area.removeFromRight(180);
    auto mainArea = area.reduced(10);
    g.setColour(juce::Colour::fromString("ff222222"));
    g.fillRoundedRectangle(mainArea, 8.0f);
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.drawRoundedRectangle(mainArea, 8.0f, 1.0f);
    auto meterBg = rightArea.reduced(10, 0);
    g.setColour(juce::Colour::fromString("ff121212"));
    g.fillRoundedRectangle(meterBg, 8.0f);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(meterBg, 8.0f, 1.0f);
    g.setColour(juce::Colours::grey);
    g.setFont(12.0f);
    g.drawText("OUTPUT", meterBg.removeFromTop(20), juce::Justification::centred, false);
}

void CoherentUpmixAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(50);
    presetSelector.setBounds(header.removeFromRight(200).reduced(10, 10));
    auto footer = area.removeFromBottom(60).reduced(20, 10);
    int totalFooterWidth = footer.getWidth();
    int selectorWidth = 250;
    int buttonWidth = 150;
    auto leftFooter = footer.removeFromLeft(selectorWidth);
    modeSelector.setBounds(leftFooter.reduced(0, 5));
    footer.removeFromLeft(20);
    loudnessButton.setBounds(footer.removeFromLeft(buttonWidth).reduced(0, 5));
    auto meterArea = area.removeFromRight(180).reduced(20, 20);
    meterArea.removeFromTop(20);
    int meterWidth = meterArea.getWidth() / 6;
    meterL.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    meterR.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    meterC.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    meterLFE.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    meterLs.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    meterRs.setBounds(meterArea.removeFromLeft(meterWidth).reduced(2, 0));
    auto sliderArea = area.reduced(20);
    int sliderW = sliderArea.getWidth() / 5;
    surroundBalanceSlider.setBounds(sliderArea.removeFromLeft(sliderW).reduced(5));
    lfeAmountSlider.setBounds(sliderArea.removeFromLeft(sliderW).reduced(5));
    dialogExtractSlider.setBounds(sliderArea.removeFromLeft(sliderW).reduced(5));
    centerCompSlider.setBounds(sliderArea.removeFromLeft(sliderW).reduced(5));
    delaySlider.setBounds(sliderArea.removeFromLeft(sliderW).reduced(5));
}

void CoherentUpmixAudioProcessorEditor::setupSlider (juce::Slider& slider, juce::Label& label, const juce::String& name)
{
    slider.setSliderStyle (juce::Slider::LinearVertical);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.8f));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont(juce::Font(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colours::grey);
    label.attachToComponent (&slider, false);
    addAndMakeVisible (label);
}

void CoherentUpmixAudioProcessorEditor::timerCallback()
{
    meterL.setLevel(audioProcessor.rmsLevelLeft.load());
    meterR.setLevel(audioProcessor.rmsLevelRight.load());
    meterC.setLevel(audioProcessor.rmsLevelCenter.load());
    meterLFE.setLevel(audioProcessor.rmsLevelLFE.load());
    meterLs.setLevel(audioProcessor.rmsLevelLs.load());
    meterRs.setLevel(audioProcessor.rmsLevelRs.load());
}

void CoherentUpmixAudioProcessorEditor::loadPreset(int id)
{
    auto& params = audioProcessor.getValueTreeState();
    
    // Wir nutzen hier parameter IDs und keine direkten Pointer, um Thread-Safety zu wahren
    // convertTo0to1 ist hilfreich, da setValueNotifyingHost normalisierte Werte (0..1) erwartet.
    
    switch (id)
    {
        case 1: // Default
            params.getParameter("surroundBalance")->setValueNotifyingHost(0.5f);
            // LFE range ist -60 bis 0. -12dB ist ca 0.8
            // Am sichersten ist es, den Parameter direkt nach seinem Range zu fragen, aber das ist hier im Editor schwer.
            // Wir schätzen grob oder nutzen Helper im Processor. Hier hardcoded Werte (normalisiert):
            // LFE -12dB bei Range -60..0 -> (-12 - -60) / 60 = 48/60 = 0.8
            params.getParameter("lfeAmount")->setValueNotifyingHost(0.8f);
            params.getParameter("dialogExtract")->setValueNotifyingHost(0.0f);
            params.getParameter("centerComp")->setValueNotifyingHost(0.0f);
            
            // WICHTIG: Mode auch zurücksetzen? Ja, beim Default Preset macht das Sinn.
            // ParameterChoice mappt 0..1 auf die Indices.
            // 4 Items -> Steps sind 0.0, 0.33, 0.66, 1.0 (ca.)
            // Sicherer: convertFrom0to1 nutzen, aber das geht nur im Processor leicht.
            // Bei Choice Parameter:
            // Index 0 = 0.0f
            // Index 1 = 1.0f / (numItems-1) * 1
            // Bei 4 Items: 0/3, 1/3, 2/3, 3/3
            params.getParameter("processingMode")->setValueNotifyingHost(0.0f);
            break;
            
        case 2: // Action Movie
            params.getParameter("surroundBalance")->setValueNotifyingHost(0.6f);
            // LFE -3dB -> 57/60 = 0.95
            params.getParameter("lfeAmount")->setValueNotifyingHost(0.95f);
            params.getParameter("centerComp")->setValueNotifyingHost(0.5f);
            // Mode lassen wir vielleicht so wie er ist, oder erzwingen Coherent?
            // params.getParameter("processingMode")->setValueNotifyingHost(0.0f);
            break;
            
        case 3: // Interview
            params.getParameter("surroundBalance")->setValueNotifyingHost(0.2f);
            params.getParameter("dialogExtract")->setValueNotifyingHost(0.8f);
            params.getParameter("centerComp")->setValueNotifyingHost(0.8f);
            break;
            
        case 4: // Ambient
            params.getParameter("surroundBalance")->setValueNotifyingHost(0.85f);
            // LFE -8dB -> 52/60 = 0.866
            params.getParameter("lfeAmount")->setValueNotifyingHost(0.866f);
            params.getParameter("dialogExtract")->setValueNotifyingHost(0.0f);
            params.getParameter("centerComp")->setValueNotifyingHost(0.0f);
            break;
    }
}
