#include "VulkanDevice.hpp"


#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

void VulkanDevice::initialize(){
    std::cout << "--- Initializing VulkanDevice ---" << std::endl;
    // ヘルパー関数の呼び出し。VK_CHECKが失敗時に例外を投げるため、安全に処理できます。
    createInstance();
    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
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
            // 最もシンプルなのは、Computeビットを持つ最初のファミリーを選択すること
            // 描画（Graphics）機能は不要なので、Compute専用キューがあれば理想的ですが、
            // 多くのGPUではGraphicsとComputeが兼用（両方のビットが立っている）されています。
            // 今回はComputeビットが立っているものを見つけたら採用します。
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

    // 7. 有効化するデバイス機能
    VkPhysicalDeviceFeatures deviceFeatures{};
    // 今回はComputeのみなので、特に有効化する機能は不要ですが、
    // 必要に応じてここで `samplerAnisotropy` などを設定します。

    // 8. 論理デバイス作成情報の定義
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    
    // ToDo: 有効化するデバイス拡張機能，あとで付け加えやすく
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = nullptr;

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
    // VMAアロケータの解放
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
    }
    
    // 論理デバイスの解放
    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
    }
    
    // インスタンスの解放
    if (m_instance != VK_NULL_HANDLE) {
        // デバッグメッセンジャーの破棄（もしあれば）
        if (m_debugMessenger != VK_NULL_HANDLE) {
            // vkDestroyDebugUtilsMessengerEXTは拡張関数なので、手動で取得が必要
            auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
            if (func != nullptr) {
                func(m_instance, m_debugMessenger, nullptr);
            }
        }
        vkDestroyInstance(m_instance, nullptr);
    }
    std::cout << "VulkanDevice destroyed." << std::endl;
}