cmake_minimum_required(VERSION 3.15)

# Minimum macOS supported by the shipped binaries. MUST be set before project()
# so it flows into the compiler/linker's -mmacosx-version-min flag. When unset,
# the toolchain stamps the binary with the *build machine's* OS version, which
# made 0.3.0 require macOS 26 and refuse to load on anything older (e.g. Sonoma).
# 11.0 (Big Sur) is the first Apple Silicon OS. Keep in lock-step with the bundled
# onnxruntime's own minos: onnxruntime 1.19.2 is minos 11.0; 1.20+ is 13.3+, 1.28 is
# 14.0 — bumping ORT past 1.19.2 REQUIRES raising this target to match, or the
# plugin binary will claim 11.0 yet fail to load its dylib on older macOS.
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
endif ()

project(ISODrums VERSION 0.3.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime — inference backend (replaces LibTorch: ~37MB vs ~170MB, faster on CPU).
set(ORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
set(ORT_INCLUDE_DIR "${ORT_DIR}/include")
find_library(ORT_LIB onnxruntime PATHS "${ORT_DIR}/lib" NO_DEFAULT_PATH REQUIRED)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" ./JUCE)

juce_add_plugin(ISODrums
    COMPANY_NAME "Plait Audio"
    BUNDLE_ID "com.plaitaudio.isodrums"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Isod
    PLUGIN_CODE Isod
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ISO Drums"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ISO-Drums-Avatar.png")

juce_generate_juce_header(ISODrums)

target_sources(ISODrums
    PRIVATE
        src/ISODesign.cpp
        src/ISODesign.h
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/SeparationEngine.cpp
        src/SeparationEngine.h
        src/Utils.cpp
        src/Utils.h
        src/OnsetDetector.cpp
        src/OnsetDetector.h
        src/DrumHit.h
        src/DrumMap.h
        src/MidiExporter.cpp
        src/MidiExporter.h
        src/MidiSettingsComponent.cpp
        src/MidiSettingsComponent.h
        src/LicenseManager.cpp
        src/LicenseManager.h
        src/Analytics.cpp
        src/Analytics.h
        src/LicenseDialog.cpp
        src/LicenseDialog.h
        src/AudioSettingsComponent.cpp
        src/AudioSettingsComponent.h
        src/CustomStandaloneApp.cpp)

target_compile_definitions(ISODrums
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
    PRIVATE
        ISO_VERSION_STRING="${PROJECT_VERSION}"
        # Free public beta: no trial expiry, no export caps, neutral "Beta"
        # labelling. Flip to 0 in phase two when paid licensing goes live.
        ISO_FREE_BETA=1)

file(GLOB ISO_MODEL_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "Resources/onnx/*.onnx")
file(GLOB ISO_IMAGE_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "Resources/*.png" "Resources/*.jpg" "Resources/*.jpeg" "Resources/*.svg")
file(GLOB ISO_FONT_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "Resources/fonts/*.ttf" "Resources/fonts/*.otf")
set(ISO_BINARY_DATA_FILES ${ISO_MODEL_FILES} ${ISO_IMAGE_FILES} ${ISO_FONT_FILES})

if (ISO_BINARY_DATA_FILES)
    juce_add_binary_data(ISODrums_data SOURCES ${ISO_BINARY_DATA_FILES})
else ()
    add_library(ISODrums_data INTERFACE)
endif ()

target_include_directories(ISODrums PRIVATE "${ORT_INCLUDE_DIR}")

target_link_libraries(ISODrums
    PRIVATE
        ISODrums_data
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
        "${ORT_LIB}"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (APPLE)
    set(ISODRUMS_ORT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib")
    set(ISODRUMS_BUNDLE_ORT_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle_onnxruntime_macos.sh")
    set(ISODRUMS_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icon.icns")
    set(JUCE_COPY_DIR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/extras/Build/CMake/copyDir.cmake")
    foreach (_iso_bundle_target IN ITEMS ISODrums_AU ISODrums_VST3 ISODrums_Standalone)
        if (TARGET ${_iso_bundle_target})
            # 1) Install the icon FIRST, so it is present before the bundle is sealed.
            add_custom_command(TARGET ${_iso_bundle_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${ISODRUMS_ICON_ICNS}"
                        "$<TARGET_BUNDLE_DIR:${_iso_bundle_target}>/Contents/Resources/Icon.icns"
                COMMENT "Installing multi-size Icon.icns into ${_iso_bundle_target}"
                VERBATIM)
            # 2) Bundle ONNX Runtime and (re-)sign the whole bundle last.
            add_custom_command(TARGET ${_iso_bundle_target} POST_BUILD
                COMMAND /bin/bash "${ISODRUMS_BUNDLE_ORT_SCRIPT}"
                        "$<TARGET_BUNDLE_DIR:${_iso_bundle_target}>"
                        "${ISODRUMS_ORT_LIB_DIR}"
                COMMENT "Bundling ONNX Runtime into ${_iso_bundle_target}"
                VERBATIM)
            get_target_property(_iso_plugin_copy_dest ${_iso_bundle_target} JUCE_PLUGIN_COPY_DIR)
            if (_iso_plugin_copy_dest)
                add_custom_command(TARGET ${_iso_bundle_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}"
                        "-Dsrc=$<TARGET_BUNDLE_DIR:${_iso_bundle_target}>"
                        "-Ddest=$<GENEX_EVAL:${_iso_plugin_copy_dest}>"
                        "-P" "${JUCE_COPY_DIR_SCRIPT}"
                    COMMENT "Re-copying ${_iso_bundle_target} after ONNX Runtime bundle"
                    VERBATIM)
            endif ()
        endif ()
    endforeach ()
endif ()

# ---------------------------------------------------------------------------
# iso_verify — offline harness to validate the ONNX backend vs the reference.
# ---------------------------------------------------------------------------
option(ISODRUMS_BUILD_VERIFY "Build the offline separation verification tool" ON)
if (ISODRUMS_BUILD_VERIFY)
    juce_add_console_app(iso_verify PRODUCT_NAME "iso_verify")
    juce_generate_juce_header(iso_verify)
    target_sources(iso_verify PRIVATE
        tools/verify_main.cpp
        src/Utils.cpp
        src/SeparationEngine.cpp
        src/OnsetDetector.cpp)
    target_include_directories(iso_verify PRIVATE "${ORT_INCLUDE_DIR}")
    target_link_libraries(iso_verify
        PRIVATE
            ISODrums_data
            juce::juce_audio_basics
            juce::juce_dsp
            "${ORT_LIB}"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    if (APPLE)
        set_target_properties(iso_verify PROPERTIES
            BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib")
    endif ()
endif ()
