cmake_minimum_required(VERSION 3.22)
project(Secuencias VERSION 0.3.7 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # para IntelliSense / clangd

option(SECUENCIAS_BUILD_TESTS "Compilar los tests sin interfaz (SecuenciasTests)" ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()
if(MSVC)
    add_compile_options(/utf-8)   # los fuentes son UTF-8 (tr ("canción")); sin esto MSVC los lee en la página de códigos local
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# JUCE: usa una copia local si existe (-DJUCE_DIR=/ruta/a/JUCE), si no la descarga
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Signalsmith Stretch (MIT): cambio de tempo y tono, solo cabeceras (trae su dependencia "dsp" como submódulo)
include(FetchContent)
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(signalsmith_stretch)   # define el target signalsmith-stretch y descarga signalsmith-linear

set(SECUENCIAS_CORE_SOURCES
    Source/AudioEngine.cpp
    Source/Library.cpp
    Source/Analyzer.cpp
    Source/Loudness.cpp
    Source/TempoMap.cpp
    Source/Music.cpp
    Source/Arrangement.cpp
    Source/Stretcher.cpp)

set(SECUENCIAS_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1)

if(UNIX AND NOT APPLE)
    # Linux: ALSA + JACK (con PipeWire se usa a través de pw-jack)
    list(APPEND SECUENCIAS_COMMON_DEFINITIONS JUCE_ALSA=1 JUCE_JACK=1)
endif()

# ---------------------------------------------------------------- App
juce_add_gui_app(Secuencias
    PRODUCT_NAME "Secuencias"
    COMPANY_NAME "Secuencias"
    BUNDLE_ID "com.secuencias.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/mac/icono.png")   # en macOS JUCE genera el .icns del bundle

if(APPLE)
    # El instalador de los motores de IA viaja dentro del bundle (Ajustes IA > Instalar motores de IA)
    target_sources(Secuencias PRIVATE scripts/instalar-ia.sh)
    set_source_files_properties(scripts/instalar-ia.sh PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

target_sources(Secuencias PRIVATE
    ${SECUENCIAS_CORE_SOURCES}
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/Meters.cpp
    Source/MixerPanel.cpp
    Source/TimelineView.cpp
    Source/FilePicker.cpp
    Source/StageView.cpp
    Source/Separator.cpp)

target_compile_definitions(Secuencias PRIVATE
    ${SECUENCIAS_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Secuencias,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Secuencias,JUCE_VERSION>")

target_link_libraries(Secuencias
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- Tests
if(SECUENCIAS_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(SecuenciasTests PRODUCT_NAME "SecuenciasTests")
    target_sources(SecuenciasTests PRIVATE ${SECUENCIAS_CORE_SOURCES} Source/Separator.cpp Tests/EngineTests.cpp)
    target_include_directories(SecuenciasTests PRIVATE Source)
    target_compile_definitions(SecuenciasTests PRIVATE ${SECUENCIAS_COMMON_DEFINITIONS})
    target_link_libraries(SecuenciasTests
        PRIVATE
            juce::juce_audio_devices
            juce::juce_audio_formats
            signalsmith-stretch
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME SecuenciasTests COMMAND SecuenciasTests)
endif()
