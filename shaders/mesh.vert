#version 460

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 camPosTime;
    vec4 sunDirCov;
    vec4 params;
} ubo;

void main() {
    vWorld = aPos;
    vNormal = aNormal;
    vUV = aUV;
    vColor = aColor;
    gl_Position = ubo.viewProj * vec4(aPos, 1.0);
}
