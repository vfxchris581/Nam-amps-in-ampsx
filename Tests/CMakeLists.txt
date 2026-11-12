juce_add_console_app(NamVXTests PRODUCT_NAME "NamVXTests")

list(TRANSFORM NAMVX_CORE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE NAMVX_CORE_ABS)

target_sources(NamVXTests PRIVATE
    TestMain.cpp
    TestHelpers.h
    ${NAMVX_CORE_ABS})

target_link_libraries(NamVXTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_data_structures
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

namvx_configure_target(NamVXTests)
target_compile_definitions(NamVXTests PRIVATE
    NAMVX_EXAMPLE_MODELS="${namcore_SOURCE_DIR}/example_models"
    JUCE_MODAL_LOOPS_PERMITTED=0)

add_test(NAME NamVXTests COMMAND NamVXTests)
set_tests_properties(NamVXTests PROPERTIES TIMEOUT 900)
