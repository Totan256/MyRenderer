#pragma once
#include <memory>
#include <string>
#include <functional>
#include "rhi/Resource.hpp"
#include "RHIForward.hpp"
#include "RHIcommon.hpp"
#include "RenderGraph.hpp"
#include "CommandList.hpp"
#include "UploadManager.hpp"

namespace rhi {
    class Device {
    public:
        virtual ~Device() = default;

        virtual void initialize() = 0;
        
        // フレーム管理
        virtual void beginFrame() = 0;
        virtual void endFrame() = 0;
        virtual uint64_t getCurrentFrame() const = 0;

        // リソース作成ファクトリ
        virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc) = 0;
        virtual std::unique_ptr<Image> createImage(const ImageDesc& desc) = 0;
        virtual std::unique_ptr<RenderGraph> createRenderGraph() = 0; // 必要に応じて追加
        virtual std::unique_ptr<CommandList> createCommandList() = 0;

        // アップロードマネージャーの取得
        virtual UploadManager* getUploadManager() = 0;

        // 削除キューへの登録 (内部用)
        virtual void enqueueDeletion(std::function<void()>&& deletionFunc) = 0;
    };
}