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
    // 1. gl_VertexIndex からこのポリゴン頂点の本来の頂点IDをインデックスバッファより取得
    uint vertexId = indexBuffers[nonuniformEXT(pc.ModelIdx)].data[gl_VertexIndex];

    // 2. 頂点IDを用いて、それぞれのストリームバッファから位置と法線をフェッチ
    vec3 pos = positionBuffers[nonuniformEXT(pc.ModelPos)].data[vertexId].position.xyz;
    vec3 normal = attributeBuffers[nonuniformEXT(pc.ModelAttr)].data[vertexId].normal.xyz;

    // 3. 画面に収めるための簡易トランスフォーム (うさぎのスケール・位置調整用)
    // スタンフォード・バニーは通常、原点付近にあり高さが $0.2$ 弱の小さなモデルです。
    // カメラ行列が未実装の間は、シェーダー側で少し拡大・移動して調整します。
    vec3 adjustedPos = pos * 4.0 - vec3(0.0, 0.4, 0.0);
    gl_Position = vec4(adjustedPos, 1.0);
    
    outNormal = normal.xyz;
}