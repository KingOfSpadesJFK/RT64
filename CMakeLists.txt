cmake_minimum_required(VERSION 3.0.0)
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
set(NVPRO_DIR ${CONTRIB_DIR}/nvpro_core)
set(IMGUI_DIR ${CONTRIB_DIR}/imgui)
set(VMA_DIR ${CONTRIB_DIR}/VulkanMemoryAllocator)
set(DXC_DIR ${CONTRIB_DIR}/dxc)

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

execute_process(COMMAND echo Building geometry shaders ${shaderName}...)
execute_process(COMMAND dxc ${SHADER_DIR}/Im3DGSPoints.hlsl -spirv -fspv-target-env=vulkan1.2 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSPoints.hlsl.h -Vn Im3DGSPoints_SPIRV)
execute_process(COMMAND dxc ${SHADER_DIR}/Im3DGSLines.hlsl -spirv -fspv-target-env=vulkan1.2 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSLines.hlsl.h -Vn Im3DGSPLines_SPIRV)

#######################################################################
# The real building

# Build the nvpro_core sources
# I probs shouldn't build them all but I tried just building the
#  ones I need and got tired of it
set(NVPWINDOW_SOURCE
  ${NVPRO_DIR}/nvp/perproject_globals.cpp
)
file(GLOB NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/*.cpp ${NVPRO_DIR}/nvh/*.hpp ${NVPRO_DIR}/nvh/*.inl ${NVPRO_DIR}/nvh/*.h)
file(GLOB NVHELPERSVK_SOURCE ${NVPRO_DIR}/nvvk/*.cpp ${NVPRO_DIR}/nvvk/*.hpp ${NVPRO_DIR}/nvvk/*.inl ${NVPRO_DIR}/nvvk/*.h)

# Exclude these nvpro_core sources
list(REMOVE_ITEM NVHELPERSVK_SOURCE ${NVPRO_DIR}/nvvk/appwindowprofiler_vk.cpp)
list(REMOVE_ITEM NVHELPERSVK_SOURCE ${NVPRO_DIR}/nvvk/appwindowprofiler_vk.hpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/gltfscene.cpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/gltfscene.hpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/appwindowprofiler.cpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/appwindowprofiler.hpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/cameramanipulator.cpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/cameramanipulator.hpp)

# Include the dxc headers
if(UNIX)
    set(DXC_HEADER_DIR ${DXC_DIR}/include/linux)
else()
    set(DXC_HEADER_DIR ${DXC_DIR}/include/win)
endif()

source_group("nvvk" FILES ${NVHELPERSVK_SOURCE})
source_group("nvh" FILES ${NVHELPERS_SOURCE})
source_group("nvp" FILES ${NVPWINDOW_SOURCE})

if(UNIX)
    LIST(APPEND PLATFORM /usr/include/glm/simd)
endif()

include_directories(
    ${LIBRT64VK_DIR}/public/
)

# Build the library
add_library(rt64vk SHARED 
    ${LIBRT64VK_DIR}/private/rt64_vma.cpp
    ${LIBRT64VK_DIR}/private/rt64_common.cpp 
    ${LIBRT64VK_DIR}/private/rt64_device.cpp
    ${LIBRT64VK_DIR}/private/rt64_mesh.cpp
    ${NVHELPERS_SOURCE}
    ${NVHELPERSVK_SOURCE}
    ${NVPWINDOW_SOURCE}
)
target_include_directories(rt64vk PRIVATE 
    ${LIBRT64VK_DIR}
    ${CONTRIB_DIR}
    ${VMA_DIR}
    ${NVPRO_DIR}
    ${IMGUI_DIR}
    ${NVPWINDOW_SOURCE}
    ${PLATFORM}
    ${DXC_DIR}
)
target_link_libraries(rt64vk
    vulkan
    glfw
)

# Build the sample
add_executable(sample 
    ${SAMPLE_DIR}/main.cpp
)
target_link_libraries(sample rt64vk)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
