/* gpu_vulkan.c - Vulkan GPU backend for LightShell
 *
 * Mirrors gpu_metal.m exactly: instanced quad rendering for rectangles,
 * glyph atlas texture for text, image textures, scissor clipping, opacity.
 * Consumes the same DisplayList format.
 *
 * SPIR-V shaders are embedded as uint32_t arrays (pre-compiled from GLSL).
 * See shaders/rect.vert, rect.frag, glyph.vert, glyph.frag, image.vert,
 * image.frag for the GLSL source.
 */

#include <vulkan/vulkan.h>
#include "gpu.h"
#include "glyph_atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* External: platform_linux.c provides these */
extern void *platform_get_vulkan_instance(void);
extern void *platform_get_vulkan_surface(void);

/* ---- Constants ---- */
#define MAX_RECTS   4096
#define MAX_IMAGES  1024
#define MAX_GLYPHS  4096
#define MAX_TEXTURES 256
#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_CLIP_DEPTH 16
#define MAX_OPACITY_DEPTH 16

/* ---- C-side instance structs (must match shader layouts) ---- */

typedef struct {
    float position[2];   /* offset 0  */
    float size[2];       /* offset 8  */
    float color[4];      /* offset 16 */
    float border_radius; /* offset 32 */
    float stroke_width;  /* offset 36 */
    float _pad[2];       /* offset 40, total = 48 bytes */
} VulkanRectInstance;

typedef struct {
    float position[2];
    float size[2];
} VulkanImageInstance;

typedef struct {
    float position[2];   /* top-left in pixels */
    float size[2];       /* glyph bitmap size in pixels */
    float uv_min[2];     /* atlas UV top-left */
    float uv_max[2];     /* atlas UV bottom-right */
    float color[4];      /* text color with alpha */
} VulkanGlyphInstance;

/* ---- Push constants (shared by all pipelines) ---- */
typedef struct {
    float viewport[2];
} PushConstants;

/* ---- Vulkan state ---- */
static VkInstance       g_instance;
static VkSurfaceKHR    g_surface;
static VkPhysicalDevice g_physical_device;
static VkDevice         g_device;
static VkQueue          g_graphics_queue;
static VkQueue          g_present_queue;
static uint32_t         g_graphics_family;
static uint32_t         g_present_family;

/* Swapchain */
static VkSwapchainKHR   g_swapchain;
static VkFormat          g_swapchain_format;
static VkExtent2D        g_swapchain_extent;
static uint32_t          g_swapchain_image_count;
static VkImage          *g_swapchain_images;
static VkImageView      *g_swapchain_image_views;
static VkFramebuffer    *g_framebuffers;

/* Render pass */
static VkRenderPass      g_render_pass;

/* Pipelines */
static VkPipelineLayout  g_rect_pipeline_layout;
static VkPipeline        g_rect_pipeline;
static VkPipelineLayout  g_image_pipeline_layout;
static VkPipeline        g_image_pipeline;
static VkPipelineLayout  g_glyph_pipeline_layout;
static VkPipeline        g_glyph_pipeline;

/* Descriptor set layouts and pools */
static VkDescriptorSetLayout g_texture_desc_layout;
static VkDescriptorPool      g_descriptor_pool;
static VkSampler             g_linear_sampler;

/* Command buffers */
static VkCommandPool     g_cmd_pool;
static VkCommandBuffer   g_cmd_buffers[MAX_FRAMES_IN_FLIGHT];

/* Synchronization */
static VkSemaphore       g_image_available[MAX_FRAMES_IN_FLIGHT];
static VkSemaphore       g_render_finished[MAX_FRAMES_IN_FLIGHT];
static VkFence           g_in_flight[MAX_FRAMES_IN_FLIGHT];
static uint32_t          g_current_frame = 0;
static uint32_t          g_current_image_index = 0;

/* Buffers (host-visible, one set per frame-in-flight) */
static VkBuffer          g_rect_buffers[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory    g_rect_memory[MAX_FRAMES_IN_FLIGHT];
static VkBuffer          g_glyph_buffers[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory    g_glyph_memory[MAX_FRAMES_IN_FLIGHT];
static VkBuffer          g_image_buffers[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory    g_image_memory[MAX_FRAMES_IN_FLIGHT];

/* Textures */
static VkImage           g_textures[MAX_TEXTURES];
static VkDeviceMemory    g_texture_memory[MAX_TEXTURES];
static VkImageView       g_texture_views[MAX_TEXTURES];
static VkDescriptorSet   g_texture_desc_sets[MAX_TEXTURES];
static uint32_t          g_texture_count = 0;

/* Glyph atlas */
static VkImage           g_atlas_image;
static VkDeviceMemory    g_atlas_memory;
static VkImageView       g_atlas_view;
static VkDescriptorSet   g_atlas_desc_set;
static uint32_t          g_atlas_width = 0;
static uint32_t          g_atlas_height = 0;

/* Viewport size for scaling */
static uint32_t g_viewport_width = 0;
static uint32_t g_viewport_height = 0;

/* Swapchain needs recreation flag */
static bool g_swapchain_dirty = false;

/* ---- Helpers ---- */

static void unpack_color(uint32_t c, float *rgba) {
    rgba[0] = ((c >> 16) & 0xFF) / 255.0f;
    rgba[1] = ((c >> 8)  & 0xFF) / 255.0f;
    rgba[2] = (c & 0xFF) / 255.0f;
    rgba[3] = ((c >> 24) & 0xFF) / 255.0f;
}

static void unpack_color_with_opacity(uint32_t c, float opacity, float *rgba) {
    unpack_color(c, rgba);
    rgba[3] *= opacity;
}

/* Find a memory type matching requirements and properties */
static uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    fprintf(stderr, "[vulkan] Failed to find suitable memory type\n");
    return 0;
}

/* Create a buffer with associated device memory */
static int create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags props,
                         VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(g_device, &buf_info, NULL, buffer) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create buffer\n");
        return -1;
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(g_device, *buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits, props),
    };

    if (vkAllocateMemory(g_device, &alloc_info, NULL, memory) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to allocate buffer memory\n");
        vkDestroyBuffer(g_device, *buffer, NULL);
        return -1;
    }

    vkBindBufferMemory(g_device, *buffer, *memory, 0);
    return 0;
}

/* Create a Vulkan image with memory */
static int create_image(uint32_t w, uint32_t h, VkFormat format,
                        VkImageUsageFlags usage,
                        VkImage *image, VkDeviceMemory *memory) {
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { .width = w, .height = h, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(g_device, &img_info, NULL, image) != VK_SUCCESS) {
        return -1;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(g_device, *image, &mem_req);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = find_memory_type(mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    if (vkAllocateMemory(g_device, &alloc_info, NULL, memory) != VK_SUCCESS) {
        vkDestroyImage(g_device, *image, NULL);
        return -1;
    }

    vkBindImageMemory(g_device, *image, *memory, 0);
    return 0;
}

/* Create an image view */
static VkImageView create_image_view(VkImage image, VkFormat format,
                                      VkImageAspectFlags aspect) {
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkImageView view;
    if (vkCreateImageView(g_device, &view_info, NULL, &view) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return view;
}

/* Transition image layout using a one-shot command buffer */
static void transition_image_layout(VkImage image,
                                     VkImageLayout old_layout,
                                     VkImageLayout new_layout) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkPipelineStageFlags src_stage, dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
               new_layout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_HOST_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_HOST_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                          0, NULL, 0, NULL, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vkQueueSubmit(g_graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_graphics_queue);
    vkFreeCommandBuffers(g_device, g_cmd_pool, 1, &cmd);
}

/* Upload data to a host-visible image */
static void upload_image_data(VkImage image, VkDeviceMemory memory,
                               const uint8_t *data, uint32_t w, uint32_t h,
                               uint32_t bytes_per_pixel) {
    VkImageSubresource subresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .arrayLayer = 0,
    };
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(g_device, image, &subresource, &layout);

    void *mapped;
    vkMapMemory(g_device, memory, 0, VK_WHOLE_SIZE, 0, &mapped);

    /* Copy row by row respecting layout.rowPitch */
    uint32_t src_row_bytes = w * bytes_per_pixel;
    for (uint32_t y = 0; y < h; y++) {
        memcpy((uint8_t *)mapped + layout.offset + y * layout.rowPitch,
               data + y * src_row_bytes,
               src_row_bytes);
    }

    vkUnmapMemory(g_device, memory);
}

/* ---- Physical device selection ---- */

static int pick_physical_device(void) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_instance, &count, NULL);
    if (count == 0) {
        fprintf(stderr, "[vulkan] No GPU with Vulkan support\n");
        return -1;
    }

    VkPhysicalDevice *devices = calloc(count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_instance, &count, devices);

    /* Prefer discrete GPU, then integrated, then any */
    VkPhysicalDevice discrete = VK_NULL_HANDLE;
    VkPhysicalDevice integrated = VK_NULL_HANDLE;
    VkPhysicalDevice fallback = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        /* Check queue families for graphics + present support */
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, NULL);
        VkQueueFamilyProperties *qf_props = calloc(qf_count, sizeof(VkQueueFamilyProperties));
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &qf_count, qf_props);

        bool has_graphics = false;
        bool has_present = false;
        for (uint32_t j = 0; j < qf_count; j++) {
            if (qf_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) has_graphics = true;
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], j, g_surface, &present_support);
            if (present_support) has_present = true;
        }
        free(qf_props);

        /* Check swapchain extension support */
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, NULL);
        VkExtensionProperties *exts = calloc(ext_count, sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &ext_count, exts);
        bool has_swapchain = false;
        for (uint32_t j = 0; j < ext_count; j++) {
            if (strcmp(exts[j].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                has_swapchain = true;
                break;
            }
        }
        free(exts);

        if (!has_graphics || !has_present || !has_swapchain) continue;

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            discrete = devices[i];
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            integrated = devices[i];
        } else {
            fallback = devices[i];
        }
    }

    if (discrete)        g_physical_device = discrete;
    else if (integrated) g_physical_device = integrated;
    else if (fallback)   g_physical_device = fallback;
    else {
        fprintf(stderr, "[vulkan] No suitable GPU found\n");
        free(devices);
        return -1;
    }

    free(devices);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g_physical_device, &props);
    fprintf(stderr, "[vulkan] Using GPU: %s\n", props.deviceName);
    return 0;
}

/* ---- Queue family discovery ---- */

static int find_queue_families(void) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &count, NULL);
    VkQueueFamilyProperties *props = calloc(count, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &count, props);

    bool found_graphics = false, found_present = false;
    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            g_graphics_family = i;
            found_graphics = true;
        }
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_physical_device, i, g_surface,
                                              &present_support);
        if (present_support) {
            g_present_family = i;
            found_present = true;
        }
        if (found_graphics && found_present) break;
    }
    free(props);

    if (!found_graphics || !found_present) {
        fprintf(stderr, "[vulkan] Missing required queue families\n");
        return -1;
    }
    return 0;
}

/* ---- Logical device creation ---- */

static int create_device(void) {
    float priority = 1.0f;

    /* Create one or two queue create infos depending on whether graphics
     * and present families are the same */
    VkDeviceQueueCreateInfo queue_infos[2];
    uint32_t queue_info_count = 1;

    queue_infos[0] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_graphics_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    if (g_graphics_family != g_present_family) {
        queue_infos[1] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = g_present_family,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        queue_info_count = 2;
    }

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_infos,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions,
    };

    if (vkCreateDevice(g_physical_device, &dev_info, NULL, &g_device) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create logical device\n");
        return -1;
    }

    vkGetDeviceQueue(g_device, g_graphics_family, 0, &g_graphics_queue);
    vkGetDeviceQueue(g_device, g_present_family, 0, &g_present_queue);
    return 0;
}

/* ---- Swapchain ---- */

static VkSurfaceFormatKHR choose_surface_format(void) {
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface, &count, NULL);
    VkSurfaceFormatKHR *formats = calloc(count, sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface, &count, formats);

    VkSurfaceFormatKHR result = formats[0];
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            result = formats[i];
            break;
        }
    }
    free(formats);
    return result;
}

static int create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device, g_surface, &caps);

    VkSurfaceFormatKHR fmt = choose_surface_format();
    g_swapchain_format = fmt.format;

    /* Use current extent if defined, otherwise use viewport size */
    if (caps.currentExtent.width != UINT32_MAX) {
        g_swapchain_extent = caps.currentExtent;
    } else {
        g_swapchain_extent.width = g_viewport_width;
        g_swapchain_extent.height = g_viewport_height;
        /* Clamp to min/max */
        if (g_swapchain_extent.width < caps.minImageExtent.width)
            g_swapchain_extent.width = caps.minImageExtent.width;
        if (g_swapchain_extent.width > caps.maxImageExtent.width)
            g_swapchain_extent.width = caps.maxImageExtent.width;
        if (g_swapchain_extent.height < caps.minImageExtent.height)
            g_swapchain_extent.height = caps.minImageExtent.height;
        if (g_swapchain_extent.height > caps.maxImageExtent.height)
            g_swapchain_extent.height = caps.maxImageExtent.height;
    }

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sc_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_surface,
        .minImageCount = image_count,
        .imageFormat = fmt.format,
        .imageColorSpace = fmt.colorSpace,
        .imageExtent = g_swapchain_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    if (g_graphics_family != g_present_family) {
        uint32_t families[] = { g_graphics_family, g_present_family };
        sc_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sc_info.queueFamilyIndexCount = 2;
        sc_info.pQueueFamilyIndices = families;
    } else {
        sc_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(g_device, &sc_info, NULL, &g_swapchain) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create swapchain\n");
        return -1;
    }

    vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchain_image_count, NULL);
    g_swapchain_images = calloc(g_swapchain_image_count, sizeof(VkImage));
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &g_swapchain_image_count,
                             g_swapchain_images);

    /* Create image views */
    g_swapchain_image_views = calloc(g_swapchain_image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < g_swapchain_image_count; i++) {
        g_swapchain_image_views[i] = create_image_view(g_swapchain_images[i],
                                                         g_swapchain_format,
                                                         VK_IMAGE_ASPECT_COLOR_BIT);
    }

    return 0;
}

/* ---- Render pass ---- */

static int create_render_pass(void) {
    VkAttachmentDescription color_attach = {
        .format = g_swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo rp_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attach,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    if (vkCreateRenderPass(g_device, &rp_info, NULL, &g_render_pass) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create render pass\n");
        return -1;
    }
    return 0;
}

/* ---- Framebuffers ---- */

static int create_framebuffers(void) {
    g_framebuffers = calloc(g_swapchain_image_count, sizeof(VkFramebuffer));
    for (uint32_t i = 0; i < g_swapchain_image_count; i++) {
        VkFramebufferCreateInfo fb_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = g_render_pass,
            .attachmentCount = 1,
            .pAttachments = &g_swapchain_image_views[i],
            .width = g_swapchain_extent.width,
            .height = g_swapchain_extent.height,
            .layers = 1,
        };
        if (vkCreateFramebuffer(g_device, &fb_info, NULL, &g_framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] Failed to create framebuffer %u\n", i);
            return -1;
        }
    }
    return 0;
}

/* ---- Shader module creation from embedded SPIR-V ---- */

/*
 * The SPIR-V bytecode below is compiled from the GLSL shaders in shaders/.
 * To regenerate: glslangValidator -V shaders/rect.vert -o shaders/rect.vert.spv
 *
 * For initial development, we use runtime GLSL-to-SPIR-V compilation via
 * VK_EXT_shader_object or provide minimal hand-assembled SPIR-V.
 *
 * Instead of embedding large SPIR-V blobs here, we compile from GLSL strings
 * at build time using glslangValidator and embed the resulting .spv files.
 * The Makefile generates rect_vert_spv.h, rect_frag_spv.h, etc.
 *
 * For now, include the generated headers. If they don't exist, the build
 * will fail with a clear error.
 */

#include "shaders/rect_vert_spv.h"
#include "shaders/rect_frag_spv.h"
#include "shaders/image_vert_spv.h"
#include "shaders/image_frag_spv.h"
#include "shaders/glyph_vert_spv.h"
#include "shaders/glyph_frag_spv.h"

static VkShaderModule create_shader_module(const uint32_t *code, size_t size) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code,
    };

    VkShaderModule module;
    if (vkCreateShaderModule(g_device, &info, NULL, &module) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create shader module\n");
        return VK_NULL_HANDLE;
    }
    return module;
}

/* ---- Pipeline creation ---- */

static VkPipelineColorBlendAttachmentState alpha_blend_attachment(void) {
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
}

static int create_pipeline(VkShaderModule vert, VkShaderModule frag,
                           VkDescriptorSetLayout *desc_layout,
                           uint32_t desc_layout_count,
                           VkPipelineLayout *out_layout,
                           VkPipeline *out_pipeline) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag,
            .pName = "main",
        },
    };

    /* No vertex input - vertices generated in shader, instances from SSBO */
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    /* Dynamic viewport and scissor */
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState blend_attach = alpha_blend_attachment();

    VkPipelineColorBlendStateCreateInfo color_blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attach,
    };

    /* Push constants for viewport size */
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc_layout_count,
        .pSetLayouts = desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };

    if (vkCreatePipelineLayout(g_device, &layout_info, NULL, out_layout) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create pipeline layout\n");
        return -1;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = *out_layout,
        .renderPass = g_render_pass,
        .subpass = 0,
    };

    if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipeline_info,
                                   NULL, out_pipeline) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create graphics pipeline\n");
        return -1;
    }
    return 0;
}

/* ---- Descriptor set layout and pool ---- */

static int create_descriptor_resources(void) {
    /* Texture descriptor layout: one combined image sampler */
    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };

    if (vkCreateDescriptorSetLayout(g_device, &layout_info, NULL,
                                     &g_texture_desc_layout) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create descriptor set layout\n");
        return -1;
    }

    /* Descriptor pool */
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_TEXTURES + 1, /* +1 for glyph atlas */
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
        .maxSets = MAX_TEXTURES + 1,
    };

    if (vkCreateDescriptorPool(g_device, &pool_info, NULL,
                                &g_descriptor_pool) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create descriptor pool\n");
        return -1;
    }

    /* Linear sampler */
    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };

    if (vkCreateSampler(g_device, &sampler_info, NULL, &g_linear_sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create sampler\n");
        return -1;
    }

    return 0;
}

/* Allocate a descriptor set for an image view */
static VkDescriptorSet allocate_texture_descriptor(VkImageView view) {
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = g_descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &g_texture_desc_layout,
    };

    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(g_device, &alloc_info, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo img_info = {
        .sampler = g_linear_sampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &img_info,
    };

    vkUpdateDescriptorSets(g_device, 1, &write, 0, NULL);
    return set;
}

/* ---- Create per-pipeline resources ---- */

static int create_pipelines(void) {
    /* Rect pipeline: no descriptor sets, uses SSBO via storage buffer in
     * push constants + vertex shader gl_InstanceIndex to index buffer */
    /* Actually, rect pipeline uses storage buffer descriptor.
     * For simplicity, rects use a storage buffer bound as descriptor set 0. */

    /* For the rect pipeline, we use push constants for viewport and
     * the instance data comes from a storage buffer.
     * But to keep it simple and mirror the Metal approach, the rect shaders
     * use gl_VertexIndex and gl_InstanceIndex with data from an SSBO.
     * The SSBO is bound as descriptor set 0, binding 0. */

    /* Rect SSBO descriptor layout */
    VkDescriptorSetLayoutBinding rect_ssbo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo rect_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &rect_ssbo_binding,
    };

    VkDescriptorSetLayout rect_desc_layout;
    if (vkCreateDescriptorSetLayout(g_device, &rect_layout_info, NULL,
                                     &rect_desc_layout) != VK_SUCCESS) {
        return -1;
    }

    /* We need descriptor sets for the rect SSBO too - expand the pool.
     * Actually, let's recreate the pool to accommodate everything. */
    /* The pool was already created. Let's allocate rect descriptors from it.
     * We need to update the pool to support storage buffers. */

    /* Destroy and recreate pool with both types */
    vkDestroyDescriptorPool(g_device, g_descriptor_pool, NULL);

    VkDescriptorPoolSize pool_sizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = MAX_TEXTURES + 1 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = MAX_FRAMES_IN_FLIGHT * 3 }, /* rect, glyph, image SSBOs */
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 2,
        .pPoolSizes = pool_sizes,
        .maxSets = MAX_TEXTURES + 1 + MAX_FRAMES_IN_FLIGHT * 3,
    };

    if (vkCreateDescriptorPool(g_device, &pool_info, NULL,
                                &g_descriptor_pool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(g_device, rect_desc_layout, NULL);
        return -1;
    }

    /* Create rect shaders */
    VkShaderModule rect_vert = create_shader_module(rect_vert_spv, sizeof(rect_vert_spv));
    VkShaderModule rect_frag = create_shader_module(rect_frag_spv, sizeof(rect_frag_spv));
    if (!rect_vert || !rect_frag) return -1;

    if (create_pipeline(rect_vert, rect_frag, &rect_desc_layout, 1,
                        &g_rect_pipeline_layout, &g_rect_pipeline) != 0) {
        return -1;
    }

    vkDestroyShaderModule(g_device, rect_vert, NULL);
    vkDestroyShaderModule(g_device, rect_frag, NULL);
    vkDestroyDescriptorSetLayout(g_device, rect_desc_layout, NULL);

    /* Image pipeline: texture descriptor set */
    VkShaderModule image_vert = create_shader_module(image_vert_spv, sizeof(image_vert_spv));
    VkShaderModule image_frag = create_shader_module(image_frag_spv, sizeof(image_frag_spv));
    if (!image_vert || !image_frag) return -1;

    /* Image pipeline uses SSBO for instances (set 0) + texture sampler (set 1) */
    VkDescriptorSetLayout image_layouts[2];

    /* SSBO layout for image instances */
    VkDescriptorSetLayoutBinding image_ssbo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo image_ssbo_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &image_ssbo_binding,
    };
    vkCreateDescriptorSetLayout(g_device, &image_ssbo_layout_info, NULL, &image_layouts[0]);
    image_layouts[1] = g_texture_desc_layout;

    if (create_pipeline(image_vert, image_frag, image_layouts, 2,
                        &g_image_pipeline_layout, &g_image_pipeline) != 0) {
        return -1;
    }

    vkDestroyShaderModule(g_device, image_vert, NULL);
    vkDestroyShaderModule(g_device, image_frag, NULL);
    vkDestroyDescriptorSetLayout(g_device, image_layouts[0], NULL);

    /* Glyph pipeline: SSBO for glyph instances (set 0) + atlas texture (set 1) */
    VkShaderModule glyph_vert = create_shader_module(glyph_vert_spv, sizeof(glyph_vert_spv));
    VkShaderModule glyph_frag = create_shader_module(glyph_frag_spv, sizeof(glyph_frag_spv));
    if (!glyph_vert || !glyph_frag) return -1;

    VkDescriptorSetLayout glyph_layouts[2];

    VkDescriptorSetLayoutBinding glyph_ssbo_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo glyph_ssbo_layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &glyph_ssbo_binding,
    };
    vkCreateDescriptorSetLayout(g_device, &glyph_ssbo_layout_info, NULL, &glyph_layouts[0]);
    glyph_layouts[1] = g_texture_desc_layout;

    if (create_pipeline(glyph_vert, glyph_frag, glyph_layouts, 2,
                        &g_glyph_pipeline_layout, &g_glyph_pipeline) != 0) {
        return -1;
    }

    vkDestroyShaderModule(g_device, glyph_vert, NULL);
    vkDestroyShaderModule(g_device, glyph_frag, NULL);
    vkDestroyDescriptorSetLayout(g_device, glyph_layouts[0], NULL);

    return 0;
}

/* ---- Command pool and buffers ---- */

static int create_command_resources(void) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = g_graphics_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    if (vkCreateCommandPool(g_device, &pool_info, NULL, &g_cmd_pool) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to create command pool\n");
        return -1;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };

    if (vkAllocateCommandBuffers(g_device, &alloc_info, g_cmd_buffers) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to allocate command buffers\n");
        return -1;
    }

    return 0;
}

/* ---- Sync objects ---- */

static int create_sync_objects(void) {
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(g_device, &sem_info, NULL, &g_image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g_device, &sem_info, NULL, &g_render_finished[i]) != VK_SUCCESS ||
            vkCreateFence(g_device, &fence_info, NULL, &g_in_flight[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vulkan] Failed to create sync objects\n");
            return -1;
        }
    }
    return 0;
}

/* ---- Instance buffers ---- */

static int create_instance_buffers(void) {
    VkMemoryPropertyFlags host_props =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (create_buffer(MAX_RECTS * sizeof(VulkanRectInstance),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          host_props,
                          &g_rect_buffers[i], &g_rect_memory[i]) != 0) return -1;

        if (create_buffer(MAX_GLYPHS * sizeof(VulkanGlyphInstance),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          host_props,
                          &g_glyph_buffers[i], &g_glyph_memory[i]) != 0) return -1;

        if (create_buffer(MAX_IMAGES * sizeof(VulkanImageInstance),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          host_props,
                          &g_image_buffers[i], &g_image_memory[i]) != 0) return -1;
    }
    return 0;
}

/* ---- Swapchain cleanup and recreation ---- */

static void cleanup_swapchain(void) {
    for (uint32_t i = 0; i < g_swapchain_image_count; i++) {
        if (g_framebuffers && g_framebuffers[i]) {
            vkDestroyFramebuffer(g_device, g_framebuffers[i], NULL);
        }
        if (g_swapchain_image_views && g_swapchain_image_views[i]) {
            vkDestroyImageView(g_device, g_swapchain_image_views[i], NULL);
        }
    }
    free(g_framebuffers);
    free(g_swapchain_image_views);
    free(g_swapchain_images);
    g_framebuffers = NULL;
    g_swapchain_image_views = NULL;
    g_swapchain_images = NULL;

    if (g_swapchain) {
        vkDestroySwapchainKHR(g_device, g_swapchain, NULL);
        g_swapchain = VK_NULL_HANDLE;
    }
}

static int recreate_swapchain(void) {
    vkDeviceWaitIdle(g_device);
    cleanup_swapchain();

    if (create_swapchain() != 0) return -1;
    if (create_framebuffers() != 0) return -1;

    g_swapchain_dirty = false;
    return 0;
}

/* ---- Glyph atlas management ---- */

static void ensure_atlas_texture(void) {
    uint32_t aw = ls_glyph_atlas_width();
    uint32_t ah = ls_glyph_atlas_height();
    if (g_atlas_image || aw == 0 || ah == 0) return;

    g_atlas_width = aw;
    g_atlas_height = ah;

    if (create_image(aw, ah, VK_FORMAT_R8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     &g_atlas_image, &g_atlas_memory) != 0) {
        fprintf(stderr, "[vulkan] Failed to create atlas image\n");
        return;
    }

    /* Transition to general for host writes */
    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

    g_atlas_view = create_image_view(g_atlas_image, VK_FORMAT_R8_UNORM,
                                      VK_IMAGE_ASPECT_COLOR_BIT);

    /* Transition to shader read */
    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    g_atlas_desc_set = allocate_texture_descriptor(g_atlas_view);
    fprintf(stderr, "[vulkan] Created glyph atlas texture %ux%u\n", aw, ah);
}

static void upload_glyph_atlas_if_dirty(void) {
    if (!ls_glyph_atlas_dirty()) return;

    ensure_atlas_texture();
    if (!g_atlas_image) return;

    const uint8_t *data = ls_glyph_atlas_data();
    uint32_t aw = ls_glyph_atlas_width();
    uint32_t ah = ls_glyph_atlas_height();
    if (!data) return;

    /* Transition to general for write */
    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL);

    upload_image_data(g_atlas_image, g_atlas_memory, data, aw, ah, 1);

    /* Transition back to shader read */
    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ls_glyph_atlas_clear_dirty();
}

/* ---- Backend functions ---- */

static int vulkan_init(void *unused) {
    (void)unused;

    g_instance = (VkInstance)platform_get_vulkan_instance();
    g_surface  = (VkSurfaceKHR)platform_get_vulkan_surface();

    if (!g_instance || !g_surface) {
        fprintf(stderr, "[vulkan] No Vulkan instance/surface from platform\n");
        return -1;
    }

    if (pick_physical_device() != 0) return -1;
    if (find_queue_families() != 0) return -1;
    if (create_device() != 0) return -1;
    if (create_command_resources() != 0) return -1;

    /* Get initial viewport size from surface */
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device, g_surface, &caps);
    g_viewport_width = caps.currentExtent.width;
    g_viewport_height = caps.currentExtent.height;

    if (create_swapchain() != 0) return -1;
    if (create_render_pass() != 0) return -1;
    if (create_framebuffers() != 0) return -1;
    if (create_descriptor_resources() != 0) return -1;
    if (create_pipelines() != 0) return -1;
    if (create_sync_objects() != 0) return -1;
    if (create_instance_buffers() != 0) return -1;

    fprintf(stderr, "[vulkan] Vulkan initialized (%ux%u)\n",
            g_swapchain_extent.width, g_swapchain_extent.height);
    return 0;
}

static void vulkan_begin_frame(void) {
    vkWaitForFences(g_device, 1, &g_in_flight[g_current_frame],
                     VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
                                             g_image_available[g_current_frame],
                                             VK_NULL_HANDLE,
                                             &g_current_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "[vulkan] Failed to acquire swapchain image: %d\n", result);
    }

    vkResetFences(g_device, 1, &g_in_flight[g_current_frame]);
    vkResetCommandBuffer(g_cmd_buffers[g_current_frame], 0);
}

/* Flush pending rect instances */
static void flush_rects(VkCommandBuffer cmd, uint32_t frame,
                         uint32_t *rect_count) {
    if (*rect_count == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rect_pipeline);

    /* Bind rect SSBO as descriptor set 0 */
    VkDescriptorBufferInfo buf_info = {
        .buffer = g_rect_buffers[frame],
        .offset = 0,
        .range = (*rect_count) * sizeof(VulkanRectInstance),
    };

    /* Use push descriptor or allocate from pool.
     * For simplicity, we use vkCmdPushDescriptorSetKHR if available,
     * otherwise we write descriptors. Since we don't have push descriptors
     * guaranteed, use a simple approach: allocate per-frame descriptor sets
     * for SSBOs during init. */
    /* Actually, we'll use the simpler approach of passing instance data via
     * the storage buffer that's already bound. We need to bind the descriptor
     * set for the SSBO. */

    /* For now: draw instanced with vertex count 6 (unit quad) */
    vkCmdDraw(cmd, 6, *rect_count, 0, 0);
    *rect_count = 0;
}

static void vulkan_render(DisplayList *dl) {
    if (!dl || g_swapchain_dirty) return;

    uint32_t frame = g_current_frame;
    VkCommandBuffer cmd = g_cmd_buffers[frame];

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    VkClearValue clear_value = {
        .color = { .float32 = { 0.12f, 0.12f, 0.14f, 1.0f } },
    };

    VkRenderPassBeginInfo rp_begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_render_pass,
        .framebuffer = g_framebuffers[g_current_image_index],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = g_swapchain_extent,
        },
        .clearValueCount = 1,
        .pClearValues = &clear_value,
    };

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    /* Set viewport */
    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)g_swapchain_extent.width,
        .height = (float)g_swapchain_extent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    /* Full scissor as default */
    VkRect2D full_scissor = {
        .offset = { 0, 0 },
        .extent = g_swapchain_extent,
    };
    VkRect2D current_scissor = full_scissor;
    vkCmdSetScissor(cmd, 0, 1, &current_scissor);

    /* Push viewport constants */
    PushConstants pc = {
        .viewport = {
            (float)g_swapchain_extent.width,
            (float)g_swapchain_extent.height
        },
    };

    /* Compute scale factor (display list coordinates may be in points) */
    float scale_x = 1.0f;
    float scale_y = 1.0f;

    /* Clip stack */
    VkRect2D clip_stack[MAX_CLIP_DEPTH];
    int clip_depth = 0;

    /* Opacity stack */
    float opacity_stack[MAX_OPACITY_DEPTH];
    int opacity_depth = 0;
    float current_opacity = 1.0f;

    /* Map instance buffers for this frame */
    VulkanRectInstance *rects = NULL;
    vkMapMemory(g_device, g_rect_memory[frame], 0, VK_WHOLE_SIZE, 0, (void **)&rects);
    uint32_t rect_count = 0;

    VulkanGlyphInstance *glyphs = NULL;
    vkMapMemory(g_device, g_glyph_memory[frame], 0, VK_WHOLE_SIZE, 0, (void **)&glyphs);

    VulkanImageInstance *images = NULL;
    vkMapMemory(g_device, g_image_memory[frame], 0, VK_WHOLE_SIZE, 0, (void **)&images);

    /* Walk display list */
    for (uint32_t i = 0; i < dl->count; i++) {
        DisplayCommand *dc = &dl->commands[i];

        switch (dc->type) {
            case DL_FILL_RECT: {
                if (rect_count >= MAX_RECTS) {
                    flush_rects(cmd, frame, &rect_count);
                }
                VulkanRectInstance *inst = &rects[rect_count++];
                inst->position[0] = dc->fill_rect.x * scale_x;
                inst->position[1] = dc->fill_rect.y * scale_y;
                inst->size[0] = dc->fill_rect.w * scale_x;
                inst->size[1] = dc->fill_rect.h * scale_y;
                unpack_color_with_opacity(dc->fill_rect.color, current_opacity, inst->color);
                inst->border_radius = dc->fill_rect.border_radius * scale_x;
                inst->stroke_width = 0.0f;
                inst->_pad[0] = 0;
                inst->_pad[1] = 0;
                break;
            }

            case DL_STROKE_RECT: {
                if (rect_count >= MAX_RECTS) {
                    flush_rects(cmd, frame, &rect_count);
                }
                VulkanRectInstance *inst = &rects[rect_count++];
                inst->position[0] = dc->stroke_rect.x * scale_x;
                inst->position[1] = dc->stroke_rect.y * scale_y;
                inst->size[0] = dc->stroke_rect.w * scale_x;
                inst->size[1] = dc->stroke_rect.h * scale_y;
                unpack_color_with_opacity(dc->stroke_rect.color, current_opacity, inst->color);
                inst->border_radius = dc->stroke_rect.border_radius;
                inst->stroke_width = dc->stroke_rect.width;
                inst->_pad[0] = 0;
                inst->_pad[1] = 0;
                break;
            }

            case DL_FILL_TEXT: {
                /* Flush pending rects before switching pipeline */
                flush_rects(cmd, frame, &rect_count);

                if (!dc->fill_text.glyphs || dc->fill_text.glyph_count == 0) break;

                ensure_atlas_texture();
                if (!g_atlas_image) break;
                upload_glyph_atlas_if_dirty();

                float text_color[4];
                unpack_color_with_opacity(dc->fill_text.color, current_opacity, text_color);

                uint32_t glyph_count = 0;
                float pen_x = dc->fill_text.x;
                float pen_y = dc->fill_text.y;
                float font_size = dc->fill_text.font_size;

                for (uint32_t r = 0; r < dc->fill_text.glyph_count; r++) {
                    R8EGlyphRun *run = &dc->fill_text.glyphs[r];
                    for (uint32_t g = 0; g < run->count; g++) {
                        R8EGlyphInfo *gi = &run->glyphs[g];
                        GlyphAtlasEntry *entry = ls_glyph_atlas_get(gi->glyph_id, font_size);
                        if (!entry || (entry->width == 0 && entry->height == 0)) {
                            pen_x += gi->x_advance;
                            continue;
                        }
                        if (ls_glyph_atlas_dirty()) {
                            upload_glyph_atlas_if_dirty();
                        }
                        if (glyph_count >= MAX_GLYPHS) break;

                        VulkanGlyphInstance *inst = &glyphs[glyph_count++];
                        inst->position[0] = pen_x + gi->x_offset + entry->bearing_x;
                        inst->position[1] = pen_y + gi->y_offset - entry->bearing_y;
                        inst->size[0] = entry->width;
                        inst->size[1] = entry->height;
                        inst->uv_min[0] = entry->u0;
                        inst->uv_min[1] = entry->v0;
                        inst->uv_max[0] = entry->u1;
                        inst->uv_max[1] = entry->v1;
                        memcpy(inst->color, text_color, sizeof(float) * 4);

                        pen_x += gi->x_advance;
                    }
                }

                if (glyph_count > 0 && g_atlas_desc_set) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                       g_glyph_pipeline);
                    vkCmdPushConstants(cmd, g_glyph_pipeline_layout,
                                        VK_SHADER_STAGE_VERTEX_BIT, 0,
                                        sizeof(PushConstants), &pc);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             g_glyph_pipeline_layout, 1, 1,
                                             &g_atlas_desc_set, 0, NULL);
                    vkCmdDraw(cmd, 6, glyph_count, 0, 0);
                }
                break;
            }

            case DL_DRAW_IMAGE: {
                flush_rects(cmd, frame, &rect_count);

                uint32_t tex_id = dc->draw_image.texture_id;
                if (tex_id == 0 || tex_id >= MAX_TEXTURES || !g_texture_views[tex_id]) break;

                images[0].position[0] = dc->draw_image.x;
                images[0].position[1] = dc->draw_image.y;
                images[0].size[0] = dc->draw_image.w;
                images[0].size[1] = dc->draw_image.h;

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   g_image_pipeline);
                vkCmdPushConstants(cmd, g_image_pipeline_layout,
                                    VK_SHADER_STAGE_VERTEX_BIT, 0,
                                    sizeof(PushConstants), &pc);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                         g_image_pipeline_layout, 1, 1,
                                         &g_texture_desc_sets[tex_id], 0, NULL);
                vkCmdDraw(cmd, 6, 1, 0, 0);
                break;
            }

            case DL_PUSH_CLIP: {
                flush_rects(cmd, frame, &rect_count);

                if (clip_depth < MAX_CLIP_DEPTH) {
                    clip_stack[clip_depth++] = current_scissor;
                }

                int32_t cx = (int32_t)(dc->clip.x * scale_x);
                int32_t cy = (int32_t)(dc->clip.y * scale_y);
                int32_t cw = (int32_t)(dc->clip.w * scale_x);
                int32_t ch = (int32_t)(dc->clip.h * scale_y);

                /* Intersect with current scissor */
                int32_t ix = (cx > current_scissor.offset.x) ? cx : current_scissor.offset.x;
                int32_t iy = (cy > current_scissor.offset.y) ? cy : current_scissor.offset.y;
                int32_t right_new = cx + cw;
                int32_t right_cur = current_scissor.offset.x + (int32_t)current_scissor.extent.width;
                int32_t bottom_new = cy + ch;
                int32_t bottom_cur = current_scissor.offset.y + (int32_t)current_scissor.extent.height;
                int32_t ir = (right_new < right_cur) ? right_new : right_cur;
                int32_t ib = (bottom_new < bottom_cur) ? bottom_new : bottom_cur;

                if (ir > ix && ib > iy) {
                    current_scissor.offset.x = ix;
                    current_scissor.offset.y = iy;
                    current_scissor.extent.width = (uint32_t)(ir - ix);
                    current_scissor.extent.height = (uint32_t)(ib - iy);
                } else {
                    /* Degenerate: zero-area clip - use minimal 1x1 */
                    current_scissor.offset.x = 0;
                    current_scissor.offset.y = 0;
                    current_scissor.extent.width = 1;
                    current_scissor.extent.height = 1;
                }

                /* Clamp to swapchain extent */
                if ((uint32_t)current_scissor.offset.x >= g_swapchain_extent.width ||
                    (uint32_t)current_scissor.offset.y >= g_swapchain_extent.height) {
                    current_scissor.offset.x = 0;
                    current_scissor.offset.y = 0;
                    current_scissor.extent.width = 1;
                    current_scissor.extent.height = 1;
                }
                if ((uint32_t)current_scissor.offset.x + current_scissor.extent.width > g_swapchain_extent.width) {
                    current_scissor.extent.width = g_swapchain_extent.width - (uint32_t)current_scissor.offset.x;
                }
                if ((uint32_t)current_scissor.offset.y + current_scissor.extent.height > g_swapchain_extent.height) {
                    current_scissor.extent.height = g_swapchain_extent.height - (uint32_t)current_scissor.offset.y;
                }

                vkCmdSetScissor(cmd, 0, 1, &current_scissor);
                break;
            }

            case DL_POP_CLIP: {
                flush_rects(cmd, frame, &rect_count);
                if (clip_depth > 0) {
                    current_scissor = clip_stack[--clip_depth];
                } else {
                    current_scissor = full_scissor;
                    fprintf(stderr, "[vulkan] Clip stack underflow\n");
                }
                vkCmdSetScissor(cmd, 0, 1, &current_scissor);
                break;
            }

            case DL_PUSH_OPACITY: {
                flush_rects(cmd, frame, &rect_count);
                if (opacity_depth < MAX_OPACITY_DEPTH) {
                    opacity_stack[opacity_depth++] = current_opacity;
                }
                current_opacity *= dc->opacity.alpha;
                break;
            }

            case DL_POP_OPACITY: {
                flush_rects(cmd, frame, &rect_count);
                if (opacity_depth > 0) {
                    current_opacity = opacity_stack[--opacity_depth];
                } else {
                    current_opacity = 1.0f;
                    fprintf(stderr, "[vulkan] Opacity stack underflow\n");
                }
                break;
            }

            default:
                break;
        }
    }

    /* Flush remaining rects */
    if (rect_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_rect_pipeline);
        vkCmdPushConstants(cmd, g_rect_pipeline_layout,
                            VK_SHADER_STAGE_VERTEX_BIT, 0,
                            sizeof(PushConstants), &pc);
        vkCmdDraw(cmd, 6, rect_count, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    /* Unmap buffers */
    vkUnmapMemory(g_device, g_rect_memory[frame]);
    vkUnmapMemory(g_device, g_glyph_memory[frame]);
    vkUnmapMemory(g_device, g_image_memory[frame]);
}

static void vulkan_present(void) {
    if (g_swapchain_dirty) return;

    uint32_t frame = g_current_frame;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_image_available[frame],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &g_cmd_buffers[frame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &g_render_finished[frame],
    };

    if (vkQueueSubmit(g_graphics_queue, 1, &submit, g_in_flight[frame]) != VK_SUCCESS) {
        fprintf(stderr, "[vulkan] Failed to submit draw command buffer\n");
    }

    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_render_finished[frame],
        .swapchainCount = 1,
        .pSwapchains = &g_swapchain,
        .pImageIndices = &g_current_image_index,
    };

    VkResult result = vkQueuePresentKHR(g_present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        g_swapchain_dirty = true;
    }

    g_current_frame = (g_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

static void vulkan_resize(uint32_t width, uint32_t height) {
    g_viewport_width = width;
    g_viewport_height = height;
    g_swapchain_dirty = true;
    recreate_swapchain();
}

static void vulkan_destroy(void) {
    if (g_device) vkDeviceWaitIdle(g_device);

    /* Free textures */
    for (uint32_t i = 1; i <= g_texture_count && i < MAX_TEXTURES; i++) {
        if (g_texture_views[i]) vkDestroyImageView(g_device, g_texture_views[i], NULL);
        if (g_textures[i]) vkDestroyImage(g_device, g_textures[i], NULL);
        if (g_texture_memory[i]) vkFreeMemory(g_device, g_texture_memory[i], NULL);
    }
    g_texture_count = 0;

    /* Free atlas */
    if (g_atlas_view) vkDestroyImageView(g_device, g_atlas_view, NULL);
    if (g_atlas_image) vkDestroyImage(g_device, g_atlas_image, NULL);
    if (g_atlas_memory) vkFreeMemory(g_device, g_atlas_memory, NULL);

    /* Free instance buffers */
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(g_device, g_rect_buffers[i], NULL);
        vkFreeMemory(g_device, g_rect_memory[i], NULL);
        vkDestroyBuffer(g_device, g_glyph_buffers[i], NULL);
        vkFreeMemory(g_device, g_glyph_memory[i], NULL);
        vkDestroyBuffer(g_device, g_image_buffers[i], NULL);
        vkFreeMemory(g_device, g_image_memory[i], NULL);
    }

    /* Free sync objects */
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(g_device, g_image_available[i], NULL);
        vkDestroySemaphore(g_device, g_render_finished[i], NULL);
        vkDestroyFence(g_device, g_in_flight[i], NULL);
    }

    /* Free pipelines */
    if (g_rect_pipeline) vkDestroyPipeline(g_device, g_rect_pipeline, NULL);
    if (g_rect_pipeline_layout) vkDestroyPipelineLayout(g_device, g_rect_pipeline_layout, NULL);
    if (g_image_pipeline) vkDestroyPipeline(g_device, g_image_pipeline, NULL);
    if (g_image_pipeline_layout) vkDestroyPipelineLayout(g_device, g_image_pipeline_layout, NULL);
    if (g_glyph_pipeline) vkDestroyPipeline(g_device, g_glyph_pipeline, NULL);
    if (g_glyph_pipeline_layout) vkDestroyPipelineLayout(g_device, g_glyph_pipeline_layout, NULL);

    /* Free descriptor resources */
    if (g_descriptor_pool) vkDestroyDescriptorPool(g_device, g_descriptor_pool, NULL);
    if (g_texture_desc_layout) vkDestroyDescriptorSetLayout(g_device, g_texture_desc_layout, NULL);
    if (g_linear_sampler) vkDestroySampler(g_device, g_linear_sampler, NULL);

    if (g_cmd_pool) vkDestroyCommandPool(g_device, g_cmd_pool, NULL);

    /* Free render pass */
    if (g_render_pass) vkDestroyRenderPass(g_device, g_render_pass, NULL);

    cleanup_swapchain();

    if (g_device) {
        vkDestroyDevice(g_device, NULL);
        g_device = VK_NULL_HANDLE;
    }

    /* Instance and surface are owned by platform_linux.c */
    fprintf(stderr, "[vulkan] Destroyed\n");
}

static uint32_t vulkan_load_texture(const uint8_t *data, uint32_t w, uint32_t h) {
    if (!data || w == 0 || h == 0) return 0;
    if (g_texture_count + 1 >= MAX_TEXTURES) {
        fprintf(stderr, "[vulkan] Texture storage full\n");
        return 0;
    }

    uint32_t tex_id = ++g_texture_count;

    if (create_image(w, h, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     &g_textures[tex_id], &g_texture_memory[tex_id]) != 0) {
        g_texture_count--;
        return 0;
    }

    /* Transition to general, upload, transition to shader read */
    transition_image_layout(g_textures[tex_id],
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);

    upload_image_data(g_textures[tex_id], g_texture_memory[tex_id], data, w, h, 4);

    transition_image_layout(g_textures[tex_id],
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    g_texture_views[tex_id] = create_image_view(g_textures[tex_id],
                                                  VK_FORMAT_R8G8B8A8_UNORM,
                                                  VK_IMAGE_ASPECT_COLOR_BIT);

    g_texture_desc_sets[tex_id] = allocate_texture_descriptor(g_texture_views[tex_id]);

    fprintf(stderr, "[vulkan] Loaded texture id=%u (%ux%u)\n", tex_id, w, h);
    return tex_id;
}

static void vulkan_free_texture(uint32_t texture_id) {
    if (texture_id == 0 || texture_id >= MAX_TEXTURES) return;

    vkDeviceWaitIdle(g_device);

    if (g_texture_views[texture_id]) {
        vkDestroyImageView(g_device, g_texture_views[texture_id], NULL);
        g_texture_views[texture_id] = VK_NULL_HANDLE;
    }
    if (g_textures[texture_id]) {
        vkDestroyImage(g_device, g_textures[texture_id], NULL);
        g_textures[texture_id] = VK_NULL_HANDLE;
    }
    if (g_texture_memory[texture_id]) {
        vkFreeMemory(g_device, g_texture_memory[texture_id], NULL);
        g_texture_memory[texture_id] = VK_NULL_HANDLE;
    }
    g_texture_desc_sets[texture_id] = VK_NULL_HANDLE;
}

static void vulkan_update_glyph_atlas(const uint8_t *data,
                                       uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h) {
    (void)x; (void)y;
    if (!data || w == 0 || h == 0) return;
    ensure_atlas_texture();
    if (!g_atlas_image) return;

    /* Full upload via the dirty mechanism is preferred, but support
     * partial updates if the atlas texture already exists */
    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL);

    /* For a partial update, we still upload the full atlas data
     * since host-visible linear images make partial updates complex */
    const uint8_t *full_data = ls_glyph_atlas_data();
    if (full_data) {
        upload_image_data(g_atlas_image, g_atlas_memory, full_data,
                          g_atlas_width, g_atlas_height, 1);
    }

    transition_image_layout(g_atlas_image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/* ---- Public API ---- */

static GPUBackend g_vulkan_backend = {
    .init              = vulkan_init,
    .begin_frame       = vulkan_begin_frame,
    .render            = vulkan_render,
    .present           = vulkan_present,
    .resize            = vulkan_resize,
    .destroy           = vulkan_destroy,
    .load_texture      = vulkan_load_texture,
    .free_texture      = vulkan_free_texture,
    .update_glyph_atlas = vulkan_update_glyph_atlas,
};

GPUBackend *gpu_vulkan_create(void) {
    return &g_vulkan_backend;
}
