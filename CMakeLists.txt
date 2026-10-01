cmake_minimum_required(VERSION 3.21)
project(PathTracerCPP LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The current preview implementation requires Windows with MSVC.")
endif()

set(PT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/PathTracer-CPP")

add_executable(PathTracer
    ${PT_SRC}/Renderer.cpp
    ${PT_SRC}/Scenes.cpp
    ${PT_SRC}/Tests.cpp
    ${PT_SRC}/Benchmark.cpp
    ${PT_SRC}/Stats.cpp
    ${PT_SRC}/rtw_stb_image.cpp
    ${PT_SRC}/tiny_obj_impl.cpp
)
target_compile_features(PathTracer PRIVATE cxx_std_20)
target_compile_definitions(PathTracer PRIVATE _CONSOLE UNICODE _UNICODE)
target_compile_options(PathTracer PRIVATE /utf-8 /W3 /permissive-)
target_link_libraries(PathTracer PRIVATE user32 gdi32)
# Scenes and tests load assets relative to PathTracer-CPP/.
set_property(TARGET PathTracer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${PT_SRC}")

enable_testing()
add_test(NAME unit_tests COMMAND PathTracer --test WORKING_DIRECTORY "${PT_SRC}")
add_test(NAME regression COMMAND PathTracer --regress WORKING_DIRECTORY "${PT_SRC}")
