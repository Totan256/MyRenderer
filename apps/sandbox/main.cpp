#include "core/Application.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/ModelBuilder.hpp"
#include "rhi/Device.hpp"
#include "utils/ImageExporter.hpp"
#include <iostream>

class SandboxApp : public core::Application {
public:
    SandboxApp() : Application({ "MyRenderer Realtime", 1280, 720 }) {
        std::cout << "SandboxApp Initialized." << std::endl;
    }

    void run() {
        std::cout << "--- Loading Model Assets ---" << std::endl;
        // 1. アセットファイルからロードして初期化
    
        // assets/bunny.obj からデータを読み込む
        CpuModelData cpuData = ModelImporter::loadFromFile("assets/bunny.obj");
        
        // GPUバッファの作成とアップロードのキューイング
        auto bunnyModel = ModelBuilder::buildAndEnqueue(getDevice(), getDevice().getUploadManager(), cpuData);
        
        // 転送コマンドの即時送信と完了待機
        getDevice().getUploadManager()->submitUploadsAsync();
        getDevice().getUploadManager()->waitUploads();
        std::cout << "Successfully loaded bunny.obj" << std::endl;
        

        auto outputImage = getDevice().createImage({
            getWidth(), getHeight(), 1, 1, 1, 
            rhi::Format::R8G8B8A8_Unorm,
            rhi::ImageUsageFlags::ColorAttachment | rhi::ImageUsageFlags::TransferSrc | rhi::ImageUsageFlags::Storage
        });

        auto depthImage = getDevice().createImage({
            getWidth(), getHeight(), 1, 1, 1, 
            rhi::Format::D32_Sfloat, // 深度フォーマット
            rhi::ImageUsageFlags::DepthStencilAttachment | rhi::ImageUsageFlags::Storage
        });

        size_t pixelBufferSize = getWidth() * getHeight() * 4;
        auto outputBuffer = getDevice().createBuffer({
            pixelBufferSize, 
            rhi::BufferUsageFlags::TransferDst | rhi::BufferUsageFlags::StorageBuffer,
            true
        });

        auto graph = getDevice().createRenderGraph();
        auto hOutputImg = graph->importResource(outputImage.get(), "outputImage"_hash);
        auto hOutputBuf = graph->importResource(outputBuffer.get(), "outputBuffer"_hash);
        auto hDepthImg = graph->importResource(depthImage.get(), "depthImage"_hash);

        // 2. モデルのバッファ群をRenderGraphに一括インポート
        // 内部で "ModelPos"_hash, "ModelAttr"_hash, "ModelIdx"_hash が登録されます
        if (bunnyModel) {
            bunnyModel->importToGraph(*graph);
        }

        // 3. グラフィックパスクラスの構築
        auto& pass = graph->addGraphicsPass("ModelRenderPass", "shaders/model.vert", "shaders/model.frag")
            .addColorOutput(0, hOutputImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.05f, 0.05f, 0.1f, 1.0f})
            .setDepthOutput(hDepthImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {1.0f, 0})
            .setGraphicsState({
                .cullMode = rhi::CullMode::Back, // ★ 変更: 背面カリングを有効に
                .depthTestEnable = true,         // ★ 変更: 深度テストを有効に
                .depthWriteEnable = true         // ★ 変更: 深度書き込みを有効に
            });

        if (bunnyModel) {
            // サブメッシュごとに描画コマンドをレコード
            // PVP方式のため、頂点数として「インデックス数」を流し込みます
            for (const auto& subMesh : bunnyModel->subMeshes) {
                pass.draw(subMesh.indexCount, 1, subMesh.indexBase, 0)
                    .read(bunnyModel->hPosition)
                    .read(bunnyModel->hAttribute)
                    .read(bunnyModel->hIndex);
            }
        }

        graph->addCopyPass("CopyToBuffer", hOutputImg, hOutputBuf, pixelBufferSize, rhi::QueueType::Compute);
        graph->compile();

        auto frameInfo = this->beginFrame();
        graph->execute({});
        this->endFrame(frameInfo->imageIndex);
        this->getDevice().waitForFrame(frameInfo->frameIndex);


        std::cout << "Saving result to model_test.png..." << std::endl;
        outputBuffer->invalidate();

        ImageExporter::savePngUint8("workspace/model_test.png", getWidth(), getHeight(), outputBuffer->map());
        outputBuffer->unmap();
    }
};

int main() {
    try {
        SandboxApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}