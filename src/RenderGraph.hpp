#pragma once
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <variant>
#include <map>
#include <memory>
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "rhi/Resource.hpp"
#include "rhi/CommandList.hpp"

namespace rhi {
    using ResourceHandle = uint32_t;
    const ResourceHandle InvalidResource = 0xFFFFFFFF;

    class BindGroup {
    public:
        BindGroup(const std::vector<ResourceRequirement>& reqs) : requirements(reqs) {}
        std::vector<ResourceRequirement> requirements;
    };
    
    class DispatchObject {
    public:
        virtual ~DispatchObject() = default;
        virtual DispatchObject& updateConstantRaw(uint32_t offset, const void* data, size_t size) = 0;
        virtual DispatchObject& updateResource(uint32_t offset, ResourceHandle handle) = 0;

        virtual DispatchObject& setStaticUniform(uint32_t offset, ResourceHandle handle) = 0;
        virtual DispatchObject& setUniformRaw(uint32_t offset, const void* data, size_t size) = 0;

        virtual DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) = 0;

        // テンプレートでのヘルパー
        template<typename T>
        DispatchObject& updateConstant(uint32_t offset, const T& value) {
            updateConstantRaw(offset, &value, sizeof(T));
            return *this;
        }
        template<typename T>
        DispatchObject& setUniform(uint32_t offset, const T& value) {
            static_assert(sizeof(T) % 16 == 0, "Uniform data must be 16-byte aligned for std140.");
            return setUniformRaw(offset, &value, sizeof(T));
        }
    };
    

    // コマンド記録用のインターフェース
    class PassBuilder {
    public:
        virtual ~PassBuilder() = default;
        virtual PassBuilder& bind(const BindGroup& desc) = 0;
        virtual PassBuilder& bind(uint32_t offset, ResourceState state) = 0;
        // 計算実行
        virtual DispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    protected:
    };

    struct ResourceRegistration {
        std::variant<ImageDesc, BufferDesc> desc;    // 作成用
        bool isImage() const {
            if (isImported && physicalResource) {
                return physicalResource->isImage();
            }
            return std::holds_alternative<ImageDesc>(desc);
        }
        
        rhi::Resource* physicalResource = nullptr; // import用
        bool isImported = false;
        
        // 依存関係解析用
        std::vector<uint32_t> producers; // このリソースに書き込むパスのIndex
        std::vector<uint32_t> consumers; // このリソースを読み込むパスのIndex

    };

    struct ResourceLifetime {
        uint32_t firstPass = 0xFFFFFFFF; // 最初に登場する実行順インデックス
        uint32_t lastPass  = 0;          // 最後に登場する実行順インデックス
    };

    struct LogicalPass {// Tod最適化検討
        std::string name;
        std::string shaderPath;
        PassType type = PassType::Compute;
        // パスのシグネチャ (オフセット -> Usage)
        std::map<uint32_t, ResourceState> signature;
        std::vector<ResourceHandle> resourceHandles;
        std::vector<ResourceRequirement> requirements;
        
        struct DispatchState {
            uint32_t id;
            std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> pushData{};
            uint32_t pushDataSize = 0;
            std::map<uint32_t, ResourceHandle> resourceOffsets;
            // 動的Uniformデータを一時保存するマップ
            std::map<uint32_t, std::vector<uint8_t>> dynamicUniforms;

            // struct UBOBinding { uint32_t index; uint32_t offset; };
            // std::map<uint32_t, UBOBinding> uboBindings;
            uint32_t x, y, z;
        };
        std::vector<DispatchState> dispatchStates;
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
        BindGroup& createBindGroup(const std::vector<ResourceRequirement>& bindings) {
            m_bindGroups.push_back(std::make_unique<BindGroup>(bindings));
            return *m_bindGroups.back();
        }
        virtual PassBuilder& addPass(const std::string& name, const std::string& shaderPath) = 0;
        virtual void addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size) = 0;

        // バリアの計算
        virtual void compile() = 0;

        // 実際のコマンド発行
        virtual void execute(CommandList& cmd) = 0;

        virtual uint32_t getPhysicalIndex(ResourceHandle handle) = 0;
    private:
    protected:
        std::vector<std::unique_ptr<BindGroup>> m_bindGroups;
        bool isWriteUsage(rhi::ResourceState state);
        std::vector<uint32_t> getSortPasses(std::vector<uint32_t> passIndices);
        void calculateLifetimes(const std::vector<uint32_t>& sortedPassIndices);
        
        std::vector<ResourceRegistration> m_resourceRegistry;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        std::deque<LogicalPass> m_logicalNodes;

    };
}