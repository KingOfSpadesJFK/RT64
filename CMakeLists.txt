cmake_minimum_required(VERSION 3.0.0)
project(rt64 VERSION 0.1.0)

include(CTest)
enable_testing()

set(LIBRT64VK_DIR ${PROJECT_SOURCE_DIR}/src/rt64vk)
set(SAMPLE_DIR ${PROJECT_SOURCE_DIR}/src/sample)
set(RT64_HEADER ${LIBRT64VK_DIR}/public/rt64.h)
set(NVPRO_DIR ${LIBRT64VK_DIR}/contrib/nvpro_core)
set(SHADER_DIR ${LIBRT64VK_DIR}/shaders)

if(NOT (CMAKE_SIZEOF_VOID_P EQUAL 8))
  message( FATAL_ERROR "64-bit builds are mandatory for this framework" )
endif()

#######################################################################
# Shader building

# Build vertex shaders
file(GLOB vertexShaders ${SHADER_DIR}/*VS.hlsl)
foreach(shader ${vertexShaders})
    cmake_path(GET shader STEM shaderName)
    execute_process(COMMAND echo Building vertex shader ${shaderName}...)
    execute_process(COMMAND dxc ${shader} -spirv -fspv-target-env=vulkan1.2 -T vs_6_3 -E VSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV)
endforeach()

# Build pixel shaders
file(GLOB pixelShaders ${SHADER_DIR}/*PS.hlsl)
foreach(shader ${pixelShaders})
    cmake_path(GET shader STEM shaderName)
    execute_process(COMMAND echo Building pixel shader ${shaderName}...)
    execute_process(COMMAND dxc ${shader} -spirv -fspv-target-env=vulkan1.2 -T ps_6_3 -E PSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV)
endforeach()

# Build compute shaders
file(GLOB computeShaders ${SHADER_DIR}/*CS.hlsl)
foreach(shader ${computeShaders})
    cmake_path(GET shader STEM shaderName)
    execute_process(COMMAND echo Building compute shader ${shaderName}...)
    execute_process(COMMAND dxc ${shader} -spirv -fspv-target-env=vulkan1.2 -T cs_6_3 -E mainCS -Fh ${shader}.h -Vn ${shaderName}_SPIRV)
endforeach()

# Build ray generation shaders
file(GLOB rayShaders ${SHADER_DIR}/*RayGen.hlsl)
foreach(shader ${rayShaders})
    cmake_path(GET shader STEM shaderName)
    execute_process(COMMAND echo Building raygen shader ${shaderName}...)
    execute_process(COMMAND dxc ${shader} -spirv -fspv-target-env=vulkan1.2 -T lib_6_3 -Fh ${shader}.h -Vn ${shaderName}_SPIRV)
endforeach()

#######################################################################
# The real building

include_directories(
    ${LIBRT64VK_DIR}/public/
    /usr/include/glm/simd/
)

# Build the library
add_library(rt64vk SHARED 
    ${LIBRT64VK_DIR}/private/rt64_common.cpp 
    ${LIBRT64VK_DIR}/private/rt64_device.cpp
)
target_include_directories(rt64vk PRIVATE 
    ${NVPRO_DIR}/
    ${NVPRO_DIR}/nvvkhl/
    ${NVPRO_DIR}/nvvk/
    ${NVPRO_DIR}/nvh/
    ${NVPRO_DIR}/nvp/
    ${NVPRO_DIR}/nvmath/
    ${NVPRO_DIR}/fileformats/
)
target_link_libraries(rt64vk
    vulkan
    glfw
)

# Build the sample
add_executable(sample ${SAMPLE_DIR}/main.cpp)
target_link_libraries(sample rt64vk)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
