#include "PluginEditor.h"
#include <cmath>

using namespace juce;

static void styleKnob (Slider& s)
{
    s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (Slider::TextBoxBelow, true, 64, 18);
}

Buffr3AudioProcessorEditor::Buffr3AudioProcessorEditor (Buffr3AudioProcessor& p)
: AudioProcessorEditor (&p), proc (p),
  recView (p, false), snapView (p, true),
  meterPass (meterPassVal), meterLoop (meterLoopVal)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (980, 560);

    // === Controls ===
    addAndMakeVisible (midiEnabled);  aMidiEn  = std::make_unique<BAttach> (proc.getAPVTS(), "midiEnabled", midiEnabled);
    addAndMakeVisible (hold);         aHold    = std::make_unique<BAttach> (proc.getAPVTS(), "hold", hold);
    addAndMakeVisible (useUser);      aUseUser = std::make_unique<BAttach> (proc.getAPVTS(), "useUserSample", useUser);

    squeeze.setTextValueSuffix (" %"); styleKnob (squeeze);
    portamentoMs.setTextValueSuffix (" ms"); styleKnob (portamentoMs);
    pbRange.setTextValueSuffix (" st"); styleKnob (pbRange);
    playback.setTextValueSuffix (" x"); styleKnob (playback);
    releaseMs.setTextValueSuffix (" ms"); styleKnob (releaseMs);
    loopGain.setTextValueSuffix (" x"); styleKnob (loopGain);
    passGain.setTextValueSuffix (" x"); styleKnob (passGain);
    mix.setTextValueSuffix (""); styleKnob (mix);
    latencyMs.setTextValueSuffix (" ms"); styleKnob (latencyMs);

    addAndMakeVisible (squeeze);     aSqueeze  = std::make_unique<Attach> (proc.getAPVTS(), "squeeze", squeeze);
    addAndMakeVisible (portamentoMs);aPort     = std::make_unique<Attach> (proc.getAPVTS(), "portamentoMs", portamentoMs);
    addAndMakeVisible (pbRange);     aPbRange  = std::make_unique<Attach> (proc.getAPVTS(), "pitchBendRange", pbRange);
    addAndMakeVisible (playback);    aPlayback = std::make_unique<Attach> (proc.getAPVTS(), "playbackSpeed", playback);
    addAndMakeVisible (releaseMs);   aRelease  = std::make_unique<Attach> (proc.getAPVTS(), "releaseMs", releaseMs);
    addAndMakeVisible (loopGain);    aLoopGain = std::make_unique<Attach> (proc.getAPVTS(), "loopGain", loopGain);
    addAndMakeVisible (passGain);    aPassGain = std::make_unique<Attach> (proc.getAPVTS(), "passGain", passGain);
    addAndMakeVisible (mix);         aMix      = std::make_unique<Attach> (proc.getAPVTS(), "mix", mix);
    addAndMakeVisible (latencyMs);   aLat      = std::make_unique<Attach> (proc.getAPVTS(), "latencyCompMs", latencyMs);

    // Load WAV
    addAndMakeVisible (loadBtn);
    loadBtn.onClick = [this]
    {
        juce::FileChooser chooser ("Load WAV (will be cropped/padded to 4 s)", {}, "*.wav");
        chooser.launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this] (const juce::FileChooser& fc)
                             {
                                 auto file = fc.getResult();
                                 if (file.existsAsFile())
                                 {
                                     juce::String err;
                                     proc.loadWavFile (file, err);
                                     if (err.isNotEmpty())
                                         juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                                  "Load WAV", err);
                                 }
                             });
    };

    dropHint.setText ("Drop WAV here", dontSendNotification);
    dropHint.setJustificationType (Justification::centred);

    // Displays
    addAndMakeVisible (recView);
    addAndMakeVisible (snapView);

    // Keyboard + pitch wheel
    keyboard.setScrollButtonsVisible (false);
    keyboard.setKeyPressBaseOctave (3);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setAvailableRange (24, 108);
    keyboard.setColour (MidiKeyboardComponent::keyDownOverlayColourId, Colours::cyan.withAlpha (0.35f));
    addAndMakeVisible (keyboard);
    keyboard.setLookAndFeel (&glowKeysLnf);

    // Route on-screen keyboard to the processor
    kbForwarder = std::make_unique<KBForwarder> (proc);
    kbState.addListener (kbForwarder.get());

    addAndMakeVisible (pitchWheel);
    pitchWheel.setSliderStyle (Slider::LinearVertical);
    pitchWheel.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    pitchWheel.setRange (-1.0, 1.0, 0.0001);
    pitchWheel.onValueChange = [this]
    {
        const int value = juce::jlimit (0, 16383, (int) std::lround ((pitchWheel.getValue() * 8192.0) + 8192.0));
        auto m = juce::MidiMessage::pitchWheel (1, value);
        m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        proc.getKeyboardCollector().addMessageToQueue (m);
    };
    pitchWheel.onDragEnd = [this] { pitchWheel.setValue (0.0, dontSendNotification); };

    // Meters
    addAndMakeVisible (meterPass);
    addAndMakeVisible (meterLoop);

    startTimerHz (30);
}

void GlowKeysLnF::drawWhiteNote (int midiNoteNumber,
                                 juce::Graphics& g,
                                 juce::Rectangle<float> area,
                                 bool isDown,
                                 bool isOver,
                                 juce::Colour lineColour,
                                 juce::Colour textColour,
                                 juce::MidiKeyboardComponent& /*keyboard*/)
{
    // Base key
    g.setColour (juce::Colours::white);
    g.fillRect (area);

    // Key border
    g.setColour (lineColour);
    g.drawRect (area, 1.0f);

    // “Glow” overlay on hover/down
    if (isOver || isDown)
    {
        auto glow = area.reduced (area.getWidth() * 0.1f, area.getHeight() * 0.2f);
        g.setColour (glowColour);
        g.fillEllipse (glow);
    }

    // Optional label (note name)
    g.setColour (textColour);
    g.setFont (12.0f);
    const auto name = juce::MidiMessage::getMidiNoteName (midiNoteNumber, true, true, 4);
    g.drawFittedText (name, area.toNearestInt(), juce::Justification::centredBottom, 1);
}

void GlowKeysLnF::drawBlackNote (int /*midiNoteNumber*/,
                                 juce::Graphics& g,
                                 juce::Rectangle<float> area,
                                 bool isDown,
                                 bool isOver,
                                 juce::Colour noteFillColour,
                                 juce::MidiKeyboardComponent& /*keyboard*/)
{
    // Base key
    g.setColour (noteFillColour);
    g.fillRect (area);

    // Subtle highlight for glow
    if (isOver || isDown)
    {
        auto glow = area.reduced (area.getWidth() * 0.15f, area.getHeight() * 0.25f);
        g.setColour (glowColour);
        g.fillEllipse (glow);
    }

    // Edge
    g.setColour (juce::Colours::black);
    g.drawRect (area, 1.0f);
}

Buffr3AudioProcessorEditor::~Buffr3AudioProcessorEditor()
{
    kbState.removeListener (kbForwarder.get());
    keyboard.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

bool Buffr3AudioProcessorEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".wav")) return true;
    return false;
}

void Buffr3AudioProcessorEditor::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".wav"))
        {
            String err; proc.loadWavFile (File (f), err);
            if (err.isNotEmpty()) AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon, "Load WAV", err);
            break;
        }
}

void Buffr3AudioProcessorEditor::timerCallback()
{
    // Show real RMS meters exposed by the processor
    meterPassVal = proc.getMeterPassthrough();
    meterLoopVal = proc.getMeterLoop();
    repaint();
}

void Buffr3AudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Colours::black);

    // Overlay image placeholder driven by loop envelope
    const float overlayAlpha = proc.getLoopEnv();
    if (overlayAlpha > 0.01f)
    {
        g.setColour (Colours::purple.withAlpha (overlayAlpha * 0.6f));
        auto r = getLocalBounds().toFloat();
        g.fillRect (r.withBottom (r.getY() + r.getHeight() * 0.75f));
    }

    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRect (getLocalBounds());
}

void Buffr3AudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    // Top: displays
    auto top = r.removeFromTop (r.getHeight() * 0.30f);
    recView.setBounds (top.removeFromLeft (top.getWidth() / 2).reduced (8));
    snapView.setBounds (top.reduced (8));

    // Mid: controls
    auto mid = r.removeFromTop (r.getHeight() * 0.30f).reduced (8);
    auto left = mid.removeFromLeft (mid.getWidth() * 0.60f);
    auto right = mid;

    auto grid = left.reduced (8);
    const int knobW = 110, knobH = 88;

    midiEnabled.setBounds (grid.removeFromTop (22)); grid.removeFromTop (8);
    hold.setBounds       (grid.removeFromTop (22)); grid.removeFromTop (8);
    useUser.setBounds    (grid.removeFromTop (22)); grid.removeFromTop (8);
    loadBtn.setBounds    (grid.removeFromTop (26)); grid.removeFromTop (8);
    dropHint.setBounds   (grid.removeFromTop (18));

    squeeze.setBounds    (right.removeFromLeft (knobW).removeFromTop (knobH));
    portamentoMs.setBounds(right.removeFromLeft (knobW).removeFromTop (knobH));
    pbRange.setBounds    (right.removeFromLeft (knobW).removeFromTop (knobH));
    playback.setBounds   (right.removeFromLeft (knobW).removeFromTop (knobH));
    releaseMs.setBounds  (right.removeFromLeft (knobW).removeFromTop (knobH));
    loopGain.setBounds   (right.removeFromLeft (knobW).removeFromTop (knobH));
    passGain.setBounds   (right.removeFromLeft (knobW).removeFromTop (knobH));
    mix.setBounds        (right.removeFromLeft (knobW).removeFromTop (knobH));
    latencyMs.setBounds  (right.removeFromLeft (knobW).removeFromTop (knobH));

    // Bottom: meters + keyboard + wheel
    auto bottom = r.reduced (8);
    auto leftK = bottom.removeFromLeft (70);
    pitchWheel.setBounds (leftK.withTrimmedTop (20));

    auto meters = bottom.removeFromTop (22);
    meterPass.setBounds (meters.removeFromLeft (bottom.getWidth()/2).reduced (4));
    meterLoop.setBounds (meters.reduced (4));

    keyboard.setBounds (bottom.withTrimmedTop (12));
}
