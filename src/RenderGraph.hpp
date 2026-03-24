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
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;
    // その他 bindPipeline, pushConstants など
};// Todo addPass->addPassができないようにしとく

// レンダーグラフ本体
class RenderGraph {
public:
    virtual ~RenderGraph() = default;

    // パスの登録（この時点では実行されず，記録のみ）
    virtual PassBuilder& addPass(const PassTemplate& proto, const std::vector<rhi::Image*>& resources) = 0;

    // バリアの計算（アルゴリズム）
    virtual void compile() = 0;

    // 実際のコマンド発行
    virtual void execute(rhi::CommandList& cmd) = 0;
};
