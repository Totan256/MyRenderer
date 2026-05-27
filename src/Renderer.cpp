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

    // 毎フレームの開始処理 (Ring Buffer 等のリセット)
    m_device.beginFrame();

    std::cout << "--- Initializing Buffers ---" << std::endl;

    // 1. 出力先バッファ
    size_t pixelBufferSize = m_width * m_height * sizeof(ColorVec4);
    auto outputBuffer = m_device.createBuffer({pixelBufferSize, rhi::BufferUsageFlags::StorageBuffer, true});
    struct Vertex {
        float x, y, z;
        float r, g, b;
    };

    // CPU側で頂点情報を作成
    std::vector<Vertex> vertices = {
        {  0.0f, -0.5f, 0.0f,   1.0f, 0.0f, 0.0f }, // 上 (赤)
        {  0.5f,  0.5f, 0.0f,   0.0f, 1.0f, 0.0f }, // 右下 (緑)
        { -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f }  // 左下 (青)
    };

    // Storage Buffer としてGPUメモリを確保 (TransferDstフラグも付ける)
    auto vertexBuffer = m_device.createBuffer(
        {vertices.size() * sizeof(Vertex),
        rhi::BufferUsageFlags::StorageBuffer | rhi::BufferUsageFlags::TransferDst,
        true
    });

    // UploadManager を使ってCPUからGPUへデータを転送
    m_device.getUploadManager()->enqueueBufferUpload(vertexBuffer.get(), vertices.data(), vertices.size() * sizeof(Vertex));

    std::cout << "--- Building Render Graph ---" << std::endl;
    auto graph = m_device.createRenderGraph();

    rhi::Format outputFormat = rhi::Format::R8G8B8A8_Unorm; // 仮

    // 1. Graphics Pass の追加
    auto& pass = graph->addGraphicsPass("TestTrianglePass", "shaders/test.vert", "shaders/test.frag")
        .setColorFormat(0, outputFormat)
        .setCullMode(rhi::CullMode::None)
        .setDepthTest(false);

    // 2. リソースのバインド
    // 出力先イメージを ColorAttachment としてバインド (m_outputImage 等)
    pass.bind(0, rhi::ResourceState::ColorAttachment);
    pass.bind(4, rhi::ResourceState::StorageRead);

    // 3. Draw コマンドの発行と Push Constants のセット
    // ResourceHandle からバインドレスインデックスを取得
    auto vbIndex = graph->importResource(vertexBuffer.get(), "vertexBufferIndex"_hash);

    // 3頂点、1インスタンスを描画
    pass.draw(3, 1)
        .read(vbIndex);

    // グラフのコンパイル
    graph->compile();

    uint64_t currentFrame = m_device.getCurrentFrame();

    // 5. 実行 (フェンスをシグナルし、CPUは待たずに即座に抜ける)
    std::cout << "--- Executing Render Graph ---" << std::endl;
    graph->execute();

    // フレーム終了処理
    m_device.endFrame();

    // 6. 画像保存のため、たった今投げたフレームの完了を待機
    std::cout << "Waiting for frame to complete..." << std::endl;
    m_device.waitForFrame(currentFrame);

    std::cout << "Saving result to graphics_test.png..." << std::endl;
    ImageExporter::savePng("graphics_test.png", m_width, m_height, outputBuffer->map());
    outputBuffer->unmap();
}