#version 460
// banjo-dk ubo=0x6 ssbo=0x0 tex=0 texb=0:0:384
// scudrace-nx (2026-11-06): ps2d (r3d_d3d9's tilegen 2D layers; the game's HLSL is in default.xex) by hand for deko3d:
// integer texel reads (texelFetch), the bit table's lookup (row 288 + j, column b: bit j of b) as a shift, the loop left
// at the first opaque pixel. The same words and the same arithmetic on them, the output x c245 as the translation's.
// Replaces XenosRecomp's translation of ps_5B84D2A36E94E30B (11 ms of GPU a frame in the course intro). The header (the
// bindings, the pipeline's specialisation constants) and the tail (alpha test, blend skip, output trim) are the
// translation's. tools/linux/deko3d_chain.sh copies app/shaders_native/*.glsl over glsl_opt before packaging.
#ifndef SPIRV_CROSS_CONSTANT_ID_909
#define SPIRV_CROSS_CONSTANT_ID_909 0u
#define DK_EARLY_Z 0
#else
#define DK_EARLY_Z 1
#endif
#if DK_EARLY_Z
layout(early_fragment_tests) in;
#endif
layout(binding = 0) uniform sampler2D dk_tex_b0;

#ifndef SPIRV_CROSS_CONSTANT_ID_900
#define SPIRV_CROSS_CONSTANT_ID_900 0u
#endif
const uint g_banjoBlendSkip = SPIRV_CROSS_CONSTANT_ID_900;
#ifndef SPIRV_CROSS_CONSTANT_ID_901
#define SPIRV_CROSS_CONSTANT_ID_901 0u
#endif
const uint g_banjoOutputTrim = SPIRV_CROSS_CONSTANT_ID_901;
#ifndef SPIRV_CROSS_CONSTANT_ID_902
#define SPIRV_CROSS_CONSTANT_ID_902 0u
#endif
const uint g_banjoTrimPoison = SPIRV_CROSS_CONSTANT_ID_902;
const bool _80 = (g_banjoBlendSkip == 1u);
const bool _81 = (g_banjoBlendSkip == 2u);
const bool _82 = (g_banjoTrimPoison != 0u);
vec4 _152;

layout(binding = 1, std140) uniform type_ConstantBuffer_RenutUboFloat
{
    vec4 v[256];
} g_UboPixel;

layout(binding = 2, std140) uniform type_ConstantBuffer_RenutUboShared
{
    uvec4 v[85];
} g_UboShared;


layout(location = 0) in vec4 in_var_TEXCOORD0;
layout(location = 0) out vec4 out_var_SV_Target0;

#ifndef SPIRV_CROSS_CONSTANT_ID_903
#define SPIRV_CROSS_CONSTANT_ID_903 0u
#endif
const uint g_banjoAlphaFunc = SPIRV_CROSS_CONSTANT_ID_903;
#define DK_ALPHA_FUNC ((g_banjoAlphaFunc != 0u) ? (g_banjoAlphaFunc - 1u) : g_UboShared.v[34u].w)
#ifndef SPIRV_CROSS_CONSTANT_ID_906
#define SPIRV_CROSS_CONSTANT_ID_906 0u
#endif
const uint g_banjoPsZeroIn = SPIRV_CROSS_CONSTANT_ID_906;
#define DK_IN_in_var_TEXCOORD0 (((g_banjoPsZeroIn & 1u) != 0u) ? vec4(0.0) : in_var_TEXCOORD0)
float dk_sel(bool c, float a, float b) { return mix(b, a, c); }
vec2 dk_sel(bool c, vec2 a, vec2 b) { return mix(b, a, bvec2(c)); }
vec3 dk_sel(bool c, vec3 a, vec3 b) { return mix(b, a, bvec3(c)); }
vec4 dk_sel(bool c, vec4 a, vec4 b) { return mix(b, a, bvec4(c)); }
int dk_sel(bool c, int a, int b) { return mix(b, a, c); }
ivec2 dk_sel(bool c, ivec2 a, ivec2 b) { return mix(b, a, bvec2(c)); }
ivec3 dk_sel(bool c, ivec3 a, ivec3 b) { return mix(b, a, bvec3(c)); }
ivec4 dk_sel(bool c, ivec4 a, ivec4 b) { return mix(b, a, bvec4(c)); }
uint dk_sel(bool c, uint a, uint b) { return mix(b, a, c); }
uvec2 dk_sel(bool c, uvec2 a, uvec2 b) { return mix(b, a, bvec2(c)); }
uvec3 dk_sel(bool c, uvec3 a, uvec3 b) { return mix(b, a, bvec3(c)); }
uvec4 dk_sel(bool c, uvec4 a, uvec4 b) { return mix(b, a, bvec4(c)); }
bool dk_sel(bool c, bool a, bool b) { return mix(b, a, c); }
bvec2 dk_sel(bool c, bvec2 a, bvec2 b) { return mix(b, a, bvec2(c)); }
bvec3 dk_sel(bool c, bvec3 a, bvec3 b) { return mix(b, a, bvec3(c)); }
bvec4 dk_sel(bool c, bvec4 a, bvec4 b) { return mix(b, a, bvec4(c)); }
bool dk_alpha_pass(uint f, float a, float r)
{
    // D3D compare functions are a mask: 1 less, 2 equal, 4 greater (0 never, 7 always); with a NaN only
    // not-equal (5) and always (7) pass.
    uint m = mix(mix(mix(5u, 2u, a == r), 4u, a > r), 1u, a < r);
    return mix((f & m) != 0u, (f & 5u) == 5u, m == 5u);
}
// tg_word(w): the 4 bytes of the tilegen's VRAM word w (byte k in component k), as tg_word's floor(c * 255 + 0.5).
uvec4 tg_word(uint w)
{
    vec4 t = texelFetch(dk_tex_b0, ivec2(int(w & 1023u), int(w >> 10u)), 0);
    return uvec4(floor(vec4(t.z, t.y, t.x, t.w) * 255.0 + 0.5));
}
float tg_self(vec4 v, uint k) { return k == 0u ? v.x : (k == 1u ? v.y : (k == 2u ? v.z : v.w)); }
uint tg_selu(uvec4 v, uint k) { return k == 0u ? v.x : (k == 1u ? v.y : (k == 2u ? v.z : v.w)); }
float tg_mod512(float v) { return v - floor(v / 512.0) * 512.0; }

void main()
{
    vec4 g_on = g_UboPixel.v[0u], g_four = g_UboPixel.v[1u], g_sx = g_UboPixel.v[2u], g_sy = g_UboPixel.v[3u];
    vec4 g_offa = g_UboPixel.v[4u], g_offb = g_UboPixel.v[5u], g_cand = g_UboPixel.v[6u], g_anc = g_UboPixel.v[7u];
    vec4 g_dim = g_UboPixel.v[8u], g_ls = g_UboPixel.v[9u];
    vec2 t = DK_IN_in_var_TEXCOORD0.xy * g_dim.xy;
    float px = floor(t.x), py = floor(t.y);
    vec4 res = vec4(0.0);
    vec4 _1694;
    if (g_dim.z > 0.5) {
        uvec4 dw = tg_word(uint(py * 1024.0 + px));
        _1694 = vec4(dw.z, dw.y, dw.x, dw.w) * g_UboPixel.v[245u].x;   // (M3_2D_GPU_DEBUG: the texture word)
    } else if (py >= 384.0) {
        _1694 = res * g_UboPixel.v[245u].x;
    } else {
        uint ipy = uint(py);
        uvec4 st = tg_word(252928u + ipy);
        bool pon0 = g_on.x + g_on.y > 0.5, pon1 = g_on.z + g_on.w > 0.5;
        bool lsp0 = g_ls.x + g_ls.y > 0.5, lsp1 = g_ls.z + g_ls.w > 0.5;
        for (int it = 0; it < 6; it++) {
            uint pair = it < 3 ? 0u : 1u;
            uint ci = uint(it) - 3u * pair;
            float s = ci == 0u ? g_cand.x : (ci == 1u ? g_cand.y : g_cand.z);
            float xf = px - s;
            if (!(float(ci) < g_cand.w && (pair == 0u ? pon0 : pon1) && xf >= 0.0 && xf < 496.0))
                continue;
            uint x = uint(xf);
            uint halfv = pair == 0u ? st.z + st.w * 256u : st.x + st.y * 256u;
            uint la = pair * 2u;
            float lx0 = tg_mod512(xf + tg_self(g_sx, la));
            if (pair == 0u ? lsp0 : lsp1) lx0 = xf;
            uint j = 15u - uint(floor(lx0 / 32.0));
            uint hb = j < 8u ? (halfv & 255u) : (halfv >> 8u);
            uint L = la + (((hb >> (j & 7u)) & 1u) != 0u ? 0u : 1u);
            bool ok = tg_self(g_on, L) > 0.5;
            if (ok && g_anc.w > 0.5) {
                uint a = tg_selu(tg_word(303104u + ipy * 496u + x), L);
                float sh = a == 0u ? g_anc.x : (a == 1u ? g_anc.y : g_anc.z);
                ok = abs(sh - s) < 0.5;
            }
            if (!ok)
                continue;
            float sxl = tg_self(g_sx, L);
            if (tg_self(g_ls, L) > 0.5) {               // the line's x from the table: 0x0F6000 + L x 0x400 + y x 2
                uint lo = 1007616u + L * 1024u + ipy * 2u;
                uvec4 lb = tg_word(lo >> 2u);
                uint lu = (lo & 3u) < 1u ? lb.x + lb.y * 256u : lb.z + lb.w * 256u;
                sxl = float(lu & 511u);
            }
            uint lx = uint(tg_mod512(xf + sxl));
            uint ly = uint(tg_mod512(py + tg_self(g_sy, L)));
            uint ti = (ly >> 3u) * 64u + (lx >> 3u);
            uint off = 1015808u + L * 8192u + (ti ^ 1u) * 2u; // 0x0F8000 + L x 0x2000 + (i ^ 1) x 2
            uvec4 nb = tg_word(off >> 2u);
            uint e = (off & 3u) < 1u ? nb.x + nb.y * 256u : nb.z + nb.w * 256u;
            if (e == 0u)
                continue;
            uint row = ly & 7u, col = lx & 7u;
            uint e15 = e & 32767u, e14 = e & 16383u;
            uint idx;
            if (tg_self(g_four, L) > 0.5) {
                uvec4 pb = tg_word((e14 * 2u + (e >> 15u)) * 8u + row);
                uint by = tg_selu(pb, 3u - (col >> 1u));
                uint nib = (col & 1u) == 0u ? (by >> 4u) : (by & 15u);
                idx = (e15 & ~15u) + nib;
            } else {
                uint c4 = col & 3u;
                uint o8 = e14 * 64u + row * 8u + (col - c4) + (3u - c4);   // col ^ 3
                idx = (e15 & ~255u) + tg_selu(tg_word(o8 >> 2u), o8 & 3u);
            }
            uvec4 pw = tg_word(262144u + idx);             // the palette: 0x100000 / 4 + index
            if (pw.y >= 128u)                               // bit 15 set: transparent
                continue;
            uint c = pw.x + pw.y * 256u;
            uvec3 c5 = uvec3(c & 31u, (c >> 5u) & 31u, (c >> 10u) & 31u);
            vec3 rgb = vec3(c5 * 8u + (c5 >> 2u));
            rgb = clamp(rgb + (pair == 0u ? g_offa.xyz : g_offb.xyz), 0.0, 255.0);
            res = vec4(rgb, 255.0);
            break;
        }
        _1694 = res * g_UboPixel.v[245u].x;
    }
    float _1700 = uintBitsToFloat(g_UboShared.v[34u].z);
    if (!((g_banjoAlphaFunc != 0u) ? (dk_sel((DK_ALPHA_FUNC == 0u), false, (dk_sel((DK_ALPHA_FUNC == 1u), (_1694.w < _1700), (dk_sel((DK_ALPHA_FUNC == 2u), (_1694.w == _1700), (dk_sel((DK_ALPHA_FUNC == 3u), (_1694.w <= _1700), (dk_sel((DK_ALPHA_FUNC == 4u), (_1694.w > _1700), (dk_sel((DK_ALPHA_FUNC == 5u), (_1694.w != _1700), (dk_sel((DK_ALPHA_FUNC == 6u), (_1694.w >= _1700), true)))))))))))))) : dk_alpha_pass(g_UboShared.v[34u].w, _1694.w, _1700)))
    {
        discard;
    }
    bool _1727;
    if (_80)
    {
        _1727 = _1694.w <= 0.0;
    }
    else
    {
        _1727 = false;
    }
    bool _1734;
    if (_1727)
    {
        _1734 = !any(isnan(_1694.xyz));
    }
    else
    {
        _1734 = false;
    }
    bool _1743;
    if (!_1734)
    {
        bool _1742;
        if (_81)
        {
            _1742 = all(lessThanEqual(_1694, vec4(0.0)));
        }
        else
        {
            _1742 = false;
        }
        _1743 = _1742;
    }
    else
    {
        _1743 = true;
    }
    if (_1743)
    {
        discard;
    }
    out_var_SV_Target0 = mix(_1694, vec4(dk_sel(_82, 60000.0, 0.0)), bvec4((g_banjoOutputTrim & 1u) != 0u, (g_banjoOutputTrim & 2u) != 0u, (g_banjoOutputTrim & 4u) != 0u, (g_banjoOutputTrim & 8u) != 0u));
}


