#pragma once
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <functional>
#include <variant>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <stdexcept>
#include <cstring>
#include "rhi/RHIcommon.hpp"
#include "rhi/RHIForward.hpp"
#include "rhi/Resource.hpp"
#include "rhi/CommandList.hpp"
#include "utils/StringHash.hpp"

namespace rhi {
    using ResourceHandle = uint32_t;
    const ResourceHandle InvalidResource = 0xFFFFFFFF;

    enum class CullMode { None, Front, Back, FrontAndBack };
    enum class CompareOp { Less, LessOrEqual, Greater, GreaterOrEqual, Equal, Always, NotEqual };

    struct GraphicsState {
        Topology topology = Topology::TriangleList;
        FrontFace frontFace = FrontFace::CounterClockwise;
        CullMode cullMode = CullMode::Back;
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
    };

    struct ResourceRegistration {
        StringHash nameHash = {0};
        std::variant<ImageDesc, BufferDesc> desc;
        
        bool isImage() const {
            if (isImported && physicalResource) return physicalResource->isImage();
            return std::holds_alternative<ImageDesc>(desc);
        }
        
        rhi::Resource* physicalResource = nullptr;
        bool isImported = false;
        
        std::vector<uint32_t> producers;
        std::vector<uint32_t> consumers;
    };

    struct ResourceLifetime {
        uint32_t firstPass = 0xFFFFFFFF;
        uint32_t lastPass  = 0;
    };

    class RenderGraph;

    // =========================================================
    // Pass Base Class
    // =========================================================
    class RenderPass {
    public:
        RenderPass(const std::string& name, PassType type, QueueType queueType, RenderGraph& graph) 
            : m_name(name), m_type(type), m_queueType(queueType), m_graph(graph) {}
        virtual ~RenderPass() = default;

        PassType getType() const { return m_type; }
        QueueType getQueueType() const { return m_queueType; }
        const std::string& getName() const { return m_name; }

        RenderPass& bind(uint32_t offset, ResourceState state) {
            m_signature[offset] = state;
            return *this;
        }

        RenderPass& forceBatchBreak() { m_forceBatchBreak = true; return *this; }
        bool isForceBatchBreak() const { return m_forceBatchBreak; }

        const std::map<uint32_t, ResourceState>& getSignature() const { return m_signature; }
        const std::vector<ResourceHandle>& getResourceHandles() const { return m_resourceHandles; }
        const std::vector<ResourceRequirement>& getRequirements() const { return m_requirements; }



    protected:
        void addRequirement(ResourceHandle handle, ResourceState state, ShaderStage stage) {
            m_resourceHandles.push_back(handle);
            m_requirements.push_back({0, state, stage});
        }
        void CollectRequirements(
            const std::map<uint32_t, rhi::ResourceHandle>& resourceOffsets,
            std::set<rhi::ResourceHandle>& seenHandles,
            rhi::ShaderStage stage) 
        {
            const auto& signature = this->getSignature();
            for (const auto& [offset, handle] : resourceOffsets) {
                auto it = signature.find(offset);
                if (it != signature.end()) {
                    if (seenHandles.insert(handle).second) {
                        this->addRequirement(handle, it->second, stage);
                    }
                }
            }
        }
        friend class RenderGraph;
        virtual void compile(Device& device) = 0;
        virtual void execute(CommandList& cmdList) = 0;
        std::string m_name;
        PassType m_type;
        QueueType m_queueType;
        RenderGraph& m_graph;
        
        std::map<uint32_t, ResourceState> m_signature;
        std::vector<ResourceHandle> m_resourceHandles;
        std::vector<ResourceRequirement> m_requirements;
        bool m_forceBatchBreak = false;
    };

    // =========================================================
    // Resource Binding Builder (CRTP Base)
    // =========================================================
    template <typename Derived, typename PassT>
    class ResourceBindingBuilder {
    public:
        ResourceBindingBuilder(PassT& pass, RenderGraph& graph) 
            : m_pass(pass), m_graph(graph) {}

        Derived& setResource(uint32_t offset, ResourceHandle handle) {
            m_resourceOffsets[offset] = handle;
            return static_cast<Derived&>(*this);
        }

        Derived& read(ResourceHandle handle) {
            uint32_t offset = resolveOffset(handle);
            m_pass.bind(offset, ResourceState::StorageRead);
            return setResource(offset, handle);
        }

        Derived& write(ResourceHandle handle) {
            uint32_t offset = resolveOffset(handle);
            m_pass.bind(offset, ResourceState::StorageWrite);
            return setResource(offset, handle);
        }

        Derived& readUniform(ResourceHandle handle) {
            uint32_t offset = resolveOffset(handle);
            m_pass.bind(offset, ResourceState::ConstantBuffer);
            return setResource(offset, handle);
        }

        template<typename T>
        Derived& setUniform(StringHash nameHash, const T& value) {
            static_assert(sizeof(T) % 16 == 0, "Uniform data must be 16-byte aligned");
            return setUniformRaw(nameHash, &value, sizeof(T));
        }

        std::map<uint32_t, ResourceHandle> m_resourceOffsets;
        std::map<uint32_t, std::vector<uint8_t>> m_dynamicUniforms;

    protected:
        PassT& m_pass;
        RenderGraph& m_graph;

    private:

        Derived& setUniformRaw(StringHash nameHash, const void* data, size_t size) {
            auto it = m_pass.getPushConstantOffsets().find(nameHash);
            if (it == m_pass.getPushConstantOffsets().end()) throw std::runtime_error("Uniform not found in shader push constants!");
            auto& vec = m_dynamicUniforms[it->second];
            vec.resize(size);
            std::memcpy(vec.data(), data, size);
            return static_cast<Derived&>(*this);
        }
        uint32_t resolveOffset(ResourceHandle handle);
    };

    // =========================================================
    // Compute Pass
    // =========================================================
    class ComputePass;
    class ComputeDispatch : public ResourceBindingBuilder<ComputeDispatch, ComputePass> {
    public:
        ComputeDispatch(ComputePass& pass, RenderGraph& graph, uint32_t x, uint32_t y, uint32_t z)
            : ResourceBindingBuilder(pass, graph), m_x(x), m_y(y), m_z(z) {}

        uint32_t m_x, m_y, m_z;
    };

    class ComputePass : public RenderPass {
    public:
        ComputePass(const std::string& name, const std::string& shaderPath, QueueType qType, RenderGraph& graph)
            : RenderPass(name, PassType::Compute, qType, graph), m_shaderPath(shaderPath) {}

        ComputeDispatch& dispatch(uint32_t x, uint32_t y, uint32_t z) {
            m_dispatches.emplace_back(*this, m_graph, x, y, z);
            return m_dispatches.back();
        }

        ComputeDispatch& dispatchThreads(uint32_t width, uint32_t height, uint32_t depth = 1) {
            uint32_t gx = (width + m_localSizeX - 1) / m_localSizeX;
            uint32_t gy = (height + m_localSizeY - 1) / m_localSizeY;
            uint32_t gz = (depth + m_localSizeZ - 1) / m_localSizeZ;
            return dispatch(gx, gy, gz);
        }

        const std::map<StringHash, uint32_t>& getPushConstantOffsets() const { return m_pushConstantOffsets; }

    protected:
        std::string m_shaderPath;
        std::deque<ComputeDispatch> m_dispatches;
        std::map<StringHash, uint32_t> m_pushConstantOffsets;
        uint32_t m_localSizeX = 1, m_localSizeY = 1, m_localSizeZ = 1;
    };

    // =========================================================
    // Graphics Pass
    // =========================================================
    class GraphicsPass;
    class GraphicsDraw : public ResourceBindingBuilder<GraphicsDraw, GraphicsPass> {
    public:
        struct GraphicsDrawState {
            uint32_t vertexCount, instanceCount, firstVertex, firstInstance;
            ResourceHandle indirectBuffer = InvalidResource, countBuffer = InvalidResource;
            size_t indirectOffset = 0, countOffset = 0;
            uint32_t maxDrawCount = 0;
            bool isIndirect = false;
        };
        GraphicsDraw(GraphicsPass& pass, RenderGraph& graph, uint32_t vCount, uint32_t iCount, uint32_t firstV, uint32_t firstI)
            : ResourceBindingBuilder(pass, graph), m_state({vCount, iCount, firstV, firstI}) {}

        GraphicsDraw& setIndirectParams(ResourceHandle indirectBuffer, size_t indirectOffset, ResourceHandle countBuffer, size_t countOffset, uint32_t maxDrawCount) {
            m_state.isIndirect = true; m_state.indirectBuffer = indirectBuffer; m_state.indirectOffset = indirectOffset;
            m_state.countBuffer = countBuffer; m_state.countOffset = countOffset; m_state.maxDrawCount = maxDrawCount;
            return *this;
        }
        const GraphicsDrawState& getState() const { return m_state; }
    protected:
        GraphicsDrawState m_state;
    };

    class GraphicsPass : public RenderPass {
    public:
        struct ColorAttachmentInfo {
            uint32_t location; ResourceHandle handle; LoadOp loadOp; StoreOp storeOp; ColorClearValue clearValue;
        };
        struct DepthAttachmentInfo {
            ResourceHandle handle; LoadOp loadOp; StoreOp storeOp; DepthClearValue clearValue;
        };

        GraphicsPass(const std::string& name, const std::string& vertPath, const std::string& fragPath, RenderGraph& graph)
            : RenderPass(name, PassType::Graphics, QueueType::Graphics, graph), m_vertShaderPath(vertPath), m_fragShaderPath(fragPath) {}

        GraphicsPass& setGraphicsState(const GraphicsState& state = {}) {
            m_graphicsState = state; return *this;
        }

        GraphicsPass& addColorOutput(uint32_t location, ResourceHandle handle, LoadOp loadOp = LoadOp::Clear, StoreOp storeOp = StoreOp::Store, ColorClearValue clearValue = {0,0,0,1}) {
            m_colorAttachments.push_back({location, handle, loadOp, storeOp, clearValue});
            return *this;
        }
        
        GraphicsPass& addColorOutput(StringHash nameHash, ResourceHandle handle, LoadOp loadOp = LoadOp::Clear, StoreOp storeOp = StoreOp::Store, ColorClearValue clearValue = {0,0,0,1}) {
            return addColorOutput(m_outputLocations[nameHash], handle, loadOp, storeOp, clearValue);
        }

        GraphicsPass& setDepthOutput(ResourceHandle handle, LoadOp loadOp = LoadOp::Clear, StoreOp storeOp = StoreOp::Store, DepthClearValue clearValue = {1.0f, 0}) {
            m_depthAttachment = {handle, loadOp, storeOp, clearValue};
            return *this;
        }

        GraphicsDraw& draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) {
            m_draws.emplace_back(*this, m_graph, vertexCount, instanceCount, firstVertex, firstInstance);
            return m_draws.back();
        }

        GraphicsDraw& drawIndexedIndirectCount(ResourceHandle indirectBuffer, ResourceHandle countBuffer, uint32_t maxDrawCount) {
            auto& d = draw(0, 0);
            d.setIndirectParams(indirectBuffer, 0, countBuffer, 0, maxDrawCount);
            return d;
        }

        const std::map<StringHash, uint32_t>& getPushConstantOffsets() const { return m_pushConstantOffsets; }

    protected:
        std::string m_vertShaderPath, m_fragShaderPath;
        GraphicsState m_graphicsState;
        std::vector<ColorAttachmentInfo> m_colorAttachments;
        std::optional<DepthAttachmentInfo> m_depthAttachment;
        std::deque<GraphicsDraw> m_draws;
        std::map<StringHash, uint32_t> m_pushConstantOffsets;
        std::map<StringHash, uint32_t> m_outputLocations;
    };

    // =========================================================
    // Copy Pass
    // =========================================================
    class CopyPass : public RenderPass {
    public:
        CopyPass(const std::string& name, ResourceHandle src, ResourceHandle dst, size_t size, QueueType qType, RenderGraph& graph)
            : RenderPass(name, PassType::Copy, qType, graph), m_src(src), m_dst(dst), m_size(size) {}
    protected:
        ResourceHandle m_src, m_dst;
        size_t m_size;
    };

    // =========================================================
    // Render Graph
    // =========================================================
    class RenderGraph {
    public:
        virtual ~RenderGraph() = default;

        virtual ComputePass& addComputePass(const std::string& name, const std::string& shaderPath, QueueType queueType = QueueType::Compute) = 0;
        virtual GraphicsPass& addGraphicsPass(const std::string& name, const std::string& vertShaderPath, const std::string& fragShaderPath) = 0;
        virtual CopyPass& addCopyPass(const std::string& name, ResourceHandle srcBuffer, ResourceHandle dstBuffer, size_t size, QueueType queueType = QueueType::Transfer) = 0;

        virtual ResourceHandle importResource(Resource* res, StringHash nameHash = {0}) = 0;
        virtual ResourceHandle createImage(const ImageDesc& desc, StringHash nameHash = {0}) = 0;
        virtual ResourceHandle createBuffer(const BufferDesc& desc, StringHash nameHash = {0}) = 0;
        
        virtual const ResourceRegistration& getRegistration(ResourceHandle handle) const = 0;
        virtual uint32_t getPhysicalIndex(ResourceHandle handle) = 0;
        virtual Device& getDevice() = 0;

        virtual void compile() = 0;
        virtual void execute(const std::vector<SemaphoreHandle>& waitSemaphores = {}) = 0;

    protected:
        void compilePass(RenderPass* pass, Device& device) { pass->compile(device); }
        void executePass(RenderPass* pass, CommandList& cmdList) { pass->execute(cmdList); }
        bool isWriteUsage(rhi::ResourceState state);
        std::vector<uint32_t> getSortPasses(std::vector<uint32_t> passIndices);
        void calculateLifetimes(const std::vector<uint32_t>& sortedPassIndices);
        
        std::vector<ResourceRegistration> m_resourceRegistry;
        std::vector<ResourceLifetime> m_resourceLifetimes;
        std::vector<std::unique_ptr<RenderPass>> m_passes;
    };

    // Inline Implementations for Builder
    template <typename Derived, typename PassT>
    uint32_t ResourceBindingBuilder<Derived, PassT>::resolveOffset(ResourceHandle handle) {
        StringHash nameHash = m_graph.getRegistration(handle).nameHash;
        const auto& offsets = m_pass.getPushConstantOffsets();
        auto it = offsets.find(nameHash);
        if (it == offsets.end()) throw std::runtime_error("Resource name not found in shader push constants!");
        return it->second;
    }
}