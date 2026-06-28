#pragma once
#include "RHIcommon.hpp"

#include <cstdint>

namespace rhi {
    class Swapchain; // 前方宣言

    class Resource {
    public:
        virtual ~Resource() = default;
        virtual bool isImage() const = 0;
        virtual ResourceState getCurrentState() const = 0;
        virtual ShaderStage getCurrentStage() const = 0;
        ImageDesc getDesc(){ return m_desc; }
        
        // --- 同期状態の追跡 (Timeline Semaphore 用) ---
        void setWriteSync(SyncPoint sp) { m_writeSync = sp; }
        SyncPoint getWriteSync() const  { return m_writeSync; }
        void addReadSync(SyncPoint sp)  { m_readSyncs.push_back(sp); }
        const std::vector<SyncPoint>& getReadSyncs() const { return m_readSyncs; }
        void clearReadSyncs() { m_readSyncs.clear(); }

        virtual void setState(ResourceState state, ShaderStage stage) = 0;
    protected:
        std::vector<SyncPoint> m_readSyncs;
        SyncPoint m_writeSync;
        ImageDesc m_desc;
        
    };

    constexpr size_t WHOLE_SIZE = ~0ULL;

    class Buffer : public Resource {
    public:
        virtual ~Buffer() = default;
        virtual void* map() = 0;
        virtual void unmap() = 0;
        virtual size_t getSize() const = 0;
        virtual uint32_t getBindlessIndex() const = 0;
        bool isImage() const override { return false; }
        virtual void invalidate(size_t offset = 0, size_t size = WHOLE_SIZE) = 0;
    };

    class Image : public Resource {
    public:
        virtual ~Image() = default;
        virtual uint32_t getBindlessIndex() const = 0;
        bool isImage() const override { return true; }
    protected:
        virtual bool isSwapchainImage() const { return false; }
        virtual Swapchain* getSwapchain() const { return nullptr; }
    };
}