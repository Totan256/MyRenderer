#pragma once

namespace rhi {
    enum class BufferUsage {
        VertexBuffer,
        IndexBuffer,
        UniformBuffer,
        StorageBuffer
    };

    enum class Format {
        R8G8B8A8_Unorm,
        R32G32B32A32_Sfloat,
        // ... 
    };

    struct BufferDesc {
        size_t size;
        BufferUsage usage;
        bool isCpuVisible;
    };
    
    enum class ShaderStage : uint64_t {
        None          = 0,
        Top           = 1ULL << 0, // 全体の先頭
        DrawIndirect  = 1ULL << 1,
        Vertex        = 1ULL << 2,
        Fragment      = 1ULL << 3,
        Compute       = 1ULL << 4,
        Transfer      = 1ULL << 5,
        EarlyFragment = 1ULL << 6,
        LateFragment  = 1ULL << 7,
        RayTracing    = 1ULL << 8,
        
        AllGraphics   = Vertex | Fragment | EarlyFragment | LateFragment,
        All           = ~0ULL
    };

    // リソースが「何として」使われるか（AccessとLayoutに対応）
    enum class ResourceUsage : uint32_t {
        Undefined,
        // Read系
        ConstantBuffer,
        VertexBuffer,
        IndexBuffer,
        SampledTexture,   // Descriptor 経由の Read
        StorageRead,      // RWTexture/Buffer の Read
        InputAttachment,  // サブパス入力
        DepthStencilRead, // 影マップ読み取り
        
        // Write系
        ColorAttachment,
        DepthStencilWrite,
        StorageWrite,     // RWTexture/Buffer の Write
        
        // その他
        TransferSrc,
        TransferDst,
        Present
    };
    
    struct ResourceRequirement {
        uint32_t      slotIndex;
        ResourceUsage usage;
        ShaderStage   stage;
    };

    class Resource {
    public:
        virtual ~Resource() = default;
        virtual bool isImage() const = 0;
        virtual ResourceUsage getCurrentUsage() const = 0;
        virtual ShaderStage   getCurrentStage() const = 0;
        virtual void setState(ResourceUsage usage, ShaderStage stage) = 0;
    };
}