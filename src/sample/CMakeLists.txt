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
    ${LIBRT64VK_DIR}/public
)
# Copy the res files into the build folder
add_custom_command(TARGET sample POST_BUILD
    COMMAND cp -r ${SAMPLE_DIR}/res ${CMAKE_BINARY_DIR}
)

set_property(TARGET sample PROPERTY CXX_STANDARD 20)