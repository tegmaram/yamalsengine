#version 460
layout(location=0) in vec2 uv;
layout(location=1) in vec2 lighting;
layout(location=0) out vec4 outColor;
void main() {
    float profile = (1.-uv.x*uv.x)*(1.-uv.y)*smoothstep(0.,.12,uv.y);
    outColor = vec4(mix(vec3(.36,.43,.52),vec3(1.,.96,.85),lighting.x),
                    profile*lighting.y*mix(.28,.85,lighting.x));
}
