cmake_minimum_required(VERSION 3.21)
project(forward-vulkan LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
set(GLSLANG "$ENV{HOME}/tools/glslang/bin/glslangValidator" CACHE FILEPATH "glslangValidator path")

add_executable(forward_vulkan
    src/main.cpp
    src/Device.cpp
    src/GpuResources.cpp
    src/Scene.cpp
    src/Gltf.cpp
    src/Renderer.cpp
    vendor/volk/volk.c
)
target_compile_definitions(forward_vulkan PRIVATE
    VK_NO_PROTOTYPES
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
)
target_include_directories(forward_vulkan PRIVATE
    src
    ${VENDOR}/volk
    ${VENDOR}/vulkan/include
    ${VENDOR}/glm
    ${VENDOR}/json/include
    ${VENDOR}/stb
)

find_package(Threads REQUIRED)
target_link_libraries(forward_vulkan PRIVATE dl Threads::Threads)

if(EXISTS ${GLSLANG})
    set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
    set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})
    foreach(STAGE vert frag)
        add_custom_command(
            OUTPUT ${SHADER_OUT}/forward.${STAGE}.spv
            COMMAND ${GLSLANG} -V --target-env vulkan1.3 -o ${SHADER_OUT}/forward.${STAGE}.spv ${SHADER_SRC}/forward.${STAGE}
            DEPENDS ${SHADER_SRC}/forward.${STAGE}
            COMMENT "GLSL -> SPIR-V forward.${STAGE}"
        )
    endforeach()
    add_custom_target(shaders DEPENDS
        ${SHADER_OUT}/forward.vert.spv
        ${SHADER_OUT}/forward.frag.spv)
    add_dependencies(forward_vulkan shaders)
    target_compile_definitions(forward_vulkan PRIVATE
        SPIRV_DIR="${SHADER_OUT}/")
else()
    message(WARNING "glslangValidator not found at ${GLSLANG}; SPIR-V must be provided manually")
endif()
