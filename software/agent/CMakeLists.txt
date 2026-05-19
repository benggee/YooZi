cmake_minimum_required(VERSION 3.10)
project(yoozi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(yoozi cmd/main.cpp)

target_include_directories(yoozi PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/alibabacloud-nls-cpp-sdk/build/install/NlsSdk3.X_LINUX/include
)

find_package(CURL REQUIRED)
find_package(ALSA REQUIRED)
find_package(PkgConfig REQUIRED)
target_link_libraries(yoozi PRIVATE
    CURL::libcurl
    ${ALSA_LIBRARIES}
    ${CMAKE_SOURCE_DIR}/lib/alibabacloud-nls-cpp-sdk/lib/libalibabacloud-idst-speech.a
    pthread
    z
    dl
)
target_include_directories(yoozi PRIVATE ${ALSA_INCLUDE_DIRS})
