#version 460
// A periodic volume of world-space drops. No particle buffers or CPU simulation.
layout(set=0,binding=0) uniform UBO {
    mat4 invViewProj, viewProj; vec4 camPosTime, sunDirCov, params, env, screen, lightDir;
} u;
layout(location=0) out vec2 uv;
layout(location=1) out vec2 lighting;
float random(uint i) {
    i ^= i >> 16; i *= 0x7feb352du; i ^= i >> 15; i *= 0x846ca68bu; i ^= i >> 16;
    return float(i >> 8) / 16777216.0;
}
void main() {
    uint id = uint(gl_InstanceIndex)*4u;
    vec3 size = vec3(16,9,16), eye = u.camPosTime.xyz;
    float speed = mix(5.,8.,random(id+3u));
    vec3 p = vec3(random(id),random(id+1u),random(id+2u))*size;
    p += vec3(.65,-speed,.15)*u.camPosTime.w;
    vec3 delta = mod(p-eye+size*.5,size)-size*.5;
    vec3 ray = normalize(delta);
    float beam = smoothstep(.86,.965,dot(ray,u.lightDir.xyz))*u.params.w;
    lighting = vec2(beam, (1.-smoothstep(5.,8.,length(delta))) * u.env.z);
    const vec2 corners[6] = vec2[](vec2(-1,0),vec2(1,0),vec2(-1,1),vec2(-1,1),vec2(1,0),vec2(1,1));
    uv = corners[gl_VertexIndex];
    vec3 side = normalize(vec3(ray.z,0,-ray.x)+vec3(.00001,0,0));
    vec3 offset = side*uv.x*max(.005,length(delta)*u.screen.w*.85)
                + vec3(-.012,.11+speed*.005,-.003)*uv.y;
    gl_Position = u.viewProj*vec4(eye+delta+offset,1);
}
