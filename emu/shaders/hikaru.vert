#version 450
layout(location = 0) in vec4 a_clip;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_tex;
layout(location = 4) in vec4 a_fog;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) flat out uvec4 v_tex;
layout(location = 3) out vec4 v_fog;
void main() {
    gl_Position = a_clip;
    v_color = a_color;
    v_uv = a_uv;
    v_tex = a_tex;
    v_fog = a_fog;
}
