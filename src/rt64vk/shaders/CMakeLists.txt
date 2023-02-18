#######################################################################
# Shader building

if(UNIX)
    set(DXC ${DXC_DIR}/bin/dxc)
else()
    set(DXC ${DXC_DIR}/bin/win-x64/dxc.exe)
endif()

add_custom_target(shaders)

# Build vertex shaders
file(GLOB vertexShaders ${SHADER_DIR}/*VS.hlsl)
foreach(shader ${vertexShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building vertex shader ${shaderName}...
        COMMAND ${DXC} ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T vs_6_3 -E VSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build pixel shaders
file(GLOB pixelShaders ${SHADER_DIR}/*PS.hlsl)
foreach(shader ${pixelShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building pixel shader ${shaderName}...
        COMMAND ${DXC} ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T ps_6_3 -E PSMain -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build compute shaders
file(GLOB computeShaders ${SHADER_DIR}/*CS.hlsl)
foreach(shader ${computeShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building compute shader ${shaderName}...
        COMMAND ${DXC} ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T cs_6_3 -E mainCS -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build ray generation shaders
file(GLOB rayShaders ${SHADER_DIR}/*RayGen.hlsl)
foreach(shader ${rayShaders})
    cmake_path(GET shader STEM shaderName)
    add_custom_command(TARGET shaders PRE_BUILD
        COMMAND echo Building raygen shader ${shaderName}...
        COMMAND ${DXC} ${shader} -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T lib_6_3 -Fh ${shader}.h -Vn ${shaderName}_SPIRV
    )
endforeach()

# Build geometry shaders
add_custom_command(TARGET shaders PRE_BUILD
    COMMAND echo Building geometry shaders Im3DGSPoints and Im3DGSLines..
    COMMAND ${DXC} ${SHADER_DIR}/Im3DGSPoints.hlsl -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0  -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSPoints.hlsl.h -Vn Im3DGSPoints_SPIRV
    COMMAND ${DXC} ${SHADER_DIR}/Im3DGSLines.hlsl -spirv -fspv-target-env=vulkan1.3 -fvk-t-shift ${SRV_SHIFT} 0 -fvk-s-shift ${SAMPLER_SHIFT} 0 -fvk-u-shift ${UAV_SHIFT} 0 -fvk-b-shift ${CBV_SHIFT} 0 -T gs_6_3 -E GSMain -Fh ${SHADER_DIR}/Im3DGSLines.hlsl.h -Vn Im3DGSLines_SPIRV
)