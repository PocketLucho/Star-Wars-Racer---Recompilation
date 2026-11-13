#version 450
/* framebuffer blender (CP command 181, bit 24 + rate in bits 16-23): the picture kept from the frame before is
   mixed over the 3D just drawn, new = kept * k + drawn * (1 - k). It is the motion blur of the boost. */
layout(set = 0, binding = 2) uniform sampler2D kept;
layout(push_constant) uniform PC { float k; } pc;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(texelFetch(kept, ivec2(gl_FragCoord.xy), 0).rgb, pc.k); }
