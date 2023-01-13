#!/bin/bash
# This doesn't do anything lmao
# TODO: Make it so linux users don't have to install dxc onto their system

workingDirectory=$(pwd)
rt64vkSource=$workingDirectory"/src/rt64vk"
dxcPath=$rt64vkSource"/contrib/dxc/bin/x64"

LD_LIBRARY_PATH=$"/lib::/lib64:/usr/lib:/usr/lib64:"$rt64vkSource"/contrib/dxc/lib/x64"

#$dxcPath/dxc $@ 
#./src/rt64vk/shaders/FullScreenVS.hlsl -spirv -fspv-target-env=vulkan1.2 -T vs_6_3 -E VSMain -Fh ./src/rt64vk/shaders/FullScreenVS.hlsl.h -Vn FullScreenVS_SPIRV