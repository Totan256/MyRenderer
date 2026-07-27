// ==========================================
// Data Structures
// ==========================================
struct VertexPosition { vec4 position; };

struct VertexAttributes { vec4 normal; vec2 uv; vec2 padding; };

struct SceneGlobals {
    vec4 camPos_time;
    vec4 camTarget_numIdx;
    vec4 resolution_fov;
};

// --- BVH Node Structure ---
struct BVHNode {
    vec3 aabbMin;
    uint leftChildOrPrimitiveOffset;
    vec3 aabbMax;
    uint rightChildOrPrimitiveCount;
};