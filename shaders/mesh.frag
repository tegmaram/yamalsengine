#version 460

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 camPosTime;
    vec4 sunDirCov;
    vec4 params;
    vec4 env;     // x storm, y wetness, z rain, w cloud base
    vec4 screen;
    vec4 lightDir; // xyz flashlight direction, w range
} ubo;

layout(set = 0, binding = 1) uniform sampler2D cloudTex;

const vec3 SUN_COLOR = vec3(1.0, 0.80, 0.58) * 12.0;

// Same sheet as sky.frag: R = shape, B = detail. One tap + mip chain.
float cloudShadow(vec3 wp, vec3 sd, float cov, float storm, float base) {
    if (sd.y < 0.04) return 0.0;
    float dist = (base - wp.y) / sd.y;
    if (dist <= 0.0) return 0.0;
    vec3 p = wp + sd * dist;
    vec2 uv = p.xz / 2600.0;
    float m = texture(cloudTex, uv).r;
    return smoothstep(1.0 - cov, 1.0 - cov + 0.115, m);
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 sd = normalize(ubo.sunDirCov.xyz);
    float cov = clamp(ubo.sunDirCov.w, 0.0, 1.0);
    float storm = ubo.env.x;
    float wetness = ubo.env.y;
    float base = ubo.env.w;
    float t = ubo.camPosTime.w;
    vec3 cam = ubo.camPosTime.xyz;

    // wetness only affects up-facing surfaces, and dries from the outside in
    float upness = smoothstep(0.35, 0.85, n.y);
    float wet = clamp(wetness * upness, 0.0, 1.0);

    float ndl = max(dot(n, sd), 0.0);
    float shadow = 1.0 - mix(0.82, 0.95, storm) * cloudShadow(vWorld, sd, cov, storm, base);

    vec3 sunCol = SUN_COLOR * mix(1.0, 0.55, storm);
    vec3 direct = sunCol * ndl * shadow * 0.055;

    vec3 ambSky = vec3(0.210, 0.245, 0.305) * (1.0 - 0.55 * cov) * mix(1.0, 0.70, storm);
    vec3 ambGround = vec3(0.080, 0.074, 0.064) * mix(1.0, 0.80, storm);
    vec3 ambient = mix(ambGround, ambSky, n.y * 0.5 + 0.5);

    vec3 albedo = vColor.rgb * mix(1.0, 0.42, wet);
    vec3 col = albedo * (direct + ambient);

    // camera lantern: keeps the maze readable without a real spotlight pass
    vec3 toCam = cam - vWorld;
    float camDist = length(toCam);
    float ndv = max(dot(n, toCam / max(camDist, 1e-3)), 0.0);
    float lamp = 5.0f / (1.5f + camDist * camDist);
    col += albedo * lamp * (0.25 + 0.75 * ndv) * 0.45;

    // ---- flashlight (F) ----
    float fl = ubo.params.w;
    if (fl > 0.01) {
        vec3 Lf = ubo.lightDir.xyz;
        vec3 toFrag = toCam / max(camDist, 1e-3);   // fragment -> eye (points back)
        float spot = smoothstep(0.86, 0.965, dot(-toFrag, Lf)); // eye -> fragment vs beam
        float ndlF = max(dot(n, toFrag), 0.0);      // normal towards the lamp
        float atten = 8.0 / (1.0 + camDist * camDist * 0.30);
        col += albedo * vec3(1.0, 0.96, 0.86) * spot * ndlF * atten * fl;
        // retro-reflective specular: the lamp sits at the eye
        float specF = pow(max(dot(n, toFrag), 0.0), 64.0) * spot * atten * fl * wet;
        col += vec3(1.0, 0.97, 0.90) * specF * 0.7;
    }

    // ---- lightning flash (no visible bolt, just light) ----
    col *= 1.0 + ubo.params.z * 2.0; // lightning: scales existing light, keeps contrast

    // wet sheen: specular lobe + sky reflection at grazing angles
    vec3 V = normalize(toCam);
    vec3 H = normalize(V + sd);
    float spec = pow(max(dot(n, H), 0.0), 96.0) * shadow;
    col += sunCol * spec * 0.9 * wet;
    float fres = pow(1.0 - max(dot(n, V), 0.0), 5.0);
    col += vec3(0.20, 0.26, 0.36) * fres * wet * 0.35;

    // fog thickens in the storm
    float dist = length(vWorld - cam);
    float fogAmt = 1.0 - exp(-pow(dist * mix(0.014, 0.030, storm), 2.0));
    vec3 fogCol = vec3(0.105, 0.115, 0.140) * (1.0 - 0.3 * cov) * mix(1.0, 0.65, storm);
    col = mix(col, fogCol, clamp(fogAmt, 0.0, 1.0));

    col += (hash12(gl_FragCoord.xy + fract(t) * 37.0) - 0.5) * 0.002;
    col = aces(col * ubo.params.x);
    outColor = vec4(col, 1.0);
}
