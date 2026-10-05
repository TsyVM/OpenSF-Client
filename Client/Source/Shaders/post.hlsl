// The frame after the match is drawn (Game/Render/FramePipe): the picture, drawn at its own size
// into its own targets, goes out to the window scaled to fit (ps_copy). That is all of it unless
// Fidelity is on, which finishes the same picture more finely first: contact shadow where
// surfaces meet (ps_ao*), a glow off the brightest light (ps_prefilter, ps_down, ps_up), a soft
// shoulder, a touch of colour and a vignette (ps_final), smoothed edges (ps_fxaa); and, for the
// player who asks, shafts of light from the map's own sun (ps_shaft*). Each part is the player's
// to set (Settings.hpp FidelitySettings). None of it changes what the map is: its bake, its hour
// and its sky are drawn before any of this.

cbuffer Post : register(b0) {
    float4 Dst;       // target: width, height, 1/width, 1/height
    float4 Src;       // source: the same
    float4 Proj;      // tan(half fov x), tan(half fov y), near, far (centimetres)
    float4 BloomP;    // threshold, knee, intensity
    float4 AoP;       // radius (cm), strength, bias (cm), power
    float4 Grade;     // saturation, contrast, vignette, sharpen
    float4 Finish;    // highlights (0 as drawn, 1 soft, 2 filmic), dither (0 or 1), exposure
    float4 Shaft;     // the sun on the picture (u, v), the shafts' strength
    float4 Counts;    // contact shadow samples, the shafts' reach toward the sun (a part of the way), reflections' strength
    float4 Rect;      // the part of the target a pass is drawn over: x, y, width, height (0..1)
    float4 CamR, CamU, CamF;   // the view's axes in the world (a surface's normal into view space)
    float4 LightPos;  // a light, in view space: xyz (cm), w: how far it reaches
    float4 LightCol;  // rgb times its strength; w: how much of it pools on a surface (the rest only glints)
    float4 LightAux;  // x: how strong a glint is; y: 1 a light that shines all round
    float4 SunV;      // xyz: toward the map's sun (or its moon), in view space; w: its glint's strength
    float4 SunCol;    // rgb: its light
    float4 SunUp;     // xyz: the world's up beside it, in view space (which way up the moon hangs)
    float4 Body;      // the sun or moon in the sky: tan of its radius, tan of its corona's reach, the corona's strength
    float4 BodyCol;   // rgb: its colour; w: 1 the moon (a picture), 0 the sun (a disc)
    float4 BodyUV;    // the moon's phase in its sheet: u, v, width, height
};
Texture2D T0 : register(t0);
Texture2D T1 : register(t1);
Texture2D T2 : register(t2);
Texture2DMS<float> DepthMS : register(t4);
SamplerState Lin : register(s0);
SamplerState Pnt : register(s1);

struct V {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

// One triangle over the whole target.
V vs_full(uint id : SV_VertexID) {
    V o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float lin_depth(float d) { return Proj.z * Proj.w / (Proj.w - d * (Proj.w - Proj.z)); }
float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// Interleaved gradient noise: a per-pixel rotation that a 4x4 blur smooths away.
float ign(float2 p) { return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715)))); }

// ── Out to the window: scaled, sharpened (contrast-adaptive) when asked ───────
float4 ps_copy(V i) : SV_Target {
    float3 c = T0.SampleLevel(Lin, i.uv, 0).rgb;
    if (Grade.w > 0) {
        float2 t = Src.zw;
        float3 n = T0.SampleLevel(Lin, i.uv + float2(0, -t.y), 0).rgb, s = T0.SampleLevel(Lin, i.uv + float2(0, t.y), 0).rgb;
        float3 e = T0.SampleLevel(Lin, i.uv + float2(t.x, 0), 0).rgb, w = T0.SampleLevel(Lin, i.uv + float2(-t.x, 0), 0).rgb;
        float3 mn = min(c, min(min(n, s), min(e, w))), mx = max(c, max(max(n, s), max(e, w)));
        float3 amp = sqrt(saturate(min(mn, 1 - mx) / max(mx, 1e-4)));
        float3 wt = -amp / lerp(8.0, 4.5, Grade.w);
        c = saturate((c + (n + s + e + w) * wt) / (1 + 4 * wt));
    }
    return float4(c, 1);
}

// ── Depth, in centimetres from the eye ────────────────────────────────────────
float ps_linearize(V i) : SV_Target { return lin_depth(T0.SampleLevel(Pnt, i.uv, 0).r); }
float ps_linearize_ms(V i) : SV_Target { return lin_depth(DepthMS.Load(int2(i.pos.xy), 0)); }

// ── Contact shadow (half resolution, from linear depth) ───────────────────────
float3 view_pos(float2 uv, float z) { return float3((uv.x * 2 - 1) * Proj.x * z, (1 - uv.y * 2) * Proj.y * z, z); }

// A half-resolution pixel's centre falls exactly between two full-resolution texels. Direct3D
// always takes the one after; other cards round either way from row to row, and a normal made
// from the wrong neighbours shades whole rows as occluded. So the texel is chosen here.
float2 texel_after(float2 uv) { return (floor(uv * Src.xy + 0.25) + 0.5) * Src.zw; }

float ps_ao(V i) : SV_Target {
    const float2 c = texel_after(i.uv);
    float z = T0.SampleLevel(Pnt, c, 0).r;
    if (z > Proj.w * 0.9) return 1;
    float3 p = view_pos(i.uv, z);
    // The surface's normal from its neighbours, taking the nearer side at an edge.
    float2 t = Src.zw;
    float zr = T0.SampleLevel(Pnt, c + float2(t.x, 0), 0).r, zl = T0.SampleLevel(Pnt, c - float2(t.x, 0), 0).r;
    float zd = T0.SampleLevel(Pnt, c + float2(0, t.y), 0).r, zu = T0.SampleLevel(Pnt, c - float2(0, t.y), 0).r;
    float3 dx = abs(zr - z) < abs(zl - z) ? view_pos(i.uv + float2(t.x, 0), zr) - p : p - view_pos(i.uv - float2(t.x, 0), zl);
    float3 dy = abs(zd - z) < abs(zu - z) ? view_pos(i.uv + float2(0, t.y), zd) - p : p - view_pos(i.uv - float2(0, t.y), zu);
    float3 n = normalize(cross(dx, dy));
    if (dot(n, p) > 0) n = -n;
    float a = ign(i.pos.xy) * 6.2831853;
    float3 r = float3(cos(a), sin(a), 0.3);
    float3 tg = normalize(r - n * dot(r, n));
    float3 bt = cross(n, tg);
    float occ = 0;
    const int N = int(Counts.x);
    [loop] for (int k = 0; k < N; ++k) {
        // A golden-angle spiral over the hemisphere, the samples gathering near the point.
        float f = (k + 0.5) / N;
        float ang = f * 6.2831853 * 3.883;
        float rad = sqrt(f);
        float3 dir = tg * (cos(ang) * rad) + bt * (sin(ang) * rad) + n * sqrt(max(0, 1 - rad * rad));
        float3 s = p + dir * (AoP.x * lerp(0.15, 1.0, f * f));
        float2 suv = float2(s.x / (s.z * Proj.x) * 0.5 + 0.5, 0.5 - s.y / (s.z * Proj.y) * 0.5);
        float sz = T0.SampleLevel(Pnt, suv, 0).r;
        float range = saturate(AoP.x / max(abs(p.z - sz), 1e-3));
        occ += (sz < s.z - AoP.z ? 1.0 : 0.0) * range;
    }
    return pow(saturate(1 - occ / N), AoP.w);
}

// 4x4, weighted by how close in depth each neighbour is (no halo past an edge).
float ps_ao_blur(V i) : SV_Target {
    float zc = T1.SampleLevel(Pnt, i.uv, 0).r;
    float sum = 0, wsum = 0;
    [unroll] for (int y = -2; y < 2; ++y)
        [unroll] for (int x = -2; x < 2; ++x) {
            float2 uv = i.uv + (float2(x, y) + 0.5) * Src.zw;
            float zs = T1.SampleLevel(Pnt, uv, 0).r;
            float w = saturate(1 - abs(zs - zc) / (zc * 0.04 + 5.0)) + 1e-3;
            sum += T0.SampleLevel(Pnt, uv, 0).r * w;
            wsum += w;
        }
    return sum / wsum;
}

// Multiplied onto the map and the soldiers before the smoke, the flashes and the gun in hand are
// drawn, so none of those is shaded by what stands behind it.
float4 ps_ao_apply(V i) : SV_Target {
    float ao = lerp(1.0, T0.SampleLevel(Lin, i.uv, 0).r, AoP.y);
    return float4(ao, ao, ao, 1);
}

// ── Lights on the surface targets ─────────────────────────────────────────────
// The world's solid surfaces as the scene's shaders wrote them (world.hlsl GOut): T0 the depth
// (cm), T1 the surface (normal, shine, roughness), T2 its own colour and how far the sun reaches.
float3 unpack_normal(float2 e) {
    e = e * 2 - 1;
    float3 n = float3(e.x, 1 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0) {
        float2 t = n.xz;
        n.x = (1 - abs(t.y)) * (t.x >= 0 ? 1 : -1);
        n.z = (1 - abs(t.x)) * (t.y >= 0 ? 1 : -1);
    }
    return normalize(n);
}
float3 view_normal(float2 packed) {
    float3 nw = unpack_normal(packed);
    return normalize(float3(dot(nw, CamR.xyz), dot(nw, CamU.xyz), dot(nw, CamF.xyz)));
}
// A light's reflection off a surface (GGX, no geometry term: stylised, after the lamps on a wet
// road in our Hit & Run), by how rough the surface is.
float glint(float3 n, float3 l, float3 v, float rough) {
    float a = rough * rough;
    float a2 = max(a * a, 1e-5);
    float3 h = normalize(l + v);
    float nh = saturate(dot(n, h));
    float dd = nh * nh * (a2 - 1) + 1;
    float D = min(a2 / (3.14159 * dd * dd), 80.0);
    float F = 0.04 + 0.96 * pow(1 - saturate(dot(v, h)), 5);
    return D * F * saturate(dot(n, l));
}

// One light, drawn added over the part of the picture it can reach: a pool on what it is near,
// wrapped a little round the form, and its glint off what shines.
float4 ps_light(V i) : SV_Target {
    float2 uv = Rect.xy + i.uv * Rect.zw;
    float4 al = T2.SampleLevel(Pnt, uv, 0);
    if (al.a < 0.05) return float4(0, 0, 0, 0);
    float z = T0.SampleLevel(Pnt, uv, 0).r;
    float3 p = view_pos(uv, z);
    float3 L = LightPos.xyz - p;
    float d2 = dot(L, L), r2 = LightPos.w * LightPos.w;
    if (d2 >= r2) return float4(0, 0, 0, 0);
    float4 s = T1.SampleLevel(Pnt, uv, 0);
    float3 n = view_normal(s.xy);
    L *= rsqrt(max(d2, 1.0));
    // Inverse square past a core a third of the reach, windowed to nothing at the reach.
    float window = saturate(1 - d2 / r2);
    float nl = dot(n, L);
    float3 sum = al.rgb * LightCol.rgb * (LightCol.w * window * window / (1 + 8 * d2 / r2) * saturate(nl * 0.8 + 0.2));
    if (s.z > 0.01 && nl > 0) sum += LightCol.rgb * (window * glint(n, L, -normalize(p), s.w) * s.z * LightAux.x * 0.06);
    return float4(sum, 1);
}

// The map's own sun (or moon) glinting off what shines, where the bake says its light reaches.
float4 ps_sun_glint(V i) : SV_Target {
    float4 al = T2.SampleLevel(Pnt, i.uv, 0);
    float4 s = T1.SampleLevel(Pnt, i.uv, 0);
    float sunlit = saturate((al.a - 0.1) / 0.9);
    if (al.a < 0.05 || s.z < 0.01 || sunlit <= 0) return float4(0, 0, 0, 0);
    float z = T0.SampleLevel(Pnt, i.uv, 0).r;
    float3 p = view_pos(i.uv, z);
    float3 n = view_normal(s.xy);
    return float4(SunCol.rgb * (glint(n, SunV.xyz, -normalize(p), s.w) * s.z * sunlit * SunV.w * 0.06), 1);
}

// ── Reflections ───────────────────────────────────────────────────────────────
// What shines enough mirrors what is on the picture already (a tiled floor, water, steel). Half
// resolution: each such pixel marches its reflected ray through the depth (steps crowding near
// the surface, each pixel's start offset by noise the gather after smooths), then closes on the
// crossing by halving. The hit's colour is the picture's own; its confidence fades at the
// picture's edge, down the ray's length, and for rays coming back toward the eye (the picture
// has not got what they would see). T0 the picture, T1 depth, T2 surface. After our Hit & Run's.
float2 view_to_uv(float3 q) { return float2(q.x / (q.z * Proj.x) * 0.5 + 0.5, 0.5 - q.y / (q.z * Proj.y) * 0.5); }

float4 ps_ssr(V i) : SV_Target {
    float4 s = T2.SampleLevel(Pnt, i.uv, 0);
    if (s.z < 0.3) return float4(0, 0, 0, 0);
    float z = T1.SampleLevel(Pnt, i.uv, 0).r;
    if (z > Proj.w * 0.9) return float4(0, 0, 0, 0);
    float3 p = view_pos(i.uv, z);
    float3 n = view_normal(s.xy);
    float3 r = reflect(normalize(p), n);
    if (r.z < -0.3) return float4(0, 0, 0, 0);
    const int N = 28;
    const float reach = 6000.0;
    float jit = ign(i.pos.xy);
    float3 start = p + n * (2.0 + z * 0.002);
    float prev_t = 0, hit_t = -1;
    [loop] for (int k = 1; k <= N; ++k) {
        float f = (k - 1 + jit) / N;
        float t = reach * f * f + 5.0;
        float3 q = start + r * t;
        if (q.z < Proj.z) break;
        float2 uv = view_to_uv(q);
        if (any(uv < 0) || any(uv > 1)) break;
        float sz = T1.SampleLevel(Pnt, uv, 0).r;
        float dz = q.z - sz;
        if (dz > 0 && dz < max(35.0, (t - prev_t) * 1.5) + q.z * 0.01) {
            float a = prev_t, b = t;
            [unroll] for (int h = 0; h < 4; ++h) {
                float m = (a + b) * 0.5;
                float3 qm = start + r * m;
                if (qm.z - T1.SampleLevel(Pnt, view_to_uv(qm), 0).r > 0) b = m; else a = m;
            }
            hit_t = b;
            break;
        }
        prev_t = t;
    }
    if (hit_t < 0) return float4(0, 0, 0, 0);
    float2 huv = view_to_uv(start + r * hit_t);
    float edge = saturate(min(min(huv.x, 1 - huv.x), min(huv.y, 1 - huv.y)) * 10);
    float along = 1 - saturate(hit_t / reach);
    float away = saturate(r.z * 3 + 0.9);
    return float4(T0.SampleLevel(Lin, huv, 0).rgb, edge * along * away);
}

// Full resolution, painted over the picture by its alpha: the half-resolution hits gathered
// (wider for a rougher surface, weighted by their confidence), as much as the surface is a
// mirror from where the eye is (more at a glancing angle). T0 reflections, T1 depth, T2 surface.
float4 ps_ssr_apply(V i) : SV_Target {
    float4 s = T2.SampleLevel(Pnt, i.uv, 0);
    if (s.z < 0.3) return float4(0, 0, 0, 0);
    float spread = lerp(0.6, 3.0, saturate(s.w * 3));
    float3 sum = float3(0, 0, 0);
    float wsum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x) {
            float4 h = T0.SampleLevel(Lin, i.uv + float2(x, y) * Src.zw * spread, 0);
            float w = h.a * (x == 0 && y == 0 ? 2 : 1);
            sum += h.rgb * w;
            wsum += w;
        }
    float conf = saturate(wsum / 10 * 1.6);
    float z = T1.SampleLevel(Pnt, i.uv, 0).r;
    float3 p = view_pos(i.uv, z);
    float F = 0.04 + 0.96 * pow(1 - saturate(dot(view_normal(s.xy), -normalize(p))), 5);
    float mirror = saturate((s.z - 0.3) / 0.55) * (0.12 + 0.88 * F) * Counts.z;
    return float4(sum / max(wsum, 1e-4), saturate(mirror * conf));
}

// ── The sun and the moon ──────────────────────────────────────────────────────
// Where the map's light comes from, something to see: the sun's disc by day, the moon in
// tonight's phase by night, behind everything the map has in front of it; and its corona over
// the picture, as much as the disc itself shows. T0 the depth, T1 the corona, T2 the moon's phases.
float4 ps_sky_body(V i) : SV_Target {
    float2 uv = Rect.xy + i.uv * Rect.zw;
    float3 dir = normalize(float3((uv.x * 2 - 1) * Proj.x, (1 - uv.y * 2) * Proj.y, 1));
    float c = dot(dir, SunV.xyz);
    if (c <= 0.05) return float4(0, 0, 0, 0);
    float3 right = normalize(cross(SunUp.xyz, SunV.xyz));
    float3 up = cross(SunV.xyz, right);
    float2 q = float2(dot(dir, right), dot(dir, up)) / c;
    float sky = T0.SampleLevel(Pnt, uv, 0).r > Proj.w * 0.9 ? 1.0 : 0.0;
    float3 sum = float3(0, 0, 0);
    float2 m = q / Body.x;
    if (BodyCol.w < 0.5) {
        sum += BodyCol.rgb * (sky * smoothstep(1.0, 0.8, length(m)));
    } else if (abs(m.x) < 1 && abs(m.y) < 1) {
        float2 cell = float2(m.x * 0.5 + 0.5, 0.5 - m.y * 0.5);
        sum += T2.SampleLevel(Lin, BodyUV.xy + cell * BodyUV.zw, 0).rgb * BodyCol.rgb * sky;
    }
    float2 k = q / Body.y;
    if (Body.z > 0 && abs(k.x) < 1 && abs(k.y) < 1) {
        // As much corona as there is disc in sight: the depth at its middle and round its rim
        // (a roof's edge takes it away by degrees, not at a stroke).
        float2 at = float2(SunV.x / (SunV.z * Proj.x) * 0.5 + 0.5, 0.5 - SunV.y / (SunV.z * Proj.y) * 0.5);
        float2 r = float2(Body.x / Proj.x, Body.x / Proj.y) * 0.5;
        float shown = T0.SampleLevel(Pnt, at, 0).r > Proj.w * 0.9 ? 2.0 : 0.0;
        [unroll] for (int j = 0; j < 8; ++j) {
            float a = j * 0.7853982;
            float2 o = float2(cos(a), sin(a)) * (j % 2 == 0 ? 0.9 : 0.5);
            shown += T0.SampleLevel(Pnt, at + o * r, 0).r > Proj.w * 0.9 ? 1.0 : 0.0;
        }
        sum += T1.SampleLevel(Lin, float2(k.x * 0.5 + 0.5, 0.5 - k.y * 0.5), 0).rgb * BodyCol.rgb * (Body.z * shown * 0.1);
    }
    return float4(sum, 1);
}

// ── Glow ───────────────────────────────────────────────────────────────────────
float3 soft_threshold(float3 c) {
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - BloomP.x + BloomP.y, 0, 2 * BloomP.y);
    soft = soft * soft / (4 * BloomP.y + 1e-4);
    return c * (max(soft, br - BloomP.x) / max(br, 1e-4));
}

// Thirteen taps, as the down chain of a modern bloom takes them (no flicker on thin lights).
float3 down13(float2 uv) {
    float2 t = Src.zw;
    float3 a = T0.SampleLevel(Lin, uv + t * float2(-2, -2), 0).rgb, b = T0.SampleLevel(Lin, uv + t * float2(0, -2), 0).rgb;
    float3 c = T0.SampleLevel(Lin, uv + t * float2(2, -2), 0).rgb, d = T0.SampleLevel(Lin, uv + t * float2(-2, 0), 0).rgb;
    float3 e = T0.SampleLevel(Lin, uv, 0).rgb, f = T0.SampleLevel(Lin, uv + t * float2(2, 0), 0).rgb;
    float3 g = T0.SampleLevel(Lin, uv + t * float2(-2, 2), 0).rgb, h = T0.SampleLevel(Lin, uv + t * float2(0, 2), 0).rgb;
    float3 k = T0.SampleLevel(Lin, uv + t * float2(2, 2), 0).rgb;
    float3 l = T0.SampleLevel(Lin, uv + t * float2(-1, -1), 0).rgb, m = T0.SampleLevel(Lin, uv + t * float2(1, -1), 0).rgb;
    float3 n = T0.SampleLevel(Lin, uv + t * float2(-1, 1), 0).rgb, o = T0.SampleLevel(Lin, uv + t * float2(1, 1), 0).rgb;
    return e * 0.125 + (a + c + g + k) * 0.03125 + (b + d + f + h) * 0.0625 + (l + m + n + o) * 0.125;
}
float4 ps_prefilter(V i) : SV_Target { return float4(soft_threshold(down13(i.uv)), 1); }
float4 ps_down(V i) : SV_Target { return float4(down13(i.uv), 1); }
// A tent over nine taps, added onto the next level up.
float4 ps_up(V i) : SV_Target {
    float2 t = Src.zw;
    float3 c = T0.SampleLevel(Lin, i.uv, 0).rgb * 4;
    c += (T0.SampleLevel(Lin, i.uv + float2(-t.x, 0), 0).rgb + T0.SampleLevel(Lin, i.uv + float2(t.x, 0), 0).rgb +
          T0.SampleLevel(Lin, i.uv + float2(0, -t.y), 0).rgb + T0.SampleLevel(Lin, i.uv + float2(0, t.y), 0).rgb) * 2;
    c += T0.SampleLevel(Lin, i.uv + t, 0).rgb + T0.SampleLevel(Lin, i.uv - t, 0).rgb +
         T0.SampleLevel(Lin, i.uv + float2(t.x, -t.y), 0).rgb + T0.SampleLevel(Lin, i.uv + float2(-t.x, t.y), 0).rgb;
    return float4(c / 16, 1);
}

// ── Light shafts (half resolution) ────────────────────────────────────────────
// What the sun lights the air with: the sky's own colour where the sky shows, most near the sun.
// T0 the picture, T1 its depth. A map under a dark sky has dark shafts: none to speak of.
float4 ps_shaft_mask(V i) : SV_Target {
    float z = T1.SampleLevel(Pnt, i.uv, 0).r;
    float3 c = T0.SampleLevel(Lin, i.uv, 0).rgb;
    float sky = z > Proj.w * 0.9 ? 1.0 : 0.0;
    float2 d = (i.uv - Shaft.xy) * float2(Dst.x * Dst.w, 1);
    float near = saturate(1 - length(d) * 1.25);
    // Only the bright of the sky lights the air: the glare about the sun, not the blue beside it.
    float bright = smoothstep(0.62, 0.98, luma(c));
    return float4(c * (sky * near * near * bright), 1);
}

// Smeared away from the sun. Twice over, the second a twelfth of the first's step, it is smooth.
float4 ps_shaft_blur(V i) : SV_Target {
    const int N = 12;
    float2 step = (Shaft.xy - i.uv) * (Counts.y / N);
    float2 uv = i.uv;
    float3 sum = 0;
    float w = 1, wsum = 0;
    [unroll] for (int k = 0; k < N; ++k) {
        sum += T0.SampleLevel(Lin, uv, 0).rgb * w;
        wsum += w;
        w *= 0.93;
        uv += step;
    }
    return float4(sum / wsum, 1);
}

// ── The finish ─────────────────────────────────────────────────────────────────
float3 shoulder(float3 c) {
    // The picture as drawn up to the knee; light above it rolls off instead of clipping.
    const float k = 0.85;
    float3 over = max(c - k, 0);
    return min(c, k) + (1 - k) * (1 - exp(-over / (1 - k)));
}

// T0 the picture, T1 its glow, T2 the light shafts. The alpha carries the brightness for the
// edge smoothing after.
float4 ps_final(V i) : SV_Target {
    float3 c = T0.SampleLevel(Pnt, i.uv, 0).rgb;
    c += T1.SampleLevel(Lin, i.uv, 0).rgb * BloomP.z;
    c += T2.SampleLevel(Lin, i.uv, 0).rgb * (Shaft.z * 0.55);
    c *= Finish.z;
    if (Finish.x > 0.5) c = shoulder(c);
    if (Finish.x > 1.5) {
        // Filmic: darks a little deeper, the top rolled off further.
        c = saturate(c);
        c = lerp(c, c * c * (3 - 2 * c), 0.45);
    }
    float l = luma(c);
    c = lerp(float3(l, l, l), c, Grade.x);
    c = (c - 0.5) * Grade.y + 0.5;
    float2 v = (i.uv - 0.5) * float2(Dst.x * Dst.w, 1);
    c *= 1 - Grade.z * smoothstep(0.3, 0.95, length(v));
    // A fine dither, so what the glow and the vignette add does not band.
    if (Finish.y > 0) c += (ign(i.pos.xy) - 0.5) / 255.0;
    c = saturate(c);
    return float4(c, dot(c, float3(0.299, 0.587, 0.114)));
}

// ── Edge smoothing (after the quality preset of Lottes's FXAA 3.11) ───────────
float4 ps_fxaa(V i) : SV_Target {
    float2 t = Src.zw, uv = i.uv;
    float4 rgbM = T0.SampleLevel(Lin, uv, 0);
    float lM = rgbM.a;
    float lN = T0.SampleLevel(Lin, uv + float2(0, -t.y), 0).a, lS = T0.SampleLevel(Lin, uv + float2(0, t.y), 0).a;
    float lE = T0.SampleLevel(Lin, uv + float2(t.x, 0), 0).a, lW = T0.SampleLevel(Lin, uv + float2(-t.x, 0), 0).a;
    float lmax = max(lM, max(max(lN, lS), max(lE, lW))), lmin = min(lM, min(min(lN, lS), min(lE, lW)));
    float range = lmax - lmin;
    if (range < max(0.0312, lmax * 0.125)) return rgbM;
    float lNW = T0.SampleLevel(Lin, uv + float2(-t.x, -t.y), 0).a, lNE = T0.SampleLevel(Lin, uv + float2(t.x, -t.y), 0).a;
    float lSW = T0.SampleLevel(Lin, uv + float2(-t.x, t.y), 0).a, lSE = T0.SampleLevel(Lin, uv + float2(t.x, t.y), 0).a;
    float eH = abs(-2 * lW + lNW + lSW) + abs(-2 * lM + lN + lS) * 2 + abs(-2 * lE + lNE + lSE);
    float eV = abs(-2 * lN + lNW + lNE) + abs(-2 * lM + lW + lE) * 2 + abs(-2 * lS + lSW + lSE);
    bool horz = eH >= eV;
    float l1 = horz ? lN : lW, l2 = horz ? lS : lE;
    float g1 = l1 - lM, g2 = l2 - lM;
    bool first = abs(g1) >= abs(g2);
    float grad = 0.25 * max(abs(g1), abs(g2));
    float stepLen = horz ? t.y : t.x;
    float lLocal;
    if (first) { stepLen = -stepLen; lLocal = 0.5 * (l1 + lM); } else { lLocal = 0.5 * (l2 + lM); }
    float2 cur = uv;
    if (horz) cur.y += stepLen * 0.5; else cur.x += stepLen * 0.5;
    float2 off = horz ? float2(t.x, 0) : float2(0, t.y);
    float2 uv1 = cur - off, uv2 = cur + off;
    float e1 = T0.SampleLevel(Lin, uv1, 0).a - lLocal, e2 = T0.SampleLevel(Lin, uv2, 0).a - lLocal;
    bool r1 = abs(e1) >= grad, r2 = abs(e2) >= grad;
    const float Q[11] = {1.5, 2, 2, 2, 2, 4, 8, 8, 8, 8, 8};
    [loop] for (int k = 0; k < 11 && !(r1 && r2); ++k) {
        if (!r1) { uv1 -= off * Q[k]; e1 = T0.SampleLevel(Lin, uv1, 0).a - lLocal; r1 = abs(e1) >= grad; }
        if (!r2) { uv2 += off * Q[k]; e2 = T0.SampleLevel(Lin, uv2, 0).a - lLocal; r2 = abs(e2) >= grad; }
    }
    float d1 = horz ? uv.x - uv1.x : uv.y - uv1.y, d2 = horz ? uv2.x - uv.x : uv2.y - uv.y;
    bool dir1 = d1 < d2;
    float dmin = min(d1, d2), span = d1 + d2;
    bool lSmaller = lM < lLocal;
    bool correct = ((dir1 ? e1 : e2) < 0) != lSmaller;
    float edgeOff = correct ? -dmin / span + 0.5 : 0;
    float lAvg = (2 * (lN + lS + lE + lW) + lNW + lNE + lSW + lSE) / 12;
    float s1 = saturate(abs(lAvg - lM) / range);
    float s2 = (-2 * s1 + 3) * s1 * s1;
    float subOff = s2 * s2 * 0.75;
    float o = max(edgeOff, subOff);
    float2 fuv = uv;
    if (horz) fuv.y += o * stepLen; else fuv.x += o * stepLen;
    return T0.SampleLevel(Lin, fuv, 0);
}
