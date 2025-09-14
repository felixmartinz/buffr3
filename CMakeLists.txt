cmake_minimum_required(VERSION 3.22)
project(Buffr3 VERSION 1.0.0)

# Fetch JUCE automatically if it's not already added as a subdir in your repo.
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 7.0.12  # known-good with CMake; change if you pin JUCE elsewhere
)
FetchContent_MakeAvailable(juce)

# ---- Plugin target ----
juce_add_plugin(Buffr3
    COMPANY_NAME            "Badsound"
    PLUGIN_MANUFACTURER     "Badsound"
    PLUGIN_CODE             BfR3           # 4 chars
    PRODUCT_NAME            "Buffr3"
    COPY_PLUGIN_AFTER_BUILD TRUE
    FORMATS                 VST3 AU        # AU only builds on macOS
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    AU_MAIN_TYPE            kAudioUnitType_MusicEffect
    PLUGIN_HOST_IA          FALSE
    # Mono and Stereo
    PLUGIN_CHANNEL_CONFIGURATIONS "1,1" "2,2"
)

target_sources(Buffr3 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_features(Buffr3 PRIVATE cxx_std_17)

target_compile_definitions(Buffr3 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Buffr3 PRIVATE
    juce::juce_audio_utils      # MidiKeyboardComponent, meters helper
    juce::juce_audio_formats    # WAV loading/saving
    juce::juce_dsp              # Smoothed values, etc.
    juce::juce_gui_extra
)
