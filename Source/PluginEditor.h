#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple glowing key look
class GlowKeysLnF : public juce::LookAndFeel_V4
{
public:
    GlowKeysLnF() = default;

    // Matches JUCE's current LookAndFeel hooks for MidiKeyboardComponent
    void drawWhiteNote (int /*midiNoteNumber*/,
                        juce::Graphics& g,
                        juce::Rectangle<float> area,
                        bool isDown,
                        bool isOver,
                        juce::Colour lineColour,
                        juce::Colour textColour) override;

    void drawBlackNote (int /*midiNoteNumber*/,
                        juce::Graphics& g,
                        juce::Rectangle<float> area,
                        bool isDown,
                        bool isOver,
                        juce::Colour noteFillColour) override;

private:
    float whiteKeyRoundedDiff = 8.0f;
    float blackKeyRoundedDiff = 12.0f;
};

// Wave display for recorder/snapshot
class WaveView : public juce::Component, private juce::Timer
{
public:
    WaveView (const Buffr3AudioProcessor& p, bool snapshotView) : proc (p), showSnapshot (snapshotView) { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        const auto& buf = showSnapshot ? proc.getSnapshotBuffer() : proc.getRecordBuffer();
        const int N = buf.getNumSamples(); if (N <= 0) return;

        auto r = getLocalBounds().toFloat().reduced (4.f);
        g.setColour (juce::Colours::dimgrey); g.drawRoundedRectangle (r, 6.f, 1.0f);

        juce::Path p; p.preallocateSpace (4096);
        const float midY = r.getCentreY();
        const int step = juce::jmax (1, N / juce::jmax (1, (int) r.getWidth()));
        const int ch = juce::jmin (1, buf.getNumChannels() - 1);
        const float* data = buf.getReadPointer (juce::jmax (0, ch));

        p.startNewSubPath (r.getX(), midY);
        for (int x = 0, i = 0; x < (int) r.getWidth(); ++x, i += step)
        {
            const int idx = juce::jlimit (0, N - 1, i);
            p.lineTo (r.getX() + (float) x, midY - data[idx] * (r.getHeight() * 0.48f));
        }

        g.setColour (juce::Colours::lightgreen);
        g.strokePath (p, juce::PathStrokeType (1.5f));

        if (showSnapshot && proc.isLoopingActive())
        {
            const float ms = proc.getCurrentLoopMs();
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (ms, 2) + " ms", getLocalBounds().removeFromTop (18), juce::Justification::centredRight, false);
        }
    }

private:
    void timerCallback() override { repaint(); }
    const Buffr3AudioProcessor& proc;
    bool showSnapshot = false;
};

// Forwards on-screen keyboard events into the processor's MidiMessageCollector
struct KBForwarder : public juce::MidiKeyboardStateListener
{
    explicit KBForwarder (Buffr3AudioProcessor& p) : proc (p) {}

    void handleNoteOn (juce::MidiKeyboardState*, int chan, int note, float vel) override
    {
        auto m = juce::MidiMessage::noteOn (chan, note, (juce::uint8) juce::jlimit (0, 127, (int) std::lround (vel * 127.0f)));
        m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        proc.getKeyboardCollector().addMessageToQueue (m);
    }
    void handleNoteOff (juce::MidiKeyboardState*, int chan, int note, float) override
    {
        auto m = juce::MidiMessage::noteOff (chan, note);
        m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        proc.getKeyboardCollector().addMessageToQueue (m);
    }
    Buffr3AudioProcessor& proc;
};

class Buffr3AudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer,
                                   private juce::FileDragAndDropTarget
{
public:
    explicit Buffr3AudioProcessorEditor (Buffr3AudioProcessor&);
    ~Buffr3AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Drag-and-drop WAV
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    Buffr3AudioProcessor& proc;

    // Controls
    juce::ToggleButton midiEnabled { "MIDI Enabled" };
    juce::ToggleButton hold        { "Hold" };
    juce::ToggleButton useUser     { "Use Loaded WAV" };

    juce::Slider squeeze;       // 0..100 (log mapped internally)
    juce::Slider portamentoMs;
    juce::Slider pbRange;
    juce::Slider playback;
    juce::Slider releaseMs;
    juce::Slider loopGain;
    juce::Slider passGain;
    juce::Slider mix;
    juce::Slider latencyMs;

    juce::TextButton loadBtn { "Load WAV…" };
    juce::Label      dropHint;

    // Keyboard + pitch wheel
    GlowKeysLnF lnf;
    juce::MidiKeyboardState kbState;
    juce::MidiKeyboardComponent keyboard { kbState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::Slider pitchWheel; // -1..+1 springs to center
    std::unique_ptr<KBForwarder> kbForwarder;

    // Displays
    WaveView recView, snapView;

    // Meters
    juce::ProgressBar meterPass, meterLoop;
    double meterPassVal = 0.0, meterLoopVal = 0.0;

    // Attachments
    using Attach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<Attach> aSqueeze, aPort, aPbRange, aPlayback, aRelease, aLoopGain, aPassGain, aMix, aLat;
    std::unique_ptr<BAttach> aMidiEn, aHold, aUseUser;

    void timerCallback() override;
};
