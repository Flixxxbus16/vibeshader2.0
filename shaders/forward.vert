#version 450

layout(set = 0, binding = 0) readonly buffer Positions { vec4 p[]; } positions;
layout(set = 0, binding = 1) readonly buffer Normals   { vec4 n[]; } normals;
layout(set = 0, binding = 2) readonly buffer UVs       { vec2 u[]; } uvs;
layout(set = 0, binding = 3) readonly buffer Indices   { uint i[]; } indices;
layout(set = 0, binding = 4) readonly buffer NodeMatrices { mat4 m[]; } nodeMatrices;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec3 camPos;
    uint nodeId;
    uint lodFirst;
    uint lodCount;
    uint materialId;
} push;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vUV;

void main() {
    uint idx = indices.i[push.lodFirst + gl_VertexIndex];
    vec3 pos = positions.p[idx].xyz;
    vec3 nrm = normalize(normals.n[idx].xyz);
    vec2 uv = uvs.u[idx];

    mat4 world = nodeMatrices.m[push.nodeId];
    vNormal = mat3(world) * nrm;
    vUV = uv;
    vec4 wp = world * vec4(pos, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = push.viewProj * wp;
}
