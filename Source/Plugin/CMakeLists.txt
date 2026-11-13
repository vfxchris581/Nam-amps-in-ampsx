set(NAMKNOBS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NAMKNOBS_FORMATS AU)
endif()

juce_add_plugin(NamKnobs
    PRODUCT_NAME "Nam Knobs"
    COMPANY_NAME "VFX Chris"
    BUNDLE_ID "com.vfxchris.namknobs"
    PLUGIN_MANUFACTURER_CODE Vfxc
    PLUGIN_CODE Nmkb
    FORMATS ${NAMKNOBS_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    ICON_BIG "${PROJECT_SOURCE_DIR}/Resources/Icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Nam Knobs needs audio input to process your guitar.")

juce_add_binary_data(NamKnobsBinaryData SOURCES
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-Regular.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-SemiBold.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Poppins-Bold.ttf")

list(TRANSFORM NAMKNOBS_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE NAMKNOBS_CORE_ABS)

target_sources(NamKnobs PRIVATE
    PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/NamKnobsLookAndFeel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/ToneMatrixPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/EqPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
    ${NAMKNOBS_CORE_ABS})

target_compile_definitions(NamKnobs PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(NamKnobs PRIVATE
    NamKnobsBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

namknobs_configure_target(NamKnobs)

# Headless screenshot tool (renders the editor to PNG; used for docs/CI previews)
if(NAMKNOBS_BUILD_SCREENSHOT)
    juce_add_gui_app(NamKnobsScreenshot PRODUCT_NAME "NamKnobsScreenshot")
    target_sources(NamKnobsScreenshot PRIVATE
        ${PROJECT_SOURCE_DIR}/BuildScripts/Screenshot.cpp
        PluginProcessor.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/NamKnobsLookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/ToneMatrixPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/EqPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
        ${NAMKNOBS_CORE_ABS})
    target_compile_definitions(NamKnobsScreenshot PRIVATE JucePlugin_Name="Nam Knobs" NAMKNOBS_SCREENSHOT=1
        NAMKNOBS_EXAMPLE_MODELS="${namcore_SOURCE_DIR}/example_models")
    target_link_libraries(NamKnobsScreenshot PRIVATE NamKnobsBinaryData juce::juce_audio_utils juce::juce_dsp
        juce::juce_gui_extra juce::juce_recommended_config_flags)
    namknobs_configure_target(NamKnobsScreenshot)
endif()
