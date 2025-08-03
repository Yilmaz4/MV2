cmake_minimum_required(VERSION 3.21)

project(MV2 VERSION 1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
set(B_PRODUCTION_MODE ON)
set(JSON_BuildTests OFF CACHE INTERNAL "")

set(GLFW_BUILD_WIN32 True)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(lib/boxer)
add_subdirectory(lib/embed)
add_subdirectory(lib/glm)
add_subdirectory(lib/glfw)

include_directories(SYSTEM
    include
    lib
    lib/chromium
    lib/glad/include
    lib/imgui 
    lib/imgui/backends
    lib/json
    lib/stb
    lib/tinyfiledialogs
)

find_package(OpenGL REQUIRED)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB IMGUI_GLOB
    ${IMGUI_DIR}/imgui.h
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imconfig.h
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_internal.h
    ${IMGUI_DIR}/imstb_rectpack.h
    ${IMGUI_DIR}/imstb_textedit.h
    ${IMGUI_DIR}/imstb_truetype.h
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp

    ${IMGUI_DIR}/backends/imgui_impl_glfw.h
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.h
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3_loader.cpp
)

add_library(imgui STATIC ${IMGUI_GLOB})
target_include_directories(imgui PUBLIC ${IMGUI_PATH})
target_link_libraries(imgui PRIVATE glfw)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    lib/glad/src/glad.c
    lib/chromium/cubic_bezier.cpp
    lib/tinyfiledialogs/tinyfiledialogs.cpp
    include/ImGradientHDR.cpp
)

b_embed(${PROJECT_NAME} assets/commitmono.otf)
b_embed(${PROJECT_NAME} assets/adwaita.ttf)

b_embed(${PROJECT_NAME} shaders/render.glsl)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        OpenGL::GL
        Boxer
        glm::glm
        glfw
        imgui
)
target_include_directories(${PROJECT_NAME} PUBLIC imgui)

target_compile_definitions(${PROJECT_NAME} PRIVATE CV_STATIC)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DPLATFORM_WINDOWS)
    target_compile_definitions(Boxer PRIVATE UNICODE)
    target_link_libraries(${PROJECT_NAME} PRIVATE Dwmapi)
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DPLATFORM_MAC)
elseif(UNIX)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DPLATFORM_LINUX)
endif()