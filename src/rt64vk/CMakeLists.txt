#######################################################################
# Build the library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include third party libraries
if(UNIX)
    # Vulkan and GLFW
    set(VULKAN_LIBRARY vulkan)
    set(GLFW_LIBRARY glfw)
    # DXC
    set(DXC_INCLUDE ${DXC_DIR}/include/linux)
    file(GLOB DXC_LIBRARY ${DXC_DIR}/lib/libdxcompiler.so)
    # DLSS 
    file(GLOB DLSS_LIBRARY ${DLSS_DIR}/lib/Linux_x86_64/*.a)   
    # FSR
    file(GLOB FSR_LIBRARY ${FSR_DIR}/lib/libffx_fsr2_api_*x86_64.a)
    # GLM Platform
    set(PLATFORM /usr/include/glm/simd)
    # ImGui
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp)
else() 
    # Vulkan
    set(VK_SDK_HEADERS ${VK_SDK_PATH}\\Include) 
    file(GLOB VULKAN_LIBRARY ${VK_SDK_PATH}\\Lib/*.lib)
    # DXC
    set(DXC_INCLUDE ${DXC_DIR}/include/win)
    file(GLOB DXC_LIBRARY ${DXC_DIR}/lib/win-x64/dxcompiler.lib)
    # DLSS
    if(${CMAKE_BUILD_TYPE} STREQUAL Debug)
        list(APPEND DLSS_LIBRARY ${DLSS_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d_dbg.lib)   
    else()
        list(APPEND DLSS_LIBRARY ${DLSS_DIR}/lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib)
    endif()
    # FSR
    file(GLOB FSR_LIBRARY ${FSR_DIR}/lib/ffx_fsr2_api_*x64.lib ${FSR_DIR}/lib/ffx_fsr2_api_*x64.exp)
    # GLM Platform
    set(PLATFORM ${VK_SDK_PATH}\\Include\\glm\\simd)
    # ImGui
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
endif()
message("${DLSS_LIBRARY}")

include_directories(
    ${LIBRT64VK_DIR}/public/
)

file(GLOB NVH_SOURCE ${NVPRO_DIR}/nvh/*.hpp ${NVPRO_DIR}/nvh/*.cpp ${NVPRO_DIR}/nvh/*.h ${NVPRO_DIR}/nvh/*.inl)
file(GLOB NVVK_SOURCE ${NVPRO_DIR}/nvvk/*.hpp ${NVPRO_DIR}/nvvk/*.cpp ${NVPRO_DIR}/nvvk/*.h ${NVPRO_DIR}/nvvk/*.inl)
file(GLOB NVMATH_SOURCE ${NVPRO_DIR}/*.hpp ${NVPRO_DIR}/nvmath/*.cpp ${NVPRO_DIR}/nvmath/*.h ${NVPRO_DIR}/nvmath/*.inl)
file(GLOB SHADER_HEADERS ${SHADER_DIR}/*.hlsl.h)   

# Include the shaders directory
add_subdirectory(./shaders)

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
    ${LIBRT64VK_DIR}/private/rt64_inspector.cpp
    ${LIBRT64VK_DIR}/private/rt64_upscaler.cpp
    ${LIBRT64VK_DIR}/private/rt64_mipmaps.cpp
    ${LIBRT64VK_DIR}/private/rt64_dlss.cpp
    ${LIBRT64VK_DIR}/private/rt64_fsr.cpp
    ${NVPRO_DIR}/nvp/perproject_globals.cpp
    ${SHADER_HEADERS}
    ${NVVK_SOURCE}
    ${NVH_SOURCE}
    ${NVMATH_SOURCE}
    ${IM3D_DIR}/im3d.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(rt64vk PRIVATE 
    ${MINGW64_INCLUDE}
    ${VK_SDK_HEADERS}
    ${GLFW_INCLUDE}
    ${LIBRT64VK_DIR}
    ${CONTRIB_DIR}
    ${VMA_DIR}
    ${IMGUI_DIR}
    ${DXC_DIR}
    ${DXC_INCLUDE}
    ${PLATFORM}
    ${NVPRO_DIR}
    ${DLSS_DIR}/include
    ${FSR_DIR}/include
)
target_link_libraries(rt64vk
    ${VULKAN_LIBRARY}
    ${GLFW_LIBRARY}
    ${DXC_LIBRARY}
    ${DLSS_LIBRARY}
    ${FSR_LIBRARY}
)

if(UNIX)
    # Copy the DLSS .so files into the build folder
    if(${CMAKE_BUILD_TYPE} STREQUAL Debug)
        file(COPY ${DLSS_DIR}/lib/Linux_x86_64/dev/libnvidia-ngx-dlss.so.${DLSS_VERSION} DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
    else()
        file(COPY ${DLSS_DIR}/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.${DLSS_VERSION} DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
    endif()
else()
    # Copy the DLSS .dll files into the build folder
    if(${CMAKE_BUILD_TYPE} STREQUAL Debug)
        file(COPY ${DLSS_DIR}/lib/Windows_x86_64/dev/nvngx_dlss.dll DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
    else()
        file(COPY ${DLSS_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
    endif()
    # Copy the FSR .dll files into the build folder
    file(COPY ${FSR_DIR}/lib/ffx_fsr2_api_x64.dll ${FSR_DIR}/lib/ffx_fsr2_api_vk_x64.dll DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
endif()

set_property(TARGET rt64vk PROPERTY CXX_STANDARD 20)