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

project(AeroGlow VERSION 1.0.0 LANGUAGES C CXX)

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

    juce_add_plugin(AeroGlow
        COMPANY_NAME                "AeroGlow Audio"
        COMPANY_WEBSITE             "https://example.com/aeroglow"
        BUNDLE_ID                   com.aeroglowaudio.aeroglow
        PLUGIN_MANUFACTURER_CODE    Aero          # ASSUMPTION: placeholder 4-char codes.
        PLUGIN_CODE                 AgSt          # Register your own before shipping.
        PRODUCT_NAME                "AeroGlow Saturator"
        FORMATS                     ${AEROGLOW_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE         # set TRUE to auto-install into system plugin folders
        VST3_CATEGORIES             "Fx" "Distortion"
        AU_MAIN_TYPE                kAudioUnitType_Effect
        AAX_CATEGORY                AAX_ePlugInCategory_Harmonic
        HARDENED_RUNTIME_ENABLED    TRUE
        MICROPHONE_PERMISSION_ENABLED TRUE)        # standalone needs mic access on macOS

    juce_add_binary_data(AeroGlowPresets SOURCES
        "Presets/Glass Pad Shimmer.aerosat"
        "Presets/Neon Drum Bus.aerosat"
        "Presets/Sunlit Vocal.aerosat")

    target_sources(AeroGlow PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp
        Source/PresetManager.cpp
        Source/gui/AeroLookAndFeel.cpp
        Source/gui/ModeSwitch.cpp
        Source/gui/LevelMeter.cpp)

    target_compile_definitions(AeroGlow PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)   # no-op on JUCE 8+ (splash removed); kept for JUCE 7 builds

    target_link_libraries(AeroGlow
        PRIVATE
            AeroGlowDsp
            AeroGlowPresets
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # -------------------------------------------------------------------------
    #  Host smoke test: loads the *built* plugin through JUCE's VST3/AU hosting
    #  (like a DAW), automates it, round-trips state and snapshots the editor.
    # -------------------------------------------------------------------------
    option(AEROGLOW_BUILD_HOST_TEST "Build the plugin-hosting smoke test" ON)
    if(AEROGLOW_BUILD_HOST_TEST)
        juce_add_console_app(AeroGlowHostTest PRODUCT_NAME "AeroGlowHostTest")
        target_sources(AeroGlowHostTest PRIVATE Tests/HostSmokeTest.cpp)
        target_compile_definitions(AeroGlowHostTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_AU=1
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(AeroGlowHostTest PRIVATE
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
        add_dependencies(AeroGlowHostTest AeroGlow_VST3)

        # In-process editor renderer (README screenshots / visual check).
        # Links the plugin's shared-code library directly, so it reuses the
        # already-compiled JUCE modules (no duplicate module objects).
        add_executable(AeroGlowSnapshot Tests/EditorSnapshot.cpp)
        target_compile_definitions(AeroGlowSnapshot PRIVATE
            $<TARGET_PROPERTY:AeroGlow,INTERFACE_COMPILE_DEFINITIONS>
            $<TARGET_PROPERTY:AeroGlow,COMPILE_DEFINITIONS>)
        target_include_directories(AeroGlowSnapshot PRIVATE
            $<TARGET_PROPERTY:AeroGlow,INCLUDE_DIRECTORIES>)
        target_compile_features(AeroGlowSnapshot PRIVATE cxx_std_20)
        target_link_libraries(AeroGlowSnapshot PRIVATE AeroGlow)
    endif()
endif()
