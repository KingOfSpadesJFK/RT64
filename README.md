# RT64

RT64 is a hardware-accelerated real-time raytracer that aims to recreate the visual style of promotional offline renders of the mid 90s to early 2000s. It's currently licensed under the terms of the MIT license.

The library is not meant to be used in the traditional way by linking it as an static or dynamic library. It can be loaded during runtime by other processes as long as they can include the basic C-style header and call the provided function pointers. This is mostly meant for ease of use as it allows to run the renderer and hook it to another process without having to port it to the build system used by the host application.

[sm64rt](https://github.com/DarioSamo/sm64rt) makes heavy use of this library, and its reliance on MinGW presented some problems when making D3D12 code that uses the latest raytracing features. This design allows both projects to communicate without issue.

## RT64VK

This project is a port of RT64 from DX12 to Vulkan. It should do everything as mentioned above, with real-time raytracing in a library form... but with Vulkan!

<!-- ## Status
[![Build status](https://ci.appveyor.com/api/projects/status/biwo1tfvg2cndapi?svg=true)](https://ci.appveyor.com/project/DarioSamo/rt64) -->

## Current support
* Vulkan backend for Linux

## Requirements
* For Linux users, [Microsoft DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) just so dxc can work
* [DLSS SDK 2.3.0 or newer](https://developer.nvidia.com/dlss) if you wish to build with DLSS support.

## Building
In the terminal, just type `cmake --build ./build --config Debug|Release --target all|rt64vk|sample --`

A sample is included to showcase how to use the renderer library.

## Screenshot
![Sample screenshot](/images/screen1.jpg?raw=true)

## Credits
Some of the textures used in the sample projects for this repository have been sourced from the [RENDER96-HD-TEXTURE-PACK](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK) repository.
Vulkan helper methods provided by NVIDIA in the [nvpro_core](https://github.com/nvpro-samples/nvpro_core) repository with changes made to fit the project. See src/rt64vk/contrib/nvpro_core.diff for changes. 
