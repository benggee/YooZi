cmake_minimum_required(VERSION 3.14)
project(camera-stream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# cpp-httplib (header-only HTTP/HTTPS server)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
)
FetchContent_MakeAvailable(httplib)

find_package(OpenCV REQUIRED COMPONENTS core videoio imgproc imgcodecs)

add_executable(camera-stream main.cpp)
target_link_libraries(camera-stream PRIVATE
    httplib::httplib
    ${OpenCV_LIBS}
    pthread
)
target_include_directories(camera-stream PRIVATE ${OpenCV_INCLUDE_DIRS})
