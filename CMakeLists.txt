cmake_minimum_required(VERSION 3.24)
project(OneByteLabsTransmitter VERSION 0.4.1 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Este aplicativo suporta Windows 10/11 x64.")
endif()

add_executable(onebytelabs-transmitter WIN32
  src/main.cpp
  src/capture.cpp
  src/ndi_sender.cpp
  src/ndi_sender_tray.cpp
  src/ip_filter.cpp
  src/protected_ip_ui.cpp
  src/static_preview_policy.cpp
  src/tray_controller.cpp
  src/network_status_ui.cpp
  src/brand_ui_stable.cpp
  src/settings.cpp
  src/resources.rc
)

add_executable(onebytelabs-receiver WIN32
  src/receiver_main.cpp
  src/resources.rc
)

# Renomeia somente a implementação original de sendFrame() para sendFrameRaw().
# O wrapper em ndi_sender_tray.cpp decide se envia o quadro real ou a tela manual de privacidade.
set_source_files_properties(src/ndi_sender.cpp PROPERTIES
  COMPILE_DEFINITIONS "NDI_SENDER_IMPL=1"
)

target_compile_features(onebytelabs-transmitter PRIVATE cxx_std_20)
target_compile_features(onebytelabs-receiver PRIVATE cxx_std_20)
target_compile_definitions(onebytelabs-transmitter PRIVATE
  UNICODE
  _UNICODE
  WINVER=0x0A00
  _WIN32_WINNT=0x0A00
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PROCESSINGNDILIB_STATIC
  NDILIB_CPP_DEFAULT_CONSTRUCTORS=0
)

target_compile_definitions(onebytelabs-receiver PRIVATE
  UNICODE
  _UNICODE
  WINVER=0x0A00
  _WIN32_WINNT=0x0A00
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  PROCESSINGNDILIB_STATIC
  NDILIB_CPP_DEFAULT_CONSTRUCTORS=0
)

target_include_directories(onebytelabs-transmitter PRIVATE
  src
  third_party/ndi/include
)

target_include_directories(onebytelabs-receiver PRIVATE
  src
  third_party/ndi/include
)

target_link_libraries(onebytelabs-transmitter PRIVATE
  user32
  gdi32
  comctl32
  shell32
  dwmapi
  uxtheme
  iphlpapi
  wlanapi
  ws2_32
  fwpuclnt
  ole32
  uuid
  avrt
)

target_link_libraries(onebytelabs-receiver PRIVATE
  user32
  gdi32
  comctl32
  dwmapi
  uxtheme
  winmm
)

if(MSVC)
  target_compile_options(onebytelabs-transmitter PRIVATE
    /W4
    /permissive-
    /utf-8
    /EHsc
    /FIui_compat.h
  )
  target_compile_options(onebytelabs-receiver PRIVATE
    /W4
    /permissive-
    /utf-8
    /EHsc
  )
  target_link_options(onebytelabs-transmitter PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
  target_link_options(onebytelabs-receiver PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO)
endif()

set_target_properties(onebytelabs-transmitter PROPERTIES
  OUTPUT_NAME "1Byte Labs Transmitter"
)

set_target_properties(onebytelabs-receiver PROPERTIES
  OUTPUT_NAME "1Byte Labs Receiver"
)
