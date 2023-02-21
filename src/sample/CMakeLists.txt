#######################################################################
# Build the sample

# Include third party libraries
if(UNIX)
else()
    set(VULKAN_INCLUDE ${VK_SDK_PATH}\\Include) 
    set(GLFW_INCLUDE ${PROJECT_SOURCE_DIR}/src/contrib/glfw/include)
endif()

# Add the sample as a target
add_executable(sample 
    ${SAMPLE_DIR}/main.cpp
)
if (UNIX)
    target_link_libraries(sample 
        rt64vk
    )
else()
    file(GLOB VULKAN_LIBRARY ${VK_SDK_PATH}\\Lib/*.lib)
    set(GLFW_LIBRARY ${PROJECT_SOURCE_DIR}/src/contrib/glfw/lib/glfw3dll.lib)
    target_link_libraries(sample 
        ${VULKAN_LIBRARY}
        ${GLFW_LIBRARY}
    )
endif()
target_include_directories(sample PRIVATE 
    ${GLFW_INCLUDE}
    ${VULKAN_INCLUDE}
    ${SAMPLE_DIR}/contrib/tinygltf
    ${LIBRT64VK_DIR}/public
)
# Copy the res files into the build folder
file(COPY ${SAMPLE_DIR}/res DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

set_property(TARGET sample PROPERTY CXX_STANDARD 20)