# =============================================================================
#  AeroGlow Saturator — top-level CMake build
#
#  Targets
#    AeroGlow            JUCE plugin (VST3 / AU / AAX / Standalone, per platform)
#    AeroGlowCoreTests   JUCE-free DSP unit tests (FFT harmonic analysis, latency,
#                        phase coherence, robustness, CPU benchmark)
#
#  JUCE source resolution (first match wins):
#    1. -DJUCE_DIR=/path/to/JUCE        (local checkout, recommended for CI)
#    2. FetchContent of tag ${AEROGLOW_JUCE_TAG} from GitHub
#
#  ASSUMPTION: JUCE 9.0.5 is the "latest stable" at time of writing (Nov 2026).
#  Code only uses long-stable APIs (APVTS, AudioProcessor, LookAndFeel_V4),
#  so JUCE 8.0.x also builds it.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel). Must be set before project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(AeroSound VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(AEROGLOW_BUILD_PLUGIN "Build the JUCE plugin targets"           ON)
option(AEROGLOW_BUILD_TESTS  "Build the JUCE-free DSP core test runner"  ON)
option(AEROGLOW_ENABLE_AVX2  "Compile with AVX2/FMA (x86-64 only). OFF = SSE2 baseline, runs on every x64 CPU" OFF)
set(AEROGLOW_JUCE_TAG "9.0.5" CACHE STRING "JUCE git tag used when JUCE_DIR is not given")
set(AEROGLOW_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (enables the AAX format)")

# -----------------------------------------------------------------------------
#  DSP core: header + .cpp, no JUCE dependency (shared by plugin and tests)
# -----------------------------------------------------------------------------
add_library(AeroGlowDsp STATIC
    Source/dsp/HalfbandOversampler.cpp
    Source/dsp/ModeEngine.cpp
    Source/dsp/SaturationProcessor.cpp)
target_include_directories(AeroGlowDsp PUBLIC Source)
set_target_properties(AeroGlowDsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # /fp:precise (MSVC default) on purpose: the NaN/Inf guards rely on std::isfinite.
    target_compile_options(AeroGlowDsp PRIVATE /W4 $<$<CONFIG:Release>:/O2>)
    if(AEROGLOW_ENABLE_AVX2)
        target_compile_options(AeroGlowDsp PRIVATE /arch:AVX2)
    endif()
else()
    target_compile_options(AeroGlowDsp PRIVATE -Wall -Wextra
        $<$<CONFIG:Release>:-O3 -fno-math-errno -ffp-contract=fast>)
    if(AEROGLOW_ENABLE_AVX2 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(AeroGlowDsp PRIVATE -mavx2 -mfma)
    endif()
endif()

# -----------------------------------------------------------------------------
#  AEROSOUND engines (JUCE-free): Glass, Bubbles, Space + demo renderer
# -----------------------------------------------------------------------------
add_library(AeroSoundDsp STATIC
    Source/aerosound/GlassEngine.cpp
    Source/aerosound/BubbleEngine.cpp
    Source/aerosound/SpaceReverb.cpp
    Source/aerosound/AeroSoundProcessor.cpp)
target_include_directories(AeroSoundDsp PUBLIC Source)
set_target_properties(AeroSoundDsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(AeroSoundDsp PUBLIC cxx_std_17)
if(NOT MSVC)
    target_compile_options(AeroSoundDsp PRIVATE -Wall -Wextra $<$<CONFIG:Release>:-O3>)
endif()

if(AEROGLOW_BUILD_TESTS)
    add_executable(AeroSoundDemos Tests/RenderDemos.cpp)
    target_link_libraries(AeroSoundDemos PRIVATE AeroSoundDsp)
endif()

# -----------------------------------------------------------------------------
#  Core tests (no JUCE needed — run anywhere, including headless CI)
# -----------------------------------------------------------------------------
if(AEROGLOW_BUILD_TESTS)
    add_executable(AeroGlowCoreTests Tests/CoreTests.cpp)
    target_link_libraries(AeroGlowCoreTests PRIVATE AeroGlowDsp)
    enable_testing()
    add_test(NAME AeroGlowCoreTests COMMAND AeroGlowCoreTests --quick)
endif()

# -----------------------------------------------------------------------------
#  JUCE plugin
# -----------------------------------------------------------------------------
if(AEROGLOW_BUILD_PLUGIN)
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${AEROGLOW_JUCE_TAG}
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # Formats per platform. AU only exists on macOS; AAX needs Avid's SDK.
    set(AEROGLOW_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND AEROGLOW_FORMATS AU)
    endif()
    if(AEROGLOW_AAX_SDK_PATH AND (APPLE OR WIN32))
        juce_set_aax_sdk_path(${AEROGLOW_AAX_SDK_PATH})
        list(APPEND AEROGLOW_FORMATS AAX)
    endif()

    juce_add_plugin(AeroSound
        COMPANY_NAME                "AeroSound"
        COMPANY_WEBSITE             "https://example.com/aerosound"
        BUNDLE_ID                   com.aerosound.aerosound
        PLUGIN_MANUFACTURER_CODE    AeSn          # ASSUMPTION: placeholder 4-char codes.
        PLUGIN_CODE                 AeSd          # Register your own before shipping.
        PRODUCT_NAME                "AEROSOUND"
        FORMATS                     ${AEROGLOW_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE         # set TRUE to auto-install into system plugin folders
        VST3_CATEGORIES             "Fx" "Reverb" "Modulation"
        AU_MAIN_TYPE                kAudioUnitType_Effect
        AAX_CATEGORY                AAX_ePlugInCategory_SoundField
        HARDENED_RUNTIME_ENABLED    TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE)        # standalone needs mic access on macOS

    file(GLOB AEROSOUND_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Presets/*.aerosound")
    juce_add_binary_data(AeroSoundPresets SOURCES ${AEROSOUND_PRESET_FILES})

    target_sources(AeroSound PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/PresetManager.cpp
        Source/gui/AeroLookAndFeel.cpp
        Source/gui/LevelMeter.cpp
        Source/gui/XYPad.cpp)

    target_compile_definitions(AeroSound PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)   # no-op on JUCE 8+ (splash removed); kept for JUCE 7 builds

    target_link_libraries(AeroSound
        PRIVATE
            AeroSoundDsp
            AeroSoundPresets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # -------------------------------------------------------------------------
    #  Host smoke test: loads the *built* plugin through JUCE's VST3/AU hosting
    #  (like a DAW), automates it, round-trips state, opens the editor.
    # -------------------------------------------------------------------------
    option(AEROGLOW_BUILD_HOST_TEST "Build the plugin-hosting smoke test" ON)
    if(AEROGLOW_BUILD_HOST_TEST)
        juce_add_console_app(AeroSoundHostTest PRODUCT_NAME "AeroSoundHostTest")
        target_sources(AeroSoundHostTest PRIVATE Tests/HostSmokeTest.cpp)
        target_compile_definitions(AeroSoundHostTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_AU=1
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(AeroSoundHostTest PRIVATE
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
        add_dependencies(AeroSoundHostTest AeroSound_VST3)

        # In-process editor renderer (screenshots). Not on Windows: JUCE's
        # console-style JUCEApplicationBase::main isn't available there.
      if(NOT WIN32)
        add_executable(AeroSoundSnapshot Tests/EditorSnapshot.cpp)
        target_compile_definitions(AeroSoundSnapshot PRIVATE
            $<TARGET_PROPERTY:AeroSound,INTERFACE_COMPILE_DEFINITIONS>
            $<TARGET_PROPERTY:AeroSound,COMPILE_DEFINITIONS>)
        target_include_directories(AeroSoundSnapshot PRIVATE
            $<TARGET_PROPERTY:AeroSound,INCLUDE_DIRECTORIES>)
        target_compile_features(AeroSoundSnapshot PRIVATE cxx_std_20)
        target_link_libraries(AeroSoundSnapshot PRIVATE AeroSound)
      endif()
    endif()
endif()
