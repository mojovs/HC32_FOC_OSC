cmake_minimum_required(VERSION 3.16)
project(smdrive_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive- /W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets SerialPort Test)

# ---------------------------------------------------------------------------
# 核心库：协议 / 数据 / 传输 / 配置（不依赖 Widgets，便于单元测试与后续换 UI）
# ---------------------------------------------------------------------------
add_library(smd_core STATIC
    src/protocol/frame.cpp
    src/protocol/frame.h
    src/protocol/frame_builder.cpp
    src/protocol/frame_builder.h
    src/protocol/frame_decoder.cpp
    src/protocol/frame_decoder.h
    src/data/datahub.cpp
    src/data/datahub.h
    src/data/recorder.cpp
    src/data/recorder.h
    src/data/ring_buffer.h
    src/transport/itransport.h
    src/transport/serial_transport.cpp
    src/transport/serial_transport.h
    src/transport/replay_transport.cpp
    src/transport/replay_transport.h
    src/app/app_config.cpp
    src/app/app_config.h
    src/app/channel_table.h
    src/app/serial_text_codec.cpp
    src/app/serial_text_codec.h
)
target_include_directories(smd_core PUBLIC src)
target_link_libraries(smd_core PUBLIC Qt5::Core Qt5::Gui Qt5::SerialPort)

# ---------------------------------------------------------------------------
# GUI（保留控制台，方便看日志；如需纯 GUI 去掉注释）
# ---------------------------------------------------------------------------
add_executable(smdrive-host
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/waveform_view.cpp
    src/ui/waveform_view.h
    src/ui/table_panel.cpp
    src/ui/table_panel.h
    src/ui/param_panel.cpp
    src/ui/param_panel.h
    src/ui/serial_assistant.cpp
    src/ui/serial_assistant.h
)
target_link_libraries(smdrive-host PRIVATE smd_core Qt5::Widgets)
# set_target_properties(smdrive-host PROPERTIES WIN32_EXECUTABLE ON)

# ---------------------------------------------------------------------------
# 单元测试
# ---------------------------------------------------------------------------
enable_testing()
add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE smd_core Qt5::Test)
add_test(NAME protocol COMMAND test_protocol)

find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt5_DIR}/../../../bin")
message(STATUS "Qt ${Qt5_VERSION} | windeployqt: ${WINDEPLOYQT_EXE}")
