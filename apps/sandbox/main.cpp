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

        // スワップチェーン画像を登録
        auto hSwapchainImg = graph->importSwapchain(getSwapchain(), "swapchainImage"_hash);
        
        if (bunnyModel) {
            bunnyModel->importToGraph(*graph);
        }

        // ポリゴン（インデックス）の総数を計算
        uint32_t totalIndices = 0;
        if (bunnyModel) {
            for (const auto& sm : bunnyModel->subMeshes) {
                totalIndices += sm.indexCount;
            }
        }

        // --- コンピュートパスの構築 ---
        auto& pass = graph->addComputePass("RaytracePass", "shaders/raytrace.comp");
        
        // ウィンドウサイズに追従するようにディスパッチサイズをラムダで動的設定
        auto& dispatch = pass.dispatchThreads(
            [this](uint32_t& w, uint32_t& h, uint32_t& d) {
                w = getWidth(); h = getHeight(); d = 1;
            });

        // リソースバインディング
        dispatch.write(hSwapchainImg); // resolveOffset経由で pc.swapchainImage にインデックスが渡る
        if (bunnyModel) {
            dispatch.read(bunnyModel->hPosition)
                    .read(bunnyModel->hAttribute)
                    .read(bunnyModel->hIndex);
        }

        auto profiler = getDevice().createGPUProfiler();
        graph->setProfiler(profiler.get());

        graph->compile();
        std::cout << "Start RT Loop" << std::endl;
        
        float time = 0.0f;
        
        while (isRunning()) {
            if (!this->beginFrame()) continue;

            // 簡易的な時間更新
            time += 0.016f;

            // --- Push Constants の設定 ---
            glm::vec4 camPos_time(0.0f, 2.0f, 6.0f, time);
            
            glm::vec4 camTarget_numIndices(0.0f, 0.5f, 0.0f, 0.0f);
            // floatのビット列を維持したままuint32_tを詰め込む
            std::memcpy(&camTarget_numIndices.w, &totalIndices, sizeof(uint32_t));
            
            glm::vec4 resolution_fov(getWidth(), getHeight(), glm::radians(45.0f), 0.0f);

            dispatch.setUniform("camPos_time"_hash, camPos_time)
                    .setUniform("camTarget_numIdx"_hash, camTarget_numIndices)
                    .setUniform("resolution_fov"_hash, resolution_fov);

            profiler->resolveResults(getDevice().getCurrentFrame());
            if(profiler->hasNewResults() && getDevice().getCurrentFrame() % 3000 == 0) {
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