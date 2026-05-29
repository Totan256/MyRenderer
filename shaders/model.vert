#version 460
#extension GL_EXT_nonuniform_qualifier : require

struct VertexPosition {
    vec4 position;
};

struct VertexAttributes {
    vec4 normal;
    vec2 uv;
    vec2 padding;
};

// Bindless Storage Buffer 配列 (set = 0, binding = 0)
layout(set = 0, binding = 0) readonly buffer PositionBuffers  { VertexPosition data[]; }  positionBuffers[];
layout(set = 0, binding = 0) readonly buffer AttributeBuffers { VertexAttributes data[]; } attributeBuffers[];
layout(set = 0, binding = 0) readonly buffer IndexBuffers     { uint data[]; }             indexBuffers[];

// C++側のインポート名ハッシュと完全に一致させる
layout(push_constant) uniform PushConstants {
    uint ModelPos;  // "ModelPos"_hash
    uint ModelAttr; // "ModelAttr"_hash
    uint ModelIdx;  // "ModelIdx"_hash
} pc;

layout(location = 0) out vec3 outNormal;

void main() {
    uint vertexId = indexBuffers[nonuniformEXT(pc.ModelIdx)].data[gl_VertexIndex];

    vec3 pos = positionBuffers[nonuniformEXT(pc.ModelPos)].data[vertexId].position.xyz;
    vec3 normal = attributeBuffers[nonuniformEXT(pc.ModelAttr)].data[vertexId].normal.xyz;

    // 3. 画面に収めるための簡易トランスフォーム
    vec3 adjustedPos = pos * 4.0 - vec3(0.0, 6.4, 0.0);
    
    // ★ 追加: アスペクト比の補正 (横幅を縮小して 800:600 の比率に合わせる)
    adjustedPos.x *= 600.0 / 800.0;
    
    // ★ 追加: Z座標を画面の奥へ押し込む (Zクリッピングを回避)
    // VulkanのZは 0.0 ~ 1.0 なので、少しオフセットを足して手前が切れないようにします
    adjustedPos.z += 0.5;

    gl_Position = vec4(adjustedPos, 1.0);
    
    outNormal = normal.xyz;
}