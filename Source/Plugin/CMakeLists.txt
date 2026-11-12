set(NAMIFIED_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NAMIFIED_FORMATS AU)
endif()

juce_add_plugin(Namified
    PRODUCT_NAME "Namified"
    COMPANY_NAME "VFX Chris"
    BUNDLE_ID "com.vfxchris.namified"
    PLUGIN_MANUFACTURER_CODE Vfxc
    PLUGIN_CODE Nmfd
    FORMATS ${NAMIFIED_FORMATS}
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
    MICROPHONE_PERMISSION_TEXT "Namified needs audio input to process your guitar.")

juce_add_binary_data(NamifiedBinaryData SOURCES
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-Regular.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-SemiBold.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Poppins-Bold.ttf")

list(TRANSFORM NAMIFIED_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE NAMIFIED_CORE_ABS)

target_sources(Namified PRIVATE
    PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/NamifiedLookAndFeel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
    ${NAMIFIED_CORE_ABS})

target_compile_definitions(Namified PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Namified PRIVATE
    NamifiedBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

namified_configure_target(Namified)

# Headless screenshot tool (renders the editor to PNG; used for docs/CI previews)
if(NAMIFIED_BUILD_SCREENSHOT)
    juce_add_gui_app(NamifiedScreenshot PRODUCT_NAME "NamifiedScreenshot")
    target_sources(NamifiedScreenshot PRIVATE
        ${PROJECT_SOURCE_DIR}/BuildScripts/Screenshot.cpp
        PluginProcessor.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/NamifiedLookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
        ${NAMIFIED_CORE_ABS})
    target_compile_definitions(NamifiedScreenshot PRIVATE JucePlugin_Name="Namified" NAMIFIED_SCREENSHOT=1
        NAMIFIED_EXAMPLE_MODELS="${namcore_SOURCE_DIR}/example_models")
    target_link_libraries(NamifiedScreenshot PRIVATE NamifiedBinaryData juce::juce_audio_utils juce::juce_dsp
        juce::juce_gui_extra juce::juce_recommended_config_flags)
    namified_configure_target(NamifiedScreenshot)
endif()
