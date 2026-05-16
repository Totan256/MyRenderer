#pragma once

namespace rhi {
    enum class BufferUsageFlags : uint32_t {
        None          = 0,
        TransferSrc   = 1 << 0,
        TransferDst   = 1 << 1,
        UniformBuffer = 1 << 2,
        StorageBuffer = 1 << 3,
        VertexBuffer  = 1 << 4,
        IndexBuffer   = 1 << 5,
    };
    inline BufferUsageFlags operator|(BufferUsageFlags a, BufferUsageFlags b) {
        return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlags b) {
        return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline bool operator==(BufferUsageFlags a, BufferUsageFlags b) {
        return (static_cast<uint32_t>(a) == static_cast<uint32_t>(b));
    }
    enum class ImageUsageFlags : uint32_t {
        None                   = 0,
        TransferSrc            = 1 << 0,
        TransferDst            = 1 << 1,
        Sampled                = 1 << 2, // シェーダーでテクスチャとして読み込む用
        Storage                = 1 << 3, // ComputeShader等で読み書きする用 (UAV)
        ColorAttachment        = 1 << 4, // カラーレンダーターゲット用
        DepthStencilAttachment = 1 << 5, // 深度ステンシル用
    };
    inline ImageUsageFlags operator|(ImageUsageFlags a, ImageUsageFlags b) {
        return static_cast<ImageUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline ImageUsageFlags operator&(ImageUsageFlags a, ImageUsageFlags b) {
        return static_cast<ImageUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline bool operator==(ImageUsageFlags a, ImageUsageFlags b) {
        return (static_cast<uint32_t>(a) == static_cast<uint32_t>(b));
    }

    enum class Format {
        R8G8B8A8_Unorm,
        R32G32B32A32_Sfloat,
        // ... 
    };

    struct BufferDesc {
        size_t size = 0;
        BufferUsageFlags usageFlags = BufferUsageFlags::None;
        bool isCpuVisible = false;// CPUからアクセス可能か（StagingBufferなどの用途）
        
        bool isCompatible(const BufferDesc& other) const {
            bool sizeCompatible = size >= other.size;
            bool usageCompatible = (usageFlags & other.usageFlags) == other.usageFlags;
            bool memoryCompatible = (isCpuVisible == other.isCpuVisible);
            return sizeCompatible && usageCompatible && memoryCompatible;
        }
    };
    struct ImageDesc {
        uint32_t width = 1; uint32_t height = 1; uint32_t depth = 1;
        uint32_t mipLevels = 1; uint32_t arrayLayers = 1;
        Format format = Format::R8G8B8A8_Unorm;
        ImageUsageFlags usageFlags = ImageUsageFlags::None;

        bool isCompatible(const ImageDesc& other) const {
            return width == other.width &&
                height == other.height &&
                depth == other.depth &&
                mipLevels == other.mipLevels &&
                arrayLayers == other.arrayLayers &&
                format == other.format &&
                (usageFlags&other.usageFlags) == other.usageFlags;
        }
    };

    enum class PassType {
        Compute,
        Graphics,
        Copy
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
    enum class ResourceState : uint32_t {
        Undefined,
        // Read系
        ConstantBuffer,   // UBO
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
        uint32_t      offset;
        ResourceState state;
        ShaderStage   stage;
    };
}