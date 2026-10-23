cmake_minimum_required(VERSION 3.22)
project(MyReverb VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

add_subdirectory(JUCE)

# Manufacturer/plugin codes match the original Projucer build ('Manu' / 'Ktc3'),
# so existing DAW sessions keep finding the plugin.
juce_add_plugin(MyReverb
    COMPANY_NAME                "yourcompany"
    BUNDLE_ID                   com.yourcompany.MyReverb
    PLUGIN_MANUFACTURER_CODE    Manu
    PLUGIN_CODE                 Ktc3
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "MyReverb"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

juce_generate_juce_header(MyReverb)

target_sources(MyReverb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(MyReverb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    DONT_SET_USING_JUCE_NAMESPACE=1
)

target_link_libraries(MyReverb
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
