juce_add_console_app(NamKnobsTests PRODUCT_NAME "NamKnobsTests")

list(TRANSFORM NAMKNOBS_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE NAMKNOBS_CORE_ABS)

target_sources(NamKnobsTests PRIVATE
    TestMain.cpp
    TestHelpers.h
    ${NAMKNOBS_CORE_ABS})

target_link_libraries(NamKnobsTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

namknobs_configure_target(NamKnobsTests)
target_compile_definitions(NamKnobsTests PRIVATE
    NAMKNOBS_EXAMPLE_MODELS="${namcore_SOURCE_DIR}/example_models"
    JUCE_MODAL_LOOPS_PERMITTED=0)

add_test(NAME NamKnobsTests COMMAND NamKnobsTests)
set_tests_properties(NamKnobsTests PROPERTIES TIMEOUT 900)
