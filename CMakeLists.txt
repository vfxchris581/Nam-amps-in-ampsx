cmake_minimum_required(VERSION 3.22)

project(NamVX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
endif()

option(NAMVX_BUILD_PLUGIN "Build the VST3 + Standalone application" ON)
option(NAMVX_BUILD_TESTS  "Build the automated test suite" ON)
option(NAMVX_BUILD_SCREENSHOT "Build the headless UI screenshot tool" OFF)
option(NAMVX_HEADLESS_LINUX "Linux build without ALSA/JACK/Xrandr (CI/headless boxes)" OFF)

# ----------------------------------------------------------------------------
# Dependencies (all permissively licensed, pinned)
#   JUCE 8          - AGPLv3 / commercial (see Documentation/LICENSES.md)
#   NeuralAmpModelerCore - MIT  (official NAM inference code, Steven Atkinson)
#   Eigen 3.4       - MPL2
#   nlohmann/json   - MIT (vendored inside NeuralAmpModelerCore)
# Use -DFETCHCONTENT_SOURCE_DIR_<NAME>=/path to build fully offline.
# ----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(namcore
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        d3189ed482ded880c128bf312941266c4fa5ceec
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  __no_cmake__)

FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  __no_cmake__)

FetchContent_MakeAvailable(juce namcore eigen)

# ----------------------------------------------------------------------------
# Official NAM inference core as a static library
# ----------------------------------------------------------------------------
file(GLOB NAM_CORE_SOURCES CONFIGURE_DEPENDS
    "${namcore_SOURCE_DIR}/NAM/*.cpp"
    "${namcore_SOURCE_DIR}/NAM/*/*.cpp")
# OBJECT library: NAM registers its architectures through static initialisers, which a static
# archive would let the linker drop ("No config parser registered for architecture").
add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    "${namcore_SOURCE_DIR}"
    "${namcore_SOURCE_DIR}/NAM"
    "${namcore_SOURCE_DIR}/Dependencies/nlohmann"
    "${eigen_SOURCE_DIR}")
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)
if(MSVC)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
    target_compile_options(nam_core PRIVATE /bigobj /O2 /W0)
else()
    target_compile_options(nam_core PRIVATE -O3 -w)
    if(UNIX AND NOT APPLE)
        target_link_libraries(nam_core PUBLIC stdc++fs)
    endif()
endif()

# ----------------------------------------------------------------------------
# NamVX engine sources (shared by plugin and tests)
# ----------------------------------------------------------------------------
set(NAMVX_CORE_SOURCES
    Source/NAMEngine/SincResampler.cpp
    Source/NAMEngine/NamModel.cpp
    Source/NAMMetadata/CaptureMetadata.cpp
    Source/NAMLoader/NamLoader.cpp
    Source/ProfileAnalyzer/ProfileAnalyzer.cpp
    Source/ProfileInterpolation/ProfileInterpolator.cpp
    Source/ProfileInterpolation/CaptureBlender.cpp
    Source/AmplifierEngine/AmpDefinition.cpp
    Source/AmplifierEngine/AmpRuntime.cpp
    Source/AmplifierEngine/ToneResidual.cpp
    Source/AmplifierEngine/ToneMatch.cpp
    Source/ChannelManager/ChannelManager.cpp
    Source/TubeCircuitDSP/TriodeStage.cpp
    Source/TubeCircuitDSP/PowerAmpStage.cpp
    Source/HarmonicEngine/HarmonicEngine.cpp
    Source/IRLoader/IRSlot.cpp
    Source/Convolution/DualCabinet.cpp
    Source/PresetManager/AmpLibrary.cpp
    Source/PresetManager/PresetManager.cpp
    Source/AudioEngine/EngineParameters.cpp
    Source/AudioEngine/ParametricEQ.cpp
    Source/ProfileInterpolation/PhaseMatcher.cpp
    Source/AudioEngine/SignalChain.cpp)

set(NAMVX_COMMON_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(NAMVX_HEADLESS_LINUX)
    list(APPEND NAMVX_COMMON_DEFS JUCE_ALSA=0 JUCE_JACK=0 JUCE_USE_XRANDR=0
         JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0)
endif()

function(namvx_configure_target tgt)
    target_include_directories(${tgt} PRIVATE "${PROJECT_SOURCE_DIR}/Source")
    target_compile_definitions(${tgt} PRIVATE ${NAMVX_COMMON_DEFS})
    target_link_libraries(${tgt} PRIVATE nam_core)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /bigobj /W3 "$<$<CONFIG:Release>:/O2>")
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter
                               "$<$<CONFIG:Release>:-O3>")
    endif()
endfunction()

if(NAMVX_BUILD_PLUGIN)
    add_subdirectory(Source/Plugin)
endif()

if(NAMVX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
