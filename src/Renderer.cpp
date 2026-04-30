#include "Renderer.hpp"
#include "ImageExporter.hpp"
#include "vulkan/VulkanRenderGraph.hpp"
#include "vulkan/VulkanDevice.hpp"
#include <iostream>
#include <cstddef>

// シェーダーの layout(push_constant) と一致させる構造体
struct ComputePushConstants {
    uint32_t outputImageIndex;
    float time;
    uint32_t resX;
    uint32_t resY;
};

void Renderer::render(float time) {
    // ダウンキャストしてVulkan固有の機能にアクセス
    auto& vkDevice = static_cast<rhi::vk::VulkanDevice&>(m_device);

    // ---------------------------------------------------------
    // 1. 物理リソースの作成（RenderGraph外で管理して結果を読み出すため）
    // ---------------------------------------------------------
    std::cout << "Creating physical resources..." << std::endl;
    rhi::vk::VulkanImage outputImage(vkDevice, m_width, m_height);
    
    size_t imageSize = m_width * m_height * 4;
    rhi::vk::VulkanBuffer stagingBuffer(
        vkDevice, vkDevice.getAllocator(), imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    // ---------------------------------------------------------
    // 2. RenderGraph の構築
    // ---------------------------------------------------------
    std::cout << "Building RenderGraph..." << std::endl;
    rhi::vk::VulkanRenderGraph graph(vkDevice);
    std::cout << "RenderGraph created." << std::endl;
    // 物理リソースをグラフにインポート
    auto hOutput = graph.importResource(&outputImage);
    auto hStaging = graph.importResource(&stagingBuffer);

    // バインドグループの定義（オフセットと用途の紐付け）
    auto& bindGroup = graph.createBindGroup({
        {offsetof(ComputePushConstants, outputImageIndex), rhi::ResourceUsage::StorageWrite}
    });
    std::cout << "BindGroup created." << std::endl;
    // メインのコンピュートパスを追加
    auto& mainPass = graph.addPass("MainCompute", "shaders/simple.comp")
        .bind(bindGroup);

    // ディスパッチの設定と動的パラメータの書き込み
    mainPass.dispatch((m_width + 15) / 16, (m_height + 15) / 16, 1)
        .updateResource(offsetof(ComputePushConstants, outputImageIndex), hOutput)
        .updateConstant(offsetof(ComputePushConstants, time), time)
        .updateConstant(offsetof(ComputePushConstants, resX), m_width)
        .updateConstant(offsetof(ComputePushConstants, resY), m_height);

    std::cout << "Main pass configured." << std::endl;

    // 【ハック】出力画像を TransferSrc 状態へ遷移させるための「空パス」を追加
    // これにより graph.compile() が自動でバリア（General -> TransferSrc）を張ってくれる
    auto& copyTransitionPass = graph.addPass("CopyTransition", "")
        .bind(0, rhi::ResourceUsage::TransferSrc)
        .bind(4, rhi::ResourceUsage::TransferDst);
    std::cout << "CopyTransition pass configured." << std::endl;
    copyTransitionPass.dispatch(0, 0, 0)
        .updateResource(0, hOutput)
        .updateResource(4, hStaging);
    std::cout << "compile started." << std::endl;
    // 依存関係とバリアの解決
    graph.compile();
    std::cout << "compile finished." << std::endl;
    // ---------------------------------------------------------
    // 3. コマンドの記録と実行
    // ---------------------------------------------------------
    std::cout << "Executing RenderGraph..." << std::endl;
    rhi::vk::VulkanCommandList cmd(vkDevice);
    cmd.begin();
    
    // RenderGraph によるコンピュート実行とバリア展開
    graph.execute(cmd); 

    // RenderGraph 実行後、画像は安全にコピーできる状態(TransferSrc)になっている
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {m_width, m_height, 1};

    vkCmdCopyImageToBuffer(
        cmd.getCommandBuffer(),
        outputImage.getImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer.getNativeBuffer(),
        1, &region
    );

    cmd.end();
    cmd.submitAndWait(); // GPUの処理完了を待つ

    // ---------------------------------------------------------
    // 4. 画像の保存
    // ---------------------------------------------------------
    std::cout << "Saving output image..." << std::endl;
    void* data = stagingBuffer.map();
    ImageExporter::savePngUint8("output_shader.png", m_width, m_height, data);
    stagingBuffer.unmap();
    
    std::cout << "Rendered successfully! Saved to output_shader.png" << std::endl;
}