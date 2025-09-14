cmake_minimum_required(VERSION 3.22)
project(Buffr3 VERSION 1.0.0)

# --- Get JUCE with the macOS 15 / Xcode 16 fixes ---
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED OFF)
FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9   # includes ScreenCaptureKit fix for juceaide on macOS 15+
)
FetchContent_MakeAvailable(juce)

# --- Plugin target ---
juce_add_plugin(Buffr3
    COMPANY_NAME            "Badsound"
    PLUGIN_MANUFACTURER     "Badsound"
    PLUGIN_CODE             BfR3
    PRODUCT_NAME            "Buffr3"
    COPY_PLUGIN_AFTER_BUILD TRUE
    FORMATS                 VST3 AU        # AU only builds on macOS
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    AU_MAIN_TYPE            kAudioUnitType_MusicEffect
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
    juce_audio_basics
    juce_audio_devices
    juce_audio_formats
    juce_audio_plugin_client
    juce_audio_processors
    juce_audio_utils
    juce_core
    juce_data_structures
    juce_events
    juce_graphics
    juce_gui_basics
    juce_gui_extra
)
