#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class Buffr3AudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::Timer,
                                  public juce::MidiInputCallback,
                                  public juce::MidiKeyboardStateListener,
                                  public juce::FileDragAndDropTarget
{
public:
    Buffr3AudioProcessorEditor (Buffr3AudioProcessor&);
    ~Buffr3AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    
    // MidiInputCallback methods
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;
    
    // MidiKeyboardStateListener methods
    void handleNoteOn (juce::MidiKeyboardState*,
                      int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*,
                       int midiChannel, int midiNoteNumber, float velocity) override;
    
    // FileDragAndDropTarget methods
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    Buffr3AudioProcessor& audioProcessor;
    
    // Waveform display
    juce::Path waveformPath;
    juce::Path loopWaveformPath;
    bool isLooping = false;
    
    // Components
    juce::Slider portamentoSlider;
    juce::Slider pitchBendRangeSlider;
    juce::Slider playbackSpeedSlider;
    juce::Slider releaseSlider;
    juce::Slider squeezeSlider;
    juce::Slider squeezeGainSlider;
    juce::Slider passthroughGainSlider;
    juce::Slider wetDryMixSlider;
    
    juce::ToggleButton holdButton;
    juce::ToggleButton midiDisableButton;
    juce::ToggleButton useWavFileButton;
    
    juce::Label loopDurationLabel;
    juce::Label portamentoLabel;
    juce::Label pitchBendRangeLabel;
    juce::Label playbackSpeedLabel;
    juce::Label releaseLabel;
    juce::Label squeezeLabel;
    juce::Label squeezeGainLabel;
    juce::Label passthroughGainLabel;
    juce::Label wetDryMixLabel;
    
    // File drop area
    juce::Rectangle<int> fileDropArea;
    juce::File loadedFile;
    juce::TextButton loadFileButton;
    
    // MIDI keyboard
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;
    
    // Pitch wheel
    juce::Slider pitchWheel;
    
    // Level meters
    class LevelMeter : public juce::Component, public juce::Timer
    {
    public:
        LevelMeter() { startTimerHz(30); }
        
        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            
            g.setColour(juce::Colours::darkgrey);
            g.fillRoundedRectangle(bounds, 2.0f);
            
            if (level > 0.0f)
            {
                auto fillHeight = bounds.getHeight() * level;
                auto fillBounds = bounds.removeFromBottom(fillHeight);
                
                auto colour = level > 0.9f ? juce::Colours::red :
                             level > 0.7f ? juce::Colours::orange :
                                          juce::Colours::green;
                g.setColour(colour);
                g.fillRoundedRectangle(fillBounds, 2.0f);
            }
        }
        
        void timerCallback() override
        {
            if (currentLevel != targetLevel)
            {
                currentLevel = currentLevel * 0.8f + targetLevel * 0.2f;
                level = currentLevel;
                repaint();
            }
        }
        
        void setLevel(float newLevel)
        {
            targetLevel = juce::jlimit(0.0f, 1.0f, newLevel);
        }
        
    private:
        float level = 0.0f;
        float currentLevel = 0.0f;
        float targetLevel = 0.0f;
    };
    
    LevelMeter passthroughMeter;
    LevelMeter squeezeMeter;
    
    // Background images
    juce::Image backgroundImage;
    juce::Image loopingImage;
    float loopingImageAlpha = 0.0f;
    
    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> portamentoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchBendRangeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> playbackSpeedAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> squeezeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> squeezeGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> passthroughGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetDryMixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiDisableAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> useWavFileAttachment;
    
    void updateWaveformDisplay();
    void loadWavFile(const juce::File& file);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Buffr3AudioProcessorEditor)
};
