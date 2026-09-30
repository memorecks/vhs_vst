cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) unless the caller overrides it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(VHS VERSION 1.12.0 LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 7.0.12: use third_party/JUCE (or -DJUCE_DIR=...) if present, otherwise fetch it.
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to JUCE")
if(EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 7.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(VHS
    COMPANY_NAME "Memorecks"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.memorecks.vhs"
    PLUGIN_MANUFACTURER_CODE Mmrk
    PLUGIN_CODE Mvhs
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "VHS"
    DESCRIPTION "VHS - Audio Degradation Suite"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "VHS processes the audio input in real time."
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Distortion" "Modulation"
    AU_MAIN_TYPE "kAudioUnitType_Effect")

juce_add_binary_data(VHSData SOURCES
    assets/background_upscaled.png
    assets/hum1.flac
    assets/hum2.flac
    assets/hum3.flac
    assets/hiss.flac
    assets/crackle.flac
    assets/mic_irs.bin
    assets/controls/knob_black.png
    assets/controls/knob_teal.png
    assets/controls/knob_white.png
    assets/controls/switch.png
    assets/fonts/Michroma-Regular.ttf)

target_sources(VHS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/VHSEngine.cpp
    Source/NoiseEngine.cpp)

target_include_directories(VHS PRIVATE Source)

target_compile_definitions(VHS PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1)

target_link_libraries(VHS
    PRIVATE
        VHSData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test harness (not shipped): renders test signals through the processor.
option(VHS_BUILD_TESTS "Build the offline test harness" OFF)
if(VHS_BUILD_TESTS)
    juce_add_console_app(VHSTest PRODUCT_NAME "VHSTest")
    target_sources(VHSTest PRIVATE
        Test/TestMain.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/VHSEngine.cpp
        Source/NoiseEngine.cpp)
    target_include_directories(VHSTest PRIVATE Source)
    target_compile_definitions(VHSTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(VHSTest PRIVATE
        VHSData juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
