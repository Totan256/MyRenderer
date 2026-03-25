#pragma once
#include <string>
#include <vector>
#include <functional>
#include "RHI.hpp"
#include "RHIcommon.hpp"

// class Image; // 前方宣言

// パスの「設計図」。スロットごとの要求を定義
class PassTemplate {
public:
    PassTemplate(const std::string& name) : m_name(name) {}
    PassTemplate& addSlot(uint32_t slot, rhi::ResourceUsage usage, rhi::ShaderStage stage) {
        m_requirements.push_back({slot, usage, stage});
        return *this;
    }
    std::string getName() const { return m_name; }
    std::vector<rhi::ResourceRequirement> getRequirements() const { return m_requirements; }
    // Todo read/write などのショートカットメソッド
private:
    friend class RenderGraph;
    std::string m_name;
    std::vector<rhi::ResourceRequirement> m_requirements;
};

// コマンド記録用のインターフェース
class PassBuilder {
public:
    virtual ~PassBuilder() = default;

    // パイプラインのセット
    virtual PassBuilder& bindPipeline(rhi::ComputePipeline& pipeline) = 0;

    // Push Constants のセット (生データ)
    virtual PassBuilder& setPushData(uint32_t offset, uint32_t size, const void* data) = 0;

    // Bindless用インデックスのセット
    virtual PassBuilder& setPushResource(uint32_t offset, const rhi::Buffer& resource) = 0;
    virtual PassBuilder& setPushResource(uint32_t offset, const rhi::Image& resource) = 0;

    // 計算実行
    virtual PassBuilder& dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
};// Todo addPass->addPassができないようにしとく

// レンダーグラフ本体
class RenderGraph {
public:
    virtual ~RenderGraph() = default;

    // パスの登録（この時点では実行されず，記録のみ）
    virtual PassBuilder& addPass(const PassTemplate& proto, const std::vector<rhi::Resource*>& resources) = 0;

    // バリアの計算（アルゴリズム）
    virtual void compile() = 0;

    // 実際のコマンド発行
    virtual void execute(rhi::CommandList& cmd) = 0;
};
