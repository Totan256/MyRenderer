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
        
        auto graph = getDevice().createRenderGraph();
        registerRenderGraph(graph.get());

        auto hDepthImg = graph->createImage(
            rhi::ImageDesc::Relative2D(getWidth(), getHeight(), 1.0f,1.0f,
                rhi::Format::D32_Sfloat, 
                rhi::ImageUsageFlags::DepthStencilAttachment | rhi::ImageUsageFlags::Storage), "depthImage"_hash);
    
        auto hSwapchainImg = graph->importSwapchain(getSwapchain(), "swapchainImage"_hash);
        

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

        auto profiler = getDevice().createGPUProfiler();
        graph->setProfiler(profiler.get());

        graph->compile();
        std::cout<<"start loop"<<std::endl;
        while (isRunning()) {
            if (!this->beginFrame()) continue;

            profiler->resolveResults(getDevice().getCurrentFrame());
            if(profiler->hasNewResults() && getDevice().getCurrentFrame()%3000==0) {
                profiler->dumpToConsole();
            }

            graph->execute();
            this->endFrame();
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