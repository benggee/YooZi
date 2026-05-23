cmake_minimum_required(VERSION 3.16)
project(windows-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core WebSockets Network Widgets)

add_executable(windows-proxy WIN32
    src/main.cpp
    src/ws_client.cpp
    src/command_executor.cpp
    src/ollama_client.cpp
    src/tray_icon.cpp
)

target_link_libraries(windows-proxy PRIVATE
    Qt5::Core
    Qt5::WebSockets
    Qt5::Network
    Qt5::Widgets
)
