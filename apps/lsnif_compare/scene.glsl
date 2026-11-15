#ifndef LSNIF_COMPARE_SCENE_GLSL
#define LSNIF_COMPARE_SCENE_GLSL
#extension GL_EXT_nonuniform_qualifier : require

// std140 matches Scene in main.cpp; all fields are 16-byte vec4/uvec4.
struct Scene {
    vec4 camPos, camTarget, resolutionFov, boxMin, boxMax, objectTransform;
    vec4 eps, infer;
    uvec4 dims, levels, widths, offsets0, offsets1;
};
layout(set=0,binding=2) uniform Scenes { Scene data; } scenes[];
layout(set=0,binding=1) uniform writeonly image2D storageImages[];

// The two sides use matching local pixel coordinates and the same half-width camera.
void makeRay(ivec2 localPixel, Scene s, out vec3 origin, out vec3 direction) {
    float sideWidth = float((int(s.resolutionFov.x) + 1) / 2);
    vec2 uv = ((vec2(localPixel) + 0.5) / vec2(sideWidth,s.resolutionFov.y)) * 2.0 - 1.0;
    uv.y = -uv.y;
    vec3 forward = normalize(s.camTarget.xyz - s.camPos.xyz);
    vec3 right = normalize(cross(forward, vec3(0,1,0)));
    vec3 up = cross(right,forward);
    vec3 worldDir = normalize(forward + right * uv.x * sideWidth / s.resolutionFov.y * tan(s.resolutionFov.z*0.5)
        + up * uv.y * tan(s.resolutionFov.z*0.5));
    // Do not renormalize local direction: t stays in world-ray distance units.
    float c=cos(s.camTarget.w), sn=sin(s.camTarget.w);
    vec3 relative=s.camPos.xyz-s.objectTransform.xyz;
    origin=vec3(c*relative.x-sn*relative.z,relative.y,sn*relative.x+c*relative.z)/s.objectTransform.w;
    direction=vec3(c*worldDir.x-sn*worldDir.z,worldDir.y,sn*worldDir.x+c*worldDir.z)/s.objectTransform.w;
}
vec3 normalColor(vec3 normal) { return normalize(normal)*0.5+0.5; }
const vec3 background = vec3(0.1,0.1,0.15);
#endif
