#include <rhi/RHI.h>

#include <fnd/Assert.h>
#include <fnd/Log.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace drgn
{

namespace
{

constexpr uint32_t kMaxBindlessTextures = 1024;
constexpr uint32_t kDefaultBackBufferCount = 2;
constexpr uint32_t kMaxDrawsPerCommandList = 8192;
constexpr uint32_t kDrawConstantsSize = 256;
constexpr DWORD kAcquireRetrySleepMs = 1;
constexpr VkFormat kSwapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;

VkFormat ToVkFormat(Format format)
{
    switch (format)
    {
    case Format::R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R16_UINT:             return VK_FORMAT_R16_UINT;
    case Format::R32_UINT:             return VK_FORMAT_R32_UINT;
    case Format::R32_FLOAT:            return VK_FORMAT_R32_SFLOAT;
    case Format::R32G32_FLOAT:         return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32G32B32_FLOAT:      return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32B32A32_FLOAT:   return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::Unknown:              return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

uint32_t FormatByteSize(Format format)
{
    switch (format)
    {
    case Format::R8G8B8A8_UNORM:       return 4;
    case Format::R16_UINT:             return 2;
    case Format::R32_UINT:             return 4;
    case Format::R32_FLOAT:            return 4;
    case Format::R32G32_FLOAT:         return 8;
    case Format::R32G32B32_FLOAT:      return 12;
    case Format::R32G32B32A32_FLOAT:   return 16;
    case Format::Unknown:              return 0;
    }
    return 0;
}

uint64_t AlignUp(uint64_t value, uint64_t alignment)
{
    return alignment <= 1 ? value : (value + alignment - 1) & ~(alignment - 1);
}

void CheckVk(VkResult result, const char* message)
{
    MIGI_ASSERT(result == VK_SUCCESS, message);
}

bool IsPresentResult(VkResult result)
{
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

bool IsOutOfDateResult(VkResult result)
{
    return result == VK_ERROR_OUT_OF_DATE_KHR;
}

bool IsAcquireWaitResult(VkResult result)
{
    return result == VK_NOT_READY || result == VK_TIMEOUT;
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, name) == 0)
            return true;
    }
    return false;
}

} // namespace

class VulkanRHI final : public RHI
{
public:
    VulkanRHI();
    ~VulkanRHI() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle handle) override;
    GpuAddress GetBufferGpuAddress(BufferHandle handle) const override;
    void UpdateBuffer(BufferHandle handle, const void* data, uint32_t byteSize, uint32_t byteOffset) override;

    TextureIndex CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureIndex index) override;

    ShaderPipelineHandle CreateShaderPipeline(const ShaderPipelineDesc& desc) override;
    void DestroyShaderPipeline(ShaderPipelineHandle handle) override;

    SwapChainHandle CreateSwapChain(const SwapChainDesc& desc) override;
    void ResizeSwapChain(SwapChainHandle handle, uint32_t width, uint32_t height, bool fullscreen) override;
    void DestroySwapChain(SwapChainHandle handle) override;

    CommandList* BeginCommandList() override;
    void Submit(CommandList* cmd) override;
    void Present(SwapChainHandle handle, uint32_t syncInterval) override;
    void WaitIdle() override;

private:
    struct BufferResource
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceAddress address = 0;
        VkDeviceSize byteSize = 0;
        void* mapped = nullptr;
    };

    struct BufferRecord
    {
        BufferResource resource;
        bool alive = false;
    };

    struct TextureRecord
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool alive = false;
    };

    struct ShaderPipelineRecord
    {
        VkPipeline opaquePipeline = VK_NULL_HANDLE;
        VkPipeline alphaPipeline = VK_NULL_HANDLE;
        bool alive = false;
    };

    struct FrameSync
    {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence lastFence = VK_NULL_HANDLE;
    };

    struct SwapChainRecord
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapChain = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<FrameSync> sync;
        VkExtent2D extent = {};
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t bufferCount = 0;
        uint32_t nextSync = 0;
        uint32_t pendingPresentSync = std::numeric_limits<uint32_t>::max();
        uint32_t pendingPresentImageIndex = std::numeric_limits<uint32_t>::max();
        bool fullscreen = false;
        bool alive = false;
    };

    struct BufferSlice
    {
        const BufferRecord* record = nullptr;
        VkDeviceSize offset = 0;
    };

    class VulkanCommandList final : public CommandList
    {
    public:
        explicit VulkanCommandList(VulkanRHI& owner);
        ~VulkanCommandList() override;

        bool CanReuse() const;
        void ResetForRecording();
        void MarkSubmitted();

        VkCommandBuffer Get() const { return commandBuffer; }
        VkFence GetFence() const { return fence; }
        SwapChainHandle GetActiveSwapChain() const { return activeSwapChain; }
        uint32_t GetActiveSyncIndex() const { return activeSyncIndex; }
        uint32_t GetActiveImageIndex() const { return activeImageIndex; }

        void BeginRenderPass(SwapChainHandle target, const ClearColor& clear) override;
        void EndRenderPass() override;
        void SetViewport(const Viewport& vp) override;
        void SetScissor(const Scissor& sc) override;
        void SetBlendMode(BlendMode mode) override;
        void BindShaderPipeline(ShaderPipelineHandle pipeline) override;
        void DrawIndexed(const DrawIndexedDesc& desc) override;

        bool recording = false;

    private:
        struct DrawParams
        {
            uint32_t firstIndex = 0;
            int32_t vertexOffset = 0;
            uint32_t indexFormat = 0;
            uint32_t padding = 0;
        };
        static_assert(sizeof(DrawParams) == 16);

        void ApplyPipelineState();
        VkDescriptorSet AllocateDrawDescriptorSet();

        VulkanRHI& owner;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        BufferResource drawConstantsBuffer;
        BufferResource drawParamsBuffer;
        uint32_t drawCount = 0;
        SwapChainHandle activeSwapChain;
        uint32_t activeImageIndex = 0;
        uint32_t activeSyncIndex = std::numeric_limits<uint32_t>::max();
        ShaderPipelineHandle activePipeline;
        BlendMode activeBlendMode = BlendMode::Opaque;
        bool renderPassOpen = false;
    };

    void CreateInstance();
    void PickPhysicalDevice();
    void CreateDevice();
    void CreateCommandPool();
    void CreateDescriptorLayouts();
    void CreateRenderPass();
    void CreateSampler();
    void CreateBindlessSet();
    void CreateFallbackTexture();

    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
    BufferResource CreateBufferResource(
        VkDeviceSize byteSize,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags memoryProperties,
        bool mapped);
    void DestroyBufferResource(BufferResource& resource);
    TextureRecord CreateTextureResource(uint32_t width, uint32_t height, Format format, const void* initialData);
    void DestroyTextureRecord(TextureRecord& record);
    void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void ExecuteImmediate(const std::function<void(VkCommandBuffer)>& record);
    VkShaderModule CreateShaderModule(std::span<const std::byte> bytes);
    VkPipeline CreatePipeline(const ShaderPipelineDesc& desc, BlendMode blendMode);
    VkImageView CreateImageView(VkImage image, VkFormat format);
    VkFramebuffer CreateFramebuffer(VkImageView view, VkExtent2D extent);
    void WriteBindlessTexture(TextureIndex index, VkImageView view);
    void FillBindlessTextures(VkImageView view);

    void CreateSwapChainObjects(SwapChainRecord& sc, uint32_t width, uint32_t height);
    void ReleaseSwapChainObjects(SwapChainRecord& sc);
    void RecreateSwapChainObjects(SwapChainRecord& sc, uint32_t width, uint32_t height);
    void CreateFrameSyncs(SwapChainRecord& sc);
    void DestroyFrameSyncs(SwapChainRecord& sc);
    void ClearCompletedFenceReferences(VkFence fence);
    VkSurfaceFormatKHR ChooseSurfaceFormat(VkSurfaceKHR surface) const;
    VkPresentModeKHR ChoosePresentMode(VkSurfaceKHR surface) const;
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) const;

    BufferRecord& GetBuffer(BufferHandle handle);
    const BufferRecord& GetBuffer(BufferHandle handle) const;
    ShaderPipelineRecord& GetPipeline(ShaderPipelineHandle handle);
    const ShaderPipelineRecord& GetPipeline(ShaderPipelineHandle handle) const;
    SwapChainRecord& GetSwapChain(SwapChainHandle handle);
    const SwapChainRecord& GetSwapChain(SwapChainHandle handle) const;
    BufferSlice FindBufferSlice(GpuAddress address) const;

    template<typename Record, typename Handle>
    Handle AllocateHandle(std::vector<Record>& records);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_physicalDeviceProperties = {};
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_drawSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bindlessSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorPool m_bindlessDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_bindlessSet = VK_NULL_HANDLE;
    TextureRecord m_fallbackTexture;
    VkDeviceSize m_uniformBufferAlignment = 256;
    std::mutex m_vulkanMutex;

    std::vector<BufferRecord> m_buffers;
    std::vector<TextureRecord> m_textures;
    std::vector<uint32_t> m_freeTextureIndices;
    std::vector<ShaderPipelineRecord> m_pipelines;
    std::vector<SwapChainRecord> m_swapChains;
    std::vector<std::unique_ptr<VulkanCommandList>> m_commandLists;
};

template<typename Record, typename Handle>
Handle VulkanRHI::AllocateHandle(std::vector<Record>& records)
{
    for (uint32_t i = 0; i < records.size(); ++i)
    {
        if (!records[i].alive)
        {
            records[i].alive = true;
            return Handle(i + 1);
        }
    }

    Record record{};
    record.alive = true;
    records.push_back(std::move(record));
    return Handle(static_cast<uint32_t>(records.size()));
}

VulkanRHI::VulkanRHI()
{
    MIGI_LOG_INFO("Initializing Vulkan RHI");

    CreateInstance();
    PickPhysicalDevice();
    CreateDevice();
    CreateCommandPool();
    CreateDescriptorLayouts();
    CreateRenderPass();
    CreateSampler();
    CreateBindlessSet();
    CreateFallbackTexture();

    MIGI_LOG_INFO("Vulkan RHI initialized");
}

VulkanRHI::~VulkanRHI()
{
    WaitIdle();

    m_commandLists.clear();

    for (SwapChainRecord& sc : m_swapChains)
    {
        if (sc.alive)
        {
            ReleaseSwapChainObjects(sc);
            DestroyFrameSyncs(sc);
            if (sc.surface != VK_NULL_HANDLE)
                vkDestroySurfaceKHR(m_instance, sc.surface, nullptr);
        }
    }

    for (ShaderPipelineRecord& pipeline : m_pipelines)
    {
        if (pipeline.alive)
        {
            vkDestroyPipeline(m_device, pipeline.opaquePipeline, nullptr);
            vkDestroyPipeline(m_device, pipeline.alphaPipeline, nullptr);
        }
    }

    for (TextureRecord& texture : m_textures)
    {
        if (texture.alive)
            DestroyTextureRecord(texture);
    }
    DestroyTextureRecord(m_fallbackTexture);

    for (BufferRecord& buffer : m_buffers)
    {
        if (buffer.alive)
            DestroyBufferResource(buffer.resource);
    }

    if (m_bindlessDescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_bindlessDescriptorPool, nullptr);
    if (m_sampler != VK_NULL_HANDLE)
        vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_bindlessSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_bindlessSetLayout, nullptr);
    if (m_drawSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_drawSetLayout, nullptr);
    if (m_commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_device != VK_NULL_HANDLE)
        vkDestroyDevice(m_device, nullptr);
    if (m_instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_instance, nullptr);
}

void VulkanRHI::CreateInstance()
{
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "miniengine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "miniengine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    const std::array<const char*, 2> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    CheckVk(vkCreateInstance(&createInfo, nullptr, &m_instance), "Vulkan instance creation failed");
}

void VulkanRHI::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    CheckVk(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr), "Vulkan physical device enumeration failed");
    MIGI_ASSERT(deviceCount > 0, "No Vulkan physical devices found");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    CheckVk(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()), "Vulkan physical device enumeration failed");

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(device, &properties);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

        std::optional<uint32_t> graphicsFamily;
        for (uint32_t i = 0; i < families.size(); ++i)
        {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                graphicsFamily = i;
                break;
            }
        }
        if (!graphicsFamily.has_value())
            continue;

        uint32_t extensionCount = 0;
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr), "Vulkan device extension enumeration failed");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        CheckVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data()), "Vulkan device extension enumeration failed");
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;

        VkPhysicalDeviceBufferDeviceAddressFeatures addressFeatures = {};
        addressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;

        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &addressFeatures;
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (!addressFeatures.bufferDeviceAddress)
            continue;

        if (properties.limits.maxPerStageDescriptorSampledImages < kMaxBindlessTextures ||
            properties.limits.maxDescriptorSetSampledImages < kMaxBindlessTextures)
        {
            continue;
        }

        m_physicalDevice = device;
        m_physicalDeviceProperties = properties;
        m_graphicsQueueFamily = *graphicsFamily;
        m_uniformBufferAlignment = std::max<VkDeviceSize>(properties.limits.minUniformBufferOffsetAlignment, 1);
        return;
    }

    MIGI_ABORT("No suitable Vulkan physical device found");
}

void VulkanRHI::CreateDevice()
{
    uint32_t extensionCount = 0;
    CheckVk(vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, nullptr), "Vulkan device extension enumeration failed");
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    CheckVk(vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extensionCount, availableExtensions.data()), "Vulkan device extension enumeration failed");

    std::vector<const char*> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    if (HasExtension(availableExtensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo = {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_graphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceBufferDeviceAddressFeatures addressFeatures = {};
    addressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    addressFeatures.bufferDeviceAddress = VK_TRUE;

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &addressFeatures;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    CheckVk(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device), "Vulkan device creation failed");
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_queue);
}

void VulkanRHI::CreateCommandPool()
{
    VkCommandPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = m_graphicsQueueFamily;
    CheckVk(vkCreateCommandPool(m_device, &createInfo, nullptr, &m_commandPool), "Vulkan command pool creation failed");
}

void VulkanRHI::CreateDescriptorLayouts()
{
    std::array<VkDescriptorSetLayoutBinding, 4> drawBindings = {};
    drawBindings[0].binding = 0;
    drawBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    drawBindings[0].descriptorCount = 1;
    drawBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    drawBindings[1].binding = 1;
    drawBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    drawBindings[1].descriptorCount = 1;
    drawBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    drawBindings[2].binding = 2;
    drawBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    drawBindings[2].descriptorCount = 1;
    drawBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    drawBindings[3].binding = 3;
    drawBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    drawBindings[3].descriptorCount = 1;
    drawBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo drawInfo = {};
    drawInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    drawInfo.bindingCount = static_cast<uint32_t>(drawBindings.size());
    drawInfo.pBindings = drawBindings.data();
    CheckVk(vkCreateDescriptorSetLayout(m_device, &drawInfo, nullptr, &m_drawSetLayout), "Vulkan draw descriptor layout creation failed");

    std::array<VkDescriptorSetLayoutBinding, 2> bindlessBindings = {};
    bindlessBindings[0].binding = 0;
    bindlessBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindlessBindings[0].descriptorCount = 1;
    bindlessBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindlessBindings[1].binding = 1;
    bindlessBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindlessBindings[1].descriptorCount = kMaxBindlessTextures;
    bindlessBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo bindlessInfo = {};
    bindlessInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    bindlessInfo.bindingCount = static_cast<uint32_t>(bindlessBindings.size());
    bindlessInfo.pBindings = bindlessBindings.data();
    CheckVk(vkCreateDescriptorSetLayout(m_device, &bindlessInfo, nullptr, &m_bindlessSetLayout), "Vulkan bindless descriptor layout creation failed");

    const std::array<VkDescriptorSetLayout, 2> setLayouts = {
        m_drawSetLayout,
        m_bindlessSetLayout,
    };

    VkPipelineLayoutCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineInfo.pSetLayouts = setLayouts.data();
    CheckVk(vkCreatePipelineLayout(m_device, &pipelineInfo, nullptr, &m_pipelineLayout), "Vulkan pipeline layout creation failed");
}

void VulkanRHI::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = kSwapChainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    CheckVk(vkCreateRenderPass(m_device, &createInfo, nullptr, &m_renderPass), "Vulkan render pass creation failed");
}

void VulkanRHI::CreateSampler()
{
    VkSamplerCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = VK_FILTER_LINEAR;
    createInfo.minFilter = VK_FILTER_LINEAR;
    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.maxLod = 1.0f;

    CheckVk(vkCreateSampler(m_device, &createInfo, nullptr, &m_sampler), "Vulkan sampler creation failed");
}

void VulkanRHI::CreateBindlessSet()
{
    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, kMaxBindlessTextures },
    }};

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    CheckVk(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_bindlessDescriptorPool), "Vulkan bindless descriptor pool creation failed");

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_bindlessDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_bindlessSetLayout;
    CheckVk(vkAllocateDescriptorSets(m_device, &allocInfo, &m_bindlessSet), "Vulkan bindless descriptor set allocation failed");

    VkDescriptorImageInfo samplerInfo = {};
    samplerInfo.sampler = m_sampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_bindlessSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &samplerInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanRHI::CreateFallbackTexture()
{
    const uint32_t white = 0xFFFFFFFFu;
    m_fallbackTexture = CreateTextureResource(1, 1, Format::R8G8B8A8_UNORM, &white);
    m_fallbackTexture.alive = true;
    FillBindlessTextures(m_fallbackTexture.view);
}

uint32_t VulkanRHI::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeMatches = (typeBits & (1u << i)) != 0;
        const bool propertiesMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeMatches && propertiesMatch)
            return i;
    }

    MIGI_ABORT("No suitable Vulkan memory type found");
}

VulkanRHI::BufferResource VulkanRHI::CreateBufferResource(
    VkDeviceSize byteSize,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties,
    bool mapped)
{
    MIGI_ASSERT(byteSize > 0, "Vulkan buffer byte size must be non-zero");

    BufferResource resource{};
    resource.byteSize = byteSize;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CheckVk(vkCreateBuffer(m_device, &bufferInfo, nullptr, &resource.buffer), "Vulkan buffer creation failed");

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(m_device, resource.buffer, &requirements);

    VkMemoryAllocateFlagsInfo flagsInfo = {};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0 ? &flagsInfo : nullptr;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, memoryProperties);
    CheckVk(vkAllocateMemory(m_device, &allocInfo, nullptr, &resource.memory), "Vulkan buffer memory allocation failed");
    CheckVk(vkBindBufferMemory(m_device, resource.buffer, resource.memory, 0), "Vulkan buffer memory binding failed");

    if (mapped)
        CheckVk(vkMapMemory(m_device, resource.memory, 0, byteSize, 0, &resource.mapped), "Vulkan buffer map failed");

    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0)
    {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = resource.buffer;
        resource.address = vkGetBufferDeviceAddress(m_device, &addressInfo);
        MIGI_ASSERT(resource.address != 0, "Vulkan buffer device address lookup failed");
    }

    return resource;
}

void VulkanRHI::DestroyBufferResource(BufferResource& resource)
{
    if (resource.mapped != nullptr)
    {
        vkUnmapMemory(m_device, resource.memory);
        resource.mapped = nullptr;
    }
    if (resource.buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(m_device, resource.buffer, nullptr);
        resource.buffer = VK_NULL_HANDLE;
    }
    if (resource.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, resource.memory, nullptr);
        resource.memory = VK_NULL_HANDLE;
    }
    resource.address = 0;
    resource.byteSize = 0;
}

VkImageView VulkanRHI::CreateImageView(VkImage image, VkFormat format)
{
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    CheckVk(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "Vulkan image view creation failed");
    return view;
}

VkFramebuffer VulkanRHI::CreateFramebuffer(VkImageView view, VkExtent2D extent)
{
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = m_renderPass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    CheckVk(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &framebuffer), "Vulkan framebuffer creation failed");
    return framebuffer;
}

void VulkanRHI::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        MIGI_ABORT("Unsupported Vulkan image layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanRHI::ExecuteImmediate(const std::function<void(VkCommandBuffer)>& record)
{
    // Hold m_vulkanMutex for the whole call: m_commandPool is shared between
    // any concurrent callers (e.g. CreateTexture) and its allocate / record /
    // end / submit / free must be externally synchronized per Vulkan spec.
    // vkQueueWaitIdle at the end already serializes the queue globally, so
    // extending the lock beyond the submit has no additional cost.
    std::scoped_lock lock(m_vulkanMutex);

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    CheckVk(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer), "Vulkan immediate command buffer allocation failed");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Vulkan immediate command buffer begin failed");

    record(commandBuffer);

    CheckVk(vkEndCommandBuffer(commandBuffer), "Vulkan immediate command buffer end failed");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    CheckVk(vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE), "Vulkan immediate command buffer submit failed");
    CheckVk(vkQueueWaitIdle(m_queue), "Vulkan immediate queue wait failed");

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

VulkanRHI::TextureRecord VulkanRHI::CreateTextureResource(uint32_t width, uint32_t height, Format format, const void* initialData)
{
    MIGI_ASSERT(width > 0 && height > 0, "RHI texture dimensions must be non-zero");
    MIGI_ASSERT(initialData != nullptr, "RHI texture initial data is required");
    MIGI_ASSERT(format == Format::R8G8B8A8_UNORM, "Only RGBA8 textures are currently implemented");

    TextureRecord record{};
    record.width = width;
    record.height = height;
    record.format = ToVkFormat(format);

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = record.format;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    CheckVk(vkCreateImage(m_device, &imageInfo, nullptr, &record.image), "Vulkan texture creation failed");

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(m_device, record.image, &requirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CheckVk(vkAllocateMemory(m_device, &allocInfo, nullptr, &record.memory), "Vulkan texture memory allocation failed");
    CheckVk(vkBindImageMemory(m_device, record.image, record.memory, 0), "Vulkan texture memory binding failed");

    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(width) * height * FormatByteSize(format);
    BufferResource staging = CreateBufferResource(
        dataSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
    std::memcpy(staging.mapped, initialData, static_cast<size_t>(dataSize));

    ExecuteImmediate([&](VkCommandBuffer cmd) {
        TransitionImageLayout(cmd, record.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        TransitionImageLayout(cmd, record.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    DestroyBufferResource(staging);
    record.view = CreateImageView(record.image, record.format);
    return record;
}

void VulkanRHI::DestroyTextureRecord(TextureRecord& record)
{
    if (record.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(m_device, record.view, nullptr);
        record.view = VK_NULL_HANDLE;
    }
    if (record.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(m_device, record.image, nullptr);
        record.image = VK_NULL_HANDLE;
    }
    if (record.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(m_device, record.memory, nullptr);
        record.memory = VK_NULL_HANDLE;
    }
    record.width = 0;
    record.height = 0;
    record.format = VK_FORMAT_UNDEFINED;
    record.alive = false;
}

void VulkanRHI::WriteBindlessTexture(TextureIndex index, VkImageView view)
{
    MIGI_ASSERT(index < kMaxBindlessTextures, "Invalid Vulkan bindless texture index");

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_bindlessSet;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VulkanRHI::FillBindlessTextures(VkImageView view)
{
    std::vector<VkDescriptorImageInfo> imageInfos(kMaxBindlessTextures);
    for (VkDescriptorImageInfo& imageInfo : imageInfos)
    {
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_bindlessSet;
    write.dstBinding = 1;
    write.descriptorCount = kMaxBindlessTextures;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = imageInfos.data();
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

VkShaderModule VulkanRHI::CreateShaderModule(std::span<const std::byte> bytes)
{
    MIGI_ASSERT(!bytes.empty(), "Vulkan shader module needs shader bytecode");
    MIGI_ASSERT((bytes.size_bytes() % sizeof(uint32_t)) == 0, "Vulkan shader bytecode must be SPIR-V");

    std::vector<uint32_t> words(bytes.size_bytes() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size_bytes());

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = bytes.size_bytes();
    createInfo.pCode = words.data();

    VkShaderModule module = VK_NULL_HANDLE;
    CheckVk(vkCreateShaderModule(m_device, &createInfo, nullptr, &module), "Vulkan shader module creation failed");
    return module;
}

VkPipeline VulkanRHI::CreatePipeline(const ShaderPipelineDesc& desc, BlendMode blendMode)
{
    MIGI_ASSERT(!desc.vertexShader.empty(), "Shader pipeline needs a vertex shader blob");
    MIGI_ASSERT(!desc.pixelShader.empty(), "Shader pipeline needs a pixel shader blob");

    VkShaderModule vertexModule = CreateShaderModule(desc.vertexShader);
    VkShaderModule pixelModule = CreateShaderModule(desc.pixelShader);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = pixelModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    if (blendMode == BlendMode::Alpha)
    {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    CheckVk(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "Vulkan graphics pipeline creation failed");

    vkDestroyShaderModule(m_device, pixelModule, nullptr);
    vkDestroyShaderModule(m_device, vertexModule, nullptr);
    return pipeline;
}

BufferHandle VulkanRHI::CreateBuffer(const BufferDesc& desc)
{
    MIGI_ASSERT(desc.byteSize > 0, "RHI buffer byte size must be non-zero");

    BufferHandle handle = AllocateHandle<BufferRecord, BufferHandle>(m_buffers);
    BufferRecord& record = m_buffers[handle.id - 1];
    record.resource = CreateBufferResource(
        desc.byteSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);

    if (desc.initialData != nullptr)
        std::memcpy(record.resource.mapped, desc.initialData, desc.byteSize);

    return handle;
}

void VulkanRHI::DestroyBuffer(BufferHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    BufferRecord& record = GetBuffer(handle);
    DestroyBufferResource(record.resource);
    record.alive = false;
}

GpuAddress VulkanRHI::GetBufferGpuAddress(BufferHandle handle) const
{
    const BufferRecord& record = GetBuffer(handle);
    return record.resource.address;
}

void VulkanRHI::UpdateBuffer(BufferHandle handle, const void* data, uint32_t byteSize, uint32_t byteOffset)
{
    MIGI_ASSERT(data != nullptr || byteSize == 0, "RHI buffer update data is null");
    BufferRecord& record = GetBuffer(handle);
    MIGI_ASSERT(byteOffset <= record.resource.byteSize && byteSize <= record.resource.byteSize - byteOffset, "RHI buffer update is out of bounds");

    if (byteSize == 0)
        return;

    std::memcpy(static_cast<std::byte*>(record.resource.mapped) + byteOffset, data, byteSize);
}

TextureIndex VulkanRHI::CreateTexture(const TextureDesc& desc)
{
    uint32_t index = 0;
    if (!m_freeTextureIndices.empty())
    {
        index = m_freeTextureIndices.back();
        m_freeTextureIndices.pop_back();
    }
    else
    {
        MIGI_ASSERT(m_textures.size() < kMaxBindlessTextures, "RHI bindless texture heap is full");
        index = static_cast<uint32_t>(m_textures.size());
        m_textures.push_back(TextureRecord{});
    }

    TextureRecord& record = m_textures[index];
    record = CreateTextureResource(desc.width, desc.height, desc.format, desc.initialData);
    record.alive = true;
    WriteBindlessTexture(index, record.view);
    return index;
}

void VulkanRHI::DestroyTexture(TextureIndex index)
{
    if (index == kInvalidTexture)
        return;

    MIGI_ASSERT(index < m_textures.size(), "Invalid RHI texture index");
    TextureRecord& record = m_textures[index];
    MIGI_ASSERT(record.alive, "RHI texture index was destroyed");

    WaitIdle();
    WriteBindlessTexture(index, m_fallbackTexture.view);
    DestroyTextureRecord(record);
    m_freeTextureIndices.push_back(index);
}

ShaderPipelineHandle VulkanRHI::CreateShaderPipeline(const ShaderPipelineDesc& desc)
{
    ShaderPipelineHandle handle = AllocateHandle<ShaderPipelineRecord, ShaderPipelineHandle>(m_pipelines);
    ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    record.opaquePipeline = CreatePipeline(desc, BlendMode::Opaque);
    record.alphaPipeline = CreatePipeline(desc, BlendMode::Alpha);
    return handle;
}

void VulkanRHI::DestroyShaderPipeline(ShaderPipelineHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    ShaderPipelineRecord& record = GetPipeline(handle);
    vkDestroyPipeline(m_device, record.opaquePipeline, nullptr);
    vkDestroyPipeline(m_device, record.alphaPipeline, nullptr);
    record.opaquePipeline = VK_NULL_HANDLE;
    record.alphaPipeline = VK_NULL_HANDLE;
    record.alive = false;
}

VkSurfaceFormatKHR VulkanRHI::ChooseSurfaceFormat(VkSurfaceKHR surface) const
{
    uint32_t formatCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, nullptr), "Vulkan surface format enumeration failed");
    MIGI_ASSERT(formatCount > 0, "Vulkan surface has no formats");

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    CheckVk(vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, formats.data()), "Vulkan surface format enumeration failed");

    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == kSwapChainFormat && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }

    MIGI_ABORT("Vulkan surface does not support the RHI swapchain format");
}

VkPresentModeKHR VulkanRHI::ChoosePresentMode(VkSurfaceKHR surface) const
{
    uint32_t presentModeCount = 0;
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &presentModeCount, nullptr), "Vulkan present mode enumeration failed");
    std::vector<VkPresentModeKHR> modes(presentModeCount);
    CheckVk(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &presentModeCount, modes.data()), "Vulkan present mode enumeration failed");

    for (VkPresentModeKHR mode : modes)
    {
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
            return mode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRHI::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t width, uint32_t height) const
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    VkExtent2D extent = {};
    extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return extent;
}

void VulkanRHI::CreateSwapChainObjects(SwapChainRecord& sc, uint32_t width, uint32_t height)
{
    VkBool32 presentSupported = VK_FALSE;
    CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_graphicsQueueFamily, sc.surface, &presentSupported), "Vulkan surface support query failed");
    MIGI_ASSERT(presentSupported == VK_TRUE, "Vulkan graphics queue cannot present to this surface");

    VkSurfaceCapabilitiesKHR caps = {};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, sc.surface, &caps), "Vulkan surface capabilities query failed");

    const VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(sc.surface);
    const VkPresentModeKHR presentMode = ChoosePresentMode(sc.surface);
    const VkExtent2D extent = ChooseSwapExtent(caps, std::max(width, 1u), std::max(height, 1u));

    uint32_t imageCount = sc.bufferCount;
    imageCount = std::max(imageCount, caps.minImageCount);
    if (caps.maxImageCount != 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = sc.surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    CheckVk(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &sc.swapChain), "Vulkan swapchain creation failed");

    uint32_t actualImageCount = 0;
    CheckVk(vkGetSwapchainImagesKHR(m_device, sc.swapChain, &actualImageCount, nullptr), "Vulkan swapchain image lookup failed");
    sc.images.resize(actualImageCount);
    CheckVk(vkGetSwapchainImagesKHR(m_device, sc.swapChain, &actualImageCount, sc.images.data()), "Vulkan swapchain image lookup failed");

    sc.imageViews.resize(actualImageCount);
    sc.framebuffers.resize(actualImageCount);
    for (uint32_t i = 0; i < actualImageCount; ++i)
    {
        sc.imageViews[i] = CreateImageView(sc.images[i], surfaceFormat.format);
        sc.framebuffers[i] = CreateFramebuffer(sc.imageViews[i], extent);
    }

    sc.extent = extent;
    sc.colorFormat = surfaceFormat.format;
    sc.width = extent.width;
    sc.height = extent.height;
    sc.bufferCount = actualImageCount;
    sc.nextSync = 0;
    sc.pendingPresentSync = std::numeric_limits<uint32_t>::max();
    sc.pendingPresentImageIndex = std::numeric_limits<uint32_t>::max();
    CreateFrameSyncs(sc);
}

void VulkanRHI::ReleaseSwapChainObjects(SwapChainRecord& sc)
{
    for (VkFramebuffer framebuffer : sc.framebuffers)
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    sc.framebuffers.clear();

    for (VkImageView view : sc.imageViews)
        vkDestroyImageView(m_device, view, nullptr);
    sc.imageViews.clear();
    sc.images.clear();

    if (sc.swapChain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, sc.swapChain, nullptr);
        sc.swapChain = VK_NULL_HANDLE;
    }
}

void VulkanRHI::RecreateSwapChainObjects(SwapChainRecord& sc, uint32_t width, uint32_t height)
{
    WaitIdle();
    ReleaseSwapChainObjects(sc);
    CreateSwapChainObjects(sc, std::max(width, 1u), std::max(height, 1u));
}

void VulkanRHI::CreateFrameSyncs(SwapChainRecord& sc)
{
    DestroyFrameSyncs(sc);
    sc.sync.resize(sc.bufferCount);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (FrameSync& sync : sc.sync)
    {
        CheckVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &sync.imageAvailable), "Vulkan image-available semaphore creation failed");
        CheckVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &sync.renderFinished), "Vulkan render-finished semaphore creation failed");
    }
}

void VulkanRHI::DestroyFrameSyncs(SwapChainRecord& sc)
{
    for (FrameSync& sync : sc.sync)
    {
        if (sync.imageAvailable != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, sync.imageAvailable, nullptr);
        if (sync.renderFinished != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, sync.renderFinished, nullptr);
    }
    sc.sync.clear();
}

void VulkanRHI::ClearCompletedFenceReferences(VkFence fence)
{
    for (SwapChainRecord& sc : m_swapChains)
    {
        if (!sc.alive)
            continue;

        for (FrameSync& sync : sc.sync)
        {
            if (sync.lastFence == fence)
                sync.lastFence = VK_NULL_HANDLE;
        }
    }
}

SwapChainHandle VulkanRHI::CreateSwapChain(const SwapChainDesc& desc)
{
    MIGI_ASSERT(desc.windowHandle != nullptr, "RHI swapchain needs a native window handle");
    MIGI_ASSERT(desc.colorFormat == Format::R8G8B8A8_UNORM, "Only RGBA8 Vulkan swapchains are currently implemented");

    SwapChainHandle handle = AllocateHandle<SwapChainRecord, SwapChainHandle>(m_swapChains);
    SwapChainRecord& record = m_swapChains[handle.id - 1];
    record.bufferCount = desc.bufferCount != 0 ? desc.bufferCount : kDefaultBackBufferCount;
    record.fullscreen = desc.fullscreen;

    VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = GetModuleHandleA(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(desc.windowHandle);
    CheckVk(vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &record.surface), "Vulkan Win32 surface creation failed");

    CreateSwapChainObjects(record, std::max(desc.width, 1u), std::max(desc.height, 1u));
    return handle;
}

void VulkanRHI::ResizeSwapChain(SwapChainHandle handle, uint32_t width, uint32_t height, bool fullscreen)
{
    SwapChainRecord& record = GetSwapChain(handle);
    width = std::max(width, 1u);
    height = std::max(height, 1u);

    if (record.width == width && record.height == height && record.fullscreen == fullscreen)
        return;

    WaitIdle();
    ReleaseSwapChainObjects(record);
    record.fullscreen = fullscreen;
    CreateSwapChainObjects(record, width, height);
}

void VulkanRHI::DestroySwapChain(SwapChainHandle handle)
{
    if (!handle.IsValid())
        return;

    WaitIdle();
    SwapChainRecord& record = GetSwapChain(handle);
    ReleaseSwapChainObjects(record);
    DestroyFrameSyncs(record);
    if (record.surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, record.surface, nullptr);
        record.surface = VK_NULL_HANDLE;
    }
    record.alive = false;
}

CommandList* VulkanRHI::BeginCommandList()
{
    for (auto& commandList : m_commandLists)
    {
        if (commandList->CanReuse())
        {
            {
                // Clearing stale fence references touches sync.lastFence in
                // every swapchain; Submit on another thread may write the same
                // field, so serialize through m_vulkanMutex.
                std::scoped_lock lock(m_vulkanMutex);
                ClearCompletedFenceReferences(commandList->GetFence());
            }
            commandList->ResetForRecording();
            return commandList.get();
        }
    }

    auto commandList = std::make_unique<VulkanCommandList>(*this);
    VulkanCommandList* result = commandList.get();
    m_commandLists.push_back(std::move(commandList));
    result->ResetForRecording();
    return result;
}

void VulkanRHI::Submit(CommandList* cmd)
{
    auto* vkCmd = static_cast<VulkanCommandList*>(cmd);
    MIGI_ASSERT(vkCmd != nullptr && vkCmd->recording, "RHI submit needs a recording command list");

    CheckVk(vkEndCommandBuffer(vkCmd->Get()), "Vulkan command buffer end failed");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkCommandBuffer commandBuffer = vkCmd->Get();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;

    if (vkCmd->GetActiveSwapChain().IsValid())
    {
        SwapChainRecord& sc = GetSwapChain(vkCmd->GetActiveSwapChain());
        FrameSync& sync = sc.sync[vkCmd->GetActiveSyncIndex()];
        waitSemaphore = sync.imageAvailable;
        signalSemaphore = sync.renderFinished;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &signalSemaphore;
    }

    {
        // Hold m_vulkanMutex across the submit AND the per-swapchain
        // bookkeeping update so that sync.lastFence can't race with
        // BeginRenderPass / ClearCompletedFenceReferences on the Render thread.
        std::scoped_lock lock(m_vulkanMutex);
        CheckVk(vkQueueSubmit(m_queue, 1, &submitInfo, vkCmd->GetFence()), "Vulkan command buffer submit failed");

        if (vkCmd->GetActiveSwapChain().IsValid())
        {
            SwapChainRecord& sc = GetSwapChain(vkCmd->GetActiveSwapChain());
            sc.sync[vkCmd->GetActiveSyncIndex()].lastFence = vkCmd->GetFence();
            sc.pendingPresentSync = vkCmd->GetActiveSyncIndex();
            sc.pendingPresentImageIndex = vkCmd->GetActiveImageIndex();
        }
    }

    vkCmd->MarkSubmitted();
}

void VulkanRHI::Present(SwapChainHandle handle, uint32_t)
{
    SwapChainRecord& record = GetSwapChain(handle);
    MIGI_ASSERT(record.pendingPresentSync < record.sync.size(), "Vulkan present has no submitted swapchain image");

    FrameSync& sync = record.sync[record.pendingPresentSync];
    VkSemaphore waitSemaphore = sync.renderFinished;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &record.swapChain;
    presentInfo.pImageIndices = &record.pendingPresentImageIndex;

    VkResult result = VK_SUCCESS;
    {
        std::scoped_lock lock(m_vulkanMutex);
        result = vkQueuePresentKHR(m_queue, &presentInfo);
    }
    record.pendingPresentSync = std::numeric_limits<uint32_t>::max();
    record.pendingPresentImageIndex = std::numeric_limits<uint32_t>::max();

    if (IsOutOfDateResult(result))
    {
        RecreateSwapChainObjects(record, record.width, record.height);
        return;
    }

    MIGI_ASSERT(IsPresentResult(result), "Vulkan swapchain present failed");
}

void VulkanRHI::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        std::scoped_lock lock(m_vulkanMutex);
        CheckVk(vkDeviceWaitIdle(m_device), "Vulkan device wait idle failed");
    }
}

VulkanRHI::BufferRecord& VulkanRHI::GetBuffer(BufferHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_buffers.size(), "Invalid RHI buffer handle");
    BufferRecord& record = m_buffers[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI buffer handle was destroyed");
    return record;
}

const VulkanRHI::BufferRecord& VulkanRHI::GetBuffer(BufferHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_buffers.size(), "Invalid RHI buffer handle");
    const BufferRecord& record = m_buffers[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI buffer handle was destroyed");
    return record;
}

VulkanRHI::ShaderPipelineRecord& VulkanRHI::GetPipeline(ShaderPipelineHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_pipelines.size(), "Invalid RHI pipeline handle");
    ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI pipeline handle was destroyed");
    return record;
}

const VulkanRHI::ShaderPipelineRecord& VulkanRHI::GetPipeline(ShaderPipelineHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_pipelines.size(), "Invalid RHI pipeline handle");
    const ShaderPipelineRecord& record = m_pipelines[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI pipeline handle was destroyed");
    return record;
}

VulkanRHI::SwapChainRecord& VulkanRHI::GetSwapChain(SwapChainHandle handle)
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_swapChains.size(), "Invalid RHI swapchain handle");
    SwapChainRecord& record = m_swapChains[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI swapchain handle was destroyed");
    return record;
}

const VulkanRHI::SwapChainRecord& VulkanRHI::GetSwapChain(SwapChainHandle handle) const
{
    MIGI_ASSERT(handle.IsValid() && handle.id <= m_swapChains.size(), "Invalid RHI swapchain handle");
    const SwapChainRecord& record = m_swapChains[handle.id - 1];
    MIGI_ASSERT(record.alive, "RHI swapchain handle was destroyed");
    return record;
}

VulkanRHI::BufferSlice VulkanRHI::FindBufferSlice(GpuAddress address) const
{
    for (const BufferRecord& record : m_buffers)
    {
        if (!record.alive)
            continue;

        const VkDeviceAddress begin = record.resource.address;
        const VkDeviceAddress end = begin + record.resource.byteSize;
        if (address >= begin && address < end)
            return BufferSlice{ &record, static_cast<VkDeviceSize>(address - begin) };
    }

    MIGI_ABORT("Vulkan buffer address does not belong to an RHI buffer");
}

VulkanRHI::VulkanCommandList::VulkanCommandList(VulkanRHI& owner)
    : owner(owner)
{
    // Each command list owns its own pool so recording a new list on the
    // Render thread cannot race with vkQueueSubmit of a different list on the
    // Kick thread: VkCommandPool requires external synchronization across all
    // of its command buffers, including vkQueueSubmit.
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = owner.m_graphicsQueueFamily;
    CheckVk(vkCreateCommandPool(owner.m_device, &commandPoolInfo, nullptr, &commandPool), "Vulkan command list pool creation failed");

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    CheckVk(vkAllocateCommandBuffers(owner.m_device, &allocInfo, &commandBuffer), "Vulkan command buffer allocation failed");

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    CheckVk(vkCreateFence(owner.m_device, &fenceInfo, nullptr, &fence), "Vulkan command fence creation failed");

    const std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, kMaxDrawsPerCommandList * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxDrawsPerCommandList * 2 },
    }};

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxDrawsPerCommandList;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    CheckVk(vkCreateDescriptorPool(owner.m_device, &poolInfo, nullptr, &descriptorPool), "Vulkan command descriptor pool creation failed");

    const VkDeviceSize constantsSize = AlignUp(kDrawConstantsSize, owner.m_uniformBufferAlignment) * kMaxDrawsPerCommandList;
    drawConstantsBuffer = owner.CreateBufferResource(
        constantsSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);

    const VkDeviceSize paramsSize = AlignUp(sizeof(DrawParams), owner.m_uniformBufferAlignment) * kMaxDrawsPerCommandList;
    drawParamsBuffer = owner.CreateBufferResource(
        paramsSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
}

VulkanRHI::VulkanCommandList::~VulkanCommandList()
{
    owner.DestroyBufferResource(drawParamsBuffer);
    owner.DestroyBufferResource(drawConstantsBuffer);
    if (descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(owner.m_device, descriptorPool, nullptr);
    if (fence != VK_NULL_HANDLE)
        vkDestroyFence(owner.m_device, fence, nullptr);
    if (commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(owner.m_device, commandPool, nullptr);
}

bool VulkanRHI::VulkanCommandList::CanReuse() const
{
    if (recording)
        return false;
    return vkGetFenceStatus(owner.m_device, fence) == VK_SUCCESS;
}

void VulkanRHI::VulkanCommandList::ResetForRecording()
{
    CheckVk(vkResetDescriptorPool(owner.m_device, descriptorPool, 0), "Vulkan command descriptor pool reset failed");
    CheckVk(vkResetCommandBuffer(commandBuffer, 0), "Vulkan command buffer reset failed");
    CheckVk(vkResetFences(owner.m_device, 1, &fence), "Vulkan command fence reset failed");

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "Vulkan command buffer begin failed");

    drawCount = 0;
    activeSwapChain = {};
    activeImageIndex = 0;
    activeSyncIndex = std::numeric_limits<uint32_t>::max();
    activePipeline = {};
    activeBlendMode = BlendMode::Opaque;
    renderPassOpen = false;
    recording = true;
}

void VulkanRHI::VulkanCommandList::MarkSubmitted()
{
    recording = false;
}

void VulkanRHI::VulkanCommandList::BeginRenderPass(SwapChainHandle target, const ClearColor& clear)
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(!renderPassOpen, "RHI render pass is already open");

    SwapChainRecord& sc = owner.GetSwapChain(target);
    activeSwapChain = target;
    activeSyncIndex = sc.nextSync++ % static_cast<uint32_t>(sc.sync.size());

    // Snapshot and clear sync.lastFence under the mutex so we don't race with
    // Submit writing the same field from the Kick thread. vkWaitForFences
    // itself is thread-safe, so we can release the lock before waiting.
    VkFence pendingFence = VK_NULL_HANDLE;
    {
        std::scoped_lock lock(owner.m_vulkanMutex);
        FrameSync& sync = sc.sync[activeSyncIndex];
        pendingFence = sync.lastFence;
        sync.lastFence = VK_NULL_HANDLE;
    }
    if (pendingFence != VK_NULL_HANDLE)
        CheckVk(vkWaitForFences(owner.m_device, 1, &pendingFence, VK_TRUE, UINT64_MAX), "Vulkan swapchain sync wait failed");

    auto acquireImage = [&]() {
        VkResult result = VK_SUCCESS;
        do
        {
            {
                std::scoped_lock lock(owner.m_vulkanMutex);
                result = vkAcquireNextImageKHR(
                    owner.m_device,
                    sc.swapChain,
                    0,
                    sc.sync[activeSyncIndex].imageAvailable,
                    VK_NULL_HANDLE,
                    &activeImageIndex);
            }
            if (IsAcquireWaitResult(result))
                ::Sleep(kAcquireRetrySleepMs);
        } while (IsAcquireWaitResult(result));

        return result;
    };

    VkResult acquireResult = acquireImage();

    if (IsOutOfDateResult(acquireResult))
    {
        owner.RecreateSwapChainObjects(sc, sc.width, sc.height);
        activeSyncIndex = sc.nextSync++ % static_cast<uint32_t>(sc.sync.size());
        acquireResult = acquireImage();
    }

    MIGI_ASSERT(IsPresentResult(acquireResult), "Vulkan swapchain image acquire failed");

    VkClearValue clearValue = {};
    clearValue.color.float32[0] = clear.r;
    clearValue.color.float32[1] = clear.g;
    clearValue.color.float32[2] = clear.b;
    clearValue.color.float32[3] = clear.a;

    VkRenderPassBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = owner.m_renderPass;
    beginInfo.framebuffer = sc.framebuffers[activeImageIndex];
    beginInfo.renderArea.extent = sc.extent;
    beginInfo.clearValueCount = 1;
    beginInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    renderPassOpen = true;
}

void VulkanRHI::VulkanCommandList::EndRenderPass()
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(renderPassOpen, "RHI render pass is not open");

    vkCmdEndRenderPass(commandBuffer);
    renderPassOpen = false;
}

void VulkanRHI::VulkanCommandList::SetViewport(const Viewport& vp)
{
    VkViewport viewport = {};
    viewport.x = vp.x;
    viewport.y = vp.y + vp.height;
    viewport.width = vp.width;
    viewport.height = -vp.height;
    viewport.minDepth = vp.minDepth;
    viewport.maxDepth = vp.maxDepth;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}

void VulkanRHI::VulkanCommandList::SetScissor(const Scissor& sc)
{
    VkRect2D rect = {};
    rect.offset.x = sc.x;
    rect.offset.y = sc.y;
    rect.extent.width = sc.width;
    rect.extent.height = sc.height;
    vkCmdSetScissor(commandBuffer, 0, 1, &rect);
}

void VulkanRHI::VulkanCommandList::SetBlendMode(BlendMode mode)
{
    activeBlendMode = mode;
    ApplyPipelineState();
}

void VulkanRHI::VulkanCommandList::BindShaderPipeline(ShaderPipelineHandle pipeline)
{
    activePipeline = pipeline;
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        owner.m_pipelineLayout,
        1,
        1,
        &owner.m_bindlessSet,
        0,
        nullptr);
    ApplyPipelineState();
}

void VulkanRHI::VulkanCommandList::ApplyPipelineState()
{
    if (!activePipeline.IsValid())
        return;

    const ShaderPipelineRecord& pipeline = owner.GetPipeline(activePipeline);
    VkPipeline state = activeBlendMode == BlendMode::Alpha
        ? pipeline.alphaPipeline
        : pipeline.opaquePipeline;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state);
}

VkDescriptorSet VulkanRHI::VulkanCommandList::AllocateDrawDescriptorSet()
{
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &owner.m_drawSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    CheckVk(vkAllocateDescriptorSets(owner.m_device, &allocInfo, &set), "Vulkan draw descriptor set allocation failed");
    return set;
}

void VulkanRHI::VulkanCommandList::DrawIndexed(const DrawIndexedDesc& desc)
{
    MIGI_ASSERT(recording, "RHI command list is not recording");
    MIGI_ASSERT(activePipeline.IsValid(), "RHI draw needs a shader pipeline");
    MIGI_ASSERT(desc.vertexBuffer != kNullGpuAddress, "RHI draw needs a vertex buffer");
    MIGI_ASSERT(desc.indexBuffer != kNullGpuAddress, "RHI draw needs an index buffer");
    MIGI_ASSERT(desc.userData != kNullGpuAddress, "Vulkan RHI draw needs user data");
    MIGI_ASSERT(drawCount < kMaxDrawsPerCommandList, "Vulkan command list draw capacity exceeded");
    MIGI_ASSERT(desc.indexFormat == Format::R16_UINT || desc.indexFormat == Format::R32_UINT, "RHI draw has unsupported index format");

    const VkDeviceSize drawParamStride = AlignUp(sizeof(DrawParams), owner.m_uniformBufferAlignment);
    const VkDeviceSize drawParamOffset = static_cast<VkDeviceSize>(drawCount) * drawParamStride;
    const VkDeviceSize drawConstantsStride = AlignUp(kDrawConstantsSize, owner.m_uniformBufferAlignment);
    const VkDeviceSize drawConstantsOffset = static_cast<VkDeviceSize>(drawCount) * drawConstantsStride;

    DrawParams params{};
    params.firstIndex = desc.firstIndex;
    params.vertexOffset = desc.vertexOffset;
    params.indexFormat = desc.indexFormat == Format::R32_UINT ? 1u : 0u;
    std::memcpy(static_cast<std::byte*>(drawParamsBuffer.mapped) + drawParamOffset, &params, sizeof(params));

    const BufferSlice vertex = owner.FindBufferSlice(desc.vertexBuffer);
    const BufferSlice index = owner.FindBufferSlice(desc.indexBuffer);
    const BufferSlice user = owner.FindBufferSlice(desc.userData);
    MIGI_ASSERT(user.record->resource.mapped != nullptr, "Vulkan user data buffer must be CPU visible");
    MIGI_ASSERT(user.offset <= user.record->resource.byteSize && kDrawConstantsSize <= user.record->resource.byteSize - user.offset, "Vulkan user data range is out of bounds");
    std::memcpy(
        static_cast<std::byte*>(drawConstantsBuffer.mapped) + drawConstantsOffset,
        static_cast<const std::byte*>(user.record->resource.mapped) + user.offset,
        kDrawConstantsSize);

    VkDescriptorSet set = AllocateDrawDescriptorSet();

    const std::array<VkDescriptorBufferInfo, 4> bufferInfos = {{
        { drawConstantsBuffer.buffer, 0, kDrawConstantsSize },
        { drawParamsBuffer.buffer, 0, sizeof(DrawParams) },
        { vertex.record->resource.buffer, 0, vertex.record->resource.byteSize },
        { index.record->resource.buffer, 0, index.record->resource.byteSize },
    }};

    std::array<VkWriteDescriptorSet, 4> writes = {};
    for (uint32_t i = 0; i < writes.size(); ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    vkUpdateDescriptorSets(owner.m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    const std::array<uint32_t, 2> dynamicOffsets = {{
        static_cast<uint32_t>(drawConstantsOffset),
        static_cast<uint32_t>(drawParamOffset),
    }};

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        owner.m_pipelineLayout,
        0,
        1,
        &set,
        static_cast<uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
    vkCmdDraw(commandBuffer, desc.indexCount, 1, 0, 0);
    ++drawCount;
}

std::unique_ptr<RHI> RHI::Create()
{
    return std::make_unique<VulkanRHI>();
}

} // namespace drgn
