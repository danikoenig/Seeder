cmake_minimum_required(VERSION 3.22)
project(Seeder VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

# JUCE: local path (-DJUCE_PATH=...) or automatic download
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# CLAP (Bitwig, Reaper ...) via clap-juce-extensions
include(FetchContent)
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG main
    GIT_SUBMODULES_RECURSE TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(clap-juce-extensions)

# Two variants from the same code, like Repetitor:
#  Seeder             MIDI effect (Bitwig, Logic MIDI FX, Reaper ...)
#  Seeder Instrument  instrument with MIDI out (Ableton Live: other tracks use it via "MIDI From")
function(seeder_target target product code bundle au_type is_instr)
    if(is_instr)
        set(midi_fx FALSE)
        set(synth TRUE)
        set(cats Instrument Generator)
    else()
        set(midi_fx TRUE)
        set(synth FALSE)
        set(cats Fx Generator)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME "Dani Koenig"
        BUNDLE_ID "ch.danikoenig.${bundle}"
        PLUGIN_MANUFACTURER_CODE Dkng
        PLUGIN_CODE ${code}
        FORMATS VST3 AU
        AU_MAIN_TYPE ${au_type}
        PRODUCT_NAME "${product}"
        IS_SYNTH ${synth}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT ${midi_fx}
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${cats})

    target_sources(${target} PRIVATE
        Source/Engine/Generator.cpp
        Source/Link.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Look.cpp)
    target_include_directories(${target} PRIVATE Source Source/Engine)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

seeder_target(Seeder           "Seeder"            Sdr1 seeder           kAudioUnitType_MIDIProcessor FALSE)
seeder_target(SeederInstrument "Seeder Instrument" Sdr2 seederinstrument kAudioUnitType_MusicDevice   TRUE)

clap_juce_extensions_plugin(TARGET Seeder
    CLAP_ID "ch.danikoenig.seeder"
    CLAP_FEATURES note-effect)

clap_juce_extensions_plugin(TARGET SeederInstrument
    CLAP_ID "ch.danikoenig.seederinstrument"
    CLAP_FEATURES instrument note-effect)

# Headless logic test (needs no DAW):
#   cmake -B build -DSEEDER_BUILD_TESTS=ON && cmake --build build --target SeederProcessorTest
option(SEEDER_BUILD_TESTS "Build headless tests" OFF)
if(SEEDER_BUILD_TESTS)
    juce_add_console_app(SeederProcessorTest PRODUCT_NAME "SeederProcessorTest")
    target_sources(SeederProcessorTest PRIVATE Tests/processor_test.cpp)
    target_include_directories(SeederProcessorTest PRIVATE Source Source/Engine)
    target_link_libraries(SeederProcessorTest PRIVATE SeederInstrument juce::juce_audio_utils)

    # Renders the editor to a PNG: SeederSnapshot out.png
    juce_add_console_app(SeederSnapshot PRODUCT_NAME "SeederSnapshot")
    target_sources(SeederSnapshot PRIVATE Tests/snapshot.cpp)
    target_include_directories(SeederSnapshot PRIVATE Source Source/Engine)
    target_link_libraries(SeederSnapshot PRIVATE SeederInstrument juce::juce_audio_utils)
endif()
