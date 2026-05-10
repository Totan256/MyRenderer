#pragma once
#include "RHIcommon.hpp"

#include <cstdint>

namespace rhi {
    class Resource {
    public:
        virtual ~Resource() = default;
        virtual bool isImage() const = 0;
        virtual ResourceState getCurrentState() const = 0;
        virtual ShaderStage getCurrentStage() const = 0;
        virtual void setState(ResourceState state, ShaderStage stage) = 0;
    };

    class Buffer : public Resource {
    public:
        virtual ~Buffer() = default;
        virtual void* map() = 0;
        virtual void unmap() = 0;
        virtual size_t getSize() const = 0;
        virtual uint32_t getBindlessIndex() const = 0;
        bool isImage() const override { return false; }
    };

    class Image : public Resource {
    public:
        virtual ~Image() = default;
        virtual uint32_t getBindlessIndex() const = 0;
        bool isImage() const override { return true; }
    };
}