include(FetchContent)

if(LILI_JUCE_DIR)
    get_filename_component(lili_juce_dir "${LILI_JUCE_DIR}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
    add_subdirectory("${lili_juce_dir}" "${CMAKE_BINARY_DIR}/juce")
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

option(LILI_COPY_PLUGIN "Install the built AU/VST3 into ~/Library/Audio/Plug-Ins" OFF)

juce_add_plugin(LILI8
    PRODUCT_NAME "LILI-8"
    COMPANY_NAME "Kristian Freeman"
    BUNDLE_ID com.kristianfreeman.lili8
    PLUGIN_MANUFACTURER_CODE Kfrm
    PLUGIN_CODE Lil8
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD ${LILI_COPY_PLUGIN})

target_sources(LILI8 PRIVATE PluginProcessor.cpp PluginEditor.cpp)

# Pre-rendered UI art (tools/art/export_ui.py) and the shared board layout.
juce_add_binary_data(LiliAssets
    HEADER_NAME LiliAssets.h
    NAMESPACE LiliAssets
    SOURCES
        assets/board.png
        assets/knob_strip.png
        assets/toggle_strip.png
        ${PROJECT_SOURCE_DIR}/art/board.json)

target_compile_definitions(LILI8 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LILI8
    PRIVATE
        lili::dsp
        LiliAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
