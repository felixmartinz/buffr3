#pragma once
#include <JuceHeader.h>

class Buffr3AudioProcessor : public juce::AudioProcessor
{
public:
    // ===== Construction =====
    Buffr3AudioProcessor();
    ~Buffr3AudioProcessor() override = default;

    // ===== Standard JUCE overrides =====
    const juce::String getName() const override            { return "Buffr3"; }
    bool acceptsMidi() const override                      { return true; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 4.0; }

    // Layout supports mono/stereo
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    // Buses
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // Lifecycle
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override                       {}

    // Audio + MIDI
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // State
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ===== Parameters =====
    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS& getAPVTS()                                      { return apvts; }

    // Expose read-only info to editor
    float getCurrentLoopMs() const                         { return currentLoopSamples / (float) sampleRate * 1000.0f; }
    float getPendingLoopMs() const                         { return pendingLoopSamples / (float) sampleRate * 1000.0f; }
    bool  isLoopingActive() const                          { return looping.load(); }
    float getLoopEnv() const                               { return loopEnv.getCurrentValue(); }
    float getPassthroughEnv() const                        { return 1.0f - passthroughMuteEnv.getCurrentValue(); }

    const juce::AudioBuffer<float>& getRecordBuffer() const { return recBuffer; } // continuous recorder (4s)
    const juce::AudioBuffer<float>& getSnapshotBuffer() const { return snapBuffer; } // frozen at trigger
    int  getRecorderWritePos() const                       { return recWritePos.load(); }
    int  getSnapshotEndPos() const                         { return snapEndPos; } // end is "most recent" in snapshot
    float getMeterPassthrough() const { return meterPassthrough; }
    float getMeterLoop() const { return meterLoop; }

    // WAV handling
    bool hasUserSample() const                             { return userSampleLoaded; }
    void clearUserSample();
    void loadWavFile (const juce::File& file, juce::String& error);

    // On-screen keyboard MIDI (UI->DSP)
    juce::MidiMessageCollector& getKeyboardCollector()     { return keyboardCollector; }

private:
    // ===== Parameter layout =====
    static APVTS::ParameterLayout createLayout();

    // ===== Core engine =====
    void writeToRecorder (const juce::AudioBuffer<float>& in);
    void snapshotRecorder (int latencyCompSamples);
    void computePendingLoopFromControls (int numSamples);
    void advanceLoopPlayback (juce::AudioBuffer<float>& out, int numSamples);
    void mixPassthrough (juce::AudioBuffer<float>& inout, int numSamples);

    // MIDI helpers
    void handleMidi (juce::MidiBuffer& midi, int numSamples);
    static double midiNoteToHz (int midiNote)              { return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0); }
    static double semitoneShiftToRatio (double semis)      { return std::pow (2.0, semis / 12.0); }

    // Squeeze mapping: 0→1337 ms, 100→0.14 ms, 30→330.514 ms (gamma tuned)
    static double squeezeToMs (float squeeze01)
    {
        constexpr double minMs = 0.14;
        constexpr double maxMs = 1337.0;
        constexpr double gamma = 1.562; // calibrated to hit 330.514 ms at 0.30
        const double t = std::pow (juce::jlimit (0.0, 1.0, (double) squeeze01), gamma);
        return std::exp (std::log (maxMs) + t * (std::log (minMs) - std::log (maxMs)));
    }

    // ===== State =====
    APVTS apvts { *this, nullptr, "PARAMS", createLayout() };

    // Recorder (always running, 4 s)
    juce::AudioBuffer<float> recBuffer;
    std::atomic<int> recWritePos { 0 };

    // Snapshot taken at trigger
    juce::AudioBuffer<float> snapBuffer;
    int   snapEndPos = 0;  // "most recent" end within snapshot
    bool  userSampleLoaded = false; // if true, snapshot copies from user sample instead of recorder

    // Loop playback
    std::atomic<bool> looping { false };
    int   currentLoopSamples = 1;   // strictly > 0
    int   pendingLoopSamples = 1;   // sampled at loop end
    float loopReadPos = 0.0f;       // [0, currentLoopSamples)
    int   xfadeSamples = 0;

    // Envelopes
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> loopEnv;           // loop gain env (start fast, release param)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> passthroughMuteEnv; // 0->muted, 1->full

    // Pitch + portamento
    juce::LinearSmoothedValue<double> glideHz; // continuous target, but only sampled at loop edges
    double lastTargetHz = 440.0;
    std::atomic<float> pitchBendNorm { 0.0f }; // [-1, 1], UI or incoming MIDI maps here
    int notesDown = 0;
    int lastNoteNumber = 60;

    // UI keyboard -> processor MIDI
    juce::MidiMessageCollector keyboardCollector;

    // Meters
    float meterPassthrough = 0.0f;
    float meterLoop = 0.0f;

    // Runtime
    double sampleRate = 44100.0;
    int    maxSamples4s = 44100 * 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Buffr3AudioProcessor)
};
