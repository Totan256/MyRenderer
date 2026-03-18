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
    
    enum class ShaderStage {
        Compute = 0x1,
        Vertex  = 0x2,
        Fragment = 0x4,
        All      = 0x7
    };

    enum class ResourceUsage {
        Undefined,
        ComputeWrite, // Compute Shaderでの書き込み (Storage Image)
        TransferSrc,  // コピー元
        TransferDst,  // コピー先
        Sampled       // テクスチャとして読み取り
    };
}