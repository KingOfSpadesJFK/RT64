/*
*   RT64VK
*/

// Yeah this file just exists to import the VMA header without the 
//  compiler going apeshit
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1002000	// Vulkan 1.2
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_DEBUG_INITIALIZE_ALLOCATIONS 1
#include <vk_mem_alloc.h>