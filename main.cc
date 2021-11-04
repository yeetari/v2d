#include "Vector.hh"
#include "Window.hh"

#define STB_IMAGE_IMPLEMENTATION
#include "third-party/stb_image.h"
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>

#define VK_CHECK(expr, msg)                                                                                            \
    if (VkResult result = (expr); result != VK_SUCCESS && result != VK_INCOMPLETE) {                                   \
        std::fprintf(stderr, "%s (%s != VK_SUCCESS)\n", msg, #expr);                                                   \
        std::exit(1);                                                                                                  \
    }

#undef VK_MAKE_VERSION
#define VK_MAKE_VERSION(major, minor, patch)                                                                           \
    ((static_cast<std::uint32_t>(major) << 22u) | (static_cast<std::uint32_t>(minor) << 12u) |                         \
     static_cast<std::uint32_t>(patch))

namespace {

VkShaderModule load_shader(VkDevice device, const char *path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    assert(file);
    Vector<char> binary(file.tellg());
    file.seekg(0);
    file.read(binary.data(), binary.size());
    VkShaderModuleCreateInfo module_ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = binary.size(),
        .pCode = reinterpret_cast<const std::uint32_t *>(binary.data()),
    };
    VkShaderModule module = nullptr;
    VK_CHECK(vkCreateShaderModule(device, &module_ci, nullptr, &module), "Failed to create shader module")
    return module;
}

} // namespace

int main() {
    Window window(800, 600, false);
    VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "v2d",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_MAKE_VERSION(1, 2, 0),
    };
    const auto required_extensions = Window::required_instance_extensions();
    VkInstanceCreateInfo instance_ci{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = static_cast<std::uint32_t>(required_extensions.size()),
        .ppEnabledExtensionNames = required_extensions.data(),
    };
#ifndef NDEBUG
    // TODO: Check for existence.
    const char *validation_layer_name = "VK_LAYER_KHRONOS_validation";
    instance_ci.enabledLayerCount = 1;
    instance_ci.ppEnabledLayerNames = &validation_layer_name;
#endif
    VkInstance instance = nullptr;
    VK_CHECK(vkCreateInstance(&instance_ci, nullptr, &instance), "Failed to create instance")

    VkPhysicalDevice physical_device = nullptr;
    std::uint32_t physical_device_count = 1;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &physical_device_count, &physical_device),
             "Failed to get physical device")

    std::uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
    Vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families.data());

    Vector<VkDeviceQueueCreateInfo> queue_cis;
    const float queue_priority = 1.0f;
    for (std::uint32_t i = 0; i < queue_families.size(); i++) {
        queue_cis.push({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = i,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        });
    }

    const char *swapchain_extension_name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkPhysicalDeviceVulkan12Features device_12_features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .imagelessFramebuffer = VK_TRUE,
    };
    VkDeviceCreateInfo device_ci{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &device_12_features,
        .queueCreateInfoCount = queue_cis.size(),
        .pQueueCreateInfos = queue_cis.data(),
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &swapchain_extension_name,
    };
    VkDevice device = nullptr;
    VK_CHECK(vkCreateDevice(physical_device, &device_ci, nullptr, &device), "Failed to create device")

    VkSurfaceKHR surface = nullptr;
    VK_CHECK(glfwCreateWindowSurface(instance, *window, nullptr, &surface), "Failed to create surface")

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities),
             "Failed to get surface capabilities")

    VkQueue present_queue = nullptr;
    for (std::uint32_t i = 0; i < queue_families.size(); i++) {
        VkBool32 present_supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_supported);
        if (present_supported == VK_TRUE) {
            vkGetDeviceQueue(device, i, 0, &present_queue);
            break;
        }
    }

    VkSurfaceFormatKHR surface_format{
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };
    VkSwapchainCreateInfoKHR swapchain_ci{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = surface_capabilities.minImageCount + 1,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = {window.width(), window.height()},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain = nullptr;
    VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_ci, nullptr, &swapchain), "Failed to create swapchain")

    std::uint32_t swapchain_image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);
    Vector<VkImage> swapchain_images(swapchain_image_count);
    Vector<VkImageView> swapchain_image_views(swapchain_image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());
    for (std::uint32_t i = 0; i < swapchain_image_count; i++) {
        VkImageViewCreateInfo image_view_ci{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = surface_format.format,
            .components{
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        VK_CHECK(vkCreateImageView(device, &image_view_ci, nullptr, &swapchain_image_views[i]),
                 "Failed to create swapchain image view")
    }

    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
    Vector<VkMemoryType> memory_types;
    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        memory_types.push(memory_properties.memoryTypes[i]);
    }
    auto allocate_memory = [&](const VkMemoryRequirements &requirements, VkMemoryPropertyFlags flags) {
        std::optional<std::uint32_t> memory_type_index;
        for (std::uint32_t i = 0; const auto &memory_type : memory_types) {
            if ((requirements.memoryTypeBits & (1u << i++)) == 0) {
                continue;
            }
            if ((memory_type.propertyFlags & flags) != flags) {
                continue;
            }
            memory_type_index = i - 1;
            break;
        }
        assert(memory_type_index);
        VkMemoryAllocateInfo memory_ai{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = *memory_type_index,
        };
        VkDeviceMemory memory;
        VK_CHECK(vkAllocateMemory(device, &memory_ai, nullptr, &memory), "Failed to allocate memory")
        return memory;
    };

    std::uint32_t atlas_width;
    std::uint32_t atlas_height;
    auto *atlas_texture = stbi_load("../atlas.png", reinterpret_cast<int *>(&atlas_width),
                                    reinterpret_cast<int *>(&atlas_height), nullptr, STBI_rgb_alpha);
    assert(atlas_texture != nullptr);

    VkBufferCreateInfo atlas_staging_buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = atlas_width * atlas_height * 4,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer atlas_staging_buffer = nullptr;
    VK_CHECK(vkCreateBuffer(device, &atlas_staging_buffer_ci, nullptr, &atlas_staging_buffer),
             "Failed to create atlas staging buffer")

    VkMemoryRequirements atlas_staging_buffer_requirements;
    vkGetBufferMemoryRequirements(device, atlas_staging_buffer, &atlas_staging_buffer_requirements);

    VkDeviceMemory atlas_staging_buffer_memory = allocate_memory(
        atlas_staging_buffer_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkBindBufferMemory(device, atlas_staging_buffer, atlas_staging_buffer_memory, 0),
             "Failed to bind atlas staging buffer memory")

    void *staging_data;
    vkMapMemory(device, atlas_staging_buffer_memory, 0, VK_WHOLE_SIZE, 0, &staging_data);
    std::memcpy(staging_data, atlas_texture, atlas_width * atlas_height * 4);
    vkUnmapMemory(device, atlas_staging_buffer_memory);
    stbi_image_free(atlas_texture);

    VkImageCreateInfo atlas_image_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = {atlas_width, atlas_height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage atlas_image;
    VK_CHECK(vkCreateImage(device, &atlas_image_ci, nullptr, &atlas_image), "Failed to create atlas image")

    VkMemoryRequirements atlas_image_requirements;
    vkGetImageMemoryRequirements(device, atlas_image, &atlas_image_requirements);
    VkDeviceMemory atlas_image_memory = allocate_memory(atlas_image_requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkBindImageMemory(device, atlas_image, atlas_image_memory, 0), "Failed to bind atlas image memory")

    VkCommandPool command_pool = nullptr;
    VkQueue queue = nullptr;
    for (std::uint32_t i = 0; i < queue_families.size(); i++) {
        const auto &family = queue_families[i];
        if ((family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            VkCommandPoolCreateInfo command_pool_ci{
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .queueFamilyIndex = i,
            };
            VK_CHECK(vkCreateCommandPool(device, &command_pool_ci, nullptr, &command_pool),
                     "Failed to create command pool")
            vkGetDeviceQueue(device, i, 0, &queue);
            break;
        }
    }

    VkCommandBufferAllocateInfo command_buffer_ai{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command_buffer = nullptr;
    VK_CHECK(vkAllocateCommandBuffers(device, &command_buffer_ai, &command_buffer), "Failed to allocate command buffer")

    VkImageMemoryBarrier atlas_transition_barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .image = atlas_image,
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    VkCommandBufferBeginInfo transfer_buffer_bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(command_buffer, &transfer_buffer_bi);
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &atlas_transition_barrier);
    VkBufferImageCopy atlas_copy{
        .imageSubresource{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = {atlas_width, atlas_height, 1},
    };
    vkCmdCopyBufferToImage(command_buffer, atlas_staging_buffer, atlas_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &atlas_copy);
    atlas_transition_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    atlas_transition_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    atlas_transition_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    atlas_transition_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &atlas_transition_barrier);
    vkEndCommandBuffer(command_buffer);
    VkSubmitInfo transfer_si{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };
    vkQueueSubmit(queue, 1, &transfer_si, nullptr);
    vkQueueWaitIdle(queue);
    vkFreeMemory(device, atlas_staging_buffer_memory, nullptr);
    vkDestroyBuffer(device, atlas_staging_buffer, nullptr);

    VkImageViewCreateInfo atlas_image_view_ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = atlas_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = atlas_image_ci.format,
        .subresourceRange{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    VkImageView atlas_image_view = nullptr;
    VK_CHECK(vkCreateImageView(device, &atlas_image_view_ci, nullptr, &atlas_image_view),
             "Failed to create atlas image view")

    VkSamplerCreateInfo atlas_sampler_ci{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    };
    VkSampler atlas_sampler = nullptr;
    VK_CHECK(vkCreateSampler(device, &atlas_sampler_ci, nullptr, &atlas_sampler), "Failed to create atlas sampler")

    VkDescriptorPoolSize descriptor_pool_size{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo descriptor_pool_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &descriptor_pool_size,
    };
    VkDescriptorPool descriptor_pool = nullptr;
    VK_CHECK(vkCreateDescriptorPool(device, &descriptor_pool_ci, nullptr, &descriptor_pool),
             "Failed to create descriptor pool")

    VkDescriptorSetLayoutBinding atlas_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &atlas_binding,
    };
    VkDescriptorSetLayout descriptor_set_layout = nullptr;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_set_layout_ci, nullptr, &descriptor_set_layout),
             "Failed to create descriptor set layout")

    VkDescriptorSetAllocateInfo descriptor_set_ai{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout,
    };
    VkDescriptorSet descriptor_set = nullptr;
    VK_CHECK(vkAllocateDescriptorSets(device, &descriptor_set_ai, &descriptor_set), "Failed to allocate descriptor set")

    VkDescriptorImageInfo atlas_image_info{
        .sampler = atlas_sampler,
        .imageView = atlas_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    VkWriteDescriptorSet descriptor_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &atlas_image_info,
    };
    vkUpdateDescriptorSets(device, 1, &descriptor_write, 0, nullptr);

    VkAttachmentDescription attachment{
        .format = surface_format.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference attachment_reference{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment_reference,
    };
    VkRenderPassCreateInfo render_pass_ci{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    VkRenderPass render_pass = nullptr;
    VK_CHECK(vkCreateRenderPass(device, &render_pass_ci, nullptr, &render_pass), "Failed to create render pass")

    VkFramebufferAttachmentImageInfo attachment_info{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .width = window.width(),
        .height = window.height(),
        .layerCount = 1,
        .viewFormatCount = 1,
        .pViewFormats = &attachment.format,
    };
    VkFramebufferAttachmentsCreateInfo attachments_ci{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
        .attachmentImageInfoCount = 1,
        .pAttachmentImageInfos = &attachment_info,
    };
    VkFramebufferCreateInfo framebuffer_ci{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = &attachments_ci,
        .flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .width = window.width(),
        .height = window.height(),
        .layers = 1,
    };
    VkFramebuffer framebuffer = nullptr;
    VK_CHECK(vkCreateFramebuffer(device, &framebuffer_ci, nullptr, &framebuffer), "Failed to create framebuffer")

    VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(float) * 4,
    };
    VkPipelineLayoutCreateInfo pipeline_layout_ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    VkPipelineLayout pipeline_layout = nullptr;
    VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_ci, nullptr, &pipeline_layout),
             "Failed to create pipeline layout")

    VkPipelineVertexInputStateCreateInfo vertex_input{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkRect2D scissor{
        .extent{window.width(), window.height()},
    };
    VkViewport viewport{
        .width = static_cast<float>(window.width()),
        .height = static_cast<float>(window.height()),
        .maxDepth = 1.0f,
    };
    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterisation_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1.0f,
    };

    VkPipelineColorBlendAttachmentState blend_attachment{
        // NOLINTNEXTLINE
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT, // NOLINT
    };
    VkPipelineColorBlendStateCreateInfo blend_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment,
    };

    auto *vertex_shader = load_shader(device, "main.vert.spv");
    auto *fragment_shader = load_shader(device, "main.frag.spv");
    std::array shader_stage_cis{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader,
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader,
            .pName = "main",
        },
    };
    VkGraphicsPipelineCreateInfo pipeline_ci{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = shader_stage_cis.size(),
        .pStages = shader_stage_cis.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterisation_state,
        .pMultisampleState = &multisample_state,
        .pColorBlendState = &blend_state,
        .layout = pipeline_layout,
        .renderPass = render_pass,
    };
    VkPipeline pipeline = nullptr;
    VK_CHECK(vkCreateGraphicsPipelines(device, nullptr, 1, &pipeline_ci, nullptr, &pipeline),
             "Failed to create pipeline")

    VkFenceCreateInfo fence_ci{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    VkFence fence = nullptr;
    VK_CHECK(vkCreateFence(device, &fence_ci, nullptr, &fence), "Failed to create fence")

    VkSemaphoreCreateInfo semaphore_ci{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkSemaphore image_available_semaphore = nullptr;
    VkSemaphore rendering_finished_semaphore = nullptr;
    VK_CHECK(vkCreateSemaphore(device, &semaphore_ci, nullptr, &image_available_semaphore),
             "Failed to create semaphore")
    VK_CHECK(vkCreateSemaphore(device, &semaphore_ci, nullptr, &rendering_finished_semaphore),
             "Failed to create semaphore")

    float count = 0.0f;
    float previous_time = 0.0f;
    while (!window.should_close()) {
        auto current_time = static_cast<float>(glfwGetTime());
        float dt = current_time - previous_time;
        previous_time = current_time;
        std::uint32_t image_index = 0;
        vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<std::uint64_t>::max(), image_available_semaphore,
                              nullptr, &image_index);
        // TODO: Record command buffer here, before waiting for fence, instead?
        vkWaitForFences(device, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
        vkResetFences(device, 1, &fence);

        vkResetCommandPool(device, command_pool, 0);
        VkCommandBufferBeginInfo cmd_buf_bi{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(command_buffer, &cmd_buf_bi);

        VkClearValue clear_value{.color{{0.0f, 0.0f, 0.0f, 1.0f}}};
        VkRenderPassAttachmentBeginInfo attachment_bi{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
            .attachmentCount = 1,
            .pAttachments = &swapchain_image_views[image_index],
        };
        VkRenderPassBeginInfo render_pass_bi{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext = &attachment_bi,
            .renderPass = render_pass,
            .framebuffer = framebuffer,
            .renderArea{.extent{window.width(), window.height()}},
            .clearValueCount = 1,
            .pClearValues = &clear_value,
        };
        vkCmdBeginRenderPass(command_buffer, &render_pass_bi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set,
                                0, nullptr);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        double xpos;
        double ypos;
        glfwGetCursorPos(*window, &xpos, &ypos);
        std::array<float, 4> transform{std::sin(count), std::cos(count), static_cast<float>(xpos) / 800.0f,
                                       static_cast<float>(ypos) / 600.0f};
        float scale = 42.0f * std::abs(std::sin(count)) * 8.0f;
        transform[0] = scale / 800.0f;
        transform[1] = scale / 600.0f;
        vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4,
                           transform.data());
        vkCmdDraw(command_buffer, 6, 1, 0, 0);
        vkCmdEndRenderPass(command_buffer);
        vkEndCommandBuffer(command_buffer);

        VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_available_semaphore,
            .pWaitDstStageMask = &wait_stage_mask,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &rendering_finished_semaphore,
        };
        vkQueueSubmit(queue, 1, &submit_info, fence);

        VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &rendering_finished_semaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
        };
        vkQueuePresentKHR(present_queue, &present_info);
        Window::poll_events();
        count += dt;
    }
    vkDeviceWaitIdle(device);
    vkDestroySemaphore(device, rendering_finished_semaphore, nullptr);
    vkDestroySemaphore(device, image_available_semaphore, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    vkDestroyShaderModule(device, vertex_shader, nullptr);
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    vkDestroyRenderPass(device, render_pass, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroySampler(device, atlas_sampler, nullptr);
    vkDestroyImageView(device, atlas_image_view, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    vkFreeMemory(device, atlas_image_memory, nullptr);
    vkDestroyImage(device, atlas_image, nullptr);
    for (auto *image_view : swapchain_image_views) {
        vkDestroyImageView(device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
}
