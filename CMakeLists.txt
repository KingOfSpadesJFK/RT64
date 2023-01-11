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

set(NVPWINDOW_SOURCE
  ${NVPRO_DIR}/nvp/nvpwindow.cpp
  ${NVPRO_DIR}/nvp/nvpwindow.hpp
  ${NVPRO_DIR}/nvp/nvpsystem.cpp
  ${NVPRO_DIR}/nvp/nvpsystem.hpp
  ${NVPRO_DIR}/nvp/nvpfilesystem.cpp
  ${NVPRO_DIR}/nvp/nvpfilesystem.hpp
  # Do not add project_name.cpp here as it contains per-project dependent data.
  # perproject_globals.cpp will be automatically added to the per-project builds via
  # COMMON_SOURCE_FILES
  ${NVPRO_DIR}/nvp/perproject_globals.hpp
)
if(UNIX) 
  LIST(APPEND NVPWINDOW_SOURCE ${NVPRO_DIR}/nvp/nvpsystem_linux.cpp )
else()
  LIST(APPEND NVPWINDOW_SOURCE ${NVPRO_DIR}/nvp/nvpsystem_win32.cpp )
endif()

file(GLOB FILEFORMATS_SOURCE  ${NVPRO_DIR}/fileformats/*.cpp  ${NVPRO_DIR}/fileformats/*.hpp  ${NVPRO_DIR}/fileformats/*.inl  ${NVPRO_DIR}/fileformats/*.h)
file(GLOB NVMATH_SOURCE ${NVPRO_DIR}/nvmath/*.cpp ${NVPRO_DIR}/nvmath/*.hpp ${NVPRO_DIR}/nvmath/*.inl ${NVPRO_DIR}/nvmath/*.h)
file(GLOB NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/*.cpp ${NVPRO_DIR}/nvh/*.hpp ${NVPRO_DIR}/nvh/*.inl ${NVPRO_DIR}/nvh/*.h)
file(GLOB NVHELPERSVK_SOURCE ${NVPRO_DIR}/nvvk/*.cpp ${NVPRO_DIR}/nvvk/*.hpp ${NVPRO_DIR}/nvvk/*.inl ${NVPRO_DIR}/nvvk/*.h)
file(GLOB NVVKHL_SOURCE ${NVPRO_DIR}/nvvkhl/*.cpp ${NVPRO_DIR}/nvvkhl/*.hpp ${NVPRO_DIR}nvvkl/*.inl ${NVPRO_DIR}nvvkhl/*.h)

list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/gltfscene.cpp)
list(REMOVE_ITEM NVHELPERS_SOURCE ${NVPRO_DIR}/nvh/gltfscene.hpp)
list(REMOVE_ITEM FILEFORMATS_SOURCE ${NVPRO_DIR}/fileformats/tiny_gltf_freeimage.h)
  
source_group("fileformats" FILES ${FILEFORMATS_SOURCE})
source_group("nvmath" FILES ${NVMATH_SOURCE})
source_group("nvh" FILES ${NVHELPERS_SOURCE})
source_group("nvvk" FILES ${NVHELPERSVK_SOURCE})
source_group("nvvkhl" FILES ${NVVKHL_SOURCE})
source_group("nvp" FILES ${NVPWINDOW_SOURCE})

include_directories(
    ${LIBRT64VK_DIR}/public/
    /usr/include/glm/simd/
)

# Build the library
add_library(rt64vk SHARED 
    ${LIBRT64VK_DIR}/private/rt64_common.cpp 
    ${LIBRT64VK_DIR}/private/rt64_device.cpp
    ${NVMATH_SOURCE}
    ${NVHELPERS_SOURCE}
    ${NVHELPERSVK_SOURCE}
    ${NVVKHL_SOURCE}
    ${NVPWINDOW_SOURCE}
    ${FILEFORMATS_SOURCE}
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
    platform
)

# Build the sample
add_executable(sample ${SAMPLE_DIR}/main.cpp)
target_link_libraries(sample rt64vk)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
