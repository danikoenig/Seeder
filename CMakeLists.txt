cmake_minimum_required(VERSION 3.22)
project(Seeder VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(SEEDER_SOURCES
    Source/Engine/Generator.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Two variants, like Repetitor:
#  - "Seeder"            MIDI effect (Logic, Bitwig, ...)
#  - "Seeder Instrument" instrument with MIDI out (Ableton: other MIDI tracks can use it as source)
juce_add_plugin(Seeder
    COMPANY_NAME "Dani Koenig"
    PLUGIN_MANUFACTURER_CODE DKnr
    PLUGIN_CODE Sdr1
    PRODUCT_NAME "Seeder"
    FORMATS VST3 AU
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_plugin(SeederInstrument
    COMPANY_NAME "Dani Koenig"
    PLUGIN_MANUFACTURER_CODE DKnr
    PLUGIN_CODE Sdr2
    PRODUCT_NAME "Seeder Instrument"
    FORMATS VST3 AU
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    IS_SYNTH TRUE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

foreach(t Seeder SeederInstrument)
    target_sources(${t} PRIVATE ${SEEDER_SOURCES})
    target_include_directories(${t} PRIVATE Source Source/Engine)
    target_compile_definitions(${t} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SEEDER_VERSION="${PROJECT_VERSION}")
    target_link_libraries(${t}
        PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endforeach()

# Headless logic test (needs no DAW):  cmake -B build -DSEEDER_BUILD_TESTS=ON && cmake --build build --target SeederProcessorTest
option(SEEDER_BUILD_TESTS "Build headless tests" OFF)
if(SEEDER_BUILD_TESTS)
    juce_add_console_app(SeederProcessorTest PRODUCT_NAME "SeederProcessorTest")
    target_sources(SeederProcessorTest PRIVATE Tests/processor_test.cpp)
    target_include_directories(SeederProcessorTest PRIVATE Source Source/Engine)
    target_link_libraries(SeederProcessorTest PRIVATE SeederInstrument juce::juce_audio_utils)
endif()
