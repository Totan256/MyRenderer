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
        

        auto depthImage = getDevice().createImage({
            getWidth(), getHeight(), 1, 1, 1, 
            rhi::Format::D32_Sfloat, // 深度フォーマット
            rhi::ImageUsageFlags::DepthStencilAttachment | rhi::ImageUsageFlags::Storage
        });


        auto graph = getDevice().createRenderGraph();
        // スワップチェーンの画像を取得してGraphにインポートする
        auto swapchainImage = this->getBackImage(0); // ※環境に合わせて取得メソッドを変更してください
        auto hSwapchainImg = graph->importResource(swapchainImage, "swapchainImage"_hash);
        auto hDepthImg = graph->importResource(depthImage.get(), "depthImage"_hash);

        // 2. モデルのバッファ群をRenderGraphに一括インポート
        // 内部で "ModelPos"_hash, "ModelAttr"_hash, "ModelIdx"_hash が登録されます
        if (bunnyModel) {
            bunnyModel->importToGraph(*graph);
        }

        // 3. グラフィックパスクラスの構築
        auto& pass = graph->addGraphicsPass("ModelRenderPass", "shaders/model.vert", "shaders/model.frag")
            .addColorOutput(0, hSwapchainImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.05f, 0.05f, 0.1f, 1.0f})
            .setDepthOutput(hDepthImg, rhi::LoadOp::Clear, rhi::StoreOp::Store, {1.0f, 0})
            .setGraphicsState({
                .cullMode = rhi::CullMode::Back, // 背面カリングを有効
                .depthTestEnable = true,         // 深度テストを有効
                .depthWriteEnable = true         // 深度書き込みを有効
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

        graph->compile();
        while (isRunning()) {
            auto frameInfo = this->beginFrame();
            if(!frameInfo) continue;

            graph->bindPhysicalResource(hSwapchainImg, this->getBackImage(frameInfo->imageIndex));
            graph->execute({});
            this->endFrame(frameInfo->imageIndex);
        }
        this->getDevice().waitForIdle();
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