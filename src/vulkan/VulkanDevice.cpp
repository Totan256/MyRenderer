#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"

#include "rhi/Resource.hpp"
#include "RenderGraph.hpp"
#include "VulkanRenderGraph.hpp"
#include "VulkanConstantBufferManager.hpp"
#include <iostream>


#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
namespace rhi::vk{
    void VulkanDevice::beginFrame() {
        std::lock_guard<std::mutex> lock(m_deletionMutex);
        while (!m_deletionQueue.empty() && m_deletionQueue.front().targetFrame <= m_frameCounter) {
            m_deletionQueue.front().func();
            m_deletionQueue.pop_front();
        }
    }

    VulkanDevice::VulkanDevice() = default;

    void VulkanDevice::endFrame() {
        m_frameCounter++;
    }

    void VulkanDevice::enqueueDeletion(std::function<void()>&& deletionFunc) {
        std::lock_guard<std::mutex> lock(m_deletionMutex);
        m_deletionQueue.push_back({ m_frameCounter + MAX_FRAMES_IN_FLIGHT, std::move(deletionFunc) });
    }
    
    std::unique_ptr<rhi::Buffer> VulkanDevice::createBuffer(const rhi::BufferDesc& desc) {
        VkBufferUsageFlags vkUsage = mapBufferUsage(desc.usageFlags);
        VmaMemoryUsage memUsage = desc.isCpuVisible ? 
            VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        return std::make_unique<VulkanBuffer>(*this, m_allocator, desc.size, vkUsage, memUsage);
    }

    std::unique_ptr<rhi::Image> VulkanDevice::createImage(const rhi::ImageDesc& desc) {
        VkImageUsageFlags vkUsage = mapImageUsage(desc.usageFlags);
        return std::make_unique<VulkanImage>(*this, desc, vkUsage);
    }

    std::unique_ptr<RenderGraph> VulkanDevice::createRenderGraph(){
        return std::make_unique<VulkanRenderGraph>(*this);
    }
    std::unique_ptr<rhi::CommandList> VulkanDevice::createCommandList() {
        return std::make_unique<VulkanCommandList>(*this);
    }

    void VulkanDevice::initialize(){
        std::cout << "--- Initializing VulkanDevice ---" << std::endl;
        // ヘルパー関数の呼び出し。VK_CHECKが失敗時に例外を投げるため、安全に処理できます。
        createInstance();
        pickPhysicalDevice();

        // 制限の取得
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        m_minUniformBufferOffsetAlignment = static_cast<uint32_t>(properties.limits.minUniformBufferOffsetAlignment);

        createLogicalDevice();
        createAllocator();
        createBindlessResources();

        // ConstantBufferManagerの初期化 (Ring size 16MB, 2 frames)
        // Todo　リングバッファの実装をあとでする，とりあえず3060で動く分のサイズを用意
        m_constantBufferManager = std::make_unique<ConstantBufferManager>(*this, 65536, 2);

        std::cout << "--- VulkanDevice Initialized Successfully ---" << std::endl;
    }

    void VulkanDevice::createInstance(){
        // 1. アプリケーション情報の定義
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "MyOfflineRenderer";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3; // Vulkan 1.3を使用

        // 2. 検証レイヤー（デバッグ用）の有効化設定
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        // 3. インスタンス作成情報の定義
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        // 検証レイヤーを有効化
        // 【重要】本番コードでは有効な検証レイヤーがあるかチェックすべきですが、今回は省略します
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        
        // デバッグメッセンジャー拡張機能も有効化
        const std::vector<const char*> extensions = {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        };
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // 4. Vulkanインスタンスの作成
        std::cout << "Creating Vulkan Instance..." << std::endl;
        VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_instance));
        std::cout << "Instance created successfully!" << std::endl;
        
        // todo : デバッグメッセンジャーの作成処理
    }

    std::optional<uint32_t> VulkanDevice::findComputeQueueFamilyIndex(VkPhysicalDevice device){
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            // VK_QUEUE_COMPUTE_BITが含まれ、VK_QUEUE_GRAPHICS_BITが含まれないもの（または両方含むもの）を探す
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                // 今回はComputeビットが立っているものを見つけたら採用
                // Todo 後で見直し
                return i;
            }
        }
        return std::nullopt;
    }

    void VulkanDevice::pickPhysicalDevice(){
        // 5. 物理デバイス（GPU）の列挙
        uint32_t deviceCount = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr));

        if (deviceCount == 0) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data()));

        std::cout << "Found " << deviceCount << " device(s)." << std::endl;
        
        // 【ToDo対応】Compute Queue Familyを持つ最初のデバイスを選択する
        for (const auto& device : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            std::cout << "  - Device: " << properties.deviceName << std::endl;

            std::optional<uint32_t> computeIndex = findComputeQueueFamilyIndex(device);
            
            if (computeIndex.has_value()) {
                m_physicalDevice = device;
                m_computeQueueFamilyIndex = computeIndex.value();
                std::cout << "  -> Selected as Physical Device. Compute Queue Family Index: " 
                        << m_computeQueueFamilyIndex << std::endl;
                return;
            }
        }
        
        throw std::runtime_error("failed to find a GPU with a Compute Queue Family!");
    }

    void VulkanDevice::createLogicalDevice(){
        // 6. キュー作成情報の定義（Compute Queue用）
        float queuePriority = 1.0f; // キューの優先度
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_computeQueueFamilyIndex;
        queueCreateInfo.queueCount = 1; // キューを1つ作成
        queueCreateInfo.pQueuePriorities = &queuePriority;

        // Synchronization2 機能を有効化
        VkPhysicalDeviceSynchronization2Features sync2Features{};
        sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        sync2Features.synchronization2 = VK_TRUE;

        // 7. 有効化するデバイス機能
        VkPhysicalDeviceFeatures deviceFeatures{};
        // Descriptor Indexing Feature を有効化
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
        indexingFeatures.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        indexingFeatures.pNext = &sync2Features;
        indexingFeatures.runtimeDescriptorArray = VK_TRUE;
        indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
        indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        indexingFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;

        // 8. 論理デバイス作成情報の定義
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &indexingFeatures;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures = &deviceFeatures;
        
        // ToDo: 有効化するデバイス拡張機能
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME
        };
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // 9. 論理デバイスの作成
        std::cout << "Creating Logical Device..." << std::endl;
        VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
        std::cout << "Logical Device created successfully!" << std::endl;

        // 10. キューハンドルの取得
        vkGetDeviceQueue(m_device, m_computeQueueFamilyIndex, 0, &m_computeQueue);
        std::cout << "Compute Queue retrieved." << std::endl;
    }

    void VulkanDevice::createAllocator(){
        // 11. VMAアロケータ作成情報の定義
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorInfo.instance = m_instance;
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        // toDo: 必要に応じてVMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BITなどを設定

        // VMAアロケータの作成
        std::cout << "Creating VMA Allocator..." << std::endl;
        VkResult result = vmaCreateAllocator(&allocatorInfo, &m_allocator);
        if (result != VK_SUCCESS) {
            std::cerr << "VMA Error: " << result << " at " << __FILE__ << ":" << __LINE__ << std::endl;
            throw std::runtime_error("VMA operation failed: " + std::to_string(result));
        }
        std::cout << "VMA Allocator created successfully!" << std::endl;
    }


    VulkanDevice::~VulkanDevice(){
        m_constantBufferManager.reset();
        // すべてのリソースが解放されるのを待機
        m_frameCounter += MAX_FRAMES_IN_FLIGHT + 1;
        beginFrame();

        if (m_bindlessLayout) vkDestroyDescriptorSetLayout(m_device, m_bindlessLayout, nullptr);
        if (m_bindlessPool) vkDestroyDescriptorPool(m_device, m_bindlessPool, nullptr);
        if (m_allocator) vmaDestroyAllocator(m_allocator);
        if (m_device) vkDestroyDevice(m_device, nullptr);
        if (m_instance) vkDestroyInstance(m_instance, nullptr);
        std::cout << "VulkanDevice destroyed." << std::endl;
    }

    uint32_t VulkanDevice::allocateIndex() {
        std::lock_guard<std::mutex> lock(m_indexMutex);
        
        if (!m_freeIndices.empty()) {
            uint32_t index = m_freeIndices.back();
            m_freeIndices.pop_back();
            return index;
        }

        if (m_nextIndex >= MAX_BINDLESS_RESOURCES) {
            throw std::runtime_error("Bindless descriptor limit reached!");
        }
        return m_nextIndex++;
    }
    void VulkanDevice::unregisterIndex(uint32_t index) {
        std::lock_guard<std::mutex> lock(m_indexMutex);
        m_freeIndices.push_back(index);
    }
    uint32_t VulkanDevice::registerBuffer(VkBuffer buffer, VkDeviceSize size) {
        uint32_t index = allocateIndex();

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = size;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_bindlessDescriptorSet;
        write.dstBinding = 0; // バッファ用のバインド番号
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        return index;
    }

    uint32_t VulkanDevice::registerImage(VkImageView view) {
        uint32_t index = allocateIndex();

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = view;
        imageInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_bindlessDescriptorSet;
        write.dstBinding = 1; // イメージ用のバインド番号
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        return index;
    }
    void VulkanDevice::createBindlessResources() {

        // 1. レイアウトフラグの設定
        VkDescriptorBindingFlags flags = 
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT bindingFlags{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
        std::vector<VkDescriptorBindingFlags> allFlags = {
            flags,
            flags,
            flags | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
        };
        bindingFlags.bindingCount = 3;
        bindingFlags.pBindingFlags = allFlags.data();

        VkDescriptorSetLayoutBinding bindings[3] = {};
        // Binding 0: Storage Buffers
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = MAX_BINDLESS_RESOURCES;
        bindings[0].stageFlags = VK_SHADER_STAGE_ALL;
        // Binding 1: Storage Images
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = MAX_BINDLESS_RESOURCES;
        bindings[1].stageFlags = VK_SHADER_STAGE_ALL;
        // Binding 2: Uniform Buffers
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = MAX_BINDLESS_RESOURCES;
        bindings[2].stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &bindingFlags;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bindlessLayout));

        //  プールの作成とセットの割り当て（UPDATE_AFTER_BIND フラグ
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_BINDLESS_RESOURCES },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_BINDLESS_RESOURCES},
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_BINDLESS_RESOURCES }
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_bindlessPool);

        // ディスクリプタセットの割り当て
        // 可変長配列（Variable Descriptor Count）を使用する場合 pNext で最大数を指定する必要があり
        uint32_t maxBindingCounts[3] = { MAX_BINDLESS_RESOURCES, MAX_BINDLESS_RESOURCES, MAX_BINDLESS_RESOURCES};
        VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{};
        variableCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = &maxBindingCounts[2]; // 最後のバインディング(UBO)の最大数

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = &variableCountInfo; //可変長配列の設定を渡す
        allocInfo.descriptorPool = m_bindlessPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_bindlessLayout;

        VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_bindlessDescriptorSet));
    }

    uint32_t VulkanDevice::registerUniformBuffer(VkBuffer buffer, VkDeviceSize size) {
        uint32_t index = allocateIndex();
        VkDescriptorBufferInfo bufferInfo{ buffer, 0, size };
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = m_bindlessDescriptorSet;
        write.dstBinding = 2;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        return index;
    }
}