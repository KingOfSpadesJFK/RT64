cmake_minimum_required(VERSION 3.12.0)
project(rt64 VERSION 0.1.0)

include(CTest)
enable_testing()

# rt64 directories
set(LIBRT64VK_DIR ${PROJECT_SOURCE_DIR}/src/rt64vk)
set(SAMPLE_DIR ${PROJECT_SOURCE_DIR}/src/sample)
set(CONTRIB_DIR ${LIBRT64VK_DIR}/contrib)
set(RT64_HEADER ${LIBRT64VK_DIR}/public/rt64.h)
set(SHADER_DIR ${LIBRT64VK_DIR}/shaders)
# third party directories
set(IMGUI_DIR ${CONTRIB_DIR}/imgui)
set(VMA_DIR ${CONTRIB_DIR}/VulkanMemoryAllocator)
set(DXC_DIR ${CONTRIB_DIR}/dxc)
set(NVPRO_DIR ${CONTRIB_DIR}/nvpro_core)
set(IM3D_DIR ${CONTRIB_DIR}/im3d)

# DX12 Register shift variables
#   These map the different dx12 registers to binding values on Vulkan
#   DO NOT CHANGE THESE UNLESS YOU KNOW WHAT YOU ARE DOING!!!!!!
#       And if you were to change them, change them in rt64_common.h too
set(CBV_SHIFT 0)
set(UAV_SHIFT 100)
set(SRV_SHIFT 200)
set(SAMPLER_SHIFT 300)

if(NOT (CMAKE_SIZEOF_VOID_P EQUAL 8))
  message( FATAL_ERROR "64-bit builds are mandatory for this framework" )
endif()

#######################################################################
# Shader building

add_custom_target(shaders)

# Build vertex shaders
file(GLOB vertexShaders ${SHADER_DIR}/*VS.hlsl)
foreach(shader ${vertexShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building vertex shader ${shaderName}...
        COMMAND ${DXC_DIR}/bin/dxc ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T vs_6_3 -E VSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build pixel shaders
file(GLOB pixelShaders ${SHADER_DIR}/*PS.hlsl)
foreach(shader ${pixelShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building pixel shader ${shaderName}...
        COMMAND ${DXC_DIR}/bin/dxc ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T ps_6_3 -E PSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build compute shaders
file(GLOB computeShaders ${SHADER_DIR}/*CS.hlsl)
foreach(shader ${computeShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building compute shader ${shaderName}...
        COMMAND ${DXC_DIR}/bin/dxc ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T cs_6_3 -E mainCS -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build ray generation shaders
file(GLOB rayShaders ${SHADER_DIR}/*RayGen.hlsl)
foreach(shader ${rayShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building raygen shader ${shaderName}...
        COMMAND ${DXC_DIR}/bin/dxc ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T lib_6_3 -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build geometry shaders
add_custom_command(TARGET shaders PRE_BUILD
    COMMAND echo Building geometry shaders Im3DGSPoints and Im3DGSLines..
    COMMAND ${DXC_DIR}/bin/dxc ${SHADER_DIR}/Im3DGSPoints.hlsl -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0  -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSPoints.hlsl.h -Vn Im3DGSPoints_SPIRV
    COMMAND ${DXC_DIR}/bin/dxc ${SHADER_DIR}/Im3DGSLines.hlsl -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSLines.hlsl.h -Vn Im3DGSLines_SPIRV
)

#######################################################################
# Build the library

# Include the dxc headers
if(UNIX)
    set(DXC_HEADER_DIR ${DXC_DIR}/include/linux)
    file(GLOB DXC_LIBRARIES ${DXC_DIR}/lib/libdxcompiler.so)    
else()
    set(DXC_HEADER_DIR ${DXC_DIR}/include/win)
endif()

if(UNIX)
    LIST(APPEND PLATFORM /usr/include/glm/simd)
endif()

include_directories(
    ${LIBRT64VK_DIR}/public/
)

file(GLOB NVH_SOURCE ${NVPRO_DIR}/nvh/*.hpp ${NVPRO_DIR}/nvh/*.cpp ${NVPRO_DIR}/nvh/*.h ${NVPRO_DIR}/nvh/*.inl)
file(GLOB NVVK_SOURCE ${NVPRO_DIR}/nvvk/*.hpp ${NVPRO_DIR}/nvvk/*.cpp ${NVPRO_DIR}/nvvk/*.h ${NVPRO_DIR}/nvvk/*.inl)
file(GLOB NVMATH_SOURCE ${NVPRO_DIR}/*.hpp ${NVPRO_DIR}/nvmath/*.cpp ${NVPRO_DIR}/nvmath/*.h ${NVPRO_DIR}/nvmath/*.inl)
file(GLOB SHADER_HEADERS ${SHADER_DIR}/*.hlsl.h)   

# Add the library as a target
add_library(rt64vk SHARED 
    ${LIBRT64VK_DIR}/private/rt64_vma.cpp
    ${LIBRT64VK_DIR}/private/rt64_common.cpp 
    ${LIBRT64VK_DIR}/private/rt64_device.cpp
    ${LIBRT64VK_DIR}/private/rt64_scene.cpp
    ${LIBRT64VK_DIR}/private/rt64_view.cpp
    ${LIBRT64VK_DIR}/private/rt64_mesh.cpp
    ${LIBRT64VK_DIR}/private/rt64_texture.cpp
    ${LIBRT64VK_DIR}/private/rt64_shader.cpp
    ${LIBRT64VK_DIR}/private/rt64_instance.cpp
    # ${LIBRT64VK_DIR}/private/rt64_inspector.cpp
    ${NVPRO_DIR}/nvp/perproject_globals.cpp
    ${SHADER_HEADERS}
    ${NVVK_SOURCE}
    ${NVH_SOURCE}
    ${NVMATH_SOURCE}
    ${IM3D_DIR}/im3d.cpp
)
target_include_directories(rt64vk PRIVATE 
    ${LIBRT64VK_DIR}
    ${CONTRIB_DIR}
    ${VMA_DIR}
    ${IMGUI_DIR}
    ${DXC_DIR}
    ${PLATFORM}
    ${NVPRO_DIR}
)
target_link_libraries(rt64vk
    vulkan
    glfw
    ${DXC_LIBRARIES}
)

set_property(TARGET rt64vk PROPERTY CXX_STANDARD 20)

#######################################################################
# Build the sample

# Add the sample as a target
add_executable(sample 
    ${SAMPLE_DIR}/main.cpp
)
target_link_libraries(sample 
    rt64vk
)
target_include_directories(sample PRIVATE 
    ${SAMPLE_DIR}/contrib/tinygltf
)
# Copy the res files into the build folder
add_custom_command(TARGET sample POST_BUILD
    COMMAND cp -r ${SAMPLE_DIR}/res ${CMAKE_BINARY_DIR}
)

set_property(TARGET sample PROPERTY CXX_STANDARD 20)

##########################################################

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
