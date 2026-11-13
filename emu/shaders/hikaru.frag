#version 450
/* Hikaru texture decoding in the shader: TEXRAM is bound as-is (2 layers of 2048x1024 16-bit texels),
 * so IDMA uploads never need a texture cache. Bilinear filtering + mipmap level selection. */
layout(set = 0, binding = 0) uniform usampler2DArray texram;
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) flat in uvec4 v_tex;
layout(location = 3) in vec4 v_fog;
layout(location = 0) out vec4 o_color;

int wrapc(int c, int n, bool wrap, bool mirror) {
    if (!wrap) return clamp(c, 0, n - 1);
    if (mirror) {
        int p = c >= 0 ? c / n : (c - n + 1) / n;
        int m = c - p * n;
        return (p & 1) != 0 ? n - 1 - m : m;
    }
    int m = c % n; return m < 0 ? m + n : m;
}

vec4 decode(uint fmt, uint p) {
    if (fmt == 0u) return vec4(float(p & 31u), float((p >> 5) & 31u), float((p >> 10) & 31u), 0.0) / 31.0 + vec4(0, 0, 0, float(p >> 15));
    if (fmt == 1u) return vec4(float(p & 15u), float((p >> 4) & 15u), float((p >> 8) & 15u), float((p >> 12) & 15u)) / 15.0;
    if (fmt == 4u) { float l = float(p & 255u) / 255.0; return vec4(l, l, l, float(p >> 8) / 255.0); }
    return vec4(1.0, 0.0, 1.0, 1.0);   /* unknown format */
}

vec4 texel(ivec2 c, int w, int h, ivec2 base, int bank) {
    bool wu = (v_tex.x & 0x100u) != 0u, wv = (v_tex.x & 0x200u) != 0u;
    bool mu = (v_tex.x & 0x400u) != 0u, mv = (v_tex.x & 0x800u) != 0u;
    if ((v_tex.x & 15u) == 2u) {
        /* ABGR1111: each 16-bit word holds a 2x2 block of 4-bit texels (bit3 R, bit2 G, bit1 B, bit0 A); the logical
         * texture is 2w x 2h (one word per 2x2 texels: w x h words) starting at base.x/2 */
        ivec2 q = ivec2(wrapc(c.x, 2 * w, wu, mu), wrapc(c.y, 2 * h, wv, mv));
        uint raw = texelFetch(texram, ivec3((base.x / 2 + q.x / 2) & 2047, (base.y + q.y / 2) & 1023, bank), 0).r;
        uint n = (raw >> uint(12 - 4 * ((q.x & 1) + 2 * (q.y & 1)))) & 15u;
        return vec4(float((n >> 3) & 1u), float((n >> 2) & 1u), float((n >> 1) & 1u), float(n & 1u));
    }
    ivec2 p = ivec2(wrapc(c.x, w, wu, mu), wrapc(c.y, h, wv, mv)) + base;
    uint raw = texelFetch(texram, ivec3(p.x & 2047, p.y & 1023, bank), 0).r;
    return decode(v_tex.x & 15u, raw);
}

#ifndef MIPCAP
#define MIPCAP 99
#endif
/* 501 screen-door pattern on 4x4 pixels: n = 0 all .. 8 none, 8+n = the complement of n, bit 4 mirrors.
   The hardware table is not known; an ordered (Bayer) matrix gives the same coverage steps (n/8). */
bool pattern_kill(uint pat) {
    if (pat == 0u) return false;
    const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;   /* output pixels: finer than the original at higher internal resolutions */
    if ((pat & 16u) != 0u) p.x = 3 - p.x;
    int b = bayer[p.y * 4 + p.x], n = int(pat & 15u);
    return n <= 8 ? b < 2 * n : b >= 2 * (n - 8);
}
void main() {
    if (pattern_kill((v_tex.x >> 20) & 31u)) discard;
    vec4 c = v_color;
    if ((v_tex.x & 0x10000u) != 0u) {
        int w0 = 16 << int(v_tex.z & 255u), h0 = 16 << int((v_tex.z >> 8) & 255u);
        /* mip level from screen-space derivatives (level k lives at base + (sheet - base)/2, other bank) */
        vec2 dx = dFdx(v_uv), dy = dFdy(v_uv);
        float rho = max(length(dx), length(dy));
        int maxlvl = min(int(v_tex.z & 255u), int((v_tex.z >> 8) & 255u)) + 3;
        int lvl = clamp(int(floor(log2(max(rho, 1e-6)))), 0, min(maxlvl, MIPCAP));
        if ((v_tex.x & 15u) == 2u) lvl = 0;   /* 1111: no mip chain decoding yet */
        ivec2 base = ivec2(int(v_tex.y & 0xffffu), int(v_tex.y >> 16));
        int bank = int((v_tex.x >> 4) & 1u);
        int w = w0, h = h0;
        for (int i = 0; i < lvl; i++) { base += (ivec2(2048, 1024) - base) / 2; bank ^= 1; w >>= 1; h >>= 1; }
        vec2 uv = v_uv / float(1 << lvl) - 0.5;
        ivec2 i0 = ivec2(floor(uv)); vec2 f = uv - vec2(i0);
        vec4 t00 = texel(i0, w, h, base, bank), t10 = texel(i0 + ivec2(1, 0), w, h, base, bank);
        vec4 t01 = texel(i0 + ivec2(0, 1), w, h, base, bank), t11 = texel(i0 + ivec2(1, 1), w, h, base, bank);
        vec4 t = mix(mix(t00, t10, f.x), mix(t01, t11, f.x), f.y);
        /* punch-through: hard cut at 0.5. Translucent: keep the texture alpha (soft edges: pod shadows, dust,
           glows); only fully clear texels are dropped. Cutting those at 0.5 too made the shadows look pixelated. */
        if ((v_tex.x & 0x20000u) != 0u && t.a < ((v_tex.x & 0x40000u) != 0u ? 1.0 / 512.0 : 0.5)) discard;
        c *= vec4(t.rgb, (v_tex.x & 0x20000u) != 0u ? t.a : 1.0);
    }
    c.rgb = mix(c.rgb, v_fog.rgb, v_fog.a);   /* depth queue (fog), after texturing */
    o_color = c;
}
