#version 460

// Sky: analytic atmosphere + three parallax 2D cloud layers from one baked sheet.
// Channels: R shape, G density, B detail, A thickness.
// ~6 taps per pixel, no raymarch, no history.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO {
    mat4 invViewProj;
    mat4 viewProj;
    vec4 camPosTime;
    vec4 sunDirCov;   // xyz sun, w coverage
    vec4 params;      // x exposure
    vec4 env;         // x storm, y wetness, z rain, w cloud base
    vec4 screen;
    vec4 lightDir;    // xyz flashlight direction, w range
} ubo;

layout(set = 0, binding = 1) uniform sampler2D cloudTex;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 skyBase(vec3 rd, vec3 sd, float storm) {
    float mu = dot(rd, sd);
    float up = clamp(rd.y, 0.0, 1.0);
    float sunUp = clamp(sd.y * 2.2, 0.0, 1.0);

    vec3 zenith = mix(vec3(0.030, 0.048, 0.095), vec3(0.085, 0.165, 0.360), sunUp);
    vec3 horizon = mix(vec3(0.115, 0.105, 0.110), vec3(0.470, 0.545, 0.640), sunUp);

    zenith = mix(zenith, vec3(0.045, 0.052, 0.062), storm * 0.80);
    horizon = mix(horizon, vec3(0.14, 0.15, 0.16), storm * 0.80);

    float g = pow(1.0 - up, 5.0);
    vec3 col = mix(zenith, horizon, g);

    float cosT = max(mu, 0.0);
    float mie = pow(cosT, 6.0) * 0.35 + pow(cosT, 48.0) * 1.10;
    col += vec3(1.00, 0.70, 0.40) * mie * (0.20 + 0.80 * sunUp) * 0.55 * (1.0 - storm * 0.9);

    float az = pow(cosT, 2.0) * pow(1.0 - up, 8.0);
    col += vec3(0.85, 0.50, 0.26) * az * 0.65 * (1.0 - storm * 0.85);

    float r = acos(clamp(mu, -1.0, 1.0));
    const float R_SUN = 0.0075;
    float disk = 1.0 - smoothstep(R_SUN * 0.80, R_SUN, r);
    float limb = sqrt(max(0.0, 1.0 - (r / R_SUN) * (r / R_SUN)));
    col += vec3(1.0, 0.94, 0.82) * disk * (0.35 + 0.65 * limb) * 60.0 * (1.0 - storm * 0.95);

    return col;
}

// ---------------------------------------------------------------- clouds

float layerAlpha(vec4 s, float cov, float detailAmt) {
    float thresh = 1.0 - cov;
    float aa = max(fwidth(s.r), 1e-4);
    float d = smoothstep(thresh - aa, thresh + 0.16 + aa, s.r);

    // detail only bites near the silhouette: keeps the core solid, breaks the edge
    float edge = smoothstep(0.0, 0.35, s.r) * (1.0 - smoothstep(0.55, 0.95, s.r));
    d *= mix(1.0, 0.72 + 0.56 * s.b, detailAmt * edge);
    return clamp(d, 0.0, 1.0);
}

vec3 cloudColor(vec4 s, float a, float upwind, vec3 rd, vec3 sd, float storm) {
    float cosT = max(dot(rd, sd), 0.0);
    float phase = pow(cosT, 8.0) * 0.5 + pow(cosT, 32.0) * 1.3;

    float thick = s.a;
    float ao = exp(-thick * 1.7);
    float powder = 1.0 - exp(-s.g * 2.4);

    float sunVis = clamp(1.0 - upwind * 0.90, 0.05, 1.0);
    sunVis *= mix(1.0, powder, 0.55);

    // thick core reads as the shaded base, thin edge as the sunlit top
    float body = mix(0.45, 1.0, 1.0 - thick * 0.85);

    vec3 ambient = vec3(0.30, 0.35, 0.45) * ao * 0.30;
    vec3 sun = vec3(1.0, 0.95, 0.88) * sunVis * body * (0.75 + 0.9 * phase);

    vec3 col = ambient + sun;
    col = mix(col, vec3(0.075, 0.085, 0.105), storm * 0.55);

    // silver lining: thin backlit edges
    float rim = pow(1.0 - thick, 2.0) * pow(sunVis, 2.0) * smoothstep(0.55, 0.95, cosT);
    col += vec3(1.0, 0.92, 0.78) * rim * phase * 0.9 * (1.0 - storm * 0.7);
    return col;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 ndc = vUV * 2.0 - 1.0;
    vec4 wp = ubo.invViewProj * vec4(ndc, 1.0, 1.0);
    vec3 ro = ubo.camPosTime.xyz;
    vec3 rd = normalize(wp.xyz / wp.w - ro);

    vec3 sd = normalize(ubo.sunDirCov.xyz);
    float cov = clamp(ubo.sunDirCov.w, 0.0, 1.0);
    float storm = ubo.env.x;
    float time = ubo.camPosTime.w;

    vec3 sky = skyBase(rd, sd, storm);

    // planes, scales, rotation (radians), drift, contrast, brightness
    const float H[3] = float[3](1080.0, 1260.0, 1450.0);
    const float SCALE[3] = float[3](2600.0, 1500.0, 820.0);
    const float ROT[3] = float[3](0.14, -0.12, 0.26);
    const float SPEED[3] = float[3](0.45, 0.85, 1.6);
    const float DETAIL[3] = float[3](0.10, 0.22, 0.38);
    const float BRIGHT[3] = float[3](1.00, 0.90, 0.80);
    const float WEIGHT[3] = float[3](1.00, 0.68, 0.42);

    vec3 hazeCol = mix(vec3(0.40, 0.45, 0.54), vec3(0.10, 0.11, 0.13), storm);

    vec3 accum = vec3(0.0);
    float T = 1.0;

    for (int i = 0; i < 3; ++i) {
        if (rd.y < 0.008) continue;
        float t = (H[i] - ro.y) / rd.y;
        if (t <= 0.0) continue;

        vec3 p = ro + rd * t;
        vec2 wind = vec2(time * SPEED[i] * 6.0, time * SPEED[i] * 3.0);
        vec2 uv = (p.xz + wind) / SCALE[i];

        float ca = cos(ROT[i]), sa = sin(ROT[i]);
        uv = mat2(ca, -sa, sa, ca) * uv;

        vec4 s = texture(cloudTex, uv, -0.35);
        float a = layerAlpha(s, cov, DETAIL[i]) * WEIGHT[i];
        a *= smoothstep(0.004, 0.05, rd.y);
        if (a <= 0.002) continue;

        // one runtime tap upwind of the sun: gives the shadowed side
        vec2 sunUV = sd.xz * 0.045 * (0.35 + s.a);
        float upwind = texture(cloudTex, uv + sunUV, 1.0).g;

        vec3 c = cloudColor(s, a, upwind, rd, sd, storm) * BRIGHT[i];

        // aerial perspective: distant deck fades into the horizon colour
        float haze = 1.0 - exp(-t * 0.00022);
        c = mix(c, hazeCol, haze * 0.80);

        accum += c * a * T;
        T *= (1.0 - a * 0.92);
    }

    accum *= mix(0.70, 1.0, smoothstep(0.10, 0.65, rd.y));
    accum *= mix(1.0, 0.36, storm);

    vec3 col = sky * T + accum;

    float cosT = max(dot(rd, sd), 0.0);
    col += vec3(1.0, 0.90, 0.74) * pow(cosT, 48.0) * (1.0 - T) * 0.45 * (1.0 - storm * 0.6);

    // lightning flash lights the deck from the inside
    col *= (1.0 + ubo.params.z * 1.4);

    col += (hash12(gl_FragCoord.xy + fract(time) * 37.0) - 0.5) * 0.0025;
    col = aces(col * ubo.params.x);
    outColor = vec4(col, 1.0);
}
