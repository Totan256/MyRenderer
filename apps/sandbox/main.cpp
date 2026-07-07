#include "core/Application.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/ModelBuilder.hpp"
#include "rhi/Device.hpp"
#include "utils/ImageExporter.hpp"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

class SandboxApp : public core::Application {
public:
    SandboxApp() : Application({ "MyRenderer Realtime Raytracing", 1280, 720 }) {
        std::cout << "SandboxApp Initialized." << std::endl;
    }

    void run() {
        std::cout << "--- Loading Model Assets ---" << std::endl;
        CpuModelData cpuData = ModelImporter::loadFromFile("assets/bunny.obj");
        auto bunnyModel = ModelBuilder::buildAndEnqueue(getDevice(), getDevice().getUploadManager(), cpuData);
        
        getDevice().getUploadManager()->submitUploadsAsync();
        getDevice().getUploadManager()->waitUploads();
        std::cout << "Successfully loaded bunny.obj" << std::endl;
        
        auto graph = getDevice().createRenderGraph();
        registerRenderGraph(graph.get());

        // スワップチェーン画像を登録 (名前ハッシュはShader側のPush Constantsの変数名と完全一致させる)
        auto hSwapchainImg = graph->importSwapchain(getSwapchain(), "swapchainImage"_hash);
        
        if (bunnyModel) {
            bunnyModel->importToGraph(*graph);
        }

        uint32_t totalIndices = 0;
        if (bunnyModel) {
            for (const auto& sm : bunnyModel->subMeshes) {
                totalIndices += sm.indexCount;
            }
        }

        // --- コンピュートパスの構築 ---
        auto& pass = graph->addComputePass("RaytracePass", "shaders/raytrace.comp");
        
        // ディスパッチオブジェクトの参照を保持してループ内で再利用する
        auto& dispatch = pass.dispatchThreads(
            [this](uint32_t& w, uint32_t& h, uint32_t& d) {
                w = getWidth(); h = getHeight(); d = 1;
            });

        // 1. 各種リソース(インデックス)のバインディング登録
        dispatch.write(hSwapchainImg); // pc.swapchainImage に解決される
        if (bunnyModel) {
            dispatch.read(bunnyModel->hPosition)   // pc.ModelPos に解決される
                    .read(bunnyModel->hAttribute)  // pc.ModelAttr に解決される
                    .read(bunnyModel->hIndex);      // pc.ModelIdx に解決される
        }

        auto profiler = getDevice().createGPUProfiler();
        graph->setProfiler(profiler.get());

        graph->compile();
        std::cout << "Start RT Loop" << std::endl;
        
        float time = 3.0f;
        
        while (isRunning()) {
            if (!this->beginFrame()) continue;
            processEvents();

            // フレーム時間の更新
            time += getDeltaTime(); // 依存のメンバ変数を使用

            // --- 2. 共通パラメータ(UBO)を毎フレーム設定 ---
            glm::vec4 camPos_time(time);
            
            glm::vec4 camTarget_numIndices(0.0f, 0.5f, 0.0f, 0.0f);
            std::memcpy(&camTarget_numIndices.w, &totalIndices, sizeof(uint32_t));
            
            glm::vec4 resolution_fov(getWidth(), getHeight(), glm::radians(45.0f), 0.0f);

            // setUniformを呼ぶと、内部で自動的にUBOプールに書き込まれ、
            // Push Constantsの該当位置に{インデックス, オフセット}の8バイトがセットされる
            dispatch.setUniform("camPos_time"_hash, camPos_time)
                    .setUniform("camTarget_numIdx"_hash, camTarget_numIndices)
                    .setUniform("resolution_fov"_hash, resolution_fov);

            profiler->resolveResults(getDevice().getCurrentFrame());
            if(profiler->hasNewResults() && getDevice().getCurrentFrame() % 3000 == 0) {
                profiler->dumpToConsole();
                std::cout<<"CPU DeltaTime: "<<getDeltaTime()<<" sec/frame"<<std::endl;
            }

            // 実行 (内部で最新のバックバッファが自動バインドされ、コマンドがサブミットされる)
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