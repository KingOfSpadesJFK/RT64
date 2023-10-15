cmake_minimum_required(VERSION 3.12.0)
project(rt64 VERSION 0.1.0)

set(DLSS_VERSION 3.1.1)

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
set(DLSS_DIR ${CONTRIB_DIR}/DLSS)
set(FSR_DIR ${CONTRIB_DIR}/fsr)
# DX12 Register shift variables
#   These map the different dx12 registers to binding values on Vulkan
#   DO NOT CHANGE THESE UNLESS YOU KNOW WHAT YOU ARE DOING!!!!!!
#       And if you were to change them, change them in rt64_common.h too
set(CBV_SHIFT 0)
set(UAV_SHIFT 100)
set(SRV_SHIFT 200)
set(SAMPLER_SHIFT 300)

add_compile_definitions(
    $<$<CONFIG:Debug>:RT64_DEBUG>
    $<$<CONFIG:Release>:RT64_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:RT64_DEBUG>
    $<$<CONFIG:MinSizeRel>:RT64_MINIMAL>
)

# Get the Vulkan SDK Path
if(WIN32)
  if (NOT "$ENV{VK_SDK_PATH}" STREQUAL "")
      set(VK_SDK_PATH "$ENV{VK_SDK_PATH}" CACHE INTERNAL "Copied from environment variable")
  endif()
  message("-- VK_SDK_PATH = ${VK_SDK_PATH}")
  set(VK_USE_PLATFORM_WIN32_KHR ON)
endif()

# Makes it so the DX files can compile with Clang
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_definitions("-fms-extensions")
endif()

if(NOT (CMAKE_SIZEOF_VOID_P EQUAL 8))
    message( FATAL_ERROR "64-bit builds are mandatory for this framework" )
endif()

add_subdirectory(${LIBRT64VK_DIR})
add_subdirectory(${SAMPLE_DIR})

set_target_properties(rt64vk sample
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}"
)

##########################################################

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
