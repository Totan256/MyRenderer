// ==========================================
// Intersection Math
// ==========================================

bool intersectAABB(vec3 ro, vec3 invRd, vec3 aabbMin, vec3 aabbMax, out float tMin) {
    vec3 t0 = (aabbMin - ro) * invRd;
    vec3 t1 = (aabbMax - ro) * invRd;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    tMin = max(max(tmin.x, tmin.y), tmin.z);
    float tMax = min(min(tmax.x, tmax.y), tmax.z);
    return tMax >= tMin && tMax > 0.0;
}

bool intersectTriangle(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float t, out vec2 bary) {
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    vec3 h = cross(rd, edge2);
    float a = dot(edge1, h);
    if (a > -0.0001 && a < 0.0001) return false;
    float f = 1.0 / a;
    vec3 s = ro - v0;
    bary.x = f * dot(s, h);
    if (bary.x < 0.0 || bary.x > 1.0) return false;
    vec3 q = cross(s, edge1);
    bary.y = f * dot(rd, q);
    if (bary.y < 0.0 || bary.x + bary.y > 1.0) return false;
    t = f * dot(edge2, q);
    return t > 0.0001;
}

// ヒートマップの色変換
vec3 getHeatmapColor(int count, int maxCount) {
    float heat = clamp(float(count) / float(maxCount), 0.0, 1.0);
    if (heat < 0.5) {
            // 青 -> 緑
            return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), heat * 2.0);
        } else {
            // 緑 -> 赤
            return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (heat - 0.5) * 2.0);
    }
}