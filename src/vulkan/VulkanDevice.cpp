#include "VulkanDevice.hpp"
#include "VulkanBuffer.hpp"
#include "VulkanImage.hpp"

#include "rhi/Resource.hpp"
#include "RenderGraph.hpp"
#include "VulkanRenderGraph.hpp"
#include "VulkanConstantBufferManager.hpp"
#include "VulkanCommandList.hpp"
#include "VulkanResourceAllocator.hpp"
#include "VulkanUploadManager.hpp"
#include <iostream>


#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
namespace rhi::vk{
    void VulkanDevice::beginFrame() {
        // 現在のフレームのフェンスを待機・リセット
        vkWaitForFences(m_device, 1, &m_inFlightFences[m_frameCounter % m_framesInFlight], VK_TRUE, UINT64_MAX);
        vkResetFences(m_device, 1, &m_inFlightFences[m_frameCounter % m_framesInFlight]);

        // 前フレームのGPU実行完了が保証されたタイミングで、
        // UploadManager内のリソースを安全にリセット・再利用可能にする
        if (m_uploadManager) {
            m_uploadManager->beginFrame(m_frameCounter);
        }
        std::lock_guard<std::mutex> lock(m_deletionMutex);
        while (!m_deletionQueue.empty() && m_deletionQueue.front().targetFrame <= m_frameCounter) {
            m_deletionQueue.front().func();
            m_deletionQueue.pop_front();
        }
        if (m_constantBufferManager) {
            m_constantBufferManager->nextFrame();
        }
    }

    VulkanDevice::VulkanDevice() = default;

    void VulkanDevice::endFrame() {
        m_frameCounter++;
    }

    void VulkanDevice::waitForFrame(uint64_t frameIndex) {
        uint32_t index = frameIndex % m_framesInFlight;
        vkWaitForFences(m_device, 1, &m_inFlightFences[index], VK_TRUE, UINT64_MAX);
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
    std::unique_ptr<rhi::CommandList> VulkanDevice::createCommandList(QueueType queueType) {
        return std::make_unique<VulkanCommandList>(*this, queueType);
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
        // Todo　リングバッファの実装をあとでする，とりあえず3060で動く分のサイズを用意またはconfigで指定できるようにする
        m_constantBufferManager = std::make_unique<ConstantBufferManager>(*this, 65536, 2);
        m_uploadManager = std::make_unique<VulkanUploadManager>(*this, m_framesInFlight, 16 * 1024 * 1024);

        // FIF用フェンスの作成
        m_inFlightFences.resize(m_framesInFlight);
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 最初はシグナル状態
        for (uint32_t i = 0; i < m_framesInFlight; i++) {
            VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]));
        }

        std::cout << "--- VulkanDevice Initialized Successfully ---" << std::endl;
    }
    rhi::UploadManager* VulkanDevice::getUploadManager() {
        return m_uploadManager.get();
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

    std::optional<uint32_t> VulkanDevice::findGraphicsQueueFamilyIndex(VkPhysicalDevice device) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            // Graphics QueueはVulkanの仕様上ComputeもTransferもサポートする
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
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
            std::optional<uint32_t> graphicsIndex = findGraphicsQueueFamilyIndex(device);

            if (computeIndex.has_value()) {
                m_physicalDevice = device;
                m_computeQueueFamilyIndex = computeIndex.value();
                if (graphicsIndex.has_value()) {
                    m_graphicsQueueFamilyIndex = graphicsIndex.value();
                }
                std::cout << "  -> Selected as Physical Device. Compute Queue Family Index: " 
                        << m_computeQueueFamilyIndex << std::endl;
                return;
            }
        }
        
        throw std::runtime_error("failed to find a GPU with a Compute Queue Family!");
    }

    void VulkanDevice::createLogicalDevice(){
        // キュー作成情報の定義（Compute Queue用）
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

        // Dynamic Rendering 機能を有効化
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{};
        dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
        dynamicRenderingFeatures.pNext = &sync2Features;
        
        // Extended Dynamic State (EDS) 1 & 2 & 3 の有効化
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT edsFeatures{};
        edsFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
        edsFeatures.extendedDynamicState = VK_TRUE;
        edsFeatures.pNext = &dynamicRenderingFeatures;
        VkPhysicalDeviceExtendedDynamicState2FeaturesEXT eds2Features{};
        eds2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT;
        eds2Features.extendedDynamicState2 = VK_TRUE;
        eds2Features.extendedDynamicState2PatchControlPoints = VK_FALSE;
        eds2Features.extendedDynamicState2LogicOp = VK_FALSE;
        eds2Features.pNext = &edsFeatures;
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT eds3Features{};
        eds3Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT;
        eds3Features.extendedDynamicState3PolygonMode = VK_TRUE;
        eds3Features.extendedDynamicState3DepthClampEnable = VK_TRUE;
        eds3Features.pNext = &eds2Features;

        // Descriptor Indexing 機能
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        indexingFeatures.pNext = &eds3Features; // チェーンを繋ぐ
        indexingFeatures.runtimeDescriptorArray = VK_TRUE;
        indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
        indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
        indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
        indexingFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        indexingFeatures.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

        // 有効化するデバイス機能
        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.multiDrawIndirect = VK_TRUE;

        // 論理デバイス作成情報の定義
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext = &indexingFeatures;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures = &deviceFeatures;
        
        // ToDo: 有効化するデバイス拡張機能
        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,       // Dynamic Rendering
            VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,  // EDS 1
            VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,// EDS 2
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME // EDS 3
        };
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // 論理デバイスの作成
        std::cout << "Creating Logical Device..." << std::endl;
        VK_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
        std::cout << "Logical Device created successfully!" << std::endl;

        // キューハンドルの取得
        vkGetDeviceQueue(m_device, m_computeQueueFamilyIndex, 0, &m_computeQueue);
        std::cout << "Compute Queue retrieved." << std::endl;
        vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
        std::cout << "Graphics Queue retrieved." << std::endl;
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
        enqueueDeletion([&samplers = m_samplers, device = m_device]() {
            for (auto& [hash, sampler] : samplers) {
                if (sampler != VK_NULL_HANDLE) {
                    vkDestroySampler(device, sampler, nullptr);
                }
            }
        });
        // 1. 全てのGPU処理が終わるまで安全に待機
        vkDeviceWaitIdle(m_device);
        
        // 2. マネージャ系の破棄（内部で保持しているCommandList等のフェンスもここで安全に破棄される）
        m_constantBufferManager.reset();
        m_uploadManager.reset();
        // 3. 削除キューに溜まっている遅延破棄タスク(画像やバッファなど)をすべて強制実行してクリーンアップ
        for (auto& entry : m_deletionQueue) {
            entry.func();
        }
        m_deletionQueue.clear();
        // 4. FIF用フェンスの破棄（エラーの原因だった部分）
        for (VkFence fence : m_inFlightFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_device, fence, nullptr);
            }
        }
        m_inFlightFences.clear();
        // 5. Vulkanコアオブジェクトの破棄
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
            flags, flags, flags, flags, // Binding 0~3
            flags | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT // Binding 4 (Sampler)
        };
        bindingFlags.bindingCount = 5;
        bindingFlags.pBindingFlags = allFlags.data();

        VkDescriptorSetLayoutBinding bindings[5] = {};
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
        // Binding 3: Sampled Images
        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[3].descriptorCount = MAX_BINDLESS_RESOURCES;
        bindings[3].stageFlags = VK_SHADER_STAGE_ALL;
        // Binding 4: Samplers
        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[4].descriptorCount = 32; // サンプラーは少数で十分
        bindings[4].stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &bindingFlags;
        layoutInfo.bindingCount = 5;
        layoutInfo.pBindings = bindings;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bindlessLayout));

        //  プールの作成とセットの割り当て（UPDATE_AFTER_BIND フラグ
        std::vector<VkDescriptorPoolSize> poolSizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_BINDLESS_RESOURCES },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_BINDLESS_RESOURCES},
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_BINDLESS_RESOURCES },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_RESOURCES },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 32 }
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
        uint32_t maxBindingCounts[5] = { MAX_BINDLESS_RESOURCES, MAX_BINDLESS_RESOURCES, MAX_BINDLESS_RESOURCES, MAX_BINDLESS_RESOURCES, 32 };
        VkDescriptorSetVariableDescriptorCountAllocateInfo variableCountInfo{};
        variableCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        variableCountInfo.descriptorSetCount = 1;
        variableCountInfo.pDescriptorCounts = &maxBindingCounts[4]; // 最後のバインディング(UBO)の最大数

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

    uint32_t VulkanDevice::registerSampledImage(VkImageView view) {
        uint32_t index = allocateIndex(); // 既存のインデックスアロケータを利用

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // サンプリング用レイアウト
        imageInfo.imageView = view;
        imageInfo.sampler = VK_NULL_HANDLE; // アプローチBのためサンプラーは結合しない

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_bindlessDescriptorSet;
        write.dstBinding = 3; // Binding 3: Sampled Images
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        return index;
    }

    uint32_t VulkanDevice::registerSampler(VkSampler sampler) {
        uint32_t index = allocateIndex();

        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_bindlessDescriptorSet;
        write.dstBinding = 4; // Binding 4: Samplers
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &samplerInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
        return index;
    }

    uint32_t VulkanDevice::getStaticSampler(StringHash nameHash) const {
        auto it = m_staticSamplers.find(nameHash);
        if (it == m_staticSamplers.end()) {
            // 見つからない場合はデフォルトとして LinearRepeat(最初に登録したもの) のインデックスを返す
            return m_staticSamplers.at("LinearRepeat"_hash);
        }
        return it->second;
    }

    VkQueue VulkanDevice::getQueue(QueueType type) const {
        // Todo 将来GraphicsやCopyも対応する場合はtypeに応じて適切なキューを返すようにする
        if (type == QueueType::Graphics) {
            return m_graphicsQueue;
        }
        return m_computeQueue;
    }
    
    uint32_t VulkanDevice::getQueueFamilyIndex(QueueType type) const {
        if (type == QueueType::Graphics) {
            return m_graphicsQueueFamilyIndex;
        }
        return m_computeQueueFamilyIndex;
    }

    VkSemaphore VulkanDevice::createSemaphore() {
        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore sem;
        VK_CHECK(vkCreateSemaphore(m_device, &info, nullptr, &sem));
        return sem;
    }

    void VulkanDevice::createStaticSamplers() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.mipLodBias = 0.0f;
        // 必要に応じて異方性フィルタ(Anisotropy)などを設定

        VkSampler linearRepeat;
        vkCreateSampler(m_device, &samplerInfo, nullptr, &linearRepeat);
        m_staticSamplers["LinearRepeat"_hash] = registerSampler(linearRepeat);
        m_samplers["LinearRepeat"_hash] = linearRepeat;
        
        // TODO: 同様に LinearClamp, NearestClamp などを作成
    }

    void VulkanDevice::destroySemaphore(VkSemaphore semaphore) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
    }
}