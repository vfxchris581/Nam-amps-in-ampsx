set(NAMVX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NAMVX_FORMATS AU)
endif()

juce_add_plugin(NamVX
    PRODUCT_NAME "Nam VX"
    COMPANY_NAME "VFX Chris"
    BUNDLE_ID "com.vfxchris.namvx"
    PLUGIN_MANUFACTURER_CODE Vfxc
    PLUGIN_CODE Nmvx
    FORMATS ${NAMVX_FORMATS}
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
    MICROPHONE_PERMISSION_TEXT "Nam VX needs audio input to process your guitar.")

juce_add_binary_data(NamVXBinaryData SOURCES
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-Regular.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Inter-SemiBold.otf"
    "${PROJECT_SOURCE_DIR}/Resources/Fonts/Poppins-Bold.ttf")

list(TRANSFORM NAMVX_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE NAMVX_CORE_ABS)

target_sources(NamVX PRIVATE
    PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/NamVXLookAndFeel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/ToneMatrixPanel.cpp
    ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
    ${NAMVX_CORE_ABS})

target_compile_definitions(NamVX PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(NamVX PRIVATE
    NamVXBinaryData
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

namvx_configure_target(NamVX)

# Headless screenshot tool (renders the editor to PNG; used for docs/CI previews)
if(NAMVX_BUILD_SCREENSHOT)
    juce_add_gui_app(NamVXScreenshot PRODUCT_NAME "NamVXScreenshot")
    target_sources(NamVXScreenshot PRIVATE
        ${PROJECT_SOURCE_DIR}/BuildScripts/Screenshot.cpp
        PluginProcessor.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/NamVXLookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/AmpPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CapturesPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/CabinetPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/TubePanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/LibraryPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/ToneMatrixPanel.cpp
        ${PROJECT_SOURCE_DIR}/Source/GUI/PluginEditor.cpp
        ${NAMVX_CORE_ABS})
    target_compile_definitions(NamVXScreenshot PRIVATE JucePlugin_Name="Nam VX" NAMVX_SCREENSHOT=1
        NAMVX_EXAMPLE_MODELS="${namcore_SOURCE_DIR}/example_models")
    target_link_libraries(NamVXScreenshot PRIVATE NamVXBinaryData juce::juce_audio_utils juce::juce_dsp
        juce::juce_gui_extra juce::juce_recommended_config_flags)
    namvx_configure_target(NamVXScreenshot)
endif()
