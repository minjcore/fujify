// Fuji-fy Studio — Vulkan build (Dear ImGui + GLFW + Vulkan via MoltenVK on macOS).
// All editor state + UI + engine orchestration live in fujify_engine.h (shared with the
// OpenGL build). This file only provides the Vulkan setup + texture upload + render loop.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "fujify_ui.h"   // StudioUI + jobs + engine (split headers)

#ifndef MOLTENVK_ICD
#define MOLTENVK_ICD "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json"
#endif

// ---- Vulkan globals (adapted from imgui example_glfw_vulkan) -------------
static VkAllocationCallbacks*   g_Allocator = nullptr;
static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;
static VkCommandPool            g_UploadPool = VK_NULL_HANDLE;
static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t                 g_MinImageCount = 2;
static bool                     g_SwapChainRebuild = false;

static void check_vk(VkResult e) { if (e < 0) { std::fprintf(stderr, "[vk] VkResult=%d\n", e); abort(); } }
static void glfw_error_callback(int e, const char* d) { std::fprintf(stderr, "GLFW %d: %s\n", e, d); }

static bool ext_avail(const ImVector<VkExtensionProperties>& props, const char* ext) {
    for (const VkExtensionProperties& p : props) if (strcmp(p.extensionName, ext) == 0) return true;
    return false;
}

static void SetupVulkan(ImVector<const char*> instance_extensions) {
    {
        uint32_t n; ImVector<VkExtensionProperties> props;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        props.resize(n);
        vkEnumerateInstanceExtensionProperties(nullptr, &n, props.Data);
        if (ext_avail(props, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        VkInstanceCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (ext_avail(props, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif
        ci.enabledExtensionCount = (uint32_t)instance_extensions.Size;
        ci.ppEnabledExtensionNames = instance_extensions.Data;
        check_vk(vkCreateInstance(&ci, g_Allocator, &g_Instance));
    }
    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    {
        ImVector<const char*> dev_ext; dev_ext.push_back("VK_KHR_swapchain");
        uint32_t n; ImVector<VkExtensionProperties> props;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &n, nullptr);
        props.resize(n);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &n, props.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (ext_avail(props, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            dev_ext.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
        const float prio[] = { 1.0f };
        VkDeviceQueueCreateInfo q = {};
        q.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = g_QueueFamily; q.queueCount = 1; q.pQueuePriorities = prio;
        VkDeviceCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &q;
        ci.enabledExtensionCount = (uint32_t)dev_ext.Size; ci.ppEnabledExtensionNames = dev_ext.Data;
        check_vk(vkCreateDevice(g_PhysicalDevice, &ci, g_Allocator, &g_Device));
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }
    {
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
            { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
        };
        VkDescriptorPoolCreateInfo pi = {};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        for (VkDescriptorPoolSize& s : sizes) pi.maxSets += s.descriptorCount;
        pi.poolSizeCount = (uint32_t)IM_COUNTOF(sizes); pi.pPoolSizes = sizes;
        check_vk(vkCreateDescriptorPool(g_Device, &pi, g_Allocator, &g_DescriptorPool));
    }
    {
        VkCommandPoolCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = g_QueueFamily;
        check_vk(vkCreateCommandPool(g_Device, &ci, g_Allocator, &g_UploadPool));
    }
}

static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int w, int h) {
    VkBool32 ok; vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &ok);
    if (!ok) { std::fprintf(stderr, "no WSI support\n"); exit(1); }
    const VkFormat fmts[] = { VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM };
    wd->Surface = surface;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(g_PhysicalDevice, wd->Surface,
        fmts, (size_t)IM_COUNTOF(fmts), VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    VkPresentModeKHR pm[] = { VK_PRESENT_MODE_FIFO_KHR };
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(g_PhysicalDevice, wd->Surface, &pm[0], IM_COUNTOF(pm));
    ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd,
        g_QueueFamily, g_Allocator, w, h, g_MinImageCount, 0);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data) {
    VkSemaphore img_sem = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore done_sem = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, img_sem, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) check_vk(err);

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    check_vk(vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    check_vk(vkResetFences(g_Device, 1, &fd->Fence));
    check_vk(vkResetCommandPool(g_Device, fd->CommandPool, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(vkBeginCommandBuffer(fd->CommandBuffer, &bi));
    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = wd->RenderPass; rp.framebuffer = fd->Framebuffer;
    rp.renderArea.extent.width = wd->Width; rp.renderArea.extent.height = wd->Height;
    rp.clearValueCount = 1; rp.pClearValues = &wd->ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);
    vkCmdEndRenderPass(fd->CommandBuffer);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &img_sem; si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1; si.pCommandBuffers = &fd->CommandBuffer;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &done_sem;
    check_vk(vkEndCommandBuffer(fd->CommandBuffer));
    check_vk(vkQueueSubmit(g_Queue, 1, &si, fd->Fence));
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild) return;
    VkSemaphore done_sem = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1; info.pWaitSemaphores = &done_sem;
    info.swapchainCount = 1; info.pSwapchains = &wd->Swapchain; info.pImageIndices = &wd->FrameIndex;
    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    if (err != VK_SUBOPTIMAL_KHR) check_vk(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

// ---- Vulkan texture (staging upload) ------------------------------------
struct VkTexture {
    VkDescriptorSet ds = VK_NULL_HANDLE;   // used as ImTextureID
    VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; VkSampler sampler = VK_NULL_HANDLE;
    int w = 0, h = 0;
};

static uint32_t vk_mem_type(uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return 0;
}

static void vk_destroy_texture(VkTexture& t) {
    if (t.ds) { ImGui_ImplVulkan_RemoveTexture(t.ds); t.ds = VK_NULL_HANDLE; }
    if (t.sampler) { vkDestroySampler(g_Device, t.sampler, g_Allocator); t.sampler = VK_NULL_HANDLE; }
    if (t.view) { vkDestroyImageView(g_Device, t.view, g_Allocator); t.view = VK_NULL_HANDLE; }
    if (t.image) { vkDestroyImage(g_Device, t.image, g_Allocator); t.image = VK_NULL_HANDLE; }
    if (t.mem) { vkFreeMemory(g_Device, t.mem, g_Allocator); t.mem = VK_NULL_HANDLE; }
}

static bool vk_upload_texture_from_file(const char* path, VkTexture& t, Histogram* hist) {
    int w, h, n;
    unsigned char* pixels = stbi_load(path, &w, &h, &n, 4);
    if (!pixels) return false;
    if (hist) compute_histogram(pixels, w, h, hist);
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    vkDeviceWaitIdle(g_Device);
    vk_destroy_texture(t);

    VkImageCreateInfo ic = {};
    ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_R8G8B8A8_UNORM;
    ic.extent = { (uint32_t)w, (uint32_t)h, 1 }; ic.mipLevels = 1; ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check_vk(vkCreateImage(g_Device, &ic, g_Allocator, &t.image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(g_Device, t.image, &req);
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = req.size;
    ai.memoryTypeIndex = vk_mem_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check_vk(vkAllocateMemory(g_Device, &ai, g_Allocator, &t.mem));
    vkBindImageMemory(g_Device, t.image, t.mem, 0);

    VkBuffer sbuf; VkDeviceMemory smem;
    VkBufferCreateInfo bc = {};
    bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bc.size = size;
    bc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check_vk(vkCreateBuffer(g_Device, &bc, g_Allocator, &sbuf));
    VkMemoryRequirements breq; vkGetBufferMemoryRequirements(g_Device, sbuf, &breq);
    VkMemoryAllocateInfo bai = {};
    bai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; bai.allocationSize = breq.size;
    bai.memoryTypeIndex = vk_mem_type(breq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check_vk(vkAllocateMemory(g_Device, &bai, g_Allocator, &smem));
    vkBindBufferMemory(g_Device, sbuf, smem, 0);
    void* map; vkMapMemory(g_Device, smem, 0, size, 0, &map);
    memcpy(map, pixels, size); vkUnmapMemory(g_Device, smem);
    stbi_image_free(pixels);

    VkCommandBufferAllocateInfo cba = {};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = g_UploadPool; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cba.commandBufferCount = 1;
    VkCommandBuffer cmd; vkAllocateCommandBuffers(g_Device, &cba, &cmd);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image; b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region = {};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
    vkCmdCopyBufferToImage(cmd, sbuf, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si = {}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(g_Queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_Queue);
    vkFreeCommandBuffers(g_Device, g_UploadPool, 1, &cmd);
    vkDestroyBuffer(g_Device, sbuf, g_Allocator);
    vkFreeMemory(g_Device, smem, g_Allocator);

    VkImageViewCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    check_vk(vkCreateImageView(g_Device, &vi, g_Allocator, &t.view));
    VkSamplerCreateInfo sa = {};
    sa.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sa.magFilter = VK_FILTER_LINEAR; sa.minFilter = VK_FILTER_LINEAR;
    sa.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sa.addressModeU = sa.addressModeV = sa.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sa.minLod = 0; sa.maxLod = 1; sa.maxAnisotropy = 1.0f;
    check_vk(vkCreateSampler(g_Device, &sa, g_Allocator, &t.sampler));

    t.ds = ImGui_ImplVulkan_AddTexture(t.sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t.w = w; t.h = h;
    return true;
}

// ---- app ----------------------------------------------------------------
int main(int, char**) {
    // Prefer a MoltenVK ICD bundled inside the .app (standalone); else the system path.
    {
        char exe[4096]; uint32_t sz = sizeof(exe);
        if (_NSGetExecutablePath(exe, &sz) == 0) {
            std::string d(exe); auto s = d.find_last_of('/');
            if (s != std::string::npos) {
                std::string icd = d.substr(0, s) + "/../Resources/vulkan/icd.d/MoltenVK_icd.json";
                if (FILE* f = std::fopen(icd.c_str(), "rb")) {
                    std::fclose(f);
                    setenv("VK_ICD_FILENAMES", icd.c_str(), 1);
                    setenv("VK_DRIVER_FILES", icd.c_str(), 1);
                }
            }
        }
    }
    setenv("VK_ICD_FILENAMES", MOLTENVK_ICD, 0);   // fallback (no overwrite) — system MoltenVK
    setenv("VK_DRIVER_FILES", MOLTENVK_ICD, 0);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "Fuji-fy Studio — Vulkan (MoltenVK)", nullptr, nullptr);
    if (!glfwVulkanSupported()) { std::fprintf(stderr, "GLFW: Vulkan not supported\n"); return 1; }

    ImVector<const char*> extensions;
    uint32_t ext_count = 0;
    const char** glfw_ext = glfwGetRequiredInstanceExtensions(&ext_count);
    for (uint32_t i = 0; i < ext_count; i++) extensions.push_back(glfw_ext[i]);
    SetupVulkan(extensions);

    VkSurfaceKHR surface;
    check_vk(glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface));
    int w, h; glfwGetFramebufferSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    setup_fonts();

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo init = {};
    init.Instance = g_Instance; init.PhysicalDevice = g_PhysicalDevice; init.Device = g_Device;
    init.QueueFamily = g_QueueFamily; init.Queue = g_Queue;
    init.DescriptorPool = g_DescriptorPool;
    init.MinImageCount = g_MinImageCount; init.ImageCount = wd->ImageCount;
    init.PipelineInfoMain.RenderPass = wd->RenderPass;
    init.PipelineInfoMain.Subpass = 0;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.CheckVkResultFn = check_vk;
    ImGui_ImplVulkan_Init(&init);

    // ---- backend texture ops + shared UI ----
    VkTexture tex;
    TextureOps ops;
    ops.upload = [&](const char* p, int* ow, int* oh, Histogram* hist) {
        if (!vk_upload_texture_from_file(p, tex, hist)) return false;
        *ow = tex.w; *oh = tex.h; return true;
    };
    ops.id = [&]() { return (ImTextureID)(uintptr_t)tex.ds; };

    {
        StudioUI ui("Vulkan", ops);   // owns engine state; joins worker + stops daemon on scope exit
        while (!glfwWindowShouldClose(window)) {
            glfwWaitEventsTimeout(ui.frame_timeout());   // sleep when idle instead of spinning 60fps
            int fbw, fbh; glfwGetFramebufferSize(window, &fbw, &fbh);
            if (fbw > 0 && fbh > 0 && (g_SwapChainRebuild || wd->Width != fbw || wd->Height != fbh)) {
                ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
                ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd,
                    g_QueueFamily, g_Allocator, fbw, fbh, g_MinImageCount, 0);
                wd->FrameIndex = 0; g_SwapChainRebuild = false;
            }
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) { ImGui_ImplGlfw_Sleep(10); continue; }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ui.draw();

            ImGui::Render();
            ImDrawData* dd = ImGui::GetDrawData();
            if (dd->DisplaySize.x > 0 && dd->DisplaySize.y > 0) {
                wd->ClearValue.color.float32[0] = 0.08f;
                wd->ClearValue.color.float32[1] = 0.09f;
                wd->ClearValue.color.float32[2] = 0.10f;
                wd->ClearValue.color.float32[3] = 1.0f;
                FrameRender(wd, dd);
                FramePresent(wd);
            }
        }
    }   // ui destructed (worker joined, daemon stopped)

    vkDeviceWaitIdle(g_Device);
    vk_destroy_texture(tex);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyCommandPool(g_Device, g_UploadPool, g_Allocator);
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, wd, g_Allocator);
    vkDestroySurfaceKHR(g_Instance, wd->Surface, g_Allocator);
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
