# RT64

[RT64](https://github.com/DarioSamo/RT64) is a hardware-accelerated real-time raytracer that aims to recreate the visual style of promotional offline renders of the mid 90s to early 2000s. It's currently licensed under the terms of the MIT license.

The library is not meant to be used in the traditional way by linking it as an static or dynamic library. It can be loaded during runtime by other processes as long as they can include the basic C-style header and call the provided function pointers. This is mostly meant for ease of use as it allows to run the renderer and hook it to another process without having to port it to the build system used by the host application.

[sm64rt](https://github.com/DarioSamo/sm64rt) makes heavy use of this library, and its reliance on MinGW presented some problems when making D3D12 code that uses the latest raytracing features. This design allows both projects to communicate without issue.

# RT64VK

This project is a port of RT64 from DX12 to Vulkan. It should do everything as mentioned above, with real-time raytracing in a library form... but with Vulkan!

This port is still very much a work-in-progress, only having been tested on Windows 11 and Fedora 37 with an RTX 3060Ti. Some features from RT64 DX12 are still missing (3D Debug interfaces, DLSS and XESS, background rasterizer).

<!-- ## Status
[![Build status](https://ci.appveyor.com/api/projects/status/biwo1tfvg2cndapi?svg=true)](https://ci.appveyor.com/project/DarioSamo/rt64) -->

## Current support
* Vulkan backend for Windows and Linux

## Requirements
* CMake >=3.12.0
* Support for Vulkan 1.3
* [DLSS SDK 3.1.0 or newer](https://developer.nvidia.com/dlss) if you wish to build with DLSS support.
## Windows Reqirements
* Visual Studio 2022 and Windows SDK >=10.0.22000
* [Vulkan SDK](https://vulkan.lunarg.com/)
    * Includes GLM headers
## Linux Requirements
* Vulkan packages
* GLFW packages
* GLM packages

On Ubuntu, Pop!_os, Linux Mint, and other Debian derivatives, the packages can be installed by typing this command:

        sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers-dev spirv-tools 
        sudo apt install libglfw3-dev
        sudo apt install libglm-dev

On Fedora, Nobara, and other Red Hat derivatives:

        sudo dnf install vulkan-tools vulkan-loader-devel mesa-vulkan-devel vulkan-validation-layers-devel 
        sudo dnf install glfw-devel
        sudo dnf install glm-devel

On Arch Linux and derivatives such as Manjaro, Endeavour, SteamOS:

        sudo pacman -S vulkan-devel
        sudo pacman -S glfw-wayland # glfw-x11 if you want to build on X11
        sudo pacman -S glm

## Building
In the terminal, just type `cmake --build ./build --config Debug|Release --target all|rt64vk|sample --`

A sample is included to showcase how to use the renderer library.

## Screenshots
![Sample screenshot 1](/images/Screenshot_20230220_042451.jpg?raw=true)
![Sample screenshot 2](/images/Screenshot_20230220_042358.jpg?raw=true)
![Sample screenshot 3](/images/Screenshot_20230220_041926.jpg?raw=true)
![Sample screenshot 4](/images/Screenshot_20230220_041323.jpg?raw=true)
![Sample screenshot 5](/images/Screenshot_20230220_043003.jpg?raw=true)

## Credits
Some of the textures used in the sample projects for this repository have been sourced from the [RENDER96-HD-TEXTURE-PACK](https://github.com/pokeheadroom/RENDER96-HD-TEXTURE-PACK) repository.

Sponza scene created by Frank Meinl, PBR textures by Alexandre Pestana, GlTF model provided by Khronos Group.

Created using assets from [ambientCG.com](https://ambientcg.com/), licensed under the Creative Commons CC0 1.0 Universal License.

Vulkan helper methods provided by NVIDIA DesignWorks in the [nvpro_core](https://github.com/nvpro-samples/nvpro_core) repository with changes made to fit the project.

### nvpro_core changes
#### raytraceKHR_vk additions
    destroyTlas();
        // Destroys the TLAS
    getFirstBlas();
        // Gets the first BLAS of the builder
    emplaceBlas();
        // Emplace an already existing BLAS into the builder
