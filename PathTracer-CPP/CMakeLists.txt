cmake_minimum_required(VERSION 3.20)
project(PathTracerCPP LANGUAGES CXX)

# Windows only: the live preview window (PPMPreviewWindow.h) uses Win32.
if(NOT WIN32)
    message(FATAL_ERROR "PathTracer-CPP currently targets Windows (Win32 preview window).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PathTracer-CPP
    Renderer.cpp
    Scenes.cpp
    Tests.cpp
    Benchmark.cpp
    Stats.cpp
    rtw_stb_image.cpp
    tiny_obj_impl.cpp
)

if(MSVC)
    target_compile_options(PathTracer-CPP PRIVATE /W3 /permissive- /utf-8)
    # Same as the .vcxproj (CharacterSet = Unicode).
    target_compile_definitions(PathTracer-CPP PRIVATE _CONSOLE UNICODE _UNICODE)
endif()

# Scenes load textures relative to the working directory (PathTracer-CPP/).
set_target_properties(PathTracer-CPP PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
