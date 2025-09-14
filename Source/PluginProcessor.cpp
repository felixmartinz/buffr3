#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring> // for std::memcpy used in snapshotRecorder()

using namespace juce;

// ===================== Parameter layout =====================
static String param (const char* id) { return id; }

AudioProcessorValueTreeState::ParameterLayout Buffr3AudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterBool>  (param("midiEnabled"),       "MIDI Enabled", true));
    params.push_back (std::make_unique<AudioParameterBool>  (param("hold"),              "Hold", false));
    params.push_back (std::make_unique<AudioParameterFloat> (param("squeeze"),           "Squeeze", NormalisableRange<float>(0.f, 100.f, 0.f, 1.f), 30.f));
    params.push_back (std::make_unique<AudioParameterFloat> (param("portamentoMs"),      "Portamento (ms)", NormalisableRange<float>(0.f, 2000.f, 0.01f), 60.f));
    params.push_back (std::make_unique<AudioParameterInt>   (param("pitchBendRange"),    "Pitch Bend Range (st)", 0, 48, 2));
    params.push_back (std::make_unique<AudioParameterFloat> (param("playbackSpeed"),     "Playback Speed", NormalisableRange<float>(0.5f, 2.0f, 0.0001f), 1.0f));
    params.push_back (std::make_unique<AudioParameterFloat> (param("releaseMs"),         "Release (ms)", NormalisableRange<float>(30.f, 4000.f, 0.01f), 30.f));
    params.push_back (std::make_unique<AudioParameterFloat> (param("loopGain"),          "Squeeze Gain", NormalisableRange<float>(0.f, 2.0f, 0.0001f), 1.0f));
    params.push_back (std::make_unique<AudioParameterFloat> (param("passGain"),          "Passthrough Gain", NormalisableRange<float>(0.f, 2.0f, 0.0001f), 1.0f));
    params.push_back (std::make_unique<AudioParameterFloat> (param("mix"),               "Wet / Dry", NormalisableRange<float>(0.f, 1.0f, 0.0001f), 1.0f));
    params.push_back (std::make_unique<AudioParameterBool>  (param("useUserSample"),     "Use Loaded WAV", false));
    params.push_back (std::make_unique<AudioParameterFloat> (param("latencyCompMs"),     "Latency Comp (ms)", NormalisableRange<float>(0.f, 200.f, 0.01f), 0.f));

    return { params.begin(), params.end() };
}

// ===================== Construction =====================
Buffr3AudioProcessor::Buffr3AudioProcessor()
: AudioProcessor (BusesProperties()
                  .withInput  ("Input",  AudioChannelSet::stereo(), true)
                  .withOutput ("Output", AudioChannelSet::stereo(), true))

bool Buffr3AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in.isDisabled() || out.isDisabled()) return false;
    if (!(in == AudioChannelSet::mono() || in == AudioChannelSet::stereo())) return false;
    return in == out;
}

// ===================== Prepare / Release =====================
void Buffr3AudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    maxSamples4s = (int) std::ceil (sampleRate * 4.0);
    xfadeSamples = std::max (1, (int) std::round (0.003 * sampleRate)); // 3 ms seam xfade

    recBuffer.setSize (std::max (1, getTotalNumInputChannels()), maxSamples4s);
    recBuffer.clear();
    snapBuffer.setSize (std::max (1, getTotalNumInputChannels()), maxSamples4s);
    snapBuffer.clear();

    recWritePos = 0;
    snapEndPos = 0;

    currentLoopSamples = 1;
    pendingLoopSamples = 1;
    loopReadPos = 0.f;

    // Envelopes
    loopEnv.reset (sampleRate, 0.03);           // start 30 ms fade-in
    loopEnv.setCurrentAndTargetValue (0.f);
    passthroughMuteEnv.reset (sampleRate, 0.03);// passthrough fades out over 30 ms at start, releaseMs on stop
    passthroughMuteEnv.setCurrentAndTargetValue (1.f); // 1 == full passthrough, 0 == muted

    // Portamento smoother (ms -> seconds)
    glideHz.reset (sampleRate, 0.001);
    glideHz.setCurrentAndTargetValue (440.0);

    keyboardCollector.reset (sampleRate);
}

void Buffr3AudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // save parameters
    MemoryOutputStream mos (destData, false);
    apvts.state.writeToStream (mos);

    // Save user sample if present (raw float interleaved per channel)
    MemoryBlock audioBlock;
    if (userSampleLoaded)
    {
        MemoryOutputStream aos (audioBlock, false);
        aos.writeInt (snapBuffer.getNumChannels());
        aos.writeInt (snapBuffer.getNumSamples());
        for (int ch = 0; ch < snapBuffer.getNumChannels(); ++ch)
            aos.write (snapBuffer.getReadPointer (ch), sizeof(float) * (size_t) snapBuffer.getNumSamples());
    }
    mos.writeBool (userSampleLoaded);
    mos.writeInt  ((int) audioBlock.getSize());
    mos.write (audioBlock.getData(), audioBlock.getSize());
}

void Buffr3AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    MemoryInputStream mis (data, (size_t) sizeInBytes, false);
    auto tree = juce::ValueTree::readFromStream (mis);
    if (tree.isValid())
        apvts.replaceState (tree);

    const bool hadUserSample = mis.readBool();
    const int  blobSize      = mis.readInt();
    if (hadUserSample && blobSize > 0)
    {
        HeapBlock<char> tmp (blobSize);
        mis.read (tmp.getData(), (size_t) blobSize);
        MemoryInputStream aos (tmp.getData(), (size_t) blobSize, false);
        const int ch   = aos.readInt();
        const int nSam = aos.readInt();
        snapBuffer.setSize (std::max (1, ch), std::min (nSam, maxSamples4s));
        for (int c = 0; c < snapBuffer.getNumChannels(); ++c)
            aos.read (snapBuffer.getWritePointer (c), sizeof(float) * (size_t) snapBuffer.getNumSamples());
        userSampleLoaded = true;
    }
}

// ===================== WAV loading =====================
void Buffr3AudioProcessor::clearUserSample()
{
    userSampleLoaded = false;
}

void Buffr3AudioProcessor::loadWavFile (const File& file, String& error)
{
    error.clear();
    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader)
    {
        error = "Unsupported or unreadable audio file.";
        return;
    }

    const double lengthSec = (double) reader->lengthInSamples / reader->sampleRate;
    const int    targetSamples = std::min (maxSamples4s, (int) std::ceil (sampleRate * 4.0));

    AudioBuffer<float> tmp ((int) reader->numChannels, targetSamples);
    tmp.clear();

    // Read up to 4 seconds, crop if longer, pad with silence if shorter
    const int toRead = std::min ((int) reader->lengthInSamples, targetSamples);
    reader->read (&tmp, 0, toRead, (int64) std::max<int64> (0, reader->lengthInSamples - toRead), true, true);

    // If the source sr != session sr, JUCE reader already resamples only if it's an AudioTransportSource;
    // so here we just copy raw, it's fine for a snapshot source content.
    snapBuffer = tmp;
    snapEndPos = snapBuffer.getNumSamples();
    userSampleLoaded = true;
}

// ===================== Process =====================
void Buffr3AudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const ScopedNoDenormals _noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = std::min (buffer.getNumChannels(), recBuffer.getNumChannels());

    // Merge UI keyboard MIDI into midi buffer (no MIDI out)
    MidiBuffer uiMidi;
    keyboardCollector.removeNextBlockOfMessages (uiMidi, numSamples);
    for (const auto meta : uiMidi) midi.addEvent (meta.getMessage(), meta.samplePosition);

    // Always write input into the 4s recorder (before we mute passthrough)
    writeToRecorder (buffer);

    // Handle incoming MIDI & compute pending loop settings
    handleMidi (midi, numSamples);
    computePendingLoopFromControls (numSamples);

    // Synthesize loop and mix/mute passthrough
    AudioBuffer<float> loopOut (numCh, numSamples);
    loopOut.clear();
    if (looping.load())
        advanceLoopPlayback (loopOut, numSamples);

    // Passthrough muting envelope (30ms fade out on loop start, release on stop)
    mixPassthrough (buffer, numSamples);

    // Compose wet/dry:
    const float loopGain  = *apvts.getRawParameterValue ("loopGain");
    const float passGain  = *apvts.getRawParameterValue ("passGain");
    const float mix       = *apvts.getRawParameterValue ("mix");

    // Wet = (muted-passthrough) + loop
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* dry  = buffer.getReadPointer (ch);
        auto* wet  = buffer.getWritePointer (ch);
        auto* loop = loopOut.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float wetSig  = (wet[i] * passGain) + (loop[i] * loopGain * loopEnv.getNextValue());
            const float out     = dry[i] * (1.0f - mix) + wetSig * mix;
            wet[i] = out;
        }
    }

    // Meters (RMS)
    meterPassthrough = buffer.getRMSLevel (0, 0, numSamples);
    meterLoop        = loopOut.getRMSLevel (0, 0, numSamples);

    // We are an effect; ensure we don't pass MIDI downstream
    midi.clear();
}

void Buffr3AudioProcessor::writeToRecorder (const AudioBuffer<float>& in)
{
    const int numCh = std::min (recBuffer.getNumChannels(), in.getNumChannels());
    const int n = in.getNumSamples();

    int w = recWritePos.load();
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
            recBuffer.getWritePointer (ch)[w] = in.getReadPointer (ch)[i];

        if (++w >= recBuffer.getNumSamples()) w = 0;
    }
    recWritePos.store (w);
}

void Buffr3AudioProcessor::snapshotRecorder (int latencyCompSamples)
{
    // Copy the entire 4s ring to a linear snapshot so content is frozen for the loop
    const int N = recBuffer.getNumSamples();
    const int numCh = recBuffer.getNumChannels();

    snapBuffer.setSize (numCh, N, false, false, true);

    // End should be the "most recent" audio, latency compensated
    int end = recWritePos.load() - latencyCompSamples;
    while (end < 0) end += N;
    end %= N;

    // Copy [0..end) and [end..N) into linear snap
    const int tail = N - end;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* dst = snapBuffer.getWritePointer (ch);
        const float* src = recBuffer.getReadPointer (ch);
        std::memcpy (dst,            src + end, sizeof(float) * (size_t) tail);
        std::memcpy (dst + tail,     src,       sizeof(float) * (size_t) end);
    }
    snapEndPos = N; // end of linear buffer
    loopReadPos = (float) (currentLoopSamples - 1); // begin on end boundary for clean first loop
}

void Buffr3AudioProcessor::computePendingLoopFromControls (int numSamples)
{
    const bool midiEnabled   = *apvts.getRawParameterValue ("midiEnabled") > 0.5f;
    const bool hold          = *apvts.getRawParameterValue ("hold") > 0.5f;
    const bool useUserSample = *apvts.getRawParameterValue ("useUserSample") > 0.5f;
    const float pbRange      = (float) *apvts.getRawParameterValue ("pitchBendRange");
    const float bendNorm     = pitchBendNorm.load();       // [-1,1]
    const float playback     = *apvts.getRawParameterValue ("playbackSpeed");
    const float portMs       = *apvts.getRawParameterValue ("portamentoMs");

    // Base frequency source: either last MIDI note (+bend) or Squeeze param if MIDI disabled
    double baseHz;
    if (midiEnabled)
    {
        baseHz = midiNoteToHz (lastNoteNumber);
        baseHz *= semitoneShiftToRatio ((double) bendNorm * pbRange);
    }
    else
    {
        const float s01 = juce::jlimit (0.f, 1.f, *apvts.getRawParameterValue ("squeeze") / 100.f);
        const double ms = squeezeToMs (s01);
        baseHz = 1000.0 / ms; // period(ms) -> Hz
    }

    baseHz = std::max (0.001, baseHz); // safety
    const double targetPeriodSec = 1.0 / baseHz;
    const double targetLoopSec   = targetPeriodSec * (double) playback; // speed stretches loop length
    const int    targetSamples   = juce::jlimit (1, maxSamples4s - 16, (int) std::round (targetLoopSec * sampleRate));

    // Portamento smoothing runs continuously; we sample it at loop edge
    const double portSec = std::max (0.0, (double) portMs / 1000.0);
    glideHz.reset (sampleRate, portSec);
    glideHz.setTargetValue (baseHz);
    lastTargetHz = glideHz.getNextValue(); // advance

    // The 'pending' loop length is based on the *current* smoothed value, sampled at loop end
    pendingLoopSamples = targetSamples;

    // Start/stop logic: if Hold or notesDown>0, looping; else release
    const int relMs = (int) *apvts.getRawParameterValue ("releaseMs");
    if ((hold || notesDown > 0) && ! looping.load())
    {
        // trigger: snapshot (respect latency comp and WAV toggle)
        const int latencySamples = (int) std::round ((*apvts.getRawParameterValue ("latencyCompMs") / 1000.0f) * sampleRate);
        if (useUserSample && userSampleLoaded)
        {
            // Use existing snapBuffer content as snapshot (already set by loadWavFile)
            snapEndPos = snapBuffer.getNumSamples();
        }
        else
        {
            snapshotRecorder (latencySamples);
        }

        currentLoopSamples = std::max (1, pendingLoopSamples);
        loopReadPos = (float) (currentLoopSamples - 1);
        looping.store (true);

        loopEnv.reset (sampleRate, 0.03);
        loopEnv.setCurrentAndTargetValue (0.f);
        loopEnv.setTargetValue (1.f);

        passthroughMuteEnv.reset (sampleRate, 0.03); // 30 ms mute
        passthroughMuteEnv.setTargetValue (0.f);
    }
    else if (!hold && notesDown == 0 && looping.load())
    {
        // start release; looping will be stopped at envelope end (checked in advanceLoopPlayback)
        loopEnv.reset (sampleRate, std::max (0.001, (double) relMs / 1000.0));
        loopEnv.setTargetValue (0.f);

        passthroughMuteEnv.reset (sampleRate, std::max (0.001, (double) relMs / 1000.0));
        passthroughMuteEnv.setTargetValue (1.f);
    }
}

void Buffr3AudioProcessor::advanceLoopPlayback (AudioBuffer<float>& out, int numSamples)
{
    const int numCh = std::min (out.getNumChannels(), snapBuffer.getNumChannels());
    const float speed = *apvts.getRawParameterValue ("playbackSpeed");
    const int N = snapBuffer.getNumSamples();

    if (N <= 1 || currentLoopSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        // Seamless loop with short crossfade at end -> start
        const int start = snapEndPos - currentLoopSamples; // loop start within snapshot
        const int s = juce::jlimit (0, N-1, start);

        const float pos = loopReadPos;
        const int ip = (int) pos;
        const float frac = pos - ip;

        // Base read index within [start, snapEndPos)
        int idx0 = s + ip;
        int idx1 = s + ip + 1;
        if (idx0 >= snapEndPos) idx0 -= currentLoopSamples;
        if (idx1 >= snapEndPos) idx1 -= currentLoopSamples;

        const int samplesLeft = currentLoopSamples - 1 - ip;

        // Crossfade if near end
        float xfadeA = 1.0f, xfadeB = 0.0f;
        if (samplesLeft < xfadeSamples)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (float) samplesLeft / (float) std::max (1, xfadeSamples));
            xfadeA = t;
            xfadeB = 1.0f - t;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* src = snapBuffer.getReadPointer (ch);
            const float a0 = src[idx0];
            const float a1 = src[idx1];
            const float sampA = a0 + frac * (a1 - a0);

            // Pre-read from start for crossfade-in
            int cidx0 = s + (ip + 1 - currentLoopSamples);
            int cidx1 = s + (ip + 2 - currentLoopSamples);
            while (cidx0 < 0) cidx0 += N;
            while (cidx1 < 0) cidx1 += N;
            if (cidx0 >= N) cidx0 -= N;
            if (cidx1 >= N) cidx1 -= N;

            const float b0 = src[cidx0];
            const float b1 = src[cidx1];
            const float sampB = b0 + frac * (b1 - b0);

            out.getWritePointer (ch)[i] = sampA * xfadeA + sampB * xfadeB;
        }

        // advance
        loopReadPos += speed;
        if (loopReadPos >= (float) currentLoopSamples)
        {
            // At loop end: quantise update to pending length
            currentLoopSamples = std::max (1, pendingLoopSamples);
            loopReadPos -= (float) currentLoopSamples;
        }
    }

    // If release env has reached ~0, stop looping
    if (loopEnv.isSmoothing() == false && loopEnv.getTargetValue() <= 0.001f && loopEnv.getCurrentValue() <= 0.002f)
    {
        looping.store (false);
        loopReadPos = 0.0f;
    }
}

void Buffr3AudioProcessor::mixPassthrough (AudioBuffer<float>& inout, int numSamples)
{
    const int numCh = inout.getNumChannels();

    // passthroughMuteEnv == 1 => full passthrough, 0 => muted
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* p = inout.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            p[i] *= juce::jlimit (0.0f, 1.0f, passthroughMuteEnv.getNextValue());
    }
}

void Buffr3AudioProcessor::handleMidi (MidiBuffer& midi, int /*numSamples*/)
{
    const bool midiEnabled    = *apvts.getRawParameterValue ("midiEnabled") > 0.5f;
    const bool holdParam      = *apvts.getRawParameterValue ("hold") > 0.5f;
    const bool useUserSample  = *apvts.getRawParameterValue ("useUserSample") > 0.5f;
    const int  latencySamples = (int) std::round ((*apvts.getRawParameterValue ("latencyCompMs") / 1000.0f) * sampleRate);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();

        if (m.isNoteOn())
        {
            notesDown = std::max (0, notesDown + 1);
            if (midiEnabled)
                lastNoteNumber = m.getNoteNumber();

            // Snapshot on every note-on unless HOLD is intentionally pinning content,
            // or we're using a user-loaded WAV instead of the live recorder.
            if (!holdParam && !useUserSample)
                snapshotRecorder (latencySamples);

            // If we were in release, go back to full loop quickly
            if (looping.load())
            {
                loopEnv.setTargetValue (1.f);
                passthroughMuteEnv.setTargetValue (0.f);
            }
        }
        else if (m.isNoteOff())
        {
            notesDown = std::max (0, notesDown - 1);
        }
        else if (m.isPitchWheel())
        {
            const int val = m.getPitchWheelValue(); // 0..16383
            const float norm = (val - 8192) / 8192.0f; // -1..+1
            pitchBendNorm.store (juce::jlimit (-1.0f, 1.0f, norm));
        }
    }
    // We clear MIDI later in processBlock (no MIDI out).
}

// ===================== Editor factory =====================
AudioProcessorEditor* Buffr3AudioProcessor::createEditor() { return new Buffr3AudioProcessorEditor (*this); }
