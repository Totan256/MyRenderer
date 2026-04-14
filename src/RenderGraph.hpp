#pragma once
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <variant>
#include <map>
#include "RHIcommon.hpp"
#include "RHIForward.hpp"

namespace rhi {
    using ResourceHandle = uint32_t;
    const ResourceHandle InvalidResource = 0xFFFFFFFF;
    
    class DispatchObject {
    public:
        virtual ~DispatchObject() = default;
        virtual DispatchObject& updateConstantRaw(uint32_t slot, const void* data, size_t size) = 0;
        virtual DispatchObject& updateResource(uint32_t slot, ResourceHandle handle) = 0;
        virtual DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) = 0;
        // テンプレートでのヘルパー
        template<typename T>
        DispatchObject& updateConstant(uint32_t slot, const T& value) {
            updateConstantRaw(slot, &value, sizeof(T));
            return *this;
        }
    };
    
    class PassTemplate {
    public:
        PassTemplate(const std::string& name) : m_name(name) {}
        PassTemplate& addSlot(uint32_t slot, ResourceUsage usage, ShaderStage stage) {
            m_requirements.push_back({slot, usage, stage});
            return *this;
        }
        std::string getName() const { return m_name; }
        std::vector<ResourceRequirement> getRequirements() const { return m_requirements; }
        // Todo read/write などのショートカットメソッド
    private:
        friend class RenderGraph;
        std::string m_name;
        std::vector<ResourceRequirement> m_requirements;
    };

    // コマンド記録用のインターフェース
    class PassBuilder {
    public:
        virtual ~PassBuilder() = default;
        // パイプラインのセット
        virtual PassBuilder& bindPipeline(ComputePipeline& pipeline) = 0;
        // Push Constants のセット
        template<typename T>
        PassBuilder& setConstant(uint32_t slot, const T& data) {
            return setConstantRaw(slot, &data, sizeof(T));
        }
        // Bindless用インデックスのセット
        virtual PassBuilder& setResource(uint32_t slot, rhi::ResourceHandle handle) = 0;
        // 計算実行
        virtual DispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    protected:
        virtual PassBuilder& setConstantRaw(uint32_t slot, const void* data, size_t size) = 0;
    };// Todo addPass->addPassができないようにしとく



    // リソースの設計図（仮想リソース作成用）
    


    struct ResourceRegistration {
        std::variant<ImageDesc, BufferDesc> desc;    // 作成用
        bool isImage() const { return std::holds_alternative<ImageDesc>(desc); }
        
        Resource* physicalResource = nullptr; // import用
        bool isImported = false;
        
        // 依存関係解析用
        std::vector<uint32_t> producers; // このリソースに書き込むパスのIndex
        std::vector<uint32_t> consumers; // このリソースを読み込むパスのIndex

    };

    struct ResourceLifetime {
        uint32_t firstPass = 0xFFFFFFFF; // 最初に登場する実行順インデックス
        uint32_t lastPass  = 0;          // 最後に登場する実行順インデックス
    };

    struct LogicalPass {
        std::string name;
        // struct ResourceBinding {
        //     ResourceHandle handle;
        //     uint32_t offset;
        // };
        // std::vector<ResourceBinding> resourceBindings;
        std::vector<ResourceHandle> resourceHandles;
        std::vector<ResourceRequirement> requirements;

        std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> pushData{};
        uint32_t pushDataSize = 0;
        std::map<uint32_t, ResourceHandle> slotValues;
        
        struct DispatchState {
            std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> pushData{};
            uint32_t pushDataSize = 0;
            std::map<uint32_t, ResourceHandle> slotValues;
            uint32_t x, y, z;
        };
        uint32_t nextDispatchId = 0;
        std::map<uint32_t, DispatchState> dispatchStates;

        std::vector<std::function<void(CommandList&)>> commands;
    };

    // レンダーグラフ本体
    class RenderGraph {
    public:
        virtual ~RenderGraph() = default;

        // 外部の物理リソースを登録（スワップチェーンなど）
        virtual ResourceHandle importResource(Resource* res) = 0;

        // グラフ内でのみ使うリソースの予約
        virtual ResourceHandle createImage(const ImageDesc& desc) = 0;
        virtual ResourceHandle createBuffer(const BufferDesc& desc) = 0;

        // パスの登録（この時点では実行されず，記録のみ）
        virtual PassBuilder& addPass(const PassTemplate& proto, const std::vector<ResourceHandle>& resources) = 0;

        // バリアの計算
        virtual void compile() = 0;

        // 実際のコマンド発行
        virtual void execute(CommandList& cmd) = 0;

        virtual uint32_t getPhysicalIndex(ResourceHandle handle) = 0;
    private:
    protected:
        bool isWriteUsage(rhi::ResourceUsage usage);
        std::vector<uint32_t> getSortPasses(std::vector<uint32_t> passIndices);
        void calculateLifetimes(const std::vector<uint32_t>& sortedPassIndices);
        
        std::vector<ResourceRegistration> m_resourceRegistry;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        std::deque<LogicalPass> m_logicalNodes; 
    };
}