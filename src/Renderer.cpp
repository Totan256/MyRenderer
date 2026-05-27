#include "Renderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp"
#include "rhi/UploadManager.hpp" 
#include "utils/StringHash.hpp"
#include "rhi/ModelBuilder.hpp"
#include <iostream>
#include <cstddef>
#include <vector>

struct ColorVec4 {
    float r, g, b, a;
};
void Renderer::render(float time) {
    m_device.beginFrame();

    std::cout << "--- Initializing Buffers & Images ---" << std::endl;

    // 1. レンダリングターゲット用の Image を作成 (GPU上)
    auto outputImage = m_device.createImage({
        m_width, m_height, 1, 1, 1, 
        rhi::Format::R8G8B8A8_Unorm,
        rhi::ImageUsageFlags::ColorAttachment | rhi::ImageUsageFlags::TransferSrc | rhi::ImageUsageFlags::Storage // 描画先 兼 コピー元
    });

    // 2. CPU読み取り用の Buffer を作成 (CPU可視)
    size_t pixelBufferSize = m_width * m_height * 4; // 4 bytes per pixel (RGBA8)
    auto outputBuffer = m_device.createBuffer({
        pixelBufferSize, 
        rhi::BufferUsageFlags::TransferDst | rhi::BufferUsageFlags::StorageBuffer, // コピー先
        true // CPU 可視
    });

    // 頂点データの準備 (省略: 既存のまま)
    struct Vertex { float x, y, z; float r, g, b; };
    std::vector<Vertex> vertices = {
        {  0.0f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f },
        {  0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f },
        { -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f }
    };
    auto vertexBuffer = m_device.createBuffer({
        vertices.size() * sizeof(Vertex),
        rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst,
        false
    });
    m_device.getUploadManager()->enqueueBufferUpload(vertexBuffer.get(), vertices.data(), vertices.size() * sizeof(Vertex));
    m_device.getUploadManager()->submitUploadsAsync();
    m_device.getUploadManager()->waitUploads();

    std::cout << "--- Building Render Graph ---" << std::endl;
    auto graph = m_device.createRenderGraph();

    // グラフにリソースを登録
    auto hOutputImg = graph->importResource(outputImage.get(), "outputImage"_hash);
    auto hOutputBuf = graph->importResource(outputBuffer.get(), "outputBuffer"_hash);
    auto vbIndex = graph->importResource(vertexBuffer.get(), "vertexBufferIndex"_hash);

    // 3. Graphics Pass の構築 (描画先は Image)
    auto& pass = graph->addGraphicsPass("TestTrianglePass", "shaders/test.vert", "shaders/test.frag")
        .addColorOutput(0, hOutputImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.1f, 0.1f, 0.1f, 1.0f})
        .setTopology(rhi::Topology::TriangleList)
        .setCullMode(rhi::CullMode::None)
        .setDepthTest(false);

    pass.draw(3, 1).read(vbIndex);

    // 4. Image から Buffer への Copy Pass を追加
    // ※内部のバリア計算で、Image(TransferSrc) -> Buffer(TransferDst) のレイアウト遷移が自動解決されます
    graph->addCopyPass("CopyToBuffer", hOutputImg, hOutputBuf, pixelBufferSize, rhi::QueueType::Graphics);

    graph->compile();

    uint64_t currentFrame = m_device.getCurrentFrame();

    std::cout << "--- Executing Render Graph ---" << std::endl;
    graph->execute({});

    m_device.endFrame();

    std::cout << "Waiting for frame to complete..." << std::endl;
    m_device.waitForFrame(currentFrame);

    std::cout << "Saving result to graphics_test.png..." << std::endl;
    // Bufferをマップして画像保存
    ImageExporter::savePng("graphics_test.png", m_width, m_height, outputBuffer->map());
    outputBuffer->unmap();
}