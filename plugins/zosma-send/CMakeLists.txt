cmake_minimum_required(VERSION 3.25)
project(ZosmaSend VERSION 0.1.3 LANGUAGES CXX)

include(CTest)
find_package(Threads REQUIRED)
add_executable(zosma-send-queue-test tests/audio_queue_test.cpp)
target_compile_features(zosma-send-queue-test PRIVATE cxx_std_17)
target_link_libraries(zosma-send-queue-test PRIVATE Threads::Threads)
add_test(NAME audio-queue COMMAND zosma-send-queue-test)

option(ZOSMA_QUEUE_TESTS_ONLY "Build portable queue tests without the VST SDK" OFF)
if(ZOSMA_QUEUE_TESTS_ONLY)
    return()
endif()
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "1Byte Labs Send prototype requires Windows x64")
endif()

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "Run explicitly in CI with enforced exit status" FORCE)
include(FetchContent)
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
    GIT_SUBMODULES base cmake pluginterfaces public.sdk
    GIT_SUBMODULES_RECURSE OFF
)
FetchContent_MakeAvailable(vst3sdk)
smtg_enable_vst3_sdk()
smtg_add_vst3plugin(zosma-send PACKAGE_NAME "1Byte Labs Send" SOURCES_LIST
    plugin.cpp ndi_audio.cpp editor.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
target_compile_features(zosma-send PRIVATE cxx_std_17)
target_include_directories(zosma-send PRIVATE
    ${vst3sdk_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ndi/include)
target_compile_definitions(zosma-send PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
    PROCESSINGNDILIB_STATIC NDILIB_CPP_DEFAULT_CONSTRUCTORS=0)
target_link_libraries(zosma-send PRIVATE sdk avrt user32 gdi32 uxtheme)
add_executable(zosma-send-editor-test tests/editor_smoke_test.cpp editor.cpp ndi_audio.cpp)
target_compile_features(zosma-send-editor-test PRIVATE cxx_std_17)
target_include_directories(zosma-send-editor-test PRIVATE ${vst3sdk_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ndi/include)
target_compile_definitions(zosma-send-editor-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 PROCESSINGNDILIB_STATIC NDILIB_CPP_DEFAULT_CONSTRUCTORS=0)
target_link_libraries(zosma-send-editor-test PRIVATE sdk avrt user32 gdi32 uxtheme)
add_test(NAME editor-smoke COMMAND zosma-send-editor-test)
add_executable(zosma-send-processing-test tests/processing_test.cpp editor.cpp ndi_audio.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp)
target_compile_features(zosma-send-processing-test PRIVATE cxx_std_17)
target_include_directories(zosma-send-processing-test PRIVATE ${vst3sdk_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ndi/include)
target_compile_definitions(zosma-send-processing-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 PROCESSINGNDILIB_STATIC NDILIB_CPP_DEFAULT_CONSTRUCTORS=0)
target_link_libraries(zosma-send-processing-test PRIVATE sdk avrt user32 gdi32 uxtheme)
add_test(NAME processing-transparency COMMAND zosma-send-processing-test)
if(MSVC)
    target_compile_options(zosma-send PRIVATE /W4 /utf-8 /EHsc)
    target_compile_options(zosma-send-editor-test PRIVATE /W4 /utf-8 /EHsc)
    target_compile_options(zosma-send-processing-test PRIVATE /W4 /utf-8 /EHsc)
endif()
