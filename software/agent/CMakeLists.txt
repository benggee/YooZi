cmake_minimum_required(VERSION 3.10)
project(yoozi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(yoozi cmd/main.cpp)

target_include_directories(yoozi PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

find_package(CURL REQUIRED)
find_package(ALSA REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPEEXDSP REQUIRED speexdsp)

target_link_libraries(yoozi PRIVATE
    CURL::libcurl
    ${ALSA_LIBRARIES}
    ${SPEEXDSP_LIBRARIES}
    pthread
)
target_include_directories(yoozi PRIVATE ${SPEEXDSP_INCLUDE_DIRS})
target_include_directories(yoozi PRIVATE ${ALSA_INCLUDE_DIRS})
