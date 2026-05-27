#pragma once
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <variant>
#include <map>
#include <memory>
#include <optional>
#include "RHIcommon.hpp"
#include "RHIForward.hpp"
#include "rhi/Resource.hpp"
#include "rhi/CommandList.hpp"
#include "utils/StringHash.hpp"

namespace rhi {
    using ResourceHandle = uint32_t;
    const ResourceHandle InvalidResource = 0xFFFFFFFF;

    enum class CullMode { None, Front, Back, FrontAndBack };
    enum class CompareOp { Less, LessOrEqual, Greater, GreaterOrEqual, Equal, Always, NotEqual };

    class BindGroup {
    public:
        BindGroup(const std::vector<ResourceRequirement>& reqs) : requirements(reqs) {}
        std::vector<ResourceRequirement> requirements;
    };
    
    class DispatchObject {
    public:
        virtual ~DispatchObject() = default;
        virtual DispatchObject& updateConstantRaw(uint32_t offset, const void* data, size_t size) = 0;
        virtual DispatchObject& updateSize(uint32_t x, uint32_t y, uint32_t z) = 0;
        virtual DispatchObject& read(ResourceHandle handle) = 0;
        virtual DispatchObject& write(ResourceHandle handle) = 0;
        virtual DispatchObject& readUniform(ResourceHandle handle) = 0;
        virtual DispatchObject& setUniformRaw(StringHash nameHash, const void* data, size_t size) = 0;
        // テンプレートでのヘルパー
        template<typename T>
        DispatchObject& updateConstant(uint32_t offset, const T& value) {
            updateConstantRaw(offset, &value, sizeof(T));
            return *this;
        }
        template<typename T>
        DispatchObject& setUniform(StringHash nameHash, const T& value) {
            static_assert(sizeof(T) % 16 == 0, "Uniform data must be 16-byte aligned");
            return setUniformRaw(nameHash, &value, sizeof(T));
        }
        // Graphics用のDraw，Indirect描画用パラメータ設定
        virtual DispatchObject& setDrawParams(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
        virtual DispatchObject& setIndirectParams(ResourceHandle indirectBuffer, size_t indirectOffset, ResourceHandle countBuffer, size_t countOffset, uint32_t maxDrawCount) = 0;
    };
    

    // コマンド記録用のインターフェース
    class PassBuilder {
    public:
        virtual ~PassBuilder() = default;
        virtual PassBuilder& bind(const BindGroup& desc) = 0;
        virtual PassBuilder& bind(uint32_t offset, ResourceState state) = 0;
        // Compute用メソッド
        virtual DispatchObject& dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
        virtual DispatchObject& dispatchThreads(uint32_t width, uint32_t height, uint32_t depth = 1) = 0;
        virtual PassBuilder& forceBatchBreak() = 0;
        // Graphics用メソッド
        virtual PassBuilder& setDepthFormat(Format format) = 0;
        virtual PassBuilder& setColorFormat(uint32_t attachmentIndex, Format format) = 0;
        virtual PassBuilder& setCullMode(CullMode mode) = 0;
        virtual PassBuilder& setDepthTest(bool enable, CompareOp op = CompareOp::Less) = 0;
        virtual PassBuilder& setDepthWrite(bool enable) = 0;
        virtual PassBuilder& addColorOutput(uint32_t location, ResourceHandle handle, LoadOp loadOp = LoadOp::Clear, StoreOp storeOp = StoreOp::Store, ColorClearValue clearValue = {0,0,0,1}) = 0;
        virtual PassBuilder& setDepthOutput(ResourceHandle handle, LoadOp loadOp = LoadOp::Clear, StoreOp storeOp = StoreOp::Store, DepthClearValue clearValue = {1.0f, 0}) = 0;
        virtual PassBuilder& setTopology(Topology topology) = 0;
        virtual PassBuilder& setFrontFace(FrontFace face) = 0;
        virtual DispatchObject& draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
        virtual DispatchObject& drawIndexedIndirectCount(ResourceHandle indirectBuffer, ResourceHandle countBuffer, uint32_t maxDrawCount) = 0;

    protected:
    };

    struct ResourceRegistration {
        StringHash nameHash = 0;

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
        std::string vertShaderPath; // Graphics用
        std::string fragShaderPath; // Graphics用
        PassType type = PassType::Compute;
        QueueType queueType = QueueType::Compute;
        // パスのシグネチャ (オフセット -> Usage)
        std::map<uint32_t, ResourceState> signature;
        std::vector<ResourceHandle> resourceHandles;
        std::vector<ResourceRequirement> requirements;
        bool forceBatchBreak = false; // このパスの前でバッチを分割するフラグ

        // Graphics用のステート保存
        std::map<uint32_t, Format> colorFormats;
        Format depthFormat = Format::R8G8B8A8_Unorm; // プレースホルダ
        bool hasDepth = false;
        CullMode cullMode = CullMode::Back;
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        
        struct DispatchState {
            uint32_t id;
            std::array<uint8_t, MAX_PUSH_CONSTANT_SIZE> pushData{};
            uint32_t pushDataSize = 0;
            std::map<uint32_t, ResourceHandle> resourceOffsets;
            // 動的Uniformデータを一時保存するマップ
            std::map<uint32_t, std::vector<uint8_t>> dynamicUniforms;

            // struct UBOBinding { uint32_t index; uint32_t offset; };
            // std::map<uint32_t, UBOBinding> uboBindings;
            uint32_t x = 1, y = 1, z = 1;

            // Draw用の状態
            uint32_t vertexCount = 0;
            uint32_t instanceCount = 0;
            uint32_t firstVertex = 0;
            uint32_t firstInstance = 0;
            
            // Indirect描画用の状態
            bool isIndirect = false;
            ResourceHandle indirectBuffer = InvalidResource;
            size_t indirectOffset = 0;
            ResourceHandle countBuffer = InvalidResource;
            size_t countOffset = 0;
            uint32_t maxDrawCount = 0;
        };
        uint32_t localSizeX = 1;
        uint32_t localSizeY = 1;
        uint32_t localSizeZ = 1;
        std::map<StringHash, uint32_t> pushConstantOffsets;
        std::deque<DispatchState> dispatchStates;

        std::function<void(rhi::CommandList&)> callback; // Callback Pass用

        struct ColorAttachmentInfo {
            uint32_t location;
            ResourceHandle handle;
            LoadOp loadOp;
            StoreOp storeOp;
            ColorClearValue clearValue;
        };
        struct DepthAttachmentInfo {
            ResourceHandle handle;
            LoadOp loadOp;
            StoreOp storeOp;
            DepthClearValue clearValue;
        };
        std::vector<ColorAttachmentInfo> colorAttachments;
        std::optional<DepthAttachmentInfo> depthAttachment;
        Topology topology = Topology::TriangleList;
        FrontFace frontFace = FrontFace::CounterClockwise;
        
    };

    // レンダーグラフ本体
    class RenderGraph {
    public:
        virtual ~RenderGraph() = default;

        // 外部の物理リソースを登録（スワップチェーンなど）
        virtual ResourceHandle importResource(Resource* res, StringHash nameHash = 0) = 0;
        virtual ResourceHandle createImage(const ImageDesc& desc, StringHash nameHash = 0) = 0;
        virtual ResourceHandle createBuffer(const BufferDesc& desc, StringHash nameHash = 0) = 0;
        BindGroup& createBindGroup(const std::vector<ResourceRequirement>& bindings) {
            m_bindGroups.push_back(std::make_unique<BindGroup>(bindings));
            return *m_bindGroups.back();
        }
        virtual PassBuilder& addPass(const std::string& name, const std::string& shaderPath, QueueType queueType = QueueType::Compute) = 0;
        virtual PassBuilder& addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) = 0;
        virtual void addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType) = 0;

        // バリアの計算
        virtual void compile() = 0;

        // 実際のコマンド発行
        virtual void execute(const std::vector<SemaphoreHandle>& waitSemaphores = {}) = 0;

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