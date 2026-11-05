cmake_minimum_required(VERSION 3.20)
project(PLAYR-Launcher LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "PLAYR-Launcher uses the Win32 API and MSVC-specific pragmas; build it with Visual Studio's C++ tools.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # static runtime: the exe needs nothing installed

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(playr-launcher WIN32 ${SOURCES} resources/PLAYR-Launcher.rc)
set_target_properties(playr-launcher PROPERTIES OUTPUT_NAME "PLAYR-Launcher")

target_include_directories(playr-launcher PRIVATE src)
target_compile_options(playr-launcher PRIVATE /utf-8 /EHsc /W3)
target_link_libraries(playr-launcher PRIVATE
    user32 gdi32 advapi32 shell32 ole32 gdiplus winhttp dwmapi comctl32 winmm)
