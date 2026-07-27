#include "core/Application.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/ModelBuilder.hpp"
#include "rhi/Device.hpp"
#include "utils/ImageExporter.hpp"
#include "utils/bvh/BVHManager.hpp"
#include "utils/bvh/CpuBVHBuilder.hpp"
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <cmath> // fmod

class SandboxApp : public core::Application {
public:
    SandboxApp() : Application({ "MyRenderer Realtime Raytracing", 1280, 720 }) {
        std::cout << "SandboxApp Initialized." << std::endl;
    }

    void run() {
        std::cout << "--- Loading Model Assets ---" << std::endl;
        CpuModelData cpuData = ModelImporter::loadFromFile("assets/teapot.obj");
        auto teapotModel = ModelBuilder::buildAndEnqueue(getDevice(), getDevice().getUploadManager(), cpuData);
        
        std::cout << "--- Building BVH ---" << std::endl;
        
        // --- BVHコンフィグ設定
        // maxDepth, minPrimitives を調整してstats変化確認
        BVHConfig config;
        config.maxDepth = 24;
        config.minPrimitivesPerLeaf = 4;
        auto builder = std::make_unique<CpuBVHBuilder>(BVHSplitMethod::SpatialMedian, config);
        
        auto teapotBvh = BVHManager::buildAndEnqueue(getDevice(), getDevice().getUploadManager(), cpuData, std::move(builder));

        getDevice().getUploadManager()->submitUploadsAsync();
        getDevice().getUploadManager()->waitUploads();
        std::cout << "Successfully loaded teapot.obj and BVH" << std::endl;
        
        auto graph = getDevice().createRenderGraph();
        registerRenderGraph(graph.get());

        auto hSwapchainImg = graph->importSwapchain(getSwapchain(), "swapchainImage"_hash);
        
        if (teapotModel) {
            teapotModel->importToGraph(*graph);
        }
        if (teapotBvh) {
            teapotBvh->importToGraph(*graph);
        }

        auto& pass = graph->addComputePass("RaytracePass", "shaders/raytrace.comp");
        
        auto& dispatch = pass.dispatchThreads(
            [this](uint32_t& w, uint32_t& h, uint32_t& d) {
                w = getWidth(); h = getHeight(); d = 1;
            });

        dispatch.write(hSwapchainImg);
        if (teapotModel && teapotBvh) {
            dispatch.read(teapotModel->hPosition)   
                    .read(teapotModel->hAttribute)  
                    .read(teapotBvh->hReorderedIndices)
                    .read(teapotBvh->hNodes);
        }

        auto profiler = getDevice().createGPUProfiler();
        graph->setProfiler(profiler.get());

        graph->compile();
        std::cout << "Start RT Loop" << std::endl;
        
        float time = 3.0f;

        struct SceneGlobals {
            glm::vec4 camPos_time;
            glm::vec4 camTarget_numIdx;
            glm::vec4 resolution_fov;
        };
        SceneGlobals globals{};
        
        while (isRunning()) {
            if (!this->beginFrame()) continue;
            processEvents();

            time += getDeltaTime();

            globals.camPos_time = glm::vec4(0.0f, 2.0f, 8.0f, time);
            globals.camTarget_numIdx = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
            globals.resolution_fov = glm::vec4(getWidth(), getHeight(), glm::radians(45.0f), 0.0f);
            dispatch.setUniform("sceneGlobalsPtr"_hash, globals);

            // 4秒ごとに Normal / Heatmap 切り替え
            uint32_t renderMode = (std::fmod(time, 4.0f) > 2.0f) ? 1 : 0;
            dispatch.setPushConstant("renderMode"_hash, renderMode);

            profiler->resolveResults(getDevice().getCurrentFrame());
            if(profiler->hasNewResults() && getDevice().getCurrentFrame() % 3000 == 0) {
                profiler->dumpToConsole();
                std::cout<<"CPU DeltaTime: "<<getDeltaTime()*1000 <<" ms/frame"<<std::endl;
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