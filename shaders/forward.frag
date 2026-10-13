#version 450

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec3 camPos;
    uint nodeId;
    uint lodFirst;
    uint lodCount;
    uint materialId;
} push;

struct MaterialData {
    vec4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float emissiveStrength;
    uint baseColorTex;
    uint metallicRoughnessTex;
    uint normalTex;
    uint flags;
    float alphaCutoff;
    uint pad0;
};

layout(set = 0, binding = 5) readonly buffer Materials { MaterialData m[]; } materials;

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialData mat = materials.m[push.materialId];
    vec4 base = mat.baseColorFactor;
    float rough = mat.roughnessFactor;
    float metal = mat.metallicFactor;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(push.camPos - vWorldPos);
    vec3 L = normalize(vec3(0.4, 0.9, 0.5));
    vec3 H = normalize(L + V);

    float ndl = max(dot(N, L), 0.0);
    float ndh = max(dot(N, H), 0.0);
    float ndv = max(dot(N, V), 0.0);

    // simple GGX
    float a = rough * rough + 1e-4;
    float d = ndh * ndh * (a * a - 1.0) + 1.0;
    float D = (a * a) / (3.14159265 * d * d);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = (ndv / (ndv * (1.0 - k) + k)) * (ndl / (ndl * (1.0 - k) + k));
    vec3 F0 = mix(vec3(0.04), base.rgb, metal);
    vec3 spec = D * G * F0 / (4.0 * ndl * ndv + 1e-4);

    vec3 kd = (1.0 - F0) * (1.0 - metal);
    vec3 color = kd * base.rgb * ndl + spec * ndl;

    // ambient
    color += base.rgb * 0.08 * (0.5 + 0.5 * N.y);

    // tone map
    color = color / (color + vec3(1.0));
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), base.a);
}
