// The world: Soldier Front's lightmapped levels and their props, the sky box, and every
// skinned model (characters, first-person weapons). Row vectors, row-major matrices, the
// device's conventions (Engine/Render/Device.hpp).

cbuffer Frame : register(b0) {
    row_major float4x4 ViewProj;
    float4 CameraPos;     // xyz: the eye, w: time in seconds
    float4 SunDir;        // xyz: the way sunlight travels
    float4 SunColour;     // rgb
    float4 Ambient;       // rgb, w: exposure (the brightness setting)
    float4 FogColour;     // rgb, w: density per centimetre (0 = none)
    float4 Params;        // x: lightmap gain (2: Soldier Front modulates x2), y: saturation, z: gamma, w: unused
    // The room's hour, as a grade of the light the map was baked with (WorldRenderer::set_time_of_day).
    float4 GradeSun;      // rgb: the ink on baked light the bake left bright; w: knee low
    float4 GradeShade;    // rgb: the ink on baked shade; w: knee high
    float4 GradeFill;     // rgb: light added over the bake; w: 1 when the grade is on
    float4 GradeMisc;     // x: unlit surfaces' multiplier, y: saturation, z: the bake's exposure, w: unused
    float4 SkyZenith;     // rgb: the hour's sky overhead; w: how far the box takes the gradient's colour
    float4 SkyHorizon;    // rgb: at the horizon; w: the box's gain
    float4 SkyExtra;      // x: the gradient instead of the box, y: stars, z: the moon, w: unused
    float4 MoonDir;       // xyz: toward the moon
    // Fidelity's sky and water (Game/Render/FramePipe; all zero: the map as it is).
    float4 FidSky;        // x: drifting cloud, how much of the sky it covers (0 none); y: 1 water is drawn as water;
                          // z: 1 the sky's top face is bound for water to mirror; w: unused
    // Soldiers' shadows from the map's sun (WorldRenderer::begin_shadows).
    row_major float4x4 SunViewProj;
    float4 ShadowP;       // x: strength (0: none this frame), y: a texel in UV, z: the bias in the map's depth, w: unused
};

cbuffer Object : register(b1) {
    row_major float4x4 World;
    float4 Tint;          // rgb multiply, a alpha
    float4 Flags;         // x: lightmapped, y: alpha test threshold (<0 off), z: unlit, w: skinned
    float4 Extra;         // x: rim light (the first-person weapon), y: flash (hit, muzzle),
                          // z: how much the surface shines (0 not at all), w: how rough it is (Fidelity's lights)
};

cbuffer Bones : register(b2) {
    row_major float4x4 Bones[128];
};

Texture2D Tex : register(t0);
Texture2D Lightmap : register(t1);
Texture2D SkyTop : register(t2);   // the sky box's top face (water mirrors it)
Texture2D ShadowMap : register(t3); // soldiers' shadows: depth from the sun
SamplerState Samp : register(s0);
SamplerState LmSamp : register(s1);
SamplerComparisonState ShadowSamp : register(s2);

// How much of the map's sun gets past the soldiers to a point: 1 in the open, down to 1 - strength
// behind one. Sixteen filtered taps half a texel apart (an edge about five texels soft: a staircase
// otherwise), fading out toward the edge of the map's reach rather than ending on a line.
float SoldierShadow(float3 world) {
    if (ShadowP.x <= 0.001) return 1.0;
    float4 lp = mul(float4(world, 1.0), SunViewProj);
    float3 ndc = lp.xyz / lp.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < 0.0 || ndc.z > 1.0) return 1.0;
    float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
    float z = ndc.z - ShadowP.z;
    float lit = 0.0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x)
            lit += ShadowMap.SampleCmpLevelZero(ShadowSamp, uv + (float2(x, y) - 1.5) * ShadowP.y, z);
    lit *= 1.0 / 16.0;
    float edge = 1.0 - saturate((max(abs(ndc.x), abs(ndc.y)) - 0.85) / 0.15);
    return 1.0 - ShadowP.x * (1.0 - lit) * edge;
}

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// Noise for Fidelity's cloud and water. An integer hash of the lattice point: neighbouring cells
// uncorrelated (a float hash of frac(n * k) steps evenly from cell to cell and shows its grid).
float hash21(float2 p) {
    uint2 q = uint2(int2(p)) * uint2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    return float(n) * (1.0 / 4294967295.0);
}
float vnoise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3 - 2 * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), f.x), lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), f.x), f.y);
}
float fbm(float2 p) {
    float s = 0, a = 0.5;
    [unroll] for (int k = 0; k < 5; ++k) {
        s += vnoise(p) * a;
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s;
}

// (Defined with the sky, further down; water mirrors the same sky.)
float3 GradeSky(float3 box, float3 d);
float3 Clouds(float3 c, float3 d);

// Baked light graded to the hour: its own brightness says what was in the sun and what was in
// shade, and each takes its own colour, then the fill reaches the corners the bake left black.
float3 GradeBaked(float3 light) {
    if (GradeFill.w < 0.5) return light;
    light *= GradeMisc.z;
    float t = smoothstep(GradeSun.w, GradeShade.w, Luma(light));
    return light * lerp(GradeShade.rgb, GradeSun.rgb, t) + GradeFill.rgb;
}

float3 Finish(float3 c, float3 world) {
    c *= Ambient.w;
    float luma = Luma(c);
    c = lerp(luma.xxx, c, Params.y * (GradeFill.w > 0.5 ? GradeMisc.y : 1.0));
    c = pow(max(c, 0.0), Params.z);
    if (FogColour.w > 0.0) {
        float d = length(CameraPos.xyz - world) * FogColour.w;
        c = lerp(c, FogColour.rgb, saturate(1.0 - exp(-d * d)));
    }
    return c;
}

// ── Level geometry and props ──────────────────────────────────────────────────

struct LevelIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
};
struct LevelOut {
    float4 pos : SV_Position;
    float2 uv0 : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float3 nrm : TEXCOORD2;
    float3 world : TEXCOORD3;
};

LevelOut vs_level(LevelIn i) {
    LevelOut o;
    float4 w = mul(float4(i.pos, 1.0), World);
    o.pos = mul(w, ViewProj);
    o.uv0 = i.uv0;
    o.uv1 = i.uv1;
    o.nrm = mul(float4(i.nrm, 0.0), World).xyz;
    o.world = w.xyz;
    return o;
}

// The light on a level's surface: its lightmap, graded to the hour; a prop's flat sun; or none
// asked for (an unlit sign). `sunlit` is how far the map's own sun reaches it (0 shade .. 1 sun),
// as far as the bake or the surface's turn can say.
float3 LevelLight(LevelOut i, out float sunlit) {
    if (Flags.x > 0.5) {
        float3 baked = Lightmap.Sample(LmSamp, i.uv1).rgb * Params.x;
        sunlit = smoothstep(0.7, 1.3, Luma(baked));
        return GradeBaked(baked);
    }
    if (Flags.z > 0.5) {
        sunlit = 0.0;
        return float3(1.0, 1.0, 1.0) * (GradeFill.w > 0.5 ? GradeMisc.x : 1.0);
    }
    float sun = saturate(dot(normalize(i.nrm), -SunDir.xyz));
    sunlit = sun;
    return Ambient.rgb + SunColour.rgb * sun;
}

// How far the map's sun reaches a level surface, for a soldier's shadow to take away: a
// lightmapped surface's bake (bright is where its sun fell: Soldier Front's sand in the sun bakes
// to about 0.9, its shade to 0.4), or a prop's turn toward the sun.
float SunReach(LevelOut i, float sunlit) {
    if (Flags.x > 0.5) return smoothstep(0.3, 0.75, Luma(Lightmap.Sample(LmSamp, i.uv1).rgb * Params.x));
    return sunlit;
}

float4 ps_level(LevelOut i) : SV_Target {
    float4 base = Tex.Sample(Samp, i.uv0);
    if (Flags.y >= 0.0) clip(base.a - Flags.y);
    float sunlit;
    float3 light = LevelLight(i, sunlit);
    // A soldier's shadow takes the sun away only where the bake left sun: never a second shade
    // over the map's own.
    light *= lerp(1.0, SoldierShadow(i.world), SunReach(i, sunlit));
    float3 c = base.rgb * light * Tint.rgb;
    return float4(Finish(c, i.world), base.a * Tint.a);
}

// ── Fidelity's surface targets (Game/Render/FramePipe) ────────────────────────
// With Fidelity's lights on, the solid world is drawn into three targets at once: the picture as
// ever; the surface (its normal, how much it shines, how rough it is); and its own colour before
// any light, with how far the map's sun reaches it. The lights are then laid on from those, so a
// muzzle flash or a lamp lights the wall it is next to and glints off a tiled floor. A blended
// draw (glass, glows, smoke) writes the picture only.
struct GOut {
    float4 c : SV_Target0;
    float4 surf : SV_Target1;     // rg: the normal (octahedral), b: shine, a: roughness
    float4 albedo : SV_Target2;   // rgb: the surface's own colour; a: 0 takes no light, else 0.1 + 0.9 x sunlit
};

// A unit normal in two numbers (octahedral), 0..1, for an 8-bit target.
float2 PackNormal(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 o = n.y >= 0 ? n.xz : (1 - abs(n.zx)) * float2(n.x >= 0 ? 1 : -1, n.z >= 0 ? 1 : -1);
    return o * 0.5 + 0.5;
}

// The surface's normal, towards the eye (the levels are modelled both ways round). Where the
// vertex's own is far from the face's (a mesh exported without them), the face's stands in.
float3 SurfaceNormal(float3 vertex_n, float3 face_n, float3 world) {
    float3 v = normalize(CameraPos.xyz - world);
    float3 n = normalize(vertex_n + 1e-6);
    if (dot(n, v) < 0) n = -n;
    float3 fn = normalize(face_n + 1e-7);
    if (dot(fn, v) < 0) fn = -fn;
    if (dot(n, fn) < 0.3) n = fn;
    return n;
}

// Water's face: two layers of ripple crossing each other, as the slope of a height that drifts.
float WaterHeight(float2 p, float t) {
    return vnoise(p + t * float2(0.21, 0.09)) + 0.5 * vnoise(p * 2.3 - t * float2(0.16, 0.27)) + 0.25 * vnoise(p * 5.1 + t * float2(0.3, -0.2));
}
float3 WaterNormal(float3 world) {
    float2 p = world.xz * 0.011;
    float t = CameraPos.w;
    const float e = 0.12;
    float h = WaterHeight(p, t);
    float hx = WaterHeight(p + float2(e, 0), t), hz = WaterHeight(p + float2(0, e), t);
    // Calmer in the distance, where ripples smaller than a pixel would only sparkle.
    float calm = 1.0 / (1.0 + length(CameraPos.xyz - world) * 0.0006);
    return normalize(float3((h - hx) * 0.55 * calm, 1.0, (h - hz) * 0.55 * calm));
}
// What water mirrors of the sky: the box's top face along the reflected ray (its rim for a ray
// that would leave by a side), graded and clouded as the sky itself is; under no sky, the room's light.
float3 WaterSky(float3 r) {
    if (FidSky.z < 0.5) return Ambient.rgb * 0.55;
    r.y = max(r.y, 0.04);
    r = normalize(r);
    float3 sky = SkyTop.Sample(LmSamp, saturate(r.xz / r.y * 0.5 + 0.5)).rgb;
    if (GradeFill.w > 0.5) sky = GradeSky(sky, r);
    return Clouds(sky, r);
}

GOut ps_level_g(LevelOut i) {
    // Taken before any discard: derivatives in a quad with discarded pixels are undefined on some cards.
    const float3 face_n = cross(ddy(i.world), ddx(i.world));
    float4 base = Tex.Sample(Samp, i.uv0);
    if (Flags.y >= 0.0) clip(base.a - Flags.y);
    float sunlit;
    float3 light = LevelLight(i, sunlit);
    const float shade = lerp(1.0, SoldierShadow(i.world), SunReach(i, sunlit));
    light *= shade;
    float3 c = base.rgb * light * Tint.rgb;
    float3 n = SurfaceNormal(i.nrm, face_n, i.world);
    GOut o;
    o.surf = float4(PackNormal(n), Extra.z, Extra.w);
    o.albedo = float4(base.rgb * Tint.rgb, Flags.z > 0.5 ? 0.0 : 0.1 + 0.9 * sunlit * shade);
    if (Flags.w > 0.5 && FidSky.y > 0.5 && n.y > 0.7) {
        // Water as water: its face rippling, mirroring the sky above it the more the lower the
        // eye, its own colour (the map's texture, as lit) showing where one looks down into it.
        n = WaterNormal(i.world);
        float3 v = normalize(CameraPos.xyz - i.world);
        float F = 0.02 + 0.98 * pow(1.0 - saturate(dot(n, v)), 5.0);
        c = lerp(c * 0.8, WaterSky(reflect(-v, n)), saturate(F * 0.92 + 0.05));
        o.surf = float4(PackNormal(n), 0.92, 0.08);
        o.albedo.rgb *= 0.4;
    }
    o.c = float4(Finish(c, i.world), base.a * Tint.a);
    return o;
}

// ── Sky box: six faces around the eye, drawn first with no depth ──────────────

struct SkyIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
};
struct SkyOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 dir : TEXCOORD1;
};

SkyOut vs_sky(SkyIn i) {
    SkyOut o;
    float4 w = mul(float4(i.pos, 1.0), World);
    o.pos = mul(w, ViewProj).xyww;   // on the far plane
    o.uv = i.uv;
    o.dir = i.pos;                   // the cube is centred on the eye: its corner is the view ray
    return o;
}

// A star wherever the ray crosses one of a sparse scatter of cells on a sphere: a point a pixel
// or two across, not a blob.
float Stars(float3 d) {
    float3 p = d * 420.0;
    float3 cell = floor(p);
    float h = frac(sin(dot(cell, float3(12.9898, 78.233, 37.719))) * 43758.5453);
    if (h < 0.9965) return 0.0;
    float core = saturate(1.0 - length(frac(p) - 0.5) * 2.6);
    return core * core * (0.35 + 0.65 * frac(h * 917.3));
}

// The hour's sky: the box keeps its light and dark (clouds stay clouds, a skyline a skyline) and
// takes its colour from a zenith-to-horizon gradient; under the horizon, the hour's haze.
float3 GradeSky(float3 box, float3 d) {
    float3 grad = d.y < 0.0 ? lerp(SkyHorizon.rgb, FogColour.rgb * 0.8, saturate(-d.y * 4.0))
                            : lerp(SkyHorizon.rgb, SkyZenith.rgb, pow(saturate(d.y), 0.5));
    float3 recoloured = grad / max(Luma(grad), 0.02) * Luma(box);
    float3 c = lerp(lerp(box, recoloured, SkyZenith.w) * SkyHorizon.w, grad, SkyExtra.x);
    float s = saturate(dot(d, MoonDir.xyz));
    c += float3(0.86, 0.9, 1.0) * smoothstep(0.99955, 0.99968, s) * SkyExtra.z * 1.3;   // the moon
    c += float3(0.5, 0.56, 0.75) * pow(s, 60.0) * SkyExtra.z * 0.12;                   // its halo
    if (SkyExtra.y > 0.0 && d.y > 0.06)
        c += Stars(d) * SkyExtra.y * saturate((d.y - 0.06) * 6.0) * saturate(1.0 - Luma(c) * 6.0);
    return c;
}

// Fidelity's cloud: a layer drifting high over the map's own sky, in the map's own light (so
// the room's night has night's cloud), brighter toward where that light comes from. It keeps
// clear of the horizon, where the sky box has its hills and skylines.
float3 Clouds(float3 c, float3 d) {
    if (FidSky.x <= 0.0 || d.y <= 0.08) return c;
    float2 uv = d.xz / (d.y + 0.12) * 0.55 + float2(1.0, 0.35) * (CameraPos.w * 0.004);
    float n = fbm(uv * 1.6);
    float cl = smoothstep(1.0 - FidSky.x - 0.05, 1.0 - FidSky.x + 0.35, n);
    bool night = SkyExtra.y > 0.0;
    float s = saturate(dot(d, night ? MoonDir.xyz : -SunDir.xyz));
    float3 lit = saturate(Ambient.rgb * 0.75 + SunColour.rgb * 0.6);
    if (GradeFill.w > 0.5) lit *= max(SkyHorizon.w, 0.05) * (night ? 1.6 : 1.0);
    float3 body = lit * (0.5 + 0.5 * n) * (0.75 + 0.9 * pow(s, 6.0));
    return lerp(c, body, cl * saturate((d.y - 0.08) * 5.0) * 0.9);
}

float4 ps_sky(SkyOut i) : SV_Target {
    float3 c = Tex.Sample(Samp, i.uv).rgb * Tint.rgb;
    float3 d = normalize(i.dir);
    if (GradeFill.w > 0.5) c = GradeSky(c, d);
    c = Clouds(c, d);
    c *= Ambient.w;
    float luma = Luma(c);
    c = lerp(luma.xxx, c, Params.y);
    return float4(pow(max(c, 0.0), Params.z), 1.0);
}

// ── Skinned models: characters and first-person weapons ───────────────────────

struct ModelIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 bones : BLENDINDICES;
    float4 w : BLENDWEIGHT;
};
struct ModelOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float3 world : TEXCOORD2;
};

ModelOut vs_model(ModelIn i) {
    ModelOut o;
    float4 p = float4(i.pos, 1.0);
    float4 n = float4(i.nrm, 0.0);
    float4 sp, sn;
    if (Flags.w > 0.5) {
        float4x4 m = Bones[i.bones.x] * i.w.x + Bones[i.bones.y] * i.w.y + Bones[i.bones.z] * i.w.z + Bones[i.bones.w] * i.w.w;
        sp = mul(p, m);
        sn = mul(n, m);
    } else {
        sp = p;
        sn = n;
    }
    float4 w = mul(sp, World);
    o.pos = mul(w, ViewProj);
    o.uv = i.uv;
    o.nrm = mul(sn, World).xyz;
    o.world = w.xyz;
    return o;
}

float3 ModelColour(ModelOut i, float4 base) {
    float3 n = normalize(i.nrm);
    // Soldiers shadow each other (and themselves) at half strength, and only the sun's share: the
    // side away from the sun has none to lose.
    float sun = saturate(dot(n, -SunDir.xyz)) * lerp(1.0, SoldierShadow(i.world), 0.5);
    // Half-Lambert wrap keeps the shadow side of a body readable, as the original's did.
    float wrap = saturate(dot(n, -SunDir.xyz) * 0.5 + 0.5);
    float3 light = Ambient.rgb * (0.65 + 0.35 * wrap) + SunColour.rgb * sun;
    if (Extra.x > 0.0) {
        float3 v = normalize(CameraPos.xyz - i.world);
        light += Extra.x * (GradeFill.w > 0.5 ? GradeMisc.x : 1.0) * pow(1.0 - saturate(dot(n, v)), 3.0);
    }
    return base.rgb * light * Tint.rgb + Extra.y;
}

float4 ps_model(ModelOut i) : SV_Target {
    float4 base = Tex.Sample(Samp, i.uv);
    if (Flags.y >= 0.0) clip(base.a - Flags.y);
    return float4(Finish(ModelColour(i, base), i.world), base.a * Tint.a);
}

// The same into Fidelity's surface targets. A soldier takes the lights; the map's sun is in his
// own shading already, so none of its glint.
GOut ps_model_g(ModelOut i) {
    const float3 face_n = cross(ddy(i.world), ddx(i.world));
    float4 base = Tex.Sample(Samp, i.uv);
    if (Flags.y >= 0.0) clip(base.a - Flags.y);
    GOut o;
    o.c = float4(Finish(ModelColour(i, base), i.world), base.a * Tint.a);
    o.surf = float4(PackNormal(SurfaceNormal(i.nrm, face_n, i.world)), Extra.z, Extra.w);
    o.albedo = float4(base.rgb * Tint.rgb, 0.1);
    return o;
}

// ── Soldiers' shadows: depth from the sun, nothing else ───────────────────────

struct ShadowOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

ShadowOut vs_shadow(ModelIn i) {
    ShadowOut o;
    float4 p = float4(i.pos, 1.0);
    if (Flags.w > 0.5) {
        float4x4 m = Bones[i.bones.x] * i.w.x + Bones[i.bones.y] * i.w.y + Bones[i.bones.z] * i.w.z + Bones[i.bones.w] * i.w.w;
        p = mul(p, m);
    }
    o.pos = mul(mul(p, World), ViewProj);
    o.uv = i.uv;
    return o;
}

// Nothing is written but depth; a cut-out's holes write none either.
float4 ps_shadow(ShadowOut i) : SV_Target {
    if (Flags.y >= 0.0) clip(Tex.Sample(Samp, i.uv).a - Flags.y);
    return float4(0.0, 0.0, 0.0, 0.0);
}

// ── Lines: debug shapes, collision, tracers ───────────────────────────────────

struct LineIn {
    float3 pos : POSITION;
    float4 col : COLOR0;
};
struct LineOut {
    float4 pos : SV_Position;
    float4 col : COLOR0;
};

LineOut vs_lines(LineIn i) {
    LineOut o;
    o.pos = mul(float4(i.pos, 1.0), ViewProj);
    o.col = i.col;
    return o;
}

float4 ps_lines(LineOut i) : SV_Target { return i.col; }

// ── Sprites: muzzle flashes, tracers, glows (Soldier Front's weapon flare/ art) ──
// Textured quads already placed in the world, drawn added onto what is there: the art sits on
// black, so black adds nothing. Fog takes them away with distance like everything else.

struct SpriteIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};
struct SpriteOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
    float3 world : TEXCOORD1;
};

SpriteOut vs_sprite(SpriteIn i) {
    SpriteOut o;
    o.pos = mul(float4(i.pos, 1.0), ViewProj);
    o.uv = i.uv;
    o.col = i.col;
    o.world = i.pos;
    return o;
}

float4 ps_sprite(SpriteOut i) : SV_Target {
    float4 t = Tex.Sample(Samp, i.uv) * i.col;
    float fade = 1.0;
    if (FogColour.w > 0.0) {
        float d = length(CameraPos.xyz - i.world) * FogColour.w;
        fade = exp(-d * d);
    }
    return float4(t.rgb * t.a * fade, 1.0);
}

// The same quads painted over what is there by their alpha: smoke, blood, bullet holes and the
// other marks left on the map (Soldier Front's effect/ and weapon/mark/ art), fogged like the map.
float4 ps_sprite_alpha(SpriteOut i) : SV_Target {
    float4 t = Tex.Sample(Samp, i.uv) * i.col;
    if (FogColour.w > 0.0) {
        float d = length(CameraPos.xyz - i.world) * FogColour.w;
        t.rgb = lerp(t.rgb, FogColour.rgb, saturate(1.0 - exp(-d * d)));
    }
    return t;
}
