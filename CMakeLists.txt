cmake_minimum_required(VERSION 3.15)

project(ISODrums VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libtorch")
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" ./JUCE)

juce_add_plugin(ISODrums
    COMPANY_NAME "ISO Drums"
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
        src/LicenseDialog.cpp
        src/LicenseDialog.h)

target_compile_definitions(ISODrums
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

file(GLOB ISO_MODEL_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "Resources/*.pt")
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

target_link_libraries(ISODrums
    PRIVATE
        ISODrums_data
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_cryptography
        "${TORCH_LIBRARIES}"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (APPLE)
    set(ISODRUMS_LIBTORCH_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libtorch/lib")
    set(ISODRUMS_BUNDLE_LIBTORCH_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/bundle_libtorch_macos.sh")
    set(JUCE_COPY_DIR_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/extras/Build/CMake/copyDir.cmake")
    foreach (_iso_bundle_target IN ITEMS ISODrums_AU ISODrums_VST3 ISODrums_Standalone)
        if (TARGET ${_iso_bundle_target})
            add_custom_command(TARGET ${_iso_bundle_target} POST_BUILD
                COMMAND /bin/bash "${ISODRUMS_BUNDLE_LIBTORCH_SCRIPT}"
                        "$<TARGET_BUNDLE_DIR:${_iso_bundle_target}>"
                        "${ISODRUMS_LIBTORCH_LIB_DIR}"
                COMMENT "Bundling LibTorch into ${_iso_bundle_target}"
                VERBATIM)
            get_target_property(_iso_plugin_copy_dest ${_iso_bundle_target} JUCE_PLUGIN_COPY_DIR)
            if (_iso_plugin_copy_dest)
                add_custom_command(TARGET ${_iso_bundle_target} POST_BUILD
                    COMMAND "${CMAKE_COMMAND}"
                        "-Dsrc=$<TARGET_BUNDLE_DIR:${_iso_bundle_target}>"
                        "-Ddest=$<GENEX_EVAL:${_iso_plugin_copy_dest}>"
                        "-P" "${JUCE_COPY_DIR_SCRIPT}"
                    COMMENT "Re-copying ${_iso_bundle_target} after LibTorch bundle"
                    VERBATIM)
            endif ()
        endif ()
    endforeach ()
endif ()
