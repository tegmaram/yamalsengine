#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vk_engine.h"
#include "cloudtex.h"

#include <vulkan/vulkan.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#define VK_CHECK(x)                                                                        \
    do {                                                                                   \
        VkResult _r = (x);                                                                  \
        if (_r != VK_SUCCESS) {                                                             \
            std::fprintf(stderr, "[vk] error %d at %s:%d\n", (int)_r, __FILE__, __LINE__);   \
            std::abort();                                                                   \
        }                                                                                  \
    } while (0)

#ifndef NDEBUG
#define ENABLE_VALIDATION 1
#else
#define ENABLE_VALIDATION 0
#endif

namespace {

constexpr int MAX_FRAMES = 2;

constexpr int CLOUD_TEX_SIZE = 1024;

struct UBO {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 camPosTime;  // xyz camera, w time
    vec4 sunDirCov;   // xyz sun dir, w coverage
    vec4 params;      // x exposure, y eye height, z fog density, w unused
    vec4 env;         // x storm, y wetness, z rain, w cloud base
    vec4 screen;      // xy resolution, zw 1/resolution
    vec4 lightDir;    // xyz flashlight direction, w range
    vec4 lo[8], hi[8];
};
static_assert(sizeof(UBO) == 480, "UBO layout must match the shaders");
constexpr VkDeviceSize UBO_STRIDE = 512;

uint32_t findMemoryType(VkPhysicalDeviceMemoryProperties& mp, uint32_t mask, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags) return i;
    std::fprintf(stderr, "[vk] no suitable memory type\n");
    std::abort();
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT sev,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*) {
    if (sev >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::fprintf(stderr, "[validation] %s\n", data->pMessage);
    return VK_FALSE;
}

// Minimal 24-bit BMP writer (row-padded, bottom-up). Input is B,G,R,A bytes.
void writeBmp(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h) {
    uint32_t rowSize = (w * 3 + 3) & ~3u;
    uint32_t dataSize = rowSize * h;
    uint8_t header[54]{};
    header[0] = 'B';
    header[1] = 'M';
    uint32_t fileSize = 54 + dataSize;
    std::memcpy(header + 2, &fileSize, 4);
    uint32_t off = 54;
    std::memcpy(header + 10, &off, 4);
    uint32_t hs = 40;
    std::memcpy(header + 14, &hs, 4);
    int32_t iw = (int32_t)w, ih = (int32_t)h;
    std::memcpy(header + 18, &iw, 4);
    std::memcpy(header + 22, &ih, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &dataSize, 4);

    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); std::exit(1); }
    std::fwrite(header, 1, 54, f);
    std::vector<uint8_t> row(rowSize, 0);
    for (int y = (int)h - 1; y >= 0; --y) {
        const uint8_t* src = rgba + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, rowSize, f);
    }
    if (std::ferror(f) || std::fclose(f)) { std::perror(path); std::exit(1); }
}

void cmdTransition(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                   VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                   VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, bool isDepth,
                   uint32_t mipCount = 1, uint32_t baseMip = 0) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = baseMip;
    b.subresourceRange.levelCount = mipCount;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

struct Renderer::Impl {
#ifdef _WIN32
    HWND hwnd = nullptr;
    HINSTANCE hinst = nullptr;
#endif
    std::string shaderDir;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueryPool timestamps=VK_NULL_HANDLE;
    float nsPerTick=0; uint32_t timeBits=0; bool timed[MAX_FRAMES]{};
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t qFamily = 0;
    VkPhysicalDeviceMemoryProperties memProps{};

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImage depth = VK_NULL_HANDLE;
    VkDeviceMemory depthMem = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmds[MAX_FRAMES]{};
    VkSemaphore imgAvailable[MAX_FRAMES]{};
    std::vector<VkSemaphore> renderDone;
    bool dirty = false, canCapture = false;
    uint32_t drops = 0, statsCounter = 0;
    VkFence fences[MAX_FRAMES]{};

    VkBuffer ubo = VK_NULL_HANDLE;
    VkDeviceMemory uboMem = VK_NULL_HANDLE;
    UBO* uboMapped = nullptr;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet sets[MAX_FRAMES]{};
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline skyPipe = VK_NULL_HANDLE;
    VkPipeline meshPipe = VK_NULL_HANDLE;
    VkPipeline rainPipe = VK_NULL_HANDLE;

    VkImage cloudTex = VK_NULL_HANDLE;
    VkDeviceMemory cloudTexMem = VK_NULL_HANDLE;
    VkImageView cloudTexView = VK_NULL_HANDLE;
    VkSampler cloudTexSampler = VK_NULL_HANDLE;
    uint32_t cloudMips = 1;

    VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
    VkDeviceMemory vbMem = VK_NULL_HANDLE, ibMem = VK_NULL_HANDLE;
    uint32_t indexCount = 0;
    bool meshUploaded = false;

    uint32_t frame = 0;
    uint32_t imageIndex = 0;
    bool inFrame = false;
    int width = 0, height = 0;

    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;
    VkDeviceSize readbackSize = 0;
    std::string shotPath;
    bool shotPending = false;

    bool hasMemBudget = false;
    float memUsedMB = 0.0f;
    float memBudgetMB = 0.0f;

    VkShaderModule loadShader(const char* name);
    void createSwapchain();
    void destroySwapchain();
    void createDepth();
    void createDescriptors();
    void createPipelines();
    void createCloudTexture(float sunAzimuth);
    void updateMemoryStats();
    void immediateSubmit(void (*fn)(VkCommandBuffer, void*), void* user);
};

VkShaderModule Renderer::Impl::loadShader(const char* name) {
    std::string path = shaderDir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[vk] cannot open shader %s\n", path.c_str());
        std::abort();
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint32_t> code((size_t)size / 4);
    if (std::fread(code.data(), 1, (size_t)size, f) != (size_t)size) std::abort();
    std::fclose(f);

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = (size_t)size;
    ci.pCode = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(dev, &ci, nullptr, &mod));
    return mod;
}

void Renderer::Impl::immediateSubmit(void (*fn)(VkCommandBuffer, void*), void* user) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(dev, &ai, &cmd));

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    fn(cmd, user);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    VK_CHECK(vkCreateFence(dev, &fi, nullptr, &fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, fence));
    VK_CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(dev, fence, nullptr);
    vkFreeCommandBuffers(dev, cmdPool, 1, &cmd);
}

// ---------------------------------------------------------------- init

bool Renderer::init(void* hwnd_, int w, int h, const char* shaderDir_, float sunAzimuth) {
    m = new Impl();
#ifdef _WIN32
    m->hwnd = (HWND)hwnd_;
    m->hinst = GetModuleHandleW(nullptr);
#endif
    m->shaderDir = shaderDir_;
    m->width = w;
    m->height = h;

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "horror";
    app.applicationVersion = 1;
    app.pEngineName = "horror-engine";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_3;

    std::vector<const char*> instExt{VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#else
        VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME
#endif
    };
#if ENABLE_VALIDATION
    instExt.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = (uint32_t)instExt.size();
    ici.ppEnabledExtensionNames = instExt.data();
#if ENABLE_VALIDATION
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = layers;
#endif
    VK_CHECK(vkCreateInstance(&ici, nullptr, &m->instance));

#if ENABLE_VALIDATION
    VkDebugUtilsMessengerCreateInfoEXT dci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto createFn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m->instance,
                                                                             "vkCreateDebugUtilsMessengerEXT");
    if (createFn) createFn(m->instance, &dci, nullptr, &m->debug);
#endif

#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = m->hinst;
    sci.hwnd = m->hwnd;
    VK_CHECK(vkCreateWin32SurfaceKHR(m->instance, &sci, nullptr, &m->surface));

#else
    VkHeadlessSurfaceCreateInfoEXT sci{VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    auto create = (PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(m->instance,"vkCreateHeadlessSurfaceEXT");
    if (!create || create(m->instance,&sci,nullptr,&m->surface) != VK_SUCCESS) return false;
#endif
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m->instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(m->instance, &count, gpus.data());
    for (VkPhysicalDevice g : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(g, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        supported.pNext = &features;
        vkGetPhysicalDeviceFeatures2(g,&supported);
        if (!features.dynamicRendering) continue;
        uint32_t extCount=0;
        vkEnumerateDeviceExtensionProperties(g,nullptr,&extCount,nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(g,nullptr,&extCount,available.data());
        bool hasSwapchain=false;
        for(const auto& e:available) hasSwapchain |= !std::strcmp(e.extensionName,VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        if(!hasSwapchain) continue;
        uint32_t qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(g, &qc, qs.data());
        for (uint32_t i = 0; i < qc; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(g, i, m->surface, &present);
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                m->nsPerTick=props.limits.timestampPeriod; m->timeBits=qs[i].timestampValidBits;
                m->gpu = g;
                m->qFamily = i;
                std::printf("[vk] GPU: %s\n", props.deviceName);
                break;
            }
        }
        if (m->gpu) break;
    }
    if (!m->gpu) {
        std::fprintf(stderr, "[vk] no Vulkan 1.3 device with graphics+present found\n");
        return false;
    }
    vkGetPhysicalDeviceMemoryProperties(m->gpu, &m->memProps);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m->qFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE;


    const char* devExt[2] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, nullptr};
    uint32_t devExtCount = 1;

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m->gpu, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(m->gpu, nullptr, &extCount, exts.data());
    for (auto& e : exts) {
        if (std::strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME) == 0) {
            m->hasMemBudget = true;
            devExt[devExtCount++] = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
            break;
        }
    }

    VkDeviceCreateInfo dci2{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci2.pNext = &f13;
    dci2.queueCreateInfoCount = 1;
    dci2.pQueueCreateInfos = &qci;
    dci2.enabledExtensionCount = devExtCount;
    dci2.ppEnabledExtensionNames = devExt;
    VK_CHECK(vkCreateDevice(m->gpu, &dci2, nullptr, &m->dev));
    if (profile && m->timeBits) {
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qi.queryType=VK_QUERY_TYPE_TIMESTAMP; qi.queryCount=2*MAX_FRAMES;
        VK_CHECK(vkCreateQueryPool(m->dev,&qi,nullptr,&m->timestamps));
    }
    vkGetDeviceQueue(m->dev, m->qFamily, 0, &m->queue);

    m->createSwapchain();
    m->createDepth();

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = m->qFamily;
    VK_CHECK(vkCreateCommandPool(m->dev, &pci, nullptr, &m->cmdPool));

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = m->cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = MAX_FRAMES;
    VK_CHECK(vkAllocateCommandBuffers(m->dev, &cai, m->cmds));

    VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (int i = 0; i < MAX_FRAMES; ++i) {
        VK_CHECK(vkCreateSemaphore(m->dev, &semci, nullptr, &m->imgAvailable[i]));
        VK_CHECK(vkCreateFence(m->dev, &fci, nullptr, &m->fences[i]));
    }
    m->renderDone.resize(m->images.size());
    for (size_t i = 0; i < m->images.size(); ++i)
        VK_CHECK(vkCreateSemaphore(m->dev, &semci, nullptr, &m->renderDone[i]));

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = UBO_STRIDE * MAX_FRAMES;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(m->dev, &bci, nullptr, &m->ubo));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m->dev, m->ubo, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(m->memProps, req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(m->dev, &mai, nullptr, &m->uboMem));
    VK_CHECK(vkBindBufferMemory(m->dev, m->ubo, m->uboMem, 0));
    VK_CHECK(vkMapMemory(m->dev, m->uboMem, 0, VK_WHOLE_SIZE, 0, (void**)&m->uboMapped));

    m->createCloudTexture(sunAzimuth);
    m->createDescriptors();
    m->createPipelines();

    std::printf("[vk] renderer ready (%dx%d), cloud sheet %d^2\n", w, h, CLOUD_TEX_SIZE);
    return true;
}

void Renderer::Impl::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fmtCount, formats.data());

    colorFormat = formats.empty() ? VK_FORMAT_B8G8R8A8_SRGB : formats[0].format;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            colorFormat = f.format;
            colorSpace = f.colorSpace;
            break;
        }
    }

    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)clampf((float)width, (float)caps.minImageExtent.width,
                                        (float)caps.maxImageExtent.width);
        extent.height = (uint32_t)clampf((float)height, (float)caps.minImageExtent.height,
                                         (float)caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> modes(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &pmCount, modes.data());
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    for (auto pm : modes)
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) present = VK_PRESENT_MODE_MAILBOX_KHR;
    if (present == VK_PRESENT_MODE_FIFO_KHR)
        for (auto pm : modes)
            if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR) present = VK_PRESENT_MODE_IMMEDIATE_KHR;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = colorFormat;
    sci.imageColorSpace = colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    canCapture = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
                 (colorFormat == VK_FORMAT_B8G8R8A8_SRGB || colorFormat == VK_FORMAT_B8G8R8A8_UNORM);
    if (canCapture) sci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present;
    sci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(dev, &sci, nullptr, &swapchain));

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &n, nullptr);
    images.resize(n);
    vkGetSwapchainImagesKHR(dev, swapchain, &n, images.data());

    views.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = colorFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &views[i]));
    }
}

void Renderer::Impl::destroySwapchain() {
    for (auto v : views)
        if (v) vkDestroyImageView(dev, v, nullptr);
    views.clear();
    images.clear();
    if (swapchain) vkDestroySwapchainKHR(dev, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void Renderer::Impl::createDepth() {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = depthFormat;
    ici.extent = {extent.width, extent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VK_CHECK(vkCreateImage(dev, &ici, nullptr, &depth));

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, depth, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(memProps, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(dev, &mai, nullptr, &depthMem));
    VK_CHECK(vkBindImageMemory(dev, depth, depthMem, 0));

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = depth;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = depthFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &depthView));
}

void Renderer::Impl::createCloudTexture(float sunAzimuth) {
    const int N = CLOUD_TEX_SIZE;
    CloudSheet sheet = generateCloudSheet(N, 1337u, sunAzimuth, 0.55f);

    // mip chain
    uint32_t mips = 1;
    for (int s = N; s > 1; s >>= 1) ++mips;
    cloudMips = mips;

    const VkDeviceSize bytes = (VkDeviceSize)sheet.rgba.size();

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer staging;
    VK_CHECK(vkCreateBuffer(dev, &bci, nullptr, &staging));
    VkMemoryRequirements sreq;
    vkGetBufferMemoryRequirements(dev, staging, &sreq);
    VkMemoryAllocateInfo smai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = findMemoryType(memProps, sreq.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem;
    VK_CHECK(vkAllocateMemory(dev, &smai, nullptr, &stagingMem));
    VK_CHECK(vkBindBufferMemory(dev, staging, stagingMem, 0));
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(dev, stagingMem, 0, bytes, 0, &mapped));
    std::memcpy(mapped, sheet.rgba.data(), (size_t)bytes);
    vkUnmapMemory(dev, stagingMem);

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)N, (uint32_t)N, 1};
    ici.mipLevels = mips;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK(vkCreateImage(dev, &ici, nullptr, &cloudTex));
    VkMemoryRequirements ireq;
    vkGetImageMemoryRequirements(dev, cloudTex, &ireq);
    VkMemoryAllocateInfo imai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    imai.allocationSize = ireq.size;
    imai.memoryTypeIndex = findMemoryType(memProps, ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(dev, &imai, nullptr, &cloudTexMem));
    VK_CHECK(vkBindImageMemory(dev, cloudTex, cloudTexMem, 0));

    struct UpCtx { VkBuffer staging; VkImage image; int n; uint32_t mips; } ctx{staging, cloudTex, N, mips};
    immediateSubmit(
        [](VkCommandBuffer cmd, void* u) {
            auto* c = (UpCtx*)u;
            cmdTransition(cmd, c->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, false, c->mips);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {(uint32_t)c->n, (uint32_t)c->n, 1};
            vkCmdCopyBufferToImage(cmd, c->staging, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // blit chain for mips
            int32_t w = c->n, h = c->n;
            for (uint32_t level = 1; level < c->mips; ++level) {
                cmdTransition(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, false, 1, level - 1);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
                blit.srcOffsets[1] = {w, h, 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
                blit.dstOffsets[1] = {w > 1 ? w / 2 : 1, h > 1 ? h / 2 : 1, 1};
                vkCmdBlitImage(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                cmdTransition(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                              VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, false, 1, level - 1);
                w /= 2;
                h /= 2;
                if (w < 1) w = 1;
                if (h < 1) h = 1;
            }
            cmdTransition(cmd, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, false, c->mips);
        },
        &ctx);

    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = cloudTex;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = mips;
    vci.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &cloudTexView));

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod = float(mips - 1);
    VK_CHECK(vkCreateSampler(dev, &sci, nullptr, &cloudTexSampler));
}

void Renderer::Impl::createDescriptors() {
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 2;
    lci.pBindings = b;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &lci, nullptr, &setLayout));

    VkDescriptorPoolSize sizes[2]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = MAX_FRAMES;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(dev, &pci, nullptr, &descPool));

    VkDescriptorSetLayout layouts[MAX_FRAMES];
    for (int i = 0; i < MAX_FRAMES; ++i) layouts[i] = setLayout;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool;
    ai.descriptorSetCount = MAX_FRAMES;
    ai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(dev, &ai, sets));

    for (int i = 0; i < MAX_FRAMES; ++i) {
        VkDescriptorBufferInfo bi{ubo, (VkDeviceSize)i * UBO_STRIDE, sizeof(UBO)};
        VkDescriptorImageInfo ii{cloudTexSampler, cloudTexView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2]{};
        w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = sets[i];
        w[0].dstBinding = 0;
        w[0].descriptorCount = 1;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w[0].pBufferInfo = &bi;
        w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[1].dstSet = sets[i];
        w[1].dstBinding = 1;
        w[1].descriptorCount = 1;
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = &ii;
        vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
    }

    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    VK_CHECK(vkCreatePipelineLayout(dev, &plci, nullptr, &pipeLayout));
}

void Renderer::Impl::createPipelines() {
    struct PipeDesc {
        const char* vs;
        const char* fs;
        bool hasVerts;
        bool usesDepth;
        bool depthTest;
        bool depthWrite;
        bool blend;
    };

    auto make = [&](const PipeDesc& d) {
        VkShaderModule v = loadShader(d.vs);
        VkShaderModule f = loadShader(d.fs);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = v;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = f;
        stages[1].pName = "main";

        VkVertexInputBindingDescription vbind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[4]{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, p)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, n)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, c)};

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (d.hasVerts) {
            vi.vertexBindingDescriptionCount = 1;
            vi.pVertexBindingDescriptions = &vbind;
            vi.vertexAttributeDescriptionCount = 4;
            vi.pVertexAttributeDescriptions = attrs;
        }

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = d.depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = d.depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        cba.blendEnable = d.blend ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dsi{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dsi.dynamicStateCount = 2;
        dsi.pDynamicStates = dyn;

        VkPipelineRenderingCreateInfo prci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        prci.colorAttachmentCount = 1;
        prci.pColorAttachmentFormats = &colorFormat;
        prci.depthAttachmentFormat = d.usesDepth ? depthFormat : VK_FORMAT_UNDEFINED;

        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.pNext = &prci;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dsi;
        gp.layout = pipeLayout;
        VkPipeline pipe = VK_NULL_HANDLE;
        VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe));
        vkDestroyShaderModule(dev, v, nullptr);
        vkDestroyShaderModule(dev, f, nullptr);
        return pipe;
    };

    skyPipe = make({"sky.vert.spv", "sky.frag.spv", false, true, false, false, false});
    meshPipe = make({"mesh.vert.spv", "mesh.frag.spv", true, true, true, true, false});
    rainPipe = make({"rain.vert.spv", "rain.frag.spv", false, true, true, false, true});
}

void Renderer::Impl::updateMemoryStats() {
    if (!hasMemBudget) return;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    props.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(gpu, &props);
    float used = 0.0f, total = 0.0f;
    for (uint32_t i = 0; i < props.memoryProperties.memoryHeapCount; ++i) {
        used += (float)budget.heapUsage[i] / (1024.0f * 1024.0f);
        total += (float)budget.heapBudget[i] / (1024.0f * 1024.0f);
    }
    memUsedMB = used;
    memBudgetMB = total;
}

// ---------------------------------------------------------------- frame

bool Renderer::beginFrame(int w, int h, const CameraState& cam, const SkyState& sky) {
    if (w <= 0 || h <= 0) return false;

    bool needRecreate = m->dirty || (m->swapchain == VK_NULL_HANDLE) || w != m->width || h != m->height;
    if (needRecreate) {
        vkDeviceWaitIdle(m->dev);
        m->dirty = false;
        m->destroySwapchain();
        if (m->depthView) vkDestroyImageView(m->dev, m->depthView, nullptr);
        if (m->depth) vkDestroyImage(m->dev, m->depth, nullptr);
        if (m->depthMem) vkFreeMemory(m->dev, m->depthMem, nullptr);
        m->depthView = VK_NULL_HANDLE;
        m->depth = VK_NULL_HANDLE;
        m->depthMem = VK_NULL_HANDLE;
        m->width = w;
        m->height = h;
        VkFormat oldFormat=m->colorFormat;
        m->createSwapchain();
        if(m->colorFormat!=oldFormat){
            vkDestroyPipeline(m->dev,m->skyPipe,nullptr); vkDestroyPipeline(m->dev,m->meshPipe,nullptr);
            vkDestroyPipeline(m->dev,m->rainPipe,nullptr); m->createPipelines();
        }
        m->createDepth();
        if (m->renderDone.size() < m->images.size()) m->renderDone.resize(m->images.size());
        for (size_t i = 0; i < m->images.size(); ++i) {
            if (m->renderDone[i] == VK_NULL_HANDLE) {
                VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                VK_CHECK(vkCreateSemaphore(m->dev, &sci, nullptr, &m->renderDone[i]));
            }
        }
    }

    VK_CHECK(vkWaitForFences(m->dev, 1, &m->fences[m->frame], VK_TRUE, UINT64_MAX));

    if(m->timestamps && m->timed[m->frame]) {
        uint64_t t[2];
        VK_CHECK(vkGetQueryPoolResults(m->dev,m->timestamps,m->frame*2,2,sizeof(t),t,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
        uint64_t mask=UINT64_MAX>>(64-m->timeBits);
        gpuMs=float((t[1]-t[0])&mask)*m->nsPerTick*1.e-6f;
    }
    VkResult acq = vkAcquireNextImageKHR(m->dev, m->swapchain, UINT64_MAX,
                                         m->imgAvailable[m->frame], VK_NULL_HANDLE, &m->imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) { m->dirty = true; return false; }
    m->dirty = acq == VK_SUBOPTIMAL_KHR;
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) VK_CHECK(acq);

    VK_CHECK(vkResetFences(m->dev, 1, &m->fences[m->frame]));

    float aspect = (float)m->extent.width / (float)m->extent.height;
    mat4 proj = perspectiveVulkan(cam.fovY, aspect, 0.1f, 600.0f);
    vec3 fwd{std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
             -std::cos(cam.yaw) * std::cos(cam.pitch)};
    mat4 view = lookAt(cam.pos, cam.pos + fwd, {0, 1, 0});
    mat4 vp = proj * view;

    UBO u{};
    u.viewProj = vp;
    u.invViewProj = inverse(vp);
    u.camPosTime = vec4(cam.pos, sky.time);
    u.sunDirCov = vec4(normalize(sky.sunDir), sky.coverage);
    u.params = vec4(sky.exposure, cam.pos.y, sky.flash, sky.flashlight);
    m->drops = uint32_t(4800 * clampf(sky.rain,0,1));
    u.env = vec4(sky.storm, sky.wetness, sky.rain, sky.cloudBase);
    u.screen = vec4((float)m->extent.width, (float)m->extent.height,
                    1.0f / (float)m->extent.width, 1.0f / (float)m->extent.height);
    u.lightDir = vec4(sky.lightDir, float(sky.blockers));
    std::memcpy(u.lo,sky.lo,sizeof(u.lo)); std::memcpy(u.hi,sky.hi,sizeof(u.hi));
    std::memcpy((uint8_t*)m->uboMapped + m->frame * UBO_STRIDE, &u, sizeof(UBO));

    VkCommandBuffer cmd = m->cmds[m->frame];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    if(m->timestamps){ vkCmdResetQueryPool(cmd,m->timestamps,m->frame*2,2);
        vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,m->timestamps,m->frame*2); }

    cmdTransition(cmd, m->images[m->imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, false);
    cmdTransition(cmd, m->depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, true);

    VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAtt.imageView = m->views[m->imageIndex];
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.clearValue.color = {{0.02f, 0.02f, 0.03f, 1.0f}};

    VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAtt.imageView = m->depthView;
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea.extent = m->extent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;
    ri.pDepthAttachment = &depthAtt;
    vkCmdBeginRendering(cmd, &ri);

    VkViewport vport{0, 0, (float)m->extent.width, (float)m->extent.height, 0, 1};
    VkRect2D scissor{{0, 0}, m->extent};
    vkCmdSetViewport(cmd, 0, 1, &vport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeLayout, 0, 1,
                            &m->sets[m->frame], 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m->skyPipe);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    m->inFrame = true;
    return true;
}

void Renderer::drawWorld(const MeshData& mesh) {
    if (!m->inFrame) return;
    VkCommandBuffer cmd = m->cmds[m->frame];

    if (!m->meshUploaded) {
        VkDeviceSize vbytes = mesh.verts.size() * sizeof(Vertex);
        VkDeviceSize ibytes = mesh.idx.size() * sizeof(uint32_t);

        auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf,
                                VkDeviceMemory& mem) {
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size = size;
            bci.usage = usage;
            VK_CHECK(vkCreateBuffer(m->dev, &bci, nullptr, &buf));
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(m->dev, buf, &req);
            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            mai.allocationSize = req.size;
            mai.memoryTypeIndex = findMemoryType(m->memProps, req.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_CHECK(vkAllocateMemory(m->dev, &mai, nullptr, &mem));
            VK_CHECK(vkBindBufferMemory(m->dev, buf, mem, 0));
        };

        createBuffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m->vb, m->vbMem);
        createBuffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m->ib, m->ibMem);

        void* p = nullptr;
        VK_CHECK(vkMapMemory(m->dev, m->vbMem, 0, vbytes, 0, &p));
        std::memcpy(p, mesh.verts.data(), (size_t)vbytes);
        vkUnmapMemory(m->dev, m->vbMem);
        VK_CHECK(vkMapMemory(m->dev, m->ibMem, 0, ibytes, 0, &p));
        std::memcpy(p, mesh.idx.data(), (size_t)ibytes);
        vkUnmapMemory(m->dev, m->ibMem);

        m->indexCount = (uint32_t)mesh.idx.size();
        m->meshUploaded = true;
    }

    VkBuffer bufs[] = {m->vb};
    VkDeviceSize offs[] = {0};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m->meshPipe);
    vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offs);
    vkCmdBindIndexBuffer(cmd, m->ib, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, m->indexCount, 1, 0, 0, 0);
}

void Renderer::endFrame() {
    if (!m->inFrame) return;
    m->inFrame = false;
    VkCommandBuffer cmd = m->cmds[m->frame];

    if (m->drops) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m->rainPipe);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m->pipeLayout, 0, 1,
                                &m->sets[m->frame], 0, nullptr);
        vkCmdDraw(cmd, 6, m->drops, 0, 0);
    }

    vkCmdEndRendering(cmd);

    if(m->timestamps){ vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,m->timestamps,m->frame*2+1);
        m->timed[m->frame]=true; }
    bool wantShot = m->shotPending;
    if (wantShot) {
        VkDeviceSize need = (VkDeviceSize)m->extent.width * m->extent.height * 4;
        if (need > m->readbackSize) {
            if (m->readback) vkDestroyBuffer(m->dev, m->readback, nullptr);
            if (m->readbackMem) vkFreeMemory(m->dev, m->readbackMem, nullptr);
            VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bci.size = need;
            bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VK_CHECK(vkCreateBuffer(m->dev, &bci, nullptr, &m->readback));
            VkMemoryRequirements req;
            vkGetBufferMemoryRequirements(m->dev, m->readback, &req);
            VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            mai.allocationSize = req.size;
            mai.memoryTypeIndex = findMemoryType(m->memProps, req.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_CHECK(vkAllocateMemory(m->dev, &mai, nullptr, &m->readbackMem));
            VK_CHECK(vkBindBufferMemory(m->dev, m->readback, m->readbackMem, 0));
            m->readbackSize = need;
        }

        cmdTransition(cmd, m->images[m->imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, false);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {m->extent.width, m->extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, m->images[m->imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m->readback, 1, &region);
        cmdTransition(cmd, m->images[m->imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0,
                      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, false);
    } else {
        cmdTransition(cmd, m->images[m->imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, false);
    }
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m->imgAvailable[m->frame];
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m->renderDone[m->imageIndex];
    VK_CHECK(vkQueueSubmit(m->queue, 1, &si, m->fences[m->frame]));

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m->renderDone[m->imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &m->swapchain;
    pi.pImageIndices = &m->imageIndex;
    VkResult r = vkQueuePresentKHR(m->queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) m->dirty = true;
    else VK_CHECK(r);

    if (wantShot) {
        VK_CHECK(vkWaitForFences(m->dev, 1, &m->fences[m->frame], VK_TRUE, UINT64_MAX));
        void* p = nullptr;
        VK_CHECK(vkMapMemory(m->dev, m->readbackMem, 0, m->readbackSize, 0, &p));
        writeBmp(m->shotPath.c_str(), (const uint8_t*)p, m->extent.width, m->extent.height);
        vkUnmapMemory(m->dev, m->readbackMem);
        m->shotPending = false;
        std::printf("[vk] wrote %s\n", m->shotPath.c_str());
    }

    m->frame = (m->frame + 1) % MAX_FRAMES;

    if ((++m->statsCounter & 31u) == 0) {
        m->updateMemoryStats();
        vramUsedMB = m->memUsedMB;
        vramBudgetMB = m->memBudgetMB;
    }
}

void Renderer::requestScreenshot(const char* path) {
    if (!m) return;
    if (!m->canCapture) { std::fprintf(stderr,"Surface does not support BGRA capture\n"); std::exit(1); }
    m->shotPath = path;
    m->shotPending = true;
}

void Renderer::waitIdle() {
    if (m && m->dev) vkDeviceWaitIdle(m->dev);
}

void Renderer::shutdown() {
    if (!m) return;
    vkDeviceWaitIdle(m->dev);

    for (auto& s : m->renderDone)
        if (s) vkDestroySemaphore(m->dev, s, nullptr);
    for (int i = 0; i < MAX_FRAMES; ++i) {
        if (m->imgAvailable[i]) vkDestroySemaphore(m->dev, m->imgAvailable[i], nullptr);
        if (m->fences[i]) vkDestroyFence(m->dev, m->fences[i], nullptr);
    }
    if (m->vb) vkDestroyBuffer(m->dev, m->vb, nullptr);
    if (m->vbMem) vkFreeMemory(m->dev, m->vbMem, nullptr);
    if (m->ib) vkDestroyBuffer(m->dev, m->ib, nullptr);
    if (m->ibMem) vkFreeMemory(m->dev, m->ibMem, nullptr);
    if (m->readback) vkDestroyBuffer(m->dev, m->readback, nullptr);
    if (m->readbackMem) vkFreeMemory(m->dev, m->readbackMem, nullptr);
    if (m->skyPipe) vkDestroyPipeline(m->dev, m->skyPipe, nullptr);
    if (m->meshPipe) vkDestroyPipeline(m->dev, m->meshPipe, nullptr);
    if (m->rainPipe) vkDestroyPipeline(m->dev, m->rainPipe, nullptr);
    if (m->pipeLayout) vkDestroyPipelineLayout(m->dev, m->pipeLayout, nullptr);
    if (m->descPool) vkDestroyDescriptorPool(m->dev, m->descPool, nullptr);
    if (m->setLayout) vkDestroyDescriptorSetLayout(m->dev, m->setLayout, nullptr);
    if (m->cloudTexSampler) vkDestroySampler(m->dev, m->cloudTexSampler, nullptr);
    if (m->cloudTexView) vkDestroyImageView(m->dev, m->cloudTexView, nullptr);
    if (m->cloudTex) vkDestroyImage(m->dev, m->cloudTex, nullptr);
    if (m->cloudTexMem) vkFreeMemory(m->dev, m->cloudTexMem, nullptr);
    if (m->ubo) vkDestroyBuffer(m->dev, m->ubo, nullptr);
    if (m->uboMem) vkFreeMemory(m->dev, m->uboMem, nullptr);
    if (m->depthView) vkDestroyImageView(m->dev, m->depthView, nullptr);
    if (m->depth) vkDestroyImage(m->dev, m->depth, nullptr);
    if (m->depthMem) vkFreeMemory(m->dev, m->depthMem, nullptr);
    m->destroySwapchain();
    if(m->timestamps) vkDestroyQueryPool(m->dev,m->timestamps,nullptr);
    if (m->cmdPool) vkDestroyCommandPool(m->dev, m->cmdPool, nullptr);
    if (m->dev) vkDestroyDevice(m->dev, nullptr);
    if (m->surface) vkDestroySurfaceKHR(m->instance, m->surface, nullptr);
#if ENABLE_VALIDATION
    if (m->debug) {
        auto destroyFn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            m->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFn) destroyFn(m->instance, m->debug, nullptr);
    }
#endif
    if (m->instance) vkDestroyInstance(m->instance, nullptr);
    delete m;
    m = nullptr;
}
