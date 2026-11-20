cmake_minimum_required(VERSION 3.27)
project(horror LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor /utf-8 /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # Faster debug info, full program optimization in release.
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<CONFIG:Release>:/GL>)
    add_link_options($<$<CONFIG:Release>:/LTCG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

find_package(Vulkan REQUIRED)

add_executable(horror
    src/main.cpp
    src/platform_win32.cpp
    src/player.cpp
    src/maze.cpp
    src/cloudtex.cpp
    src/audio.cpp
    src/vk_engine.cpp
)

target_include_directories(horror PRIVATE src)
target_link_libraries(horror PRIVATE Vulkan::Vulkan)

if(WIN32)
    target_link_libraries(horror PRIVATE user32 gdi32 winmm ole32 xaudio2)
endif()

# ---------------------------------------------------------------- shaders
find_program(GLSLC NAMES glslc glslc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK and re-run cmake.")
endif()
message(STATUS "glslc: ${GLSLC}")

set(SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT}")

set(SHADER_FILES sky.vert sky.frag mesh.vert mesh.frag rain.vert rain.frag)
set(SHADER_SPV)
foreach(s ${SHADER_FILES})
    set(out "${SHADER_OUT}/${s}.spv")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${GLSLC}" --target-env=vulkan1.3 -O
                "${CMAKE_SOURCE_DIR}/shaders/${s}" -o "${out}"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${s}"
        COMMENT "glslc ${s}"
        VERBATIM)
    list(APPEND SHADER_SPV "${out}")
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPV})
add_dependencies(horror shaders)

add_custom_command(TARGET horror POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_OUT}" "$<TARGET_FILE_DIR:horror>/shaders"
    COMMENT "copying shaders next to executable")
