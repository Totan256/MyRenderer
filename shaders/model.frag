#version 460

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

void main() {
    // 簡易的なカメラ方向からの平行光源
    vec3 lightDir = normalize(vec3(0.5, 0.5, 1.0));
    
    // 法線を用いた内積計算による拡散光
    float diffuse = max(dot(normalize(inNormal), lightDir), 0.0);
    
    // うさぎのベースカラー（白マットの石膏風）
    vec3 baseColor = vec3(0.85, 0.85, 0.85);
    
    // アンビエント（環境光）を少し足して陰が完全に真っ黒になるのを防ぐ
    vec3 finalColor = baseColor * (diffuse * 0.7 + 0.3);
    
    outColor = vec4(finalColor, 1.0);
}