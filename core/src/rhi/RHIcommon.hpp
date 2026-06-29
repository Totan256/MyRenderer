#pragma once
#include <cstdint>

namespace rhi {
    using SemaphoreHandle = void*;// 同期用の抽象ハンドル
    
    enum class PipelineStage {
        TopOfPipe,
        BottomOfPipe,
        ComputeShader,
        AllGraphics,
        Transfer
    };
    
    struct SwapchainConfig {
        bool enableLowLatency = true;        // true = MAILBOX(Fast V-Sync), false = FIFO(V-Sync ON)
        uint32_t desiredBufferCount = 3;     // 希望するバッファ数 (デフォルトはトリプルバッファリング)
    };

    struct GraphSignal {
        SemaphoreHandle compute;
        SemaphoreHandle graphics;
        SemaphoreHandle transfer; 
    };

    enum class QueueType {
        Graphics,
        Compute,
        Transfer
    };

    struct SyncPoint {
        QueueType queueType;
        uint64_t value;
    };

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
        B8G8R8A8_Unorm,
        B8G8R8A8_SRGB,
        R16G16B16A16_Unorm,
        R32G32B32A32_Sfloat,
        D32_Sfloat,
        D24_Unorm,

    };

    enum class SizeMode { Absolute, SwapchainRelative };
    struct BufferDesc {
        size_t size = 0;
        BufferUsageFlags usageFlags = BufferUsageFlags::None;
        bool isCpuVisible = false;// CPUからアクセス可能か（StagingBufferなどの用途）
        SizeMode sizeMode = SizeMode::Absolute;
        float scale = 1.0f; // 相対モード時のスケール
        bool isCompatible(const BufferDesc& other) const {
            bool sizeCompatible = size >= other.size;
            bool usageCompatible = (usageFlags & other.usageFlags) == other.usageFlags;
            bool memoryCompatible = (isCpuVisible == other.isCpuVisible);
            return sizeCompatible && usageCompatible && memoryCompatible;
        }
        static BufferDesc Absolute(size_t sz, BufferUsageFlags usage, bool cpuVisible = false) {
        BufferDesc desc{};
        desc.size = sz; desc.usageFlags = usage; desc.isCpuVisible = cpuVisible;
        desc.sizeMode = SizeMode::Absolute;
        return desc;
        }
        // 画面ピクセル数に比例したサイズ（例: G-Bufferのレイ数やライトタイル、パーティクル数バッファなど）
        static BufferDesc Relative(float s, BufferUsageFlags usage, bool cpuVisible = false) {
            BufferDesc desc{};
            desc.scale = s; desc.usageFlags = usage; desc.isCpuVisible = cpuVisible;
            desc.sizeMode = SizeMode::SwapchainRelative;
            return desc;
        }
    };
    struct ImageDesc {
        uint32_t width = 1; uint32_t height = 1; uint32_t depth = 1;
        uint32_t mipLevels = 1; uint32_t arrayLayers = 1;
        Format format = Format::R8G8B8A8_Unorm;
        ImageUsageFlags usageFlags = ImageUsageFlags::None;
        SizeMode sizeMode = SizeMode::Absolute;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        bool isCompatible(const ImageDesc& other) const {
            return width == other.width &&
                height == other.height &&
                depth == other.depth &&
                mipLevels == other.mipLevels &&
                arrayLayers == other.arrayLayers &&
                format == other.format &&
                (usageFlags&other.usageFlags) == other.usageFlags;
        }
        // 絶対サイズ用のヘルパー
        static ImageDesc Absolute2D(uint32_t w, uint32_t h, Format fmt, ImageUsageFlags usage) {
            ImageDesc desc{};
            desc.width = w; desc.height = h; desc.format = fmt; desc.usageFlags = usage;
            desc.sizeMode = SizeMode::Absolute;
            return desc;
        }
        // スワップチェーン相対サイズ用のヘルパー
        static ImageDesc Relative2D(uint32_t width,uint32_t height, float sx, float sy, Format fmt, ImageUsageFlags usage) {
            ImageDesc desc{};
            desc.width = (uint32_t)width*sx;
            desc.height = (uint_fast32_t)height*sy;
            desc.scaleX = sx; desc.scaleY = sy; desc.format = fmt; desc.usageFlags = usage;
            desc.sizeMode = SizeMode::SwapchainRelative;
            return desc;
        }
    };

    enum class LoadOp { Load, Clear, DontCare };
    enum class StoreOp { Store, DontCare };
    struct ColorClearValue { float r, g, b, a; };
    struct DepthClearValue { float depth; uint32_t stencil; };
    enum class Topology { PointList, LineList, LineStrip, TriangleList, TriangleStrip };
    enum class FrontFace { CounterClockwise, Clockwise };

    enum class PassType {
        Compute,
        Graphics,
        Copy,
        GenerateMipmaps
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