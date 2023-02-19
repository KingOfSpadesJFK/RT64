#######################################################################
# Build the library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include third party libraries
if(UNIX)
    set(DXC_HEADER_DIR ${DXC_DIR}/include/linux)
    file(GLOB DXC_LIBRARIES ${DXC_DIR}/lib/libdxcompiler.so)
    list(APPEND DLSS_LIBRARY ${DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a)   
    list(APPEND DLSS_LIBRARY libstdc++.so)   
    list(APPEND DLSS_LIBRARY libdl.a) 
    file(GLOB FSR_LIBRARY ${FSR_DIR}/lib/libffx_fsr2_api_*x86_64.a)
else()
    set(DXC_HEADER_DIR ${DXC_DIR}/include/win)
    file(GLOB DXC_LIBRARIES ${DXC_DIR}/lib/win-x64/dxcompiler.lib)
    file(GLOB DLSS_LIBRARY ${DLSS_DIR}/lib/Windows_x86_64/x86_64/*.lib)    
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
    # ${LIBRT64VK_DIR}/private/rt64_dlss.cpp
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
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)
target_include_directories(rt64vk PRIVATE 
    ${LIBRT64VK_DIR}
    ${CONTRIB_DIR}
    ${VMA_DIR}
    ${IMGUI_DIR}
    ${DXC_DIR}
    ${PLATFORM}
    ${NVPRO_DIR}
    ${DLSS_DIR}/include
    ${FSR_DIR}/include
)
target_link_libraries(rt64vk
    vulkan
    glfw
    ${DXC_LIBRARIES}
    ${DLSS_LIBRARY}
    ${FSR_LIBRARY}
)

if(UNIX)
    # Copy the DLSS .so files into the build folder
    if(RT64_DEBUG)
        add_custom_command(TARGET rt64vk POST_BUILD
            COMMAND cp -r ${DLSS_DIR}/lib/Linux_x86_64/dev/libnvidia-ngx-dlss.so.3.1.1 ${CMAKE_BINARY_DIR}
        )
    else()
        add_custom_command(TARGET rt64vk POST_BUILD
            COMMAND cp -r ${DLSS_DIR}/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.3.1.1 ${CMAKE_BINARY_DIR}
        )
    endif()
else()
    # Copy the DLSS .dll files into the build folder
    if(RT64_DEBUG)
        add_custom_command(TARGET rt64vk POST_BUILD
            COMMAND cp -r ${DLSS_DIR}/lib/Windows_x86_64/dev/ ${CMAKE_BINARY_DIR}
        )
    else()
        add_custom_command(TARGET rt64vk POST_BUILD
            COMMAND cp -r ${DLSS_DIR}/lib/Windows_x86_64/rel/ ${CMAKE_BINARY_DIR}
        )
    endif()
endif()
set_property(TARGET rt64vk PROPERTY CXX_STANDARD 20)