#version 460

// Screen-space rain: three depth bands with different streak length, width,
// speed and alpha. Tinted by the sky so it reads as water, not white scratches.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 camPosTime;
    vec4 sunDirCov;
    vec4 params;
    vec4 env;     // x storm, y wetness, z rain, w cloud base
    vec4 screen;  // xy resolution, zw 1/resolution
    vec4 lightDir;// xyz flashlight direction, w range
} ubo;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// One band of falling streaks. `len` in pixels, `wid` in pixels.
float band(vec2 px, float t, float len, float wid, float density, float speed, float seed) {
    vec2 q = px / vec2(len * 0.55, len);
    q.x += q.y * 0.30;                 // wind slant
    q.y += t * speed / len;            // fall

    vec2 id = floor(q);
    vec2 f = fract(q);

    if (hash12(id + seed) > density) return 0.0;

    float rx = hash12(id * 1.37 + seed + 11.0);
    float dx = abs(f.x - rx) * len * 0.55;
    float w = wid * (0.55 + 0.9 * hash12(id + seed + 5.0));

    float a = smoothstep(w, 0.0, dx);
    // tapered tail: brightest at the head (bottom of the cell), fading upward
    a *= smoothstep(0.0, 0.9, 1.0 - f.y) * smoothstep(1.0, 0.72, 1.0 - f.y);
    return a;
}

void main() {
    float rain = clamp(ubo.env.z, 0.0, 1.0);
    if (rain <= 0.002) discard;

    float t = ubo.camPosTime.w;
    vec2 px = gl_FragCoord.xy;

    // near band: long, thick, fast, brighter
    float nearA = band(px, t, 150.0, 1.6, 0.030, 1500.0, 1.0);
    // mid band
    float midA = band(px, t, 62.0, 1.0, 0.055, 620.0, 17.0);
    // far band: short, thin, slow, dim
    float farA = band(px, t, 22.0, 0.7, 0.085, 220.0, 41.0);

    float a = nearA * 0.22 + midA * 0.12 + farA * 0.06;
    a *= rain;
    a = clamp(a, 0.0, 0.45);
    if (a < 0.001) discard;

    // rain scatters scene light: warm toward the sun, cool away from it
    float sunFace = clamp(dot(normalize(ubo.sunDirCov.xyz), vec3(0.0, 0.35, -0.94)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(vec3(0.30, 0.35, 0.44), vec3(0.95, 0.92, 0.86), sunFace * 0.55);
    col *= mix(1.0, 0.55, ubo.env.x);       // dark storm: no white scratches
    col += vec3(0.55, 0.62, 0.85) * ubo.params.z * 0.8;
    col += (hash12(px + fract(t) * 13.0) - 0.5) * 0.02;

    outColor = vec4(col, a);
}
