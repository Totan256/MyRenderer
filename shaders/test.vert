#version 460
#extension GL_EXT_nonuniform_qualifier : require

// C++側の構造体とメモリレイアウトを合わせる
struct Vertex {
    vec4 position;
    vec4 color;
};

// バインドレス配列としてのStorage Buffer
layout(set = 0, binding = 0) readonly buffer VertexBuffer {
    Vertex vertices[];
} vertexBuffers[];

// Push Constants
layout(push_constant) uniform PushConstants {
    uint vertexBufferIndex;
} pc;

layout(location = 0) out vec3 outColor;

void main() {
    // 1. Push Constantsからバッファのインデックスを取得
    uint bufferIdx = pc.vertexBufferIndex;

    // 2. gl_VertexIndex を使って直接頂点データをフェッチ (PVP)
    Vertex v = vertexBuffers[nonuniformEXT(bufferIdx)].vertices[gl_VertexIndex];

    // 3. 出力
    gl_Position = vec4(v.position.xyz, 1.0);
    outColor = v.color.rgb;
}