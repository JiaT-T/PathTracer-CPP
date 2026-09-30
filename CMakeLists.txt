cmake_minimum_required(VERSION 3.21)
project(PathTracerCPP LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The current preview and parallel implementation requires Windows with MSVC.")
endif()

add_executable(PathTracer
    PathTracer-CPP/Renderer.cpp
    PathTracer-CPP/rtw_stb_image.cpp
    PathTracer-CPP/tiny_obj_impl.cpp
)
target_compile_features(PathTracer PRIVATE cxx_std_20)
target_compile_definitions(PathTracer PRIVATE UNICODE _UNICODE)
target_compile_options(PathTracer PRIVATE /utf-8 /W3 /permissive-)
target_link_libraries(PathTracer PRIVATE user32 gdi32)
set_property(TARGET PathTracer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/PathTracer-CPP")

enable_testing()
add_test(NAME pbr_smoke COMMAND PathTracer --smoke)
