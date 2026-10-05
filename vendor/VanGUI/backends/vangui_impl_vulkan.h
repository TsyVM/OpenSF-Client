// VanGUI: Renderer Backend for Vulkan
// This needs to be used along with a Platform Backend (e.g. GLFW, SDL, Win32, custom..)

// Implemented features:
//  [!] Renderer: User texture binding. Use a VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE 'VkDescriptorSet' as texture identifier. Call VanGui_ImplVulkan_AddTexture() to register one. Read the FAQ about VanTextureID/VanTextureRef.
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (VanGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Texture updates support for dynamic font atlas (VanGuiBackendFlags_RendererHasTextures).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(VanGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.

// The aim of vangui_impl_vulkan.h/.cpp is to be usable in your engine without any modification.

// You can use unmodified vangui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire vangui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about VanGUI:
// - Documentation        your local docs/ folder.
// - Introduction, links and more at the top of vangui.cpp

// Important note to the reader who wish to integrate vangui_impl_vulkan.cpp/.h in their own engine/app.
// - Common VanGui_ImplVulkan_XXX functions and structures are used to interface with vangui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper VanGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (vangui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in vangui_impl_vulkan.h.

#pragma once
#ifndef VANGUI_DISABLE
#include <vangui/vangui.h>      // VANGUI_IMPL_API

// [Configuration] in order to use a custom Vulkan function loader:
// (1) You'll need to disable default Vulkan function prototypes.
//     We provide a '#define VANGUI_IMPL_VULKAN_NO_PROTOTYPES' convenience configuration flag.
//     In order to make sure this is visible from the vangui_impl_vulkan.cpp compilation unit:
//     - Add '#define VANGUI_IMPL_VULKAN_NO_PROTOTYPES' in your vanconfig.h file
//     - Or as a compilation flag in your build system
//     - Or uncomment here (not recommended because you'd be modifying vangui sources!)
//     - Do not simply add it in a .cpp file!
// (2) Call VanGui_ImplVulkan_LoadFunctions() before VanGui_ImplVulkan_Init() with your custom function.
// If you have no idea what this is, leave it alone!
//#define VANGUI_IMPL_VULKAN_NO_PROTOTYPES

// [Configuration] Convenience support for Volk
// (you can also technically use VANGUI_IMPL_VULKAN_NO_PROTOTYPES + wrap Volk via VanGui_ImplVulkan_LoadFunctions().)
// (When using Volk from directory outside your include directories list you can specify full path to the volk.h header,
//  for example when using Volk from VulkanSDK and using include_directories(${Vulkan_INCLUDE_DIRS})' from 'find_package(Vulkan REQUIRED)')
//#define VANGUI_IMPL_VULKAN_USE_VOLK
//#define VANGUI_IMPL_VULKAN_VOLK_FILENAME    <Volk/volk.h>
//#define VANGUI_IMPL_VULKAN_VOLK_FILENAME    <volk.h>       // Default
// Reminder: make those changes in your vanconfig.h file, not here!

// Clang/GCC warnings with -Weverything
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast" // warning: use of old-style cast
#endif

#if defined(VANGUI_IMPL_VULKAN_NO_PROTOTYPES) && !defined(VK_NO_PROTOTYPES)
#define VK_NO_PROTOTYPES
#endif
#if defined(VK_USE_PLATFORM_WIN32_KHR) && !defined(NOMINMAX)
#define NOMINMAX
#endif

// Vulkan includes
#ifdef VANGUI_IMPL_VULKAN_USE_VOLK
#ifdef VANGUI_IMPL_VULKAN_VOLK_FILENAME
#include VANGUI_IMPL_VULKAN_VOLK_FILENAME
#else
#include <volk.h>
#endif
#else
#include <vulkan/vulkan.h>
#endif
#if defined(VK_VERSION_1_3) || defined(VK_KHR_dynamic_rendering)
#define VANGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
#endif

// Backend uses a small number of descriptors per font atlas + as many as additional calls done to VanGui_ImplVulkan_AddTexture().
#define VANGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE   (8)     // Minimum per atlas
#define VANGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE         (2)     // Minimum for linear + nearest

// Specify settings to create pipeline and swapchain
struct VanGui_ImplVulkan_PipelineInfo
{
    VkRenderPass                    RenderPass;                     // Ignored if using dynamic rendering
    uint32_t                        Subpass;                        //
    VkSampleCountFlagBits           MSAASamples = {};               // 0 defaults to VK_SAMPLE_COUNT_1_BIT
    VanVector<VkDynamicState>        ExtraDynamicStates;             // Optional, allows to insert more dynamic states into our VkPipeline
#ifdef VANGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
    VkPipelineRenderingCreateInfoKHR PipelineRenderingCreateInfo;   // Optional, valid if .sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR
#endif
};

// Initialization data, for VanGui_ImplVulkan_Init()
// [Please zero-clear before use!]
// - About descriptor pool:
//   - A VkDescriptorPool should be created with VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
//     and must contain a pool size large enough to hold a small number of VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors.
//   - As an convenience, by setting DescriptorPoolSize > 0 the backend will create one for you.
// - About dynamic rendering:
//   - When using dynamic rendering, set UseDynamicRendering=true + fill PipelineInfoMain.PipelineRenderingCreateInfo structure.
struct VanGui_ImplVulkan_InitInfo
{
    uint32_t                        ApiVersion;                 // Fill with API version of Instance, e.g. VK_API_VERSION_1_3 or your value of VkApplicationInfo::apiVersion. May be lower than header version (VK_HEADER_VERSION_COMPLETE)
    VkInstance                      Instance;
    VkPhysicalDevice                PhysicalDevice;
    VkDevice                        Device;
    uint32_t                        QueueFamily;
    VkQueue                         Queue;
    VkDescriptorPool                DescriptorPool;             // See requirements in note above; ignored if using DescriptorPoolSize > 0
    uint32_t                        DescriptorPoolSize;         // Optional: set to create internal ImageView descriptor pool automatically instead of using DescriptorPool.
    uint32_t                        MinImageCount;              // >= 2
    uint32_t                        ImageCount;                 // >= MinImageCount
    VkPipelineCache                 PipelineCache;              // Optional

    // Pipeline
    VanGui_ImplVulkan_PipelineInfo   PipelineInfoMain;           // Infos for Main Viewport (created by app/user)
    //VkRenderPass                  RenderPass;                 // --> Since 2025/09/26: set 'PipelineInfoMain.RenderPass' instead
    //uint32_t                      Subpass;                    // --> Since 2025/09/26: set 'PipelineInfoMain.Subpass' instead
    //VkSampleCountFlagBits         MSAASamples;                // --> Since 2025/09/26: set 'PipelineInfoMain.MSAASamples' instead
    //VkPipelineRenderingCreateInfoKHR PipelineRenderingCreateInfo; // Since 2025/09/26: set 'PipelineInfoMain.PipelineRenderingCreateInfo' instead

    // (Optional) Dynamic Rendering
    // Need to explicitly enable VK_KHR_dynamic_rendering extension to use this, even for Vulkan 1.3 + setup PipelineInfoMain.PipelineRenderingCreateInfo.
    bool                            UseDynamicRendering;

    // (Optional) Allocation, Debugging
    const VkAllocationCallbacks*    Allocator;
    void                            (*CheckVkResultFn)(VkResult err);
    VkDeviceSize                    MinAllocationSize;          // Minimum allocation size. Set to 1024*1024 to satisfy zealous best practices validation layer and waste a little memory.

    // (Optional) Customize default vertex/fragment shaders.
    // - if .sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO we use specified structs, otherwise we use defaults.
    // - Shader inputs/outputs need to match ours. Code/data pointed to by the structure needs to survive for whole during of backend usage.
    VkShaderModuleCreateInfo        CustomShaderVertCreateInfo;
    VkShaderModuleCreateInfo        CustomShaderFragCreateInfo;
};

// Follow "Getting Started" link and check examples/ folder to learn about using backends!
VANGUI_IMPL_API bool             VanGui_ImplVulkan_Init(VanGui_ImplVulkan_InitInfo* info);
VANGUI_IMPL_API void             VanGui_ImplVulkan_Shutdown();
VANGUI_IMPL_API void             VanGui_ImplVulkan_NewFrame();
VANGUI_IMPL_API void             VanGui_ImplVulkan_RenderDrawData(VanDrawData* draw_data, VkCommandBuffer command_buffer, VkPipeline pipeline = VK_NULL_HANDLE);
VANGUI_IMPL_API void             VanGui_ImplVulkan_SetMinImageCount(uint32_t min_image_count); // To override MinImageCount after initialization (e.g. if swap chain is recreated)

// (Advanced) Use e.g. if you need to recreate pipeline without reinitializing the backend (see #8110, #8111)
// The main window pipeline will be created by VanGui_ImplVulkan_Init() if possible (== RenderPass xor (UseDynamicRendering && PipelineRenderingCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR))
// Else, the pipeline can be created, or re-created, using VanGui_ImplVulkan_CreateMainPipeline() before rendering.
VANGUI_IMPL_API void             VanGui_ImplVulkan_CreateMainPipeline(const VanGui_ImplVulkan_PipelineInfo* info);

// (Advanced) Use e.g. if you need to precisely control the timing of texture updates (e.g. for staged rendering), by setting VanDrawData::Textures = nullptr to handle this manually.
VANGUI_IMPL_API void             VanGui_ImplVulkan_UpdateTexture(VanTextureData* tex);

// Register a texture (VkDescriptorSet for a VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE == VanTextureID)
VANGUI_IMPL_API VkDescriptorSet  VanGui_ImplVulkan_AddTexture(VkImageView image_view, VkImageLayout image_layout);
VANGUI_IMPL_API void             VanGui_ImplVulkan_RemoveTexture(VkDescriptorSet descriptor_set);

#ifndef VANGUI_DISABLE_OBSOLETE_FUNCTIONS
VANGUI_IMPL_API VkDescriptorSet  VanGui_ImplVulkan_AddTexture(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout); // Ignore VkSampler
#endif

// Optional: load Vulkan functions with a custom function loader
// This is only useful with VANGUI_IMPL_VULKAN_NO_PROTOTYPES / VK_NO_PROTOTYPES
VANGUI_IMPL_API bool             VanGui_ImplVulkan_LoadFunctions(uint32_t api_version, PFN_vkVoidFunction(*loader_func)(const char* function_name, void* user_data), void* user_data = nullptr);

// [BETA] Selected render state data shared with callbacks.
// This is temporarily stored in GetPlatformIO().Renderer_RenderState during the VanGui_ImplVulkan_RenderDrawData() call.
// (Please open an issue if you feel you need access to more data)
struct VanGui_ImplVulkan_RenderState
{
    VkCommandBuffer     CommandBuffer;
    VkPipeline          Pipeline;
    VkPipelineLayout    PipelineLayout;
};

//-------------------------------------------------------------------------
// Internal / Miscellaneous Vulkan Helpers
//-------------------------------------------------------------------------
// Used by example's main.cpp. Used by multi-viewport features. PROBABLY NOT used by your own engine/app.
//
// You probably do NOT need to use or care about those functions.
// WE DO NOT PROVIDE STRONG GUARANTEES OF BACKWARD/FORWARD COMPATIBILITY.
// Those functions only exist because:
//   1) they facilitate the readability and maintenance of the multiple main.cpp examples files.
//   2) the multi-viewport / platform window implementation needs them internally.
// Generally we avoid exposing any kind of superfluous high-level helpers in the backends,
// but it is too much code to duplicate everywhere so we exceptionally expose them.
//
// Your engine/app will likely _already_ have code to setup all that stuff (swap chain,
// render pass, frame buffers, etc.). You may read this code if you are curious, but
// it is recommended you use your own custom tailored code to do equivalent work.
//
// The VanGui_ImplVulkanH_XXX functions should NOT interact with any of the state used
// by the regular VanGui_ImplVulkan_XXX functions.
//-------------------------------------------------------------------------

struct VanGui_ImplVulkanH_Frame;
struct VanGui_ImplVulkanH_Window;

// Helpers
VANGUI_IMPL_API void                 VanGui_ImplVulkanH_CreateOrResizeWindow(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, VanGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count, VkImageUsageFlags image_usage);
VANGUI_IMPL_API void                 VanGui_ImplVulkanH_DestroyWindow(VkInstance instance, VkDevice device, VanGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator);
VANGUI_IMPL_API VkSurfaceFormatKHR   VanGui_ImplVulkanH_SelectSurfaceFormat(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkFormat* request_formats, int request_formats_count, VkColorSpaceKHR request_color_space);
VANGUI_IMPL_API VkPresentModeKHR     VanGui_ImplVulkanH_SelectPresentMode(VkPhysicalDevice physical_device, VkSurfaceKHR surface, const VkPresentModeKHR* request_modes, int request_modes_count);
VANGUI_IMPL_API VkPhysicalDevice     VanGui_ImplVulkanH_SelectPhysicalDevice(VkInstance instance);
VANGUI_IMPL_API uint32_t             VanGui_ImplVulkanH_SelectQueueFamilyIndex(VkPhysicalDevice physical_device);
VANGUI_IMPL_API int                  VanGui_ImplVulkanH_GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode);

// Helper structure to hold the data needed by one rendering frame
// (Used by example's main.cpp. Used by multi-viewport features. Probably NOT used by your own engine/app.)
// [Please zero-clear before use!]
struct VanGui_ImplVulkanH_Frame
{
    VkCommandPool       CommandPool;
    VkCommandBuffer     CommandBuffer;
    VkFence             Fence;
    VkImage             Backbuffer;
    VkImageView         BackbufferView;
    VkFramebuffer       Framebuffer;
};

struct VanGui_ImplVulkanH_FrameSemaphores
{
    VkSemaphore         ImageAcquiredSemaphore;
    VkSemaphore         RenderCompleteSemaphore;
};

// Helper structure to hold the data needed by one rendering context into one OS window
// (Used by example's main.cpp. Used by multi-viewport features. Probably NOT used by your own engine/app.)
struct VanGui_ImplVulkanH_Window
{
    // Input
    bool                    UseDynamicRendering;
    VkSurfaceKHR            Surface;            // Surface created and destroyed by caller.
    VkSurfaceFormatKHR      SurfaceFormat;
    VkPresentModeKHR        PresentMode;
    VkAttachmentDescription AttachmentDesc;     // RenderPass creation: main attachment description.
    VkClearValue            ClearValue;         // RenderPass creation: clear value when using VK_ATTACHMENT_LOAD_OP_CLEAR.

    // Internal
    int                     Width;              // Generally same as passed to VanGui_ImplVulkanH_CreateOrResizeWindow()
    int                     Height;
    VkSwapchainKHR          Swapchain;
    VkRenderPass            RenderPass;
    VkPipeline              Pipeline;           // The window pipeline may uses a different VkRenderPass than the one passed in VanGui_ImplVulkan_InitInfo
    uint32_t                FrameIndex;         // Current frame being rendered to (0 <= FrameIndex < FrameInFlightCount)
    uint32_t                ImageCount;         // Number of simultaneous in-flight frames (returned by vkGetSwapchainImagesKHR, usually derived from min_image_count)
    uint32_t                SemaphoreCount;     // Number of simultaneous in-flight frames + 1, to be able to use it in vkAcquireNextImageKHR
    uint32_t                SemaphoreIndex;     // Current set of swapchain wait semaphores we're using (needs to be distinct from per frame data)
    VanVector<VanGui_ImplVulkanH_Frame>           Frames;
    VanVector<VanGui_ImplVulkanH_FrameSemaphores> FrameSemaphores;

    VanGui_ImplVulkanH_Window()
    {
        memset((void*)this, 0, sizeof(*this));

        // Parameters to create SwapChain
        PresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;     // Ensure we get an error if user doesn't set this.

        // Parameters to create RenderPass
        AttachmentDesc.format = VK_FORMAT_UNDEFINED;    // Will automatically use wd->SurfaceFormat.format.
        AttachmentDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        AttachmentDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        AttachmentDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        AttachmentDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        AttachmentDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        AttachmentDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        AttachmentDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
};

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef VANGUI_DISABLE
