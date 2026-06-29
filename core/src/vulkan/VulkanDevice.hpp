// VulkanDevice.hpp
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <mutex>
#include <deque>
#include <functional>
#include <optional>
#include "rhi/Device.hpp"
#include "rhi/Resource.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/UploadManager.hpp"
#include "VulkanSemaphore.hpp"

namespace rhi::vk{
    // 前方宣言
    class ConstantBufferManager;
    class VulkanUploadManager;
    class VulkanShaderCache;
    class VulkanPipelineCache;

    // エラーチェック用の簡易マクロ
    #define VK_CHECK(call) \
        do { \
            VkResult result = call; \
            if (result != VK_SUCCESS) { \
                std::cerr << "Vulkan Error: " << result << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
                throw std::runtime_error("Vulkan operation failed: " + std::to_string(result)); \
            } \
        } while (0)

    class VulkanDevice : public rhi::Device {
    public:
        VulkanDevice();
        ~VulkanDevice() override;

        // コピー禁止
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;

        // 初期化
        void initialize(const core::VulkanProvider provider = {}) override;

        // フレーム管理
        void beginFrame() override;
        void endFrame() override;
        void waitForFrame(uint64_t frameIndex) override;
        uint64_t getCurrentFrame() const override { return m_frameCounter; }
        VkFence getCurrentFrameFence() const { return m_inFlightFences[m_frameCounter % m_framesInFlight]; }

        // 削除キュー
        void enqueueDeletion(std::function<void()>&& deletionFunc) override;

        VkSemaphore requestSemaphore();
        void releaseSemaphore(VkSemaphore semaphore);

        std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) override;
        std::unique_ptr<Image> createImage(const ImageDesc& desc) override;
        std::unique_ptr<RenderGraph> createRenderGraph() override;
        std::unique_ptr<rhi::CommandList> createCommandList(QueueType queueType = QueueType::Compute) override;
        std::unique_ptr<rhi::Swapchain> createSwapchain(const core::Window& window, const SwapchainConfig& config = {}) override;
        std::unique_ptr<rhi::GPUProfiler> createGPUProfiler() override;

        SyncPoint advanceTimeline(QueueType type) override;
        uint64_t getCompletedTimelineValue(QueueType type) override;
        void waitTimeline(const std::vector<SyncPoint>& syncPoints, uint64_t timeoutNs = UINT64_MAX) override;
        void waitForIdle() override;

        // 生のハンドル取得（拡張性のため）
        VkDevice getDevice() const { return m_device; }
        VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
        VmaAllocator getAllocator() const { return m_allocator; }
        VkPipelineCache getPipelineCache() const { return m_pipelineCache; }
        VkInstance getInstance() const { return m_instance; }
        VkSemaphore getTimelineSemaphore(QueueType type) const;
        
        // キューの取得
        VkQueue getQueue(QueueType type) const;
        uint32_t getQueueFamilyIndex(QueueType type) const;
        
        // キャッシュアクセス
        VulkanShaderCache& getShaderCache();
        VulkanPipelineCache& getPipelineCacheManager();
        // Bindless用のセットとレイアウトを取得
        VkDescriptorSetLayout getBindlessLayout() const { return m_bindlessLayout; }
        VkDescriptorSet getBindlessDescriptorSet() const { return m_bindlessDescriptorSet; }

        // UBO用
        uint32_t getMinUniformBufferOffsetAlignment() const { return m_minUniformBufferOffsetAlignment; }
        ConstantBufferManager& getConstantBufferManager() { return *m_constantBufferManager; }

        // 登録・破棄とディスクリプタバッチ更新
        uint32_t registerBuffer(VkBuffer buffer, VkDeviceSize size);
        uint32_t registerImage(VkImageView view);
        uint32_t registerUniformBuffer(VkBuffer buffer, VkDeviceSize size);
        uint32_t registerSampledImage(VkImageView view);
        uint32_t registerSampler(VkSampler sampler);
        void unregisterIndex(uint32_t index, uint32_t binding);
        
        void flushDescriptorUpdates(); // バッチ化されたディスクリプタ更新を実行

        uint32_t getStaticSampler(StringHash nameHash) const;
        rhi::UploadManager* getUploadManager() override;

        void createTimelineSemaphores(); // 論理デバイス作成後に呼ぶ
        // VulkanTimelineSemaphore& getTimelineSemaphore(QueueType type); // キューごとの対応にするため削除？

        // CPU側での一括待機 (複数キューの特定の SyncPoint を待つ場合)
        // bool waitSyncPoints(const std::vector<rhi::SyncPoint>& points, uint64_t timeoutNs = UINT64_MAX);
        VulkanTimelineSemaphore& getTimelineSemaphoreObject(QueueType type);
    private:
        struct DeletionEntry {
            uint64_t targetFrame;
            std::function<void()> func;
        };

        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_computeQueue = VK_NULL_HANDLE;
        uint32_t m_computeQueueFamilyIndex = 0;
        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        uint32_t m_graphicsQueueFamilyIndex = 0;
        VkQueue m_transferQueue = VK_NULL_HANDLE;
        uint32_t m_transferQueueFamilyIndex = 0;

        VmaAllocator m_allocator = VK_NULL_HANDLE;
        VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;

        uint32_t m_minUniformBufferOffsetAlignment = 256;
        std::unique_ptr<ConstantBufferManager> m_constantBufferManager;
        VkDescriptorPool m_bindlessPool= VK_NULL_HANDLE;
        VkDescriptorSetLayout m_bindlessLayout= VK_NULL_HANDLE;
        VkDescriptorSet m_bindlessDescriptorSet= VK_NULL_HANDLE;
        
        // インデックス管理
        std::vector<uint32_t> m_freeIndices; // 再利用可能なインデックス
        uint32_t m_nextIndex = 0;           // 新規発行用カウンタ
        std::mutex m_indexMutex;            // スレッド安全のため

        const uint32_t MAX_BINDLESS_RESOURCES = 100000;

        // ディスクリプタのバッチ更新用
        std::mutex m_descriptorMutex;
        std::vector<VkWriteDescriptorSet> m_pendingWrites;
        std::deque<VkDescriptorBufferInfo> m_pendingBufferInfos;
        std::deque<VkDescriptorImageInfo> m_pendingImageInfos;

        // 破棄時の安全化用ダミーリソース
        void createDummyResources();
        VkBuffer m_dummyBuffer = VK_NULL_HANDLE;
        VmaAllocation m_dummyBufferAlloc = VK_NULL_HANDLE;
        VkImage m_dummyImage = VK_NULL_HANDLE;
        VkImageView m_dummyImageView = VK_NULL_HANDLE;
        VmaAllocation m_dummyImageAlloc = VK_NULL_HANDLE;
        VkSampler m_dummySampler = VK_NULL_HANDLE;

        std::map<StringHash, uint32_t> m_staticSamplers;
        std::map<StringHash, VkSampler> m_samplers; 

        void createStaticSamplers(); // 後で消す

        // Frame in Flight数 (初期化時に決定)
        uint32_t m_framesInFlight = MAX_FRAMES_IN_FLIGHT;
        std::vector<VkFence> m_inFlightFences;
        uint64_t m_frameCounter = 0;
        std::deque<DeletionEntry> m_deletionQueue;
        std::mutex m_deletionMutex;
        std::unique_ptr<VulkanUploadManager> m_uploadManager;
        
        void createBindlessResources(); // 初期化時に呼ぶ

        void createInstance(std::vector<const char*> additionalExtensions = {});
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createAllocator();
        uint32_t allocateIndex();

        std::optional<uint32_t> findGraphicsQueueFamilyIndex(VkPhysicalDevice device);
        std::optional<uint32_t> findComputeQueueFamilyIndex(VkPhysicalDevice device);
        std::optional<uint32_t> findTransferQueueFamilyIndex(VkPhysicalDevice device);

        std::mutex m_semaphoreMutex;
        std::vector<VkSemaphore> m_semaphorePool;

        std::unique_ptr<VulkanShaderCache> m_shaderCache;
        std::unique_ptr<VulkanPipelineCache> m_pipelineCacheManager;
        core::VulkanProvider m_provider;

        // タイムラインセマフォ管理
        std::unordered_map<QueueType, std::unique_ptr<VulkanTimelineSemaphore>> m_timelineSemaphores;
    };
}