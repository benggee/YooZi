cmake_minimum_required(VERSION 3.10)
project(mose LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mose cmd/main.cpp)

target_include_directories(mose PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib
)

find_package(CURL REQUIRED)
find_package(ALSA REQUIRED)
target_link_libraries(mose PRIVATE
    CURL::libcurl
    ${ALSA_LIBRARIES}
    pthread
)
target_include_directories(mose PRIVATE ${ALSA_INCLUDE_DIRS})
