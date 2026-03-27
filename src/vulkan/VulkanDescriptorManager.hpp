#pragma once
#include "VulkanDevice.hpp"
#include <vector>
#include <stdexcept>
#include <list>
namespace rhi::vk {
    class DescriptorManager {
    public:
        DescriptorManager(VulkanDevice& device) : m_device(device) {
            // 汎用的なプールを作成（必要に応じて拡張・再確保するロジックを入れるのが一般的だが、今回は簡易版）
            std::vector<VkDescriptorPoolSize> poolSizes = {
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10}
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = 10; // 10個までセットを作れる

            vkCreateDescriptorPool(m_device.getDevice(), &poolInfo, nullptr, &m_pool);
        }

        ~DescriptorManager() {
            vkDestroyDescriptorPool(m_device.getDevice(), m_pool, nullptr);
        }

        VkDescriptorPool getPool() const { return m_pool; }

        // 簡易ビルダー: セットを確保して書き込みを行う
        class Builder {
        public:
            Builder(VulkanDevice& dev, VkDescriptorPool pool, VkDescriptorSetLayout layout)
                : m_device(dev), m_pool(pool), m_layout(layout) {}

            // バッファをバインドする
            Builder& bindBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDescriptorType type) {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = buffer;
                bufferInfo.offset = 0;
                bufferInfo.range = size;

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstBinding = binding;
                write.descriptorType = type;
                write.descriptorCount = 1;
                write.pBufferInfo = &m_bufferInfos.emplace_back(bufferInfo); // ポインタ有効性を保つためlist/vectorに保存

                m_writes.push_back(write);
                return *this;
            }

            // 実際にセットを作成・更新する
            VkDescriptorSet build() {
                VkDescriptorSetAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocInfo.descriptorPool = m_pool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &m_layout;

                VkDescriptorSet set;
                if (vkAllocateDescriptorSets(m_device.getDevice(), &allocInfo, &set) != VK_SUCCESS) {
                    throw std::runtime_error("Failed to allocate descriptor set!");
                }

                // 確保したセットに対してWrite情報を適用
                for (auto& w : m_writes) {
                    w.dstSet = set;
                }
                vkUpdateDescriptorSets(m_device.getDevice(), static_cast<uint32_t>(m_writes.size()), m_writes.data(), 0, nullptr);

                return set;
            }
            
            Builder& bindImage(uint32_t binding, VkImageView view, VkDescriptorType type, VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = layout;
                imageInfo.imageView = view;
                imageInfo.sampler = VK_NULL_HANDLE; // 今回はStorageImageなのでサンプラー不要

                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstBinding = binding;
                write.descriptorType = type;
                write.descriptorCount = 1;
                // ポインタ保持のためlistに追加 (m_imageInfosメンバ変数をlist<VkDescriptorImageInfo>型で追加してください)
                write.pImageInfo = &m_imageInfos.emplace_back(imageInfo); 

                m_writes.push_back(write);
                return *this;
            }

        private:
            VulkanDevice& m_device;
            VkDescriptorPool m_pool;
            VkDescriptorSetLayout m_layout;
            std::vector<VkWriteDescriptorSet> m_writes;
            // std::vector は再確保でポインタが無効になる可能性があるため、
            // 本当は std::list を使うか、事前にresizeが必要だが、今回は簡易的にメンバに持たせずローカル変数的に使う前提
            std::list<VkDescriptorBufferInfo> m_bufferInfos;
            std::list<VkDescriptorImageInfo> m_imageInfos;
        };

        Builder createBuilder(VkDescriptorSetLayout layout) {
            return Builder(m_device, m_pool, layout);
        }

    private:
        VulkanDevice& m_device;
        VkDescriptorPool m_pool;
    };
}