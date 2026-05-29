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

// Bindless Storage Buffer
layout(set = 0, binding = 0) readonly buffer PositionBuffers  { VertexPosition data[]; } positionBuffers[];
layout(set = 0, binding = 0) readonly buffer AttributeBuffers { VertexAttributes data[]; } attributeBuffers[];
layout(set = 0, binding = 0) readonly buffer IndexBuffers     { uint data[]; } indexBuffers[];

layout(push_constant) uniform PushConstants {
    uint ModelPos;
    uint ModelAttr;
    uint ModelIdx;
} pc;

layout(location = 0) out vec3 outNormal;

// --- 簡易的な行列計算関数（テスト用） ---
// 1. 透視投影行列 (VulkanのZ[0,1] および Yダウン に対応)
mat4 perspective(float fovy, float aspect, float zNear, float zFar) {
    float f = 1.0 / tan(fovy / 2.0);
    mat4 res = mat4(0.0);
    res[0][0] = f / aspect;
    res[1][1] = -f; // ★Vulkan用のY軸反転
    res[2][2] = zFar / (zNear - zFar); 
    res[2][3] = -1.0;
    res[3][2] = (zFar * zNear) / (zNear - zFar);
    return res;
}

// 2. ビュー行列 (カメラの位置と向き)
mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 res = mat4(1.0);
    res[0][0] = s.x; res[1][0] = s.y; res[2][0] = s.z;
    res[0][1] = u.x; res[1][1] = u.y; res[2][1] = u.z;
    res[0][2] =-f.x; res[1][2] =-f.y; res[2][2] =-f.z;
    res[3][0] = -dot(s, eye);
    res[3][1] = -dot(u, eye);
    res[3][2] =  dot(f, eye);
    return res;
}

void main() {
    uint vertexId = indexBuffers[nonuniformEXT(pc.ModelIdx)].data[gl_VertexIndex];

    vec3 pos = positionBuffers[nonuniformEXT(pc.ModelPos)].data[vertexId].position.xyz;
    vec3 normal = attributeBuffers[nonuniformEXT(pc.ModelAttr)].data[vertexId].normal.xyz;

    // --- カメラとプロジェクションの設定 ---
    // Z方向に5.0離れたところから、原点(0,0,0)を見る
    vec3 cameraPos = vec3(0.0, 1.0, 5.0);
    vec3 targetPos = vec3(0.0, 0.0, 0.0);
    
    mat4 view = lookAt(cameraPos, targetPos, vec3(0.0, 1.0, 0.0));
    // アスペクト比は 800 / 600
    mat4 proj = perspective(radians(45.0), 800.0 / 600.0, 0.1, 100.0);

    // --- モデルトランスフォーム ---
    mat4 model = mat4(1.0); 

    // MVP行列の合成
    mat4 mvp = proj * view * model;

    // 適用
    gl_Position = mvp * vec4(pos, 1.0);
    
    // 法線もViewとModelの回転を適用しておく (簡易的)
    outNormal = mat3(view * model) * normal;
}