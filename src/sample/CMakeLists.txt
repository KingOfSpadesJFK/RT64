#######################################################################
# Build the sample

# Include third party libraries
if(UNIX)
else()
    set(VULKAN_INCLUDE ${VK_SDK_PATH}\\Include)
endif()

# Add the sample as a target
add_executable(sample 
    ${SAMPLE_DIR}/main.cpp
)
if (UNIX)
    # Vulkan and GLFW
    set(VULKAN_LIBRARY vulkan)
    set(GLFW_LIBRARY glfw)
else()
    file(GLOB VULKAN_LIBRARY ${VK_SDK_PATH}\\Lib/*.lib)
    target_link_libraries(sample 
        ${VULKAN_LIBRARY}
    )
endif()
target_include_directories(sample PRIVATE 
    ${VULKAN_INCLUDE}
    ${SAMPLE_DIR}/contrib/tinygltf
    ${LIBRT64VK_DIR}/public
)
target_link_libraries(sample PRIVATE
    ${VULKAN_LIBRARY}
    ${GLFW_LIBRARY}
)
# Copy the res files into the build folder
file(COPY ${SAMPLE_DIR}/res DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

set_property(TARGET sample PROPERTY CXX_STANDARD 20)