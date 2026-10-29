cmake_minimum_required(VERSION 3.22)

project(DeadRoomAudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ------------------------------------------------------------------------------
# JUCE: use a local checkout when JUCE_PATH is given, otherwise download 9.0.4.
# ------------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin formats per platform. AAX builds compile against the AAX SDK that ships
# with JUCE; the result loads in Pro Tools Developer until it is PACE-signed.
# ------------------------------------------------------------------------------
option(DEADROOM_BUILD_AAX "Build AAX versions (macOS/Windows)" ON)
option(DEADROOM_BUILD_TESTS "Build the offline DSP test harness" OFF)

if(APPLE)
    set(DEADROOM_FORMATS VST3 AU Standalone)
elseif(WIN32)
    set(DEADROOM_FORMATS VST3 Standalone)
else()
    set(DEADROOM_FORMATS VST3 Standalone)
endif()

if(DEADROOM_BUILD_AAX AND (APPLE OR WIN32))
    list(APPEND DEADROOM_FORMATS AAX)
endif()

set_directory_properties(PROPERTIES JUCE_COMPANY_NAME "DeadRoom Audio")

# Shared UI + DSP code used by every plugin.
add_library(deadroom_common INTERFACE)
target_include_directories(deadroom_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

function(deadroom_configure_plugin target)
    target_link_libraries(${target}
        PRIVATE
            deadroom_common
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(${target}
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)
endfunction()

add_subdirectory(plugins/Roomless)
add_subdirectory(plugins/Duotone)
add_subdirectory(plugins/Lantern)

if(DEADROOM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
