#include "Renderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include "ImageExporter.hpp"
#include "utils/StringHash.hpp"
#include "utils/ModelImporter.hpp" // ★追加
#include <iostream>

Renderer::Renderer(rhi::Device& device, uint32_t width, uint32_t height)
    : m_device(device), m_width(width), m_height(height) 
{
    std::cout << "--- Loading Model Assets ---" << std::endl;
    // 1. アセットファイルからロードして初期化
    try {
        // assets/bunny.obj からデータを読み込む
        CpuModelData cpuData = ModelImporter::loadFromFile("assets/bunny.obj");
        
        // GPUバッファの作成とアップロードのキューイング
        m_bunnyModel = ModelBuilder::buildAndEnqueue(m_device, m_device.getUploadManager(), cpuData);
        
        // 転送コマンドの即時送信と完了待機
        m_device.getUploadManager()->submitUploadsAsync();
        m_device.getUploadManager()->waitUploads();
        std::cout << "Successfully loaded bunny.obj" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Model load error: " << e.what() << std::endl;
    }
}

Renderer::~Renderer() {}

void Renderer::render(float time) {
    m_device.beginFrame();

    auto outputImage = m_device.createImage({
        m_width, m_height, 1, 1, 1, 
        rhi::Format::R8G8B8A8_Unorm,
        rhi::ImageUsageFlags::ColorAttachment | rhi::ImageUsageFlags::TransferSrc | rhi::ImageUsageFlags::Storage
    });

    size_t pixelBufferSize = m_width * m_height * 4;
    auto outputBuffer = m_device.createBuffer({
        pixelBufferSize, 
        rhi::BufferUsageFlags::TransferDst | rhi::BufferUsageFlags::StorageBuffer,
        true
    });

    auto graph = m_device.createRenderGraph();
    auto hOutputImg = graph->importResource(outputImage.get(), "outputImage"_hash);
    auto hOutputBuf = graph->importResource(outputBuffer.get(), "outputBuffer"_hash);

    // 2. モデルのバッファ群をRenderGraphに一括インポート
    // 内部で "ModelPos"_hash, "ModelAttr"_hash, "ModelIdx"_hash が登録されます
    if (m_bunnyModel) {
        m_bunnyModel->importToGraph(*graph);
    }

    // 3. グラフィックパスクラスの構築
    auto& pass = graph->addGraphicsPass("ModelRenderPass", "shaders/model.vert", "shaders/model.frag")
        .addColorOutput(0, hOutputImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.05f, 0.05f, 0.1f, 1.0f})
        .setGraphicsState({
            .cullMode = rhi::CullMode::None, // 表裏どちらも描画
            .depthTestEnable = false        // 現在深度テクスチャがないため無効
        });

    if (m_bunnyModel) {
        // サブメッシュごとに描画コマンドをレコード
        // PVP方式のため、頂点数として「インデックス数」を流し込みます
        for (const auto& subMesh : m_bunnyModel->subMeshes) {
            pass.draw(subMesh.indexCount, 1, subMesh.indexBase, 0)
                .read(m_bunnyModel->hPosition)
                .read(m_bunnyModel->hAttribute)
                .read(m_bunnyModel->hIndex);
        }
    }

    graph->addCopyPass("CopyToBuffer", hOutputImg, hOutputBuf, pixelBufferSize, rhi::QueueType::Compute);
    graph->compile();

    uint64_t currentFrame = m_device.getCurrentFrame();
    graph->execute({});

    m_device.endFrame();
    m_device.waitForFrame(currentFrame);

    std::cout << "Saving result to model_test.png..." << std::endl;
    outputBuffer->invalidate();

    ImageExporter::savePngUint8("model_test.png", m_width, m_height, outputBuffer->map());
    outputBuffer->unmap();
}