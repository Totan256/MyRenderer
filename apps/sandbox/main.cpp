#include "core/Application.hpp"
#include "core/RenderGraph.hpp"
#include "rhi/Device.hpp"
#include <iostream>

class SandboxApp : public core::Application {
public:
    SandboxApp() : Application({ "MyRenderer Realtime", 1280, 720 }) {
        // ここでモデルのロードや、静的なテクスチャのロードなどの初期化を行います。
        std::cout << "SandboxApp Initialized." << std::endl;
    }

    void run() {
        // 1. スワップチェーンのバックバッファを取得
        rhi::Image* backBuffer1 = getBackImage(0);
        rhi::Image* backBuffer2 = getBackImage(1);
        rhi::Image* backBuffer3 = getBackImage(2);

        // 2. RenderGraphの構築
        auto graph = getDevice().createRenderGraph();
        
        // バックバッファをグラフにインポート
        rhi::ResourceHandle hOutput[3] = { graph->importResource(backBuffer1, "BackBuffer"_hash), 
                            graph->importResource(backBuffer2, "BackBuffer"_hash), 
                            graph->importResource(backBuffer3, "BackBuffer"_hash) };

        // 3. パスの追加 (例: 画面を単色でクリアするだけのパス)
        auto& mainPass = graph->addGraphicsPass("MainPass", "shaders/test.vert", "shaders/test.frag")
            .addColorOutput(0, hOutput[0], rhi::LoadOp::Clear, rhi::StoreOp::Store, { 0.1f, 0.2f, 0.3f, 1.0f });
            // ※ ここに実際の描画処理 (drawなど) を追加していきます

        // 4. コンパイル
        graph->compile();

        for (int frame = 0; frame < 4; ++frame) {
            processEvents();

            // フレームの開始
            if (auto frameInfo = beginFrame()) {
                
                mainPass.addColorOutput(0, hOutput[frameInfo->imageIndex], rhi::LoadOp::Clear, rhi::StoreOp::Store, { 0.1f, 0.2f, 0.3f, 1.0f });
                graph->execute({});

                // 5. フレームの終了と画面への表示
                endFrame(frameInfo->imageIndex);
            }
        }
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