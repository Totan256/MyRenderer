#pragma once
#include "GraphicsDevice.hpp"
#include "CommandList.hpp"
#include "DescriptorManager.hpp"
#include <memory>
#include <vector>

// 前方宣言
class Texture; 
class ComputePipeline;

class Renderer {
public:
    Renderer(int width, int height);
    ~Renderer();

    // 初期化関係
    void initialize();

    // 描画実行（これを呼ぶと画像ができる）
    void render();

    // 結果の保存
    void saveImage(const std::string& filename);

    // リソース管理（これらをRendererが持つことで、mainがスッキリする）
    GraphicsDevice& getDevice() { return *m_device; }

private:
    int m_width;
    int m_height;

    // コアシステム
    std::unique_ptr<GraphicsDevice> m_device;
    std::unique_ptr<DescriptorManager> m_descManager;
    std::unique_ptr<CommandList> m_cmdList;

    // リソース（本来はResourceManagerクラスに持たせるが、まずはここ）
    // std::unique_ptr<Texture> m_outputTexture; // まだGpuBufferのままでOK
    
    // パイプライン管理（簡易的な保持）
    std::unique_ptr<ComputePipeline> m_raytracePipeline;
};