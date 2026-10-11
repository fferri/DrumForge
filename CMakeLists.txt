cmake_minimum_required(VERSION 3.22)

project(DrumForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    # Builds for the host architecture by default. Pass
    # -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal binary.
endif()

add_subdirectory(JUCE)

#===============================================================================
# Shared core: synthesis engines, track/processor, editor.
# Kept as its own library so the offline test and render tools can reuse it.
#===============================================================================
add_library(DrumForgeCore STATIC
    Source/Core/Parameters.cpp
    Source/Core/ModulationState.cpp
    Source/Core/Randomizer.cpp
    Source/Core/KitLibrary.cpp
    Source/Core/Settings.cpp
    Source/Core/DrumKitProcessor.cpp
    Source/UI/Theme.cpp
    Source/UI/ModelKnob.cpp
    Source/UI/VelocityKnob.cpp
    Source/UI/TrackStrip.cpp
    Source/UI/DrumKitEditor.cpp)

target_include_directories(DrumForgeCore PUBLIC Source)

target_compile_definitions(DrumForgeCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(DrumForgeCore PUBLIC
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

#===============================================================================
# The plugin itself.
#===============================================================================
juce_add_plugin(DrumForge
    COMPANY_NAME "DrumForge Audio"
    BUNDLE_ID "com.drumforgeaudio.drumforge"
    PLUGIN_MANUFACTURER_CODE Dfga
    PLUGIN_CODE Df01
    FORMATS VST3
    PRODUCT_NAME "DrumForge"
    DESCRIPTION "8-track drum and percussion synthesiser with seven synthesis engines"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES "Instrument" "Drum"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DrumForge PRIVATE Source/PluginEntry.cpp)

target_link_libraries(DrumForge PRIVATE DrumForgeCore)

target_compile_definitions(DrumForge PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0)

#===============================================================================
# Offline tools (headless; they drive the processor directly).
#===============================================================================
juce_add_console_app(DrumForgeRender PRODUCT_NAME "DrumForgeRender")
target_sources(DrumForgeRender PRIVATE Tools/RenderDemo.cpp)
target_link_libraries(DrumForgeRender PRIVATE DrumForgeCore juce::juce_audio_formats)

# Kit design harness: renders .dfkit files through lane-matched patterns.
juce_add_console_app(DrumForgeKitLab PRODUCT_NAME "DrumForgeKitLab")
target_sources(DrumForgeKitLab PRIVATE Tools/KitLab.cpp)
target_link_libraries(DrumForgeKitLab PRIVATE DrumForgeCore juce::juce_audio_formats)

juce_add_console_app(DrumForgeTests PRODUCT_NAME "DrumForgeTests")
target_sources(DrumForgeTests PRIVATE Tools/RenderTests.cpp)
target_link_libraries(DrumForgeTests PRIVATE DrumForgeCore)

# GUI smoke test: constructs the real editor and renders it to a PNG.
juce_add_gui_app(DrumForgeScreenshot PRODUCT_NAME "DrumForgeScreenshot")
target_sources(DrumForgeScreenshot PRIVATE Tools/RenderScreenshot.cpp)
target_link_libraries(DrumForgeScreenshot PRIVATE DrumForgeCore)

# End-to-end test: loads the built .vst3 through JUCE's VST3 hosting code, exactly as
# a DAW would. Deliberately does NOT link DrumForgeCore - it exercises the shipped
# binary as a black box, so the host-side code must be compiled into this target.
# Must be a gui_app rather than a console_app: on macOS JUCE drives the message loop with
# [NSApp run], which only works from a real app bundle.
juce_add_gui_app(DrumForgeHostTest PRODUCT_NAME "DrumForgeHostTest")
target_sources(DrumForgeHostTest PRIVATE Tools/HostLoadTest.cpp)
target_compile_definitions(DrumForgeHostTest PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(DrumForgeHostTest PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
add_dependencies(DrumForgeHostTest DrumForge)
