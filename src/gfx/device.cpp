#include <cstdlib>
#include "device.h"
#ifndef GL_SAMPLES
#define GL_SAMPLES 0x80A9
#endif
#include "../core/config.h"
#include "../core/log.h"
#include "../framework/d3dutil.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

static Device g_device;
// true while draws are recorded and not yet sent (a plain flag, so a texture freed at exit, after
// the device itself, never touches it)
static bool g_recordPending = false;
Device& GetDevice() { return g_device; }

// ---------------------------------------------------------------------------
// Shaders: D3D7 fixed-function subset
// ---------------------------------------------------------------------------
// One set of sources serves desktop GL and GLSL ES; only the version line and the ES precision
// defaults differ. uBgra swizzles the original's packed BGRA vertex colours, which desktop GL
// accepts as an attribute type and ES does not.
#if defined(MBN_GLES)
#define MBN_GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler2DShadow;\n"
#else
#define MBN_GLSL_HEADER "#version 330 core\n"
#endif

static const char* kVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=4) in vec2 aUV;
layout(location=3) in float aMaterial;  // merged opaque model: its material slot
uniform mat4 uWorld, uView, uProj;
uniform bool uLighting, uLightOn, uHasVertexColor, uScreen2D;
uniform float uBgra;  // 1: the colours arrived as RGBA and are really BGRA (ES attribute)
uniform int uMatMode;  // 1: take the material from aMaterial, 0: the single material below
uniform vec4 uMatDiffuse[16], uMatAmbient[16], uMatEmissive[16], uAmbient, uLightDiffuse;
uniform vec3 uLightDir;
uniform vec4 uViewport2D;
// Dynamic shadows (G4): 2 for a lit model, whose light is passed in two parts (what the light
// itself gives, which a shadow takes away, and the rest); the level's position for the lookup
uniform int uShadowKind;
out vec4 vColor;
flat out vec4 vColorFlat;
out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos, vAmbPart, vDirPart;
flat out vec3 vAmbFlat, vDirFlat;
flat out vec4 vMatDiffuse;
vec4 vc(vec4 c) { return uBgra > 0.5 ? c.bgra : c; }
vec4 d3dLight(vec3 n, vec4 md, vec4 ma, vec4 me) {
    vec3 c = me.rgb + uAmbient.rgb * ma.rgb;
    if (uLightOn) c += uLightDiffuse.rgb * md.rgb * max(dot(n, uLightDir), 0.0);
    return vec4(clamp(c, 0.0, 1.0), clamp(md.a, 0.0, 1.0));
}
void main() {
    if (uScreen2D) {
        gl_Position = vec4(aPos.x * uViewport2D.x + uViewport2D.z, aPos.y * uViewport2D.y + uViewport2D.w, 0.0, 1.0);
        vColor = vc(aColor); vColorFlat = vc(aColor); vUV = aUV; vNormal = vec3(0.0);
        vWorldPos = vec3(0.0); vAmbPart = vDirPart = vAmbFlat = vDirFlat = vec3(0.0);
        return;
    }
    vec4 wp = uWorld * vec4(aPos, 1.0);
    vec4 cp = uProj * (uView * wp);
    cp.z = 2.0 * cp.z - cp.w;          // D3D clip z [0,w] -> GL [-w,w]
    gl_Position = cp;
    // D3D7 transforms normals by the inverse transpose of the world (and view) matrix and, with
    // NORMALIZENORMALS off (its default), never renormalizes them: an object drawn at scale s gets
    // normals 1/s long, so small scaled models (the coins at 0.35 and 0.6) light up brighter.
    vec3 n = transpose(inverse(mat3(uWorld))) * aNormal;
    vNormal = n;
    int mi = uMatMode > 0 ? int(aMaterial + 0.5) : 0;
    vec4 md = uMatDiffuse[mi], ma = uMatAmbient[mi], me = uMatEmissive[mi];
    vMatDiffuse = md;
    vec4 col = uLighting ? d3dLight(n, md, ma, me) : (uHasVertexColor ? vc(aColor) : vec4(1.0));
    vColor = col; vColorFlat = col; vUV = aUV;
    vWorldPos = wp.xyz;
    vec3 amb = vec3(0.0), dir = vec3(0.0);
    if (uShadowKind == 2) {
        amb = me.rgb + uAmbient.rgb * ma.rgb;
        if (uLightOn) dir = uLightDiffuse.rgb * md.rgb * max(dot(n, uLightDir), 0.0);
    }
    vAmbPart = amb; vDirPart = dir; vAmbFlat = amb; vDirFlat = dir;
}
)GLSL";

static const char* kFS = MBN_GLSL_HEADER R"GLSL(
in vec4 vColor;
flat in vec4 vColorFlat;
in vec2 vUV;
in vec3 vNormal;
uniform sampler2D uTex;
uniform bool uTexOn, uFlat, uAlphaTest;
uniform int uColorOp, uColorArg1, uColorArg2, uAlphaOp, uAlphaArg1, uAlphaArg2;
uniform vec4 uConstColor;
// Native (mods): blend modes that ignore alpha - additive glows (ONE, ONE) and darkening
// shadows (ZERO, SRCCOLOR) - get a mod image's own transparency applied to its colour instead:
// 1 = fade to black (adds nothing), 2 = fade to white (darkens nothing). 0 = off (originals).
uniform int uAlphaFix;
// Ambient occlusion (G3): how open this pixel is, from the AO texture at its screen position, and
// how much of it this draw takes (0: none, as everything outside a level's level and solid models)
uniform sampler2D uAOTex;
uniform float uAOStrength;
uniform vec2 uFboSize;
// Dynamic shadows (G4): which way this draw takes them (0 not at all, 1 darkened where it faces
// the light: the pre-lit level, 2 a lit model: its direct light only), the map and how to read it
in vec3 vWorldPos, vAmbPart, vDirPart;
flat in vec3 vAmbFlat, vDirFlat;
flat in vec4 vMatDiffuse;
uniform int uShadowKind, uShadowGrid;
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightVP;
uniform float uShadowTexel, uShadowOffset, uShadowStrength;
uniform vec3 uEye, uLightDir;
// Dynamic lights (G5): how this draw takes them (0 not at all, 1 a pre-lit or unlit surface: its
// texture lit, 2 a lit model: its material lit), and the frame's lights (position, colour with
// brightness, reach)
uniform int uLightKind, uLightCount;
uniform vec3 uLightPos[16], uLightColor[16];
uniform float uLightReach[16];
// With Dynamic lights, the level dimmed so they stand out (1 = as the original; the Ambient light
// setting). Every draw of a level's 3D but the glowing sprites and the blob shadows.
uniform float uSceneDim;
// Bloom (G6): what this draw leaves in the glow layer (only drawn into during a level's 3D with
// Bloom on): 1 a glowing sprite: its own colour; 2 a draw that multiplies what is there (the blob
// shadows): nothing changed; 0 anything else: black, so it hides the glow behind it as on screen
uniform int uGlowMode;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 glowColor;
// How lit a point is by the game's light, 0 to 1: the map read on a grid round it (each read
// already blended over 2 x 2 map pixels by the card), fading out near the map's edges
float LitBy(vec3 wp, vec3 n) {
    vec4 lc = uLightVP * vec4(wp + n * uShadowOffset, 1.0);
    vec3 c = lc.xyz * 0.5 + 0.5;
    if (c.x < 0.0 || c.y < 0.0 || c.x > 1.0 || c.y > 1.0 || c.z > 1.0) return 1.0;
    float o = (float(uShadowGrid) - 1.0) * 0.5, s = 0.0;
    for (int y = 0; y < uShadowGrid; y++)
        for (int x = 0; x < uShadowGrid; x++)
            s += texture(uShadowMap, vec3(c.xy + (vec2(float(x), float(y)) - o) * uShadowTexel, c.z - 0.0004));
    s /= float(uShadowGrid * uShadowGrid);
    vec2 e = min(c.xy, 1.0 - c.xy);
    return mix(1.0, s, smoothstep(0.0, 0.03, min(e.x, e.y)));
}
vec4 pick(int a, vec4 diff, vec4 tex) { return a == 2 ? tex : diff; }
vec4 op(int o, vec4 a1, vec4 a2) {
    if (o == 2) return a1;               // SELECTARG1
    if (o == 3) return a2;               // SELECTARG2
    if (o == 4) return a1 * a2;          // MODULATE
    if (o == 5) return clamp(a1 * a2 * 2.0, 0.0, 1.0);
    if (o == 6) return clamp(a1 * a2 * 4.0, 0.0, 1.0);
    if (o == 7) return clamp(a1 + a2, 0.0, 1.0);
    return a2;
}
void main() {
    vec4 diff = uFlat ? vColorFlat : vColor;
    vec3 faceN = cross(dFdx(vWorldPos), dFdy(vWorldPos));  // (outside any branch, as derivatives must be)
    float levelShade = 1.0;
    if (uShadowKind != 0) {
        vec3 n = uShadowKind == 2 ? normalize(vNormal) : normalize(faceN);
        if (uShadowKind == 1 && dot(n, uEye - vWorldPos) < 0.0) n = -n;  // the side the camera sees
        float lit = dot(n, uLightDir) > 0.0 ? LitBy(vWorldPos, n) : 1.0;
        float shade = (1.0 - lit) * uShadowStrength;
        if (uShadowKind == 2) {
            vec3 a = uFlat ? vAmbFlat : vAmbPart, d = uFlat ? vDirFlat : vDirPart;
            diff.rgb = clamp(a + d * (1.0 - shade), 0.0, 1.0);
        } else {
            levelShade = 1.0 - 0.55 * shade;  // (the level's baked colours: about what the blob shadow does)
        }
    }
    vec4 result = diff;
    if (uTexOn && uColorOp != 1) {
        vec4 t = texture(uTex, vUV);
        result.rgb = op(uColorOp, pick(uColorArg1, diff, t), pick(uColorArg2, diff, t)).rgb;
        result.a = uAlphaOp == 1 ? diff.a : op(uAlphaOp, pick(uAlphaArg1, diff, t), pick(uAlphaArg2, diff, t)).a;
    }
    if (uAlphaTest && result.a < 0.5) discard;
    if (uAlphaFix != 0 && uTexOn) {
        float ta = texture(uTex, vUV).a;
        result.rgb = uAlphaFix == 1 ? result.rgb * ta : mix(vec3(1.0), result.rgb, ta);
    }
    result.rgb *= levelShade;
    if (uAOStrength > 0.0) result.rgb *= mix(1.0, texture(uAOTex, gl_FragCoord.xy / uFboSize).r, uAOStrength);
    result.rgb *= uSceneDim;
    if (uLightKind != 0) {
        // after the shadows and the AO: a torch lights a shaded corner
        vec3 n = uLightKind == 2 ? normalize(vNormal) : normalize(faceN);
        if (uLightKind == 1 && dot(n, uEye - vWorldPos) < 0.0) n = -n;
        vec3 sum = vec3(0.0);
        for (int i = 0; i < uLightCount; i++) {
            vec3 L = uLightPos[i] - vWorldPos;
            float d = length(L);
            float a = clamp(1.0 - d / uLightReach[i], 0.0, 1.0);
            float facing = clamp((dot(n, L / max(d, 1e-3)) + 0.3) / 1.3, 0.0, 1.0);  // (a little round the side too)
            sum += uLightColor[i] * (a * a * facing);
        }
        vec3 albedo = uLightKind == 2 ? vMatDiffuse.rgb : (uTexOn ? texture(uTex, vUV).rgb : vec3(1.0));
        result.rgb = min(result.rgb + albedo * sum, vec3(1.0));
    }
    fragColor = result * uConstColor;
    glowColor = uGlowMode == 1 ? fragColor : (uGlowMode == 2 ? vec4(1.0) : vec4(0.0, 0.0, 0.0, fragColor.a));
}
)GLSL";

// The level's path: its pre-lit, point-sampled meshes are drawn with one texture fetch and a
// vertex-colour multiply (D3DTOP_MODULATE with the texture and the diffuse/current colour), the
// only state the level uses at the defaults. It produces the same RGB as the full shader.
static const char* kLevelVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=2) in vec4 aColor;
layout(location=4) in vec2 aUV;
// native (R36S fragments): per-instance world matrix for batched shadows
layout(location=5) in vec4 aInst0;
layout(location=6) in vec4 aInst1;
layout(location=7) in vec4 aInst2;
layout(location=8) in vec4 aInst3;
layout(location=9) in vec4 aInstUV;    // u0, u1, v0, v1
layout(location=10) in vec2 aInstScale;  // the quad's full width and height
uniform mat4 uWorld, uView, uProj;
uniform float uBgra;
uniform bool uInstanced;
out vec4 vColor;
flat out vec4 vColorFlat;
out vec2 vUV;
void main() {
    mat4 world = uWorld;
    vec4 local = vec4(aPos, 1.0);
    vec2 uv = aUV;
    if (uInstanced) {
        world = mat4(aInst0, aInst1, aInst2, aInst3);
        local.xyz *= vec3(aInstScale.x, 1.0, aInstScale.y);
        uv = vec2(aUV.x < 0.5 ? aInstUV.x : aInstUV.y,
                  aUV.y < 0.5 ? aInstUV.z : aInstUV.w);
    }
    gl_Position = uProj * (uView * (world * local));
    vec4 c = uBgra > 0.5 ? aColor.bgra : aColor;
    vColor = c; vColorFlat = c; vUV = uv;
}
)GLSL";

static const char* kLevelFS = MBN_GLSL_HEADER R"GLSL(
in vec4 vColor;
flat in vec4 vColorFlat;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uConstColor;
uniform bool uFlat;
layout(location = 0) out vec4 fragColor;
void main() {
    vec4 diff = uFlat ? vColorFlat : vColor;
    fragColor = texture(uTex, vUV) * diff * uConstColor;
}
)GLSL";

// The base program: the same result as the full shader with every enhancement off (no ambient
// occlusion, shadows, dynamic lights or bloom), with only the varyings that path reads. It keeps
// the picture identical and cuts the per-pixel interpolation and branching on weak hardware.
static const char* kBaseVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=4) in vec2 aUV;
layout(location=3) in float aMaterial;
// native (R36S fragments): one draw for several identical opaque models; the world matrix comes
// from a per-instance attribute instead of the uniform.
layout(location=5) in vec4 aInst0;
layout(location=6) in vec4 aInst1;
layout(location=7) in vec4 aInst2;
layout(location=8) in vec4 aInst3;
uniform mat4 uWorld, uView, uProj;
uniform bool uLighting, uLightOn, uHasVertexColor, uInstanced;
uniform float uBgra;
uniform int uMatMode;
uniform vec4 uMatDiffuse[16], uMatAmbient[16], uMatEmissive[16], uAmbient, uLightDiffuse;
uniform vec3 uLightDir;
out vec4 vColor;
flat out vec4 vColorFlat;
out vec2 vUV;
vec4 vc(vec4 c) { return uBgra > 0.5 ? c.bgra : c; }
void main() {
    mat4 world = uInstanced ? mat4(aInst0, aInst1, aInst2, aInst3) : uWorld;
    vec4 wp = world * vec4(aPos, 1.0);
    gl_Position = uProj * (uView * wp);
    vec3 n = transpose(inverse(mat3(world))) * aNormal;
    int mi = uMatMode > 0 ? int(aMaterial + 0.5) : 0;
    vec4 md = uMatDiffuse[mi], ma = uMatAmbient[mi], me = uMatEmissive[mi];
    vec4 col;
    if (uLighting) {
        vec3 c = me.rgb + uAmbient.rgb * ma.rgb;
        if (uLightOn) c += uLightDiffuse.rgb * md.rgb * max(dot(n, uLightDir), 0.0);
        col = vec4(clamp(c, 0.0, 1.0), clamp(md.a, 0.0, 1.0));
    } else {
        col = uHasVertexColor ? vc(aColor) : vec4(1.0);
    }
    vColor = col; vColorFlat = col; vUV = aUV;
}
)GLSL";

static const char* kBaseFS = MBN_GLSL_HEADER R"GLSL(
in vec4 vColor;
flat in vec4 vColorFlat;
in vec2 vUV;
uniform sampler2D uTex;
uniform bool uTexOn, uFlat, uAlphaTest;
uniform int uColorOp, uColorArg1, uColorArg2, uAlphaOp, uAlphaArg1, uAlphaArg2;
uniform vec4 uConstColor;
uniform int uAlphaFix;
layout(location = 0) out vec4 fragColor;
vec4 pick(int a, vec4 diff, vec4 tex) { return a == 2 ? tex : diff; }
vec4 op(int o, vec4 a1, vec4 a2) {
    if (o == 2) return a1;
    if (o == 3) return a2;
    if (o == 4) return a1 * a2;
    if (o == 5) return clamp(a1 * a2 * 2.0, 0.0, 1.0);
    if (o == 6) return clamp(a1 * a2 * 4.0, 0.0, 1.0);
    if (o == 7) return clamp(a1 + a2, 0.0, 1.0);
    return a2;
}
void main() {
    vec4 diff = uFlat ? vColorFlat : vColor;
    vec4 result = diff;
    if (uTexOn && uColorOp != 1) {
        vec4 t = texture(uTex, vUV);
        result.rgb = op(uColorOp, pick(uColorArg1, diff, t), pick(uColorArg2, diff, t)).rgb;
        result.a = uAlphaOp == 1 ? diff.a : op(uAlphaOp, pick(uAlphaArg1, diff, t), pick(uAlphaArg2, diff, t)).a;
    }
    if (uAlphaTest && result.a < 0.5) discard;
    if (uAlphaFix != 0 && uTexOn) {
        float ta = texture(uTex, vUV).a;
        result.rgb = uAlphaFix == 1 ? result.rgb * ta : mix(vec3(1.0), result.rgb, ta);
    }
    fragColor = result * uConstColor;
}
)GLSL";

// Ambient occlusion (G3). The depth pass: view-space depth (distance along the view) of the solid
// draws, 0 where nothing is drawn.
static const char* kDepthVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uWorld, uView, uProj;
out float vZ;
void main() {
    vec4 vp = uView * (uWorld * vec4(aPos, 1.0));
    vec4 cp = uProj * vp;
    cp.z = 2.0 * cp.z - cp.w;
    gl_Position = cp;
    vZ = vp.z;
}
)GLSL";
static const char* kDepthFS = MBN_GLSL_HEADER R"GLSL(
in float vZ;
out vec4 fragZ;
void main() { fragZ = vec4(vZ); }
)GLSL";
// Dynamic shadows (G4): the solid draws seen from the light, depth only
static const char* kShadowVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
uniform mat4 uWorld, uLightVP;
void main() { gl_Position = uLightVP * (uWorld * vec4(aPos, 1.0)); }
)GLSL";
static const char* kShadowFS = MBN_GLSL_HEADER R"GLSL(
void main() {}
)GLSL";
// Bloom (G6): the "dual filter" blur. Down: a texture to half its size, each pixel from 5 reads
// (the centre and the four diagonals, half a source pixel away, each blended over 2 x 2 by the card)
static const char* kBloomDownFS = MBN_GLSL_HEADER R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel, uSrcTexel;  // the target's pixel, the source's
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    vec2 h = uSrcTexel;
    vec3 s = texture(uSrc, uv).rgb * 4.0;
    s += texture(uSrc, uv - h).rgb + texture(uSrc, uv + h).rgb;
    s += texture(uSrc, uv + vec2(h.x, -h.y)).rgb + texture(uSrc, uv - vec2(h.x, -h.y)).rgb;
    fragColor = vec4(s / 8.0, 1.0);
}
)GLSL";
// Up: a texture to twice its size, each pixel from 8 reads round it
static const char* kBloomUpFS = MBN_GLSL_HEADER R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel, uSrcTexel;
out vec4 fragColor;
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    vec2 h = uSrcTexel * 0.5;
    vec3 s = texture(uSrc, uv + vec2(-h.x * 2.0, 0.0)).rgb + texture(uSrc, uv + vec2(h.x * 2.0, 0.0)).rgb;
    s += texture(uSrc, uv + vec2(0.0, h.y * 2.0)).rgb + texture(uSrc, uv + vec2(0.0, -h.y * 2.0)).rgb;
    s += (texture(uSrc, uv + vec2(-h.x, h.y)).rgb + texture(uSrc, uv + vec2(h.x, h.y)).rgb) * 2.0;
    s += (texture(uSrc, uv + vec2(h.x, -h.y)).rgb + texture(uSrc, uv + vec2(-h.x, -h.y)).rgb) * 2.0;
    fragColor = vec4(s / 12.0, 1.0);
}
)GLSL";
// Add: the blurred glow onto the scene (blended by adding), by the strength
static const char* kBloomAddFS = MBN_GLSL_HEADER R"GLSL(
uniform sampler2D uSrc;
uniform vec2 uTexel;
uniform float uStrength;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uSrc, gl_FragCoord.xy * uTexel).rgb * uStrength, 1.0); }
)GLSL";
// A triangle over the whole target, for the passes below
static const char* kFullVS = MBN_GLSL_HEADER R"GLSL(
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";
// The occlusion: for each pixel, the surface's position and facing from the depths around it,
// then samples in the half-sphere above it, each counted as hidden where the depth there is in
// front of it (within reach: a far wall behind doesn't count). The sample pattern turns over a 4 x 4
// block of pixels, which the blur then evens out.
static const char* kAOFS = MBN_GLSL_HEADER R"GLSL(
uniform sampler2D uZ;
uniform vec2 uTexel, uProjScale;
uniform float uRadius;
uniform int uSamples;
out vec4 fragAO;
vec3 ViewPos(vec2 uv, float z) {
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * z / uProjScale.x, ndc.y * z / uProjScale.y, z);
}
float Z(vec2 uv) { return texture(uZ, uv).r; }
const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    float z = Z(uv);
    if (z <= 0.0) { fragAO = vec4(1.0); return; }
    vec3 P = ViewPos(uv, z);
    vec2 ox = vec2(uTexel.x, 0.0), oy = vec2(0.0, uTexel.y);
    float zr = Z(uv + ox), zl = Z(uv - ox), zu = Z(uv + oy), zd = Z(uv - oy);
    // the nearer neighbour on each axis, so an edge doesn't bend the facing
    vec3 dx = abs(zr - z) < abs(z - zl) ? ViewPos(uv + ox, zr) - P : P - ViewPos(uv - ox, zl);
    vec3 dy = abs(zu - z) < abs(z - zd) ? ViewPos(uv + oy, zu) - P : P - ViewPos(uv - oy, zd);
    vec3 N = normalize(cross(dx, dy));
    if (dot(N, P) > 0.0) N = -N;  // towards the camera
    ivec2 px = ivec2(gl_FragCoord.xy) & 3;
    float turn = kBayer[px.y * 4 + px.x] * (6.2831853 / 16.0);
    vec3 T = normalize(abs(N.y) < 0.9 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
    vec3 B = cross(N, T);
    float bias = 0.3 + 0.002 * z;
    float occ = 0.0;
    for (int i = 0; i < uSamples; i++) {
        float t = (float(i) + 0.5) / float(uSamples);
        float phi = float(i) * 2.3999632 + turn;
        float ct = sqrt(1.0 - t);
        float st = sqrt(1.0 - ct * ct);
        vec3 dir = T * (cos(phi) * st) + B * (sin(phi) * st) + N * ct;
        vec3 S = P + dir * (uRadius * mix(0.15, 1.0, t * t));
        if (S.z < 1.0) continue;
        vec2 suv = vec2(S.x * uProjScale.x, S.y * uProjScale.y) / S.z * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;
        float sz = Z(suv);
        if (sz <= 0.0 || sz >= S.z - bias) continue;
        // what is in front of the sample: counted by how far above this surface it stands and how
        // near it is in the level (not on screen), so a post well in front of a wall leaves no
        // dark outline on the wall
        vec3 v = ViewPos(suv, sz) - P;
        float d = length(v);
        // and not by anything much nearer the camera than this surface (a barrel or post in front of
        // a wall would otherwise draw a dark outline round itself on it)
        float nearer = P.z - sz;
        occ += max(dot(N, v) / max(d, 1e-3) - 0.1, 0.0) * (1.0 - smoothstep(0.5 * uRadius, uRadius, d)) *
               (1.0 - smoothstep(0.25 * uRadius, 0.5 * uRadius, nearer));
    }
    fragAO = vec4(pow(clamp(1.0 - 3.0 * occ / float(uSamples), 0.0, 1.0), 1.5));
}
)GLSL";
// The blur: the 4 x 4 block around each pixel, each weighed by how close its depth is (so the
// shading doesn't spread across an edge onto what is in front or behind)
static const char* kBlurFS = MBN_GLSL_HEADER R"GLSL(
uniform sampler2D uZ, uAO;
uniform vec2 uTexel;
out vec4 fragAO;
void main() {
    vec2 uv = gl_FragCoord.xy * uTexel;
    float zc = texture(uZ, uv).r;
    if (zc <= 0.0) { fragAO = vec4(1.0); return; }
    float tol = 1.0 + 0.01 * zc;
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y < 2; y++)
        for (int x = -2; x < 2; x++) {
            vec2 s = uv + vec2(float(x), float(y)) * uTexel;
            float zs = texture(uZ, s).r;
            float w = zs > 0.0 ? max(0.0, 1.0 - abs(zs - zc) / tol) : 0.0;
            sum += texture(uAO, s).r * w;
            wsum += w;
        }
    fragAO = vec4(wsum > 0.0 ? sum / wsum : 1.0);
}
)GLSL";

static const char* kPresentVS = MBN_GLSL_HEADER R"GLSL(
layout(location=0) in vec3 aPos;
layout(location=4) in vec2 aUV;
out vec2 vUV;
void main() { gl_Position = vec4(aPos.xy, 0.0, 1.0); vUV = aUV; }
)GLSL";
static const char* kPresentFS = MBN_GLSL_HEADER R"GLSL(
in vec2 vUV;
uniform sampler2D uTex;
uniform float uGamma;  // 1.0 = unchanged; brightness setting as a gamma curve
out vec4 fragColor;
void main() { fragColor = vec4(pow(texture(uTex, vUV).rgb, vec3(uGamma)), 1.0); }
)GLSL";

// ---------------------------------------------------------------------------
// Texture / Surface
// ---------------------------------------------------------------------------
// (a recorded draw may still use this texture, or its old image: it is sent first)
Texture::~Texture() {
    if (g_recordPending) g_device.FlushRecording();
    if (id_) glDeleteTextures(1, &id_);
    GetDevice().InvalidateState();  // a deleted texture's name can be reused
}

Texture* Texture::Create(const Image& img, bool colorKey, uint32_t keyRGB, bool mipmaps) {
    Texture* t = new Texture();
    t->mips_ = mipmaps;
    glGenTextures(1, &t->id_);
    t->Upload(img, colorKey, keyRGB);  // (new: no recorded draw can use it yet)
    return t;
}

void Texture::Update(const Image& img, bool colorKey, uint32_t keyRGB) {
    if (g_recordPending) g_device.FlushRecording();
    Upload(img, colorKey, keyRGB);
    GetDevice().InvalidateState();  // it bound the texture and set its filtering
}

void Texture::Upload(const Image& img, bool colorKey, uint32_t keyRGB) {
    width_ = img.width;
    height_ = img.height;
    softAlpha_ = img.hasAlpha;
    std::vector<uint32_t> px(img.pixels.size());
    for (size_t i = 0; i < px.size(); i++) {
        uint32_t c = img.pixels[i];
        uint32_t rgb = c & 0xFFFFFF;
        // a mod's PNG with transparency uses its own alpha; everything else the colour key
        uint32_t a = img.hasAlpha ? (c >> 24) : ((colorKey && rgb == keyRGB) ? 0 : 0xFF);
        // store as RGBA bytes
        uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        px[i] = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)b << 16 | a << 24;
    }
    {  // the average colour of the visible pixels (for the dynamic lights)
        double sr = 0, sg = 0, sb = 0;
        size_t n = 0;
        for (uint32_t c : px)
            if (c >> 24) sr += c & 0xFF, sg += (c >> 8) & 0xFF, sb += (c >> 16) & 0xFF, n++;
        if (n) avg_[0] = (float)(sr / n / 255.0), avg_[1] = (float)(sg / n / 255.0), avg_[2] = (float)(sb / n / 255.0);
    }
    if (colorKey || img.hasAlpha) {
        // Transparent pixels keep the key colour (e.g. black), which smooth filtering and mipmaps
        // would blend into the visible edge as a dark fringe. Give them the colour of their visible
        // neighbours instead (alpha stays 0, so point-sampled output is unchanged).
        int w = img.width, h = img.height;
        std::vector<uint8_t> filled(px.size());
        for (size_t i = 0; i < px.size(); i++) filled[i] = (px[i] >> 24) != 0;
        for (int pass = 0; pass < 4; pass++) {
            std::vector<uint32_t> next = px;
            std::vector<uint8_t> nextFilled = filled;
            bool any = false;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    size_t i = (size_t)y * w + x;
                    if (filled[i]) continue;
                    unsigned sr = 0, sg = 0, sb = 0, n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                            size_t j = (size_t)ny * w + nx;
                            if (!filled[j]) continue;
                            sr += px[j] & 0xFF; sg += (px[j] >> 8) & 0xFF; sb += (px[j] >> 16) & 0xFF;
                            n++;
                        }
                    if (!n) continue;
                    next[i] = (sr / n) | (sg / n) << 8 | (sb / n) << 16;  // alpha 0
                    nextFilled[i] = 1;
                    any = true;
                }
            px.swap(next);
            filled.swap(nextFilled);
            if (!any) break;
        }
    }
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (mips_) glGenerateMipmap(GL_TEXTURE_2D);
    else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GetDevice().InvalidateState();  // this path bound the texture outside ApplyDrawState
#ifdef MBN_TEXTURE_DIAG
    {
        static std::set<uint32_t> seen;
        if (seen.insert(((uint32_t)width_ << 16) | (uint32_t)height_).second)
            Log("Texture %dx%d mipmaps=%d", width_, height_, mips_ ? 1 : 0);
    }
    if (GLenum e = glGetError()) Log("Texture %dx%d: GL error 0x%x", width_, height_, (unsigned)e);
#endif
}

Surface::Surface(const Image& img) : image_(img) {}
Surface::~Surface() { delete tex_; }

void Surface::SetColorKey(COLORREF key) {
    keyEnabled_ = true;
    if (key == CLR_INVALID) {
        keyRGB_ = image_.pixels.empty() ? 0 : (image_.pixels[0] & 0xFFFFFF);
    } else {
        // COLORREF is 0x00BBGGRR
        keyRGB_ = (key & 0xFF) << 16 | (key & 0xFF00) | ((key >> 16) & 0xFF);
    }
    dirty_ = true;
}

void Surface::ClearColorKey() {
    keyEnabled_ = false;
    dirty_ = true;
}

Texture* Surface::Tex() {
    if (!tex_) {
        tex_ = Texture::Create(image_, keyEnabled_, keyRGB_, false);
        dirty_ = false;
    } else if (dirty_) {
        tex_->Update(image_, keyEnabled_, keyRGB_);
        dirty_ = false;
    }
    return tex_;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
static void SetIdentity(D3DMATRIX& m) {
    m = D3DMATRIX();
    m._11 = m._22 = m._33 = m._44 = 1.f;
}

GLuint Device::CompileProgram(const char* vs, const char* fs) {
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            Log("Shader compile error:\n%s", log);
        }
        return s;
    };
    GLuint p = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        Log("Program link error:\n%s", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static GLenum MapBlend(DWORD b);

bool Device::BasePath() const {
    const Config& cfg = GetConfig();
    return !cfg.ambientOcclusion && cfg.dynamicShadows == 0 && !cfg.dynamicLights && cfg.bloom == 0;
}

bool Device::LevelPath(const DrawState& st, DWORD fvf) const {
    if (fvf != D3DFVF_LVERTEX || st.lighting || !st.texture) return false;
    if (st.shadeMode == D3DSHADE_FLAT) return false;
    if (st.colorOp != D3DTOP_MODULATE || st.colorArg1 != D3DTA_TEXTURE) return false;
    if (st.colorArg2 != D3DTA_CURRENT && st.colorArg2 != D3DTA_DIFFUSE) return false;
    if (st.alphaOp != D3DTOP_SELECTARG1 && st.alphaOp != D3DTOP_MODULATE) return false;
    if (st.alphaArg1 != D3DTA_TEXTURE) return false;
    if (st.alphaArg2 != D3DTA_CURRENT && st.alphaArg2 != D3DTA_DIFFUSE) return false;
    if (st.texture->SoftAlpha()) return false;
    return true;
}

void Device::ApplyLevelDrawState(const DrawState& st) {
    Flush2D();
    activeU_ = &uLevel_;
    if (!glc_.valid || glc_.fbo != sceneFbo_) {
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
        glc_.fbo = sceneFbo_;
    }
    if (!glc_.valid || glc_.vpW != layout_.fboW || glc_.vpH != layout_.fboH) {
        glViewport(0, 0, layout_.fboW, layout_.fboH);
        glc_.vpW = layout_.fboW;
        glc_.vpH = layout_.fboH;
    }
    if (!glc_.valid || glc_.prog != progLevel_) {
        glUseProgram(progLevel_);
        glc_.valid = false;
        glc_.prog = progLevel_;
    }
    if (!glc_.valid || glc_.depthTest != (int)st.zEnable) {
        if (st.zEnable) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glc_.depthTest = (int)st.zEnable;
    }
    if (!glc_.valid || glc_.depthWrite != (int)st.zWrite) {
        glDepthMask(st.zWrite ? GL_TRUE : GL_FALSE);
        glc_.depthWrite = (int)st.zWrite;
    }
    GLenum src = st.blendEnable ? MapBlend(st.srcBlend) : GL_ONE;
    GLenum dst = st.blendEnable ? MapBlend(st.destBlend) : GL_ZERO;
    if (!glc_.valid || glc_.blend != (int)st.blendEnable ||
        (st.blendEnable && (glc_.srcBlend != src || glc_.dstBlend != dst))) {
        if (st.blendEnable) {
            glEnable(GL_BLEND);
            glBlendFunc(src, dst);
        } else {
            glDisable(GL_BLEND);
        }
        glc_.blend = (int)st.blendEnable;
        glc_.srcBlend = src;
        glc_.dstBlend = dst;
    }
    if (!glc_.valid || glc_.cull != (int)st.cullMode) {
        DWORD cull = st.cullMode;
        if (cull == D3DCULL_NONE) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glFrontFace(cull == D3DCULL_CCW ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        glc_.cull = (int)st.cullMode;
    }
    D3DMATRIX world = st.world;
    D3DMATRIX proj = DrawProj(st);
    if (!glc_.valid || !glc_.worldSet || memcmp(&glc_.world, &world, sizeof world) != 0) {
        glUniformMatrix4fv(activeU_->world, 1, GL_FALSE, world.m());
        glc_.world = world;
        glc_.worldSet = true;
    }
    if (!glc_.valid || !glc_.viewSet || memcmp(&glc_.view, &st.view, sizeof st.view) != 0) {
        glUniformMatrix4fv(activeU_->view, 1, GL_FALSE, st.view.m());
        glc_.view = st.view;
        glc_.viewSet = true;
    }
    if (!glc_.valid || !glc_.projSet || memcmp(&glc_.proj, &proj, sizeof proj) != 0) {
        glUniformMatrix4fv(activeU_->proj, 1, GL_FALSE, proj.m());
        glc_.proj = proj;
        glc_.projSet = true;
    }
    SetConstColor(1, 1, 1, 1);
    if (!glc_.valid || glc_.instanced != 0) {
        glUniform1i(uLevel_.instanced, 0);
        glc_.instanced = 0;
    }
    if (!glc_.valid) glUniform1i(activeU_->flat, 0);
    if (!glc_.valid || glc_.texture != st.texture->Id()) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, st.texture->Id());
        SetTexUnit(0);
        glc_.texture = st.texture->Id();
        glc_.minFilter = -1;
        glc_.magFilter = -1;
        glc_.anisoSet = false;
    }
    const Config& cfg = GetConfig();
    bool linMag = st.magFilter >= D3DTFG_LINEAR || cfg.smoothTextures;
    bool linMin = st.minFilter >= D3DTFG_LINEAR || cfg.smoothTextures;
    GLint minF = linMin ? GL_LINEAR : GL_NEAREST;
    bool aniso = st.texture->HasMips() && cfg.anisotropy > 1;
    if (st.texture->HasMips() && (aniso || (linMin && cfg.smoothTextures))) minF = GL_LINEAR_MIPMAP_LINEAR;
    GLint magF = linMag ? GL_LINEAR : GL_NEAREST;
    GLfloat anisoVal = (maxAniso_ > 1.f && aniso) ? std::min((float)cfg.anisotropy, maxAniso_) : 1.f;
    if (glc_.minFilter != minF || glc_.magFilter != magF || !glc_.anisoSet || glc_.aniso != anisoVal) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
        if (maxAniso_ > 1.f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, anisoVal);
        glc_.minFilter = minF;
        glc_.magFilter = magF;
        glc_.aniso = anisoVal;
        glc_.anisoSet = true;
    }
    glc_.valid = true;
}

bool Device::Init(SDL_Window* window) {
    window_ = window;
    if (!hgl::Load()) return false;
    Log("OpenGL %s | %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    auto glStr = [](GLenum e) {
        const char* s = (const char*)glGetString(e);
        return std::string(s ? s : "?");
    };
    glRenderer_ = glStr(GL_RENDERER);
    glVendor_ = glStr(GL_VENDOR);
    glVersion_ = glStr(GL_VERSION);
    glGetIntegerv(GL_SAMPLES, &windowSamples_);
    Log("Window framebuffer: MSAA %dx", windowSamples_);
    GLint maxRb = 0, maxTex = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRb);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    Log("Limits: renderbuffer %d, texture %d", (int)maxRb, (int)maxTex);
    Log("OpenGL vendor: %s", glVendor_.c_str());
#if defined(MBN_GLES)
    Log("Renderer backend: gles");
#else
    Log("Renderer backend: desktop");
#endif

    prog_ = CompileProgram(kVS, kFS);
    progBase_ = CompileProgram(kBaseVS, kBaseFS);
    progLevel_ = CompileProgram(kLevelVS, kLevelFS);
    progPresent_ = CompileProgram(kPresentVS, kPresentFS);
#define U(n) u_.n = glGetUniformLocation(prog_, "u" #n[0] == 0 ? "" : nullptr)
    auto loc = [&](const char* n) { return glGetUniformLocation(prog_, n); };
    u_.world = loc("uWorld"); u_.view = loc("uView"); u_.proj = loc("uProj");
    u_.lighting = loc("uLighting"); u_.matDiffuse = loc("uMatDiffuse[0]"); u_.matAmbient = loc("uMatAmbient[0]");
    u_.matEmissive = loc("uMatEmissive[0]"); u_.matMode = loc("uMatMode"); u_.ambient = loc("uAmbient");
    u_.lightOn = loc("uLightOn");
    u_.lightDir = loc("uLightDir"); u_.lightDiffuse = loc("uLightDiffuse"); u_.flat = loc("uFlat");
    u_.texOn = loc("uTexOn"); u_.colorOp = loc("uColorOp"); u_.colorArg1 = loc("uColorArg1");
    u_.colorArg2 = loc("uColorArg2"); u_.alphaOp = loc("uAlphaOp"); u_.alphaArg1 = loc("uAlphaArg1");
    u_.alphaArg2 = loc("uAlphaArg2"); u_.alphaTest = loc("uAlphaTest"); u_.alphaFix = loc("uAlphaFix"); u_.hasVertexColor = loc("uHasVertexColor");
    u_.tex = loc("uTex"); u_.constColor = loc("uConstColor");
    u_.screen2D = loc("uScreen2D"); u_.viewport2D = loc("uViewport2D");
    u_.bgra = loc("uBgra");
#undef U
    presentTexLoc_ = glGetUniformLocation(progPresent_, "uTex");
    presentGammaLoc_ = glGetUniformLocation(progPresent_, "uGamma");
    u_.aoStrength = loc("uAOStrength"); u_.aoTex = loc("uAOTex"); u_.fboSize = loc("uFboSize");
    u_.shadowKind = loc("uShadowKind"); u_.shadowMap = loc("uShadowMap"); u_.lightVP = loc("uLightVP");
    u_.shadowTexel = loc("uShadowTexel"); u_.shadowOffset = loc("uShadowOffset");
    u_.shadowStrength = loc("uShadowStrength"); u_.shadowGrid = loc("uShadowGrid"); u_.eye = loc("uEye");
    u_.lightKind = loc("uLightKind"); u_.lightCount = loc("uLightCount"); u_.lightPos = loc("uLightPos");
    u_.lightColor = loc("uLightColor"); u_.lightReach = loc("uLightReach"); u_.sceneDim = loc("uSceneDim");
    glUseProgram(prog_);
    glUniform1i(u_.aoTex, 1);      // (the AO texture on unit 1; the draws' own on 0)
    glUniform1i(u_.shadowMap, 2);  // (the shadow map on unit 2)
    glUniform1i(u_.matMode, 0);
    glUniform1i(u_.shadowKind, 0);
    glUniform1i(u_.lightKind, 0);
    glUniform1f(u_.sceneDim, 1.f);
    u_.glowMode = loc("uGlowMode");
    glUniform1i(u_.glowMode, 0);
    {
        auto lvl = [&](const char* n) { return glGetUniformLocation(progLevel_, n); };
        uLevel_.world = lvl("uWorld"); uLevel_.view = lvl("uView"); uLevel_.proj = lvl("uProj");
        uLevel_.bgra = lvl("uBgra"); uLevel_.tex = lvl("uTex"); uLevel_.constColor = lvl("uConstColor");
        uLevel_.flat = lvl("uFlat"); uLevel_.instanced = lvl("uInstanced");
        glUseProgram(progLevel_);
        glUniform1f(uLevel_.bgra, 1.0f);
        glUniform1i(uLevel_.tex, 0);
        glUniform1i(uLevel_.flat, 0);
        glUniform1i(uLevel_.instanced, 0);
        glUniform4f(uLevel_.constColor, 1, 1, 1, 1);
    }
    {
        auto bloc = [&](const char* n) { return glGetUniformLocation(progBase_, n); };
        uBase_.world = bloc("uWorld"); uBase_.view = bloc("uView"); uBase_.proj = bloc("uProj");
        uBase_.lighting = bloc("uLighting"); uBase_.lightOn = bloc("uLightOn");
        uBase_.hasVertexColor = bloc("uHasVertexColor"); uBase_.bgra = bloc("uBgra");
        uBase_.flat = bloc("uFlat"); uBase_.tex = bloc("uTex"); uBase_.texOn = bloc("uTexOn");
        uBase_.colorOp = bloc("uColorOp"); uBase_.colorArg1 = bloc("uColorArg1");
        uBase_.colorArg2 = bloc("uColorArg2"); uBase_.alphaOp = bloc("uAlphaOp");
        uBase_.alphaArg1 = bloc("uAlphaArg1"); uBase_.alphaArg2 = bloc("uAlphaArg2");
        uBase_.alphaTest = bloc("uAlphaTest"); uBase_.alphaFix = bloc("uAlphaFix");
        uBase_.constColor = bloc("uConstColor");
        uBase_.matDiffuse = bloc("uMatDiffuse[0]"); uBase_.matAmbient = bloc("uMatAmbient[0]");
        uBase_.matEmissive = bloc("uMatEmissive[0]"); uBase_.matMode = bloc("uMatMode");
        uBase_.ambient = bloc("uAmbient"); uBase_.lightDir = bloc("uLightDir");
        uBase_.lightDiffuse = bloc("uLightDiffuse");
        uBase_.instanced = bloc("uInstanced");
        glUseProgram(progBase_);
        glUniform1i(uBase_.instanced, 0);
    }
    progBloomDown_ = CompileProgram(kFullVS, kBloomDownFS);
    progBloomUp_ = CompileProgram(kFullVS, kBloomUpFS);
    progBloomAdd_ = CompileProgram(kFullVS, kBloomAddFS);
    progShadow_ = CompileProgram(kShadowVS, kShadowFS);
    shadowLoc_[0] = glGetUniformLocation(progShadow_, "uWorld");
    shadowLoc_[1] = glGetUniformLocation(progShadow_, "uLightVP");
    progDepth_ = CompileProgram(kDepthVS, kDepthFS);
    progAO_ = CompileProgram(kFullVS, kAOFS);
    progBlur_ = CompileProgram(kFullVS, kBlurFS);
    depthLoc_[0] = glGetUniformLocation(progDepth_, "uWorld");
    depthLoc_[1] = glGetUniformLocation(progDepth_, "uView");
    depthLoc_[2] = glGetUniformLocation(progDepth_, "uProj");
    const char* aoNames[5] = {"uZ", "uTexel", "uProjScale", "uRadius", "uSamples"};
    for (int i = 0; i < 5; i++) aoLoc_[i] = glGetUniformLocation(progAO_, aoNames[i]);
    const char* blurNames[3] = {"uZ", "uAO", "uTexel"};
    for (int i = 0; i < 3; i++) blurLoc_[i] = glGetUniformLocation(progBlur_, blurNames[i]);
    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &prepVbo_);
    glGenBuffers(1, &prepIbo_);

    // The ES attribute has no BGRA type, so the vertex shader swizzles; desktop GL does not.
#if defined(MBN_GLES)
    constexpr float kBgraSwizzle = 1.0f;
#else
    constexpr float kBgraSwizzle = 0.0f;
#endif
    if (u_.bgra >= 0) {
        glUseProgram(prog_);
        glUniform1f(u_.bgra, kBgraSwizzle);
        glUseProgram(0);
    }
    if (progBase_ && uBase_.bgra >= 0) {
        glUseProgram(progBase_);
        glUniform1f(uBase_.bgra, kBgraSwizzle);
        glUniform1i(uBase_.matMode, 0);
        glUniform1i(uBase_.tex, 0);
        glUseProgram(0);
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    // the level's 3D, recorded and sent together (G2); MBN_NO_RECORD=1 sends each draw at once,
    // as before 0.4.39, to compare pictures and timings
    gpu3D_.Init();
    gpuAO_.Init();
    gpuShadow_.Init();
    gpuBloom_.Init();
    const char* noRec = SDL_getenv("MBN_NO_RECORD");
    recordEnabled_ = !(noRec && noRec[0] && noRec[0] != '0');
    if (!recordEnabled_) Log("MBN_NO_RECORD: the level's 3D is drawn without recording");
    if (glProvokingVertex) glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);  // D3D flat shading uses vertex 0
    // macOS (and GLES) may not offer it; the game only shades smoothly today, so log it and carry on
    Log("Provoking vertex: %s", glProvokingVertex ? "available" : "absent (smooth shading only)");
    if (hgl::HasExtension("GL_ARB_texture_filter_anisotropic") || hgl::HasExtension("GL_EXT_texture_filter_anisotropic"))
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso_);

    // D3D7 default state
    for (auto& v : rs_) v = 0;
    for (auto& v : tss_) v = 0;
    rs_[D3DRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
    rs_[D3DRENDERSTATE_ZENABLE] = TRUE;
    rs_[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_GOURAUD;
    rs_[D3DRENDERSTATE_ZWRITEENABLE] = TRUE;
    rs_[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
    rs_[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ZERO;
    rs_[D3DRENDERSTATE_CULLMODE] = D3DCULL_CCW;
    rs_[D3DRENDERSTATE_LIGHTING] = TRUE;
    rs_[D3DRENDERSTATE_AMBIENT] = 0;
    tss_[D3DTSS_COLOROP] = D3DTOP_MODULATE;
    tss_[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    tss_[D3DTSS_COLORARG2] = D3DTA_CURRENT;
    tss_[D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
    tss_[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    tss_[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
    tss_[D3DTSS_MAGFILTER] = D3DTFG_POINT;
    tss_[D3DTSS_MINFILTER] = D3DTFG_POINT;
    tss_[D3DTSS_MIPFILTER] = D3DTFP_NONE;
    SetIdentity(world_);
    SetIdentity(view_);
    SetIdentity(proj_);
    OnSettingsChanged();
    return true;
}

void Device::Shutdown() {
    rec_.clear();
    recording_ = false;
    DestroyTargets();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    gpu3D_.Free();
    gpuAO_.Free();
    gpuShadow_.Free();
    gpuBloom_.Free();
    DestroyBloomTargets();
    for (GLuint* p : {&progBloomDown_, &progBloomUp_, &progBloomAdd_})
        if (*p) glDeleteProgram(*p), *p = 0;
    DestroyAOTargets();
    DestroyShadowTarget();
    if (progShadow_) glDeleteProgram(progShadow_);
    progShadow_ = 0;
    if (prepVbo_) glDeleteBuffers(1, &prepVbo_);
    if (prepIbo_) glDeleteBuffers(1, &prepIbo_);
    if (instVbo_) glDeleteBuffers(1, &instVbo_);
    if (instVao_) glDeleteVertexArrays(1, &instVao_);
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    for (GLuint* p : {&progDepth_, &progAO_, &progBlur_})
        if (*p) glDeleteProgram(*p);
    prepVbo_ = prepIbo_ = instVbo_ = instVao_ = emptyVao_ = progDepth_ = progAO_ = progBlur_ = 0;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (prog_) glDeleteProgram(prog_);
    if (progPresent_) glDeleteProgram(progPresent_);
    vbo_ = ibo_ = vao_ = prog_ = progPresent_ = 0;
}

void Device::ComputeLayout() {
    const Config& cfg = GetConfig();
    int w = 640, h = 480;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    FrameLayout L;
    L.windowW = w;
    L.windowH = h;  // (the whole window: the GL viewport counts from its bottom)
    if (h > hiddenBottomRows_ + 1) h -= hiddenBottomRows_;  // the picture goes on the visible rows
    float A = (float)w / (float)h;
    if (cfg.aspect == AspectMode::Stretch) {
        L.destX = 0; L.destY = 0; L.destW = w; L.destH = h;
    } else if (cfg.aspect == AspectMode::Widescreen && A >= 4.f / 3.f) {
        L.destX = 0; L.destY = 0; L.destW = w; L.destH = h;
        L.virtualW = 480.f * A;
        L.offsetX = (L.virtualW - 640.f) * 0.5f;
    } else {
        // pillar/letterbox to 4:3
        if (A > 4.f / 3.f) {
            L.destH = h; L.destW = (int)(h * 4.f / 3.f + 0.5f);
        } else {
            L.destW = w; L.destH = (int)(w * 3.f / 4.f + 0.5f);
        }
        L.destX = (w - L.destW) / 2;
        L.destY = (h - L.destH) / 2;
    }
    float s = cfg.renderScale;
    L.fboW = std::max(1, (int)(L.destW * s + 0.5f));
    L.fboH = std::max(1, (int)(L.destH * s + 0.5f));
    GLint maxRb = 0;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRb);
    if (maxRb < 1) maxRb = 8192;  // (a driver that will not say: the old fixed cap)
    L.fboW = std::min(L.fboW, (int)maxRb);
    L.fboH = std::min(L.fboH, (int)maxRb);
    bool changed = L.fboW != layout_.fboW || L.fboH != layout_.fboH || !sceneFbo_ ||
                   (cfg.bloom > 0) != (glowRb_ != 0);  // (Bloom on: the glow layer, G6)
    layout_ = L;
    bool wantDirect = WantsDirectWindow();
    changed = changed || wantDirect != directWindow_;
    if (changed) CreateTargets();
}

bool Device::WantsDirectWindow() const {
#if defined(MBN_GLES)
#if defined(__ANDROID__)
    // Always through the off-screen target: one path on every Android driver.
    return false;
#else
    const Config& cfg = GetConfig();
    return windowSamples_ == cfg.msaa && cfg.renderScale == 1.f && cfg.bloom == 0 && cfg.brightness == 100 &&
           layout_.destX == 0 && layout_.destY == 0 && layout_.destW == layout_.windowW &&
           layout_.destH == layout_.windowH;
#endif
#else
    return false;
#endif
}

void Device::DestroyTargets() {
    if (sceneFbo_) glDeleteFramebuffers(1, &sceneFbo_);
    if (sceneColorRb_) glDeleteRenderbuffers(1, &sceneColorRb_);
    if (sceneDepthRb_) glDeleteRenderbuffers(1, &sceneDepthRb_);
    if (glowRb_) glDeleteRenderbuffers(1, &glowRb_);
    glowRb_ = 0;
    if (resolveFbo_) glDeleteFramebuffers(1, &resolveFbo_);
    if (resolveTex_) glDeleteTextures(1, &resolveTex_);
    sceneFbo_ = sceneColorRb_ = sceneDepthRb_ = resolveFbo_ = resolveTex_ = 0;
}

void Device::CreateTargets() {
    if (WantsDirectWindow()) {
        DestroyTargets();
        directWindow_ = true;
        samples_ = windowSamples_;
        layout_.fboW = layout_.destW;
        layout_.fboH = layout_.destH;
        InvalidateState();
        InvalidateAttribs();
        Log("Render target: the window, %dx%d, MSAA %dx", layout_.fboW, layout_.fboH, samples_);
        return;
    }
    directWindow_ = false;
    DestroyTargets();
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(GetConfig().msaa, (int)maxSamples);
    if (samples_ < 2) samples_ = 0;
    int W = layout_.fboW, H = layout_.fboH;
    glGenRenderbuffers(1, &sceneColorRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneColorRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, W, H);
    glGenRenderbuffers(1, &sceneDepthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_DEPTH24_STENCIL8, W, H);
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorRb_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRb_);
    if (GetConfig().bloom > 0) {
        // the glow layer: drawn into only during a level's 3D with Bloom on (glDrawBuffers), so
        // everything else draws as before into the first layer alone
        glGenRenderbuffers(1, &glowRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, glowRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, W, H);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, glowRb_);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Log("Scene framebuffer incomplete");
    glGenTextures(1, &resolveTex_);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    resolveMips_ = layout_.destW > 0 && W > layout_.destW * 2;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, resolveMips_ ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (resolveMips_) glGenerateMipmap(GL_TEXTURE_2D);  // (the levels exist from here on)
    else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glGenFramebuffers(1, &resolveFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    InvalidateState();
    InvalidateAttribs();
    Log("Render target %dx%d, MSAA %dx", W, H, samples_);
}

void Device::OnSettingsChanged() {
    FlushRecording();
    if (window_) {
        layout_.fboW = -1;
        ComputeLayout();
    }
}

void Device::BeginFrame() {
    FlushRecording();  // (nothing should be left: SetGameplayView(false) sends it)
    Flush2D();
    InvalidateState();
    InvalidateAttribs();
    frameDraws_ = 0;
    frameUploadBytes_ = 0;
    gpu3D_.Poll();
    gpu3D_.FrameDone();
    gpuAO_.Poll();
    gpuAO_.FrameDone();
    gpuShadow_.Poll();
    gpuShadow_.FrameDone();
    gpuBloom_.Poll();
    gpuBloom_.FrameDone();
    if (frameDraws3D_ > 0) lastDraws3D_ = frameDraws3D_;
    if (frameDraws3D_ > 0) {
        lastOpaque3D_ = frameOpaque3D_;
        lastShadow3D_ = frameShadow3D_;
        lastBlended3D_ = frameBlended3D_;
    }
    frameDraws3D_ = 0;
    frameOpaque3D_ = frameShadow3D_ = frameBlended3D_ = 0;
    hudTLRight_ = -1e9f;
    hudTLBottom_ = 0.f;
    ComputeLayout();
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, layout_.fboW, layout_.fboH);
}

HRESULT Device::BeginScene() {
    inScene_ = true;
    return D3D_OK;
}

HRESULT Device::EndScene() {
    inScene_ = false;
    return D3D_OK;
}

void Device::WaitForGpu() { glFinish(); }

void Device::Present() {
    FlushRecording();
    Flush2D();
    InvalidateState();
    InvalidateAttribs();
    if (directWindow_) {
        lastFrameDraws_ = frameDraws_ + 1;
        lastFrameUploadBytes_ = frameUploadBytes_;
        frameDraws_ = frameUploadBytes_ = 0;
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
    glBlitFramebuffer(0, 0, layout_.fboW, layout_.fboH, 0, 0, layout_.fboW, layout_.fboH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, layout_.windowW, layout_.windowH);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(layout_.destX, layout_.windowH - layout_.destY - layout_.destH, layout_.destW, layout_.destH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glUseProgram(progPresent_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    if (resolveMips_) glGenerateMipmap(GL_TEXTURE_2D);
    glUniform1i(presentTexLoc_, 0);
    // brightness 150% -> exponent 0.67 (lighter mid-tones), 50% -> 2.0 (darker); black and white stay put
    glUniform1f(presentGammaLoc_, 100.f / (float)std::clamp(GetConfig().brightness, 50, 150));
    struct V { float x, y, z, u, v; } q[4] = {{-1, -1, 0, 0, 0}, {1, -1, 0, 1, 0}, {-1, 1, 0, 0, 1}, {1, 1, 0, 1, 1}};
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    BufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    for (int i = 0; i < 6; i++) glDisableVertexAttribArray(i);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)0);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)12);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    lastFrameDraws_ = frameDraws_ + 1;  // the present's own quad
    lastFrameUploadBytes_ = frameUploadBytes_;
    frameDraws_ = 0;
    frameUploadBytes_ = 0;
}

bool Device::SaveScreenshot(const char* path) {
    FlushRecording();
    Flush2D();
    InvalidateState();
    InvalidateAttribs();
    int W = layout_.fboW, H = layout_.fboH;
    std::vector<uint8_t> px((size_t)W * H * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, directWindow_ ? 0 : resolveFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // the Brightness setting, as Present applies it to the screen (left alone at 100%)
    int brightness = std::clamp(GetConfig().brightness, 50, 150);
    if (brightness != 100) {
        uint8_t curve[256];
        for (int i = 0; i < 256; i++)
            curve[i] = (uint8_t)std::lround(255.0 * std::pow(i / 255.0, 100.0 / brightness));
        for (size_t i = 0; i < px.size(); i++)
            if (i % 4 != 3) px[i] = curve[px[i]];
    }
    FILE* f = OpenFileUtf8(path, "wb");
    if (!f) return false;
    int stride = (W * 3 + 3) & ~3;
    uint32_t fileSize = 54 + stride * H;
    uint8_t hdr[54] = {'B', 'M'};
    auto w32 = [&](int o, uint32_t v) { hdr[o] = v; hdr[o + 1] = v >> 8; hdr[o + 2] = v >> 16; hdr[o + 3] = v >> 24; };
    w32(2, fileSize); w32(10, 54); w32(14, 40); w32(18, W); w32(22, H);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(stride, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t* p = &px[((size_t)y * W + x) * 4];
            row[x * 3] = p[2]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[0];
        }
        fwrite(row.data(), 1, stride, f);
    }
    fclose(f);
    return true;
}

HRESULT Device::Clear(DWORD, const RECT*, DWORD flags, D3DCOLOR color, float z, DWORD) {
    FlushRecording();
    Flush2D();
    InvalidateState();
    InvalidateAttribs();
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, layout_.fboW, layout_.fboH);
    glDisable(GL_SCISSOR_TEST);
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        glColorMask(1, 1, 1, 1);
        glClearColor(((color >> 16) & 0xFF) / 255.f, ((color >> 8) & 0xFF) / 255.f, (color & 0xFF) / 255.f, 1.f);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        glDepthMask(GL_TRUE);
#if defined(MBN_GLES)
        if (glClearDepthf) glClearDepthf((GLfloat)z);  // ES has no double-precision clear
#else
        glClearDepth(z);
#endif
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    return D3D_OK;
}

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE s, DWORD v) {
    if ((unsigned)s < D3DRENDERSTATE_MAX) rs_[s] = v;
    return D3D_OK;
}
HRESULT Device::GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) {
    if ((unsigned)s < D3DRENDERSTATE_MAX && v) *v = rs_[s];
    return D3D_OK;
}
HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, DWORD v) {
    if (stage == 0 && (unsigned)s < D3DTSS_MAX) tss_[s] = v;
    return D3D_OK;
}
HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, DWORD* v) {
    if (stage == 0 && (unsigned)s < D3DTSS_MAX && v) *v = tss_[s];
    return D3D_OK;
}
HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) {
    if (!m) return E_FAIL;
    if (t == D3DTRANSFORMSTATE_WORLD) world_ = *m;
    else if (t == D3DTRANSFORMSTATE_VIEW) view_ = *m;
    else if (t == D3DTRANSFORMSTATE_PROJECTION) proj_ = *m;
    return D3D_OK;
}
HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) {
    if (!m) return E_FAIL;
    if (t == D3DTRANSFORMSTATE_WORLD) *m = world_;
    else if (t == D3DTRANSFORMSTATE_VIEW) *m = view_;
    else if (t == D3DTRANSFORMSTATE_PROJECTION) *m = proj_;
    return D3D_OK;
}
HRESULT Device::SetMaterial(const D3DMATERIAL7* m) {
    if (m) material_ = *m;
    return D3D_OK;
}
HRESULT Device::GetMaterial(D3DMATERIAL7* m) {
    if (m) *m = material_;
    return D3D_OK;
}
HRESULT Device::SetLight(DWORD index, const D3DLIGHT7* l) {
    if (index == 0 && l) light_ = *l;
    return D3D_OK;
}
HRESULT Device::LightEnable(DWORD index, BOOL enable) {
    if (index == 0) lightEnabled_ = enable != 0;
    return D3D_OK;
}
HRESULT Device::SetTexture(DWORD stage, Texture* t) {
    if (stage == 0) texture_ = t;
    return D3D_OK;
}
HRESULT Device::GetTexture(DWORD stage, Texture** t) {
    if (t) *t = stage == 0 ? texture_ : nullptr;
    return D3D_OK;
}

static GLenum MapBlend(DWORD b) {
    switch (b) {
        case D3DBLEND_ZERO: return GL_ZERO;
        case D3DBLEND_ONE: return GL_ONE;
        case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        // the original's back buffer has no alpha channel, where DirectX reads destination alpha
        // as 1 (Mage Bros.' particles blend SRCALPHA / DESTALPHA, i.e. additively)
        case D3DBLEND_DESTALPHA: return GL_ONE;
        case D3DBLEND_INVDESTALPHA: return GL_ZERO;
        case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    }
    return GL_ONE;
}

Device::DrawState Device::CurrentState() const {
    DrawState st;
    st.world = world_;
    st.view = view_;
    st.proj = proj_;
    st.material = material_;
    st.light = light_;
    st.lightEnabled = lightEnabled_;
    st.gameplayView = gameplayView_;
    st.texture = texture_;
    st.zEnable = rs_[D3DRENDERSTATE_ZENABLE];
    st.zWrite = rs_[D3DRENDERSTATE_ZWRITEENABLE];
    st.blendEnable = rs_[D3DRENDERSTATE_ALPHABLENDENABLE];
    st.srcBlend = rs_[D3DRENDERSTATE_SRCBLEND];
    st.destBlend = rs_[D3DRENDERSTATE_DESTBLEND];
    st.cullMode = rs_[D3DRENDERSTATE_CULLMODE];
    st.lighting = rs_[D3DRENDERSTATE_LIGHTING];
    st.ambient = rs_[D3DRENDERSTATE_AMBIENT];
    st.shadeMode = rs_[D3DRENDERSTATE_SHADEMODE];
    st.colorOp = tss_[D3DTSS_COLOROP];
    st.colorArg1 = tss_[D3DTSS_COLORARG1];
    st.colorArg2 = tss_[D3DTSS_COLORARG2];
    st.alphaOp = tss_[D3DTSS_ALPHAOP];
    st.alphaArg1 = tss_[D3DTSS_ALPHAARG1];
    st.alphaArg2 = tss_[D3DTSS_ALPHAARG2];
    st.magFilter = tss_[D3DTSS_MAGFILTER];
    st.minFilter = tss_[D3DTSS_MINFILTER];
    return st;
}

void Device::ApplyState(DWORD fvf, bool twoD) {
    if (!twoD) {
        ApplyDrawState(CurrentState(), fvf);
        return;
    }
    FlushRecording();  // (2D is drawn over the 3D already asked for)
    InvalidateState();  // the 2D path sets program and state outside ApplyDrawState
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, layout_.fboW, layout_.fboH);
    glUseProgram(prog_);
    activeU_ = &u_;
    glUniform1i(u_.screen2D, 1);
    glUniform4f(u_.constColor, 1, 1, 1, 1);
    glUniform1i(u_.alphaFix, 0);
    glUniform1f(u_.aoStrength, 0.f);
    glUniform1i(u_.matMode, 0);
    glUniform1i(u_.shadowKind, 0);
    glUniform1i(u_.lightKind, 0);
    glUniform1f(u_.sceneDim, 1.f);
    glUniform1i(u_.glowMode, 0);
}

// A 3D draw's state sent to OpenGL: the draw being asked for now, or a recorded one (the state
// as it was when it was asked for).
void Device::ApplyDrawState(const DrawState& st, DWORD fvf, bool mergedMaterial) {
    Flush2D();
    if (BasePath() && LevelPath(st, fvf)) {
        ApplyLevelDrawState(st);
        return;
    }
    const Config& cfg = GetConfig();
    const bool base = BasePath();
    GLuint prog = base ? progBase_ : prog_;
    Uniforms& U = base ? uBase_ : u_;
    activeU_ = &U;
    if (!glc_.valid || glc_.fbo != sceneFbo_) {
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
        glc_.fbo = sceneFbo_;
    }
    if (!glc_.valid || glc_.vpW != layout_.fboW || glc_.vpH != layout_.fboH) {
        glViewport(0, 0, layout_.fboW, layout_.fboH);
        glc_.vpW = layout_.fboW;
        glc_.vpH = layout_.fboH;
    }
    if (!glc_.valid || glc_.prog != prog) {
        glUseProgram(prog);
        glc_.valid = false;  // the cached values belong to the other program
        glc_.prog = prog;
    }
    if (!base && (!glc_.valid || glc_.screen2D != 0)) {
        glUniform1i(U.screen2D, 0);
        glc_.screen2D = 0;
    }
    // the values the replay sets differently for some draws: cached like the rest, and reset here
    // so each draw starts from the same state
    SetConstColor(1, 1, 1, 1);
    SetAlphaFix(0);
    if (base && (!glc_.valid || glc_.instanced != 0)) {
        glUniform1i(U.instanced, 0);
        glc_.instanced = 0;
    }
    if (!base) {
        SetAOStrength(0.f);
        SetShadowKind(0);
        SetLightKind(0);
        SetSceneDim(1.f);
        SetGlowMode(0);
    }

    // depth
    if (!glc_.valid || glc_.depthTest != (int)st.zEnable) {
        if (st.zEnable) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glc_.depthTest = (int)st.zEnable;
    }
    if (!glc_.valid || glc_.depthWrite != (int)st.zWrite) {
        glDepthMask(st.zWrite ? GL_TRUE : GL_FALSE);
        glc_.depthWrite = (int)st.zWrite;
    }
    // blending
    GLenum src = st.blendEnable ? MapBlend(st.srcBlend) : GL_ONE;
    GLenum dst = st.blendEnable ? MapBlend(st.destBlend) : GL_ZERO;
    if (!glc_.valid || glc_.blend != (int)st.blendEnable || (st.blendEnable && (glc_.srcBlend != src || glc_.dstBlend != dst))) {
        if (st.blendEnable) {
            glEnable(GL_BLEND);
            glBlendFunc(src, dst);
        } else {
            glDisable(GL_BLEND);
        }
        glc_.blend = (int)st.blendEnable;
        glc_.srcBlend = src;
        glc_.dstBlend = dst;
    }
    // culling: D3DCULL_CCW removes triangles that appear counter-clockwise on screen, i.e. the
    // front faces are the visually clockwise ones (GL_CW in GL's convention).
    if (!glc_.valid || glc_.cull != (int)st.cullMode) {
        DWORD cull = st.cullMode;
        if (cull == D3DCULL_NONE) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glFrontFace(cull == D3DCULL_CCW ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        glc_.cull = (int)st.cullMode;
    }

    // transforms (with optional widescreen projection)
    D3DMATRIX world = st.world;
    D3DMATRIX proj = DrawProj(st);
    if (!glc_.valid || !glc_.worldSet || memcmp(&glc_.world, &world, sizeof world) != 0) {
        glUniformMatrix4fv(U.world, 1, GL_FALSE, world.m());
        glc_.world = world;
        glc_.worldSet = true;
    }
    if (!glc_.valid || !glc_.viewSet || memcmp(&glc_.view, &st.view, sizeof st.view) != 0) {
        glUniformMatrix4fv(U.view, 1, GL_FALSE, st.view.m());
        glc_.view = st.view;
        glc_.viewSet = true;
    }
    if (!glc_.valid || !glc_.projSet || memcmp(&glc_.proj, &proj, sizeof proj) != 0) {
        glUniformMatrix4fv(U.proj, 1, GL_FALSE, proj.m());
        glc_.proj = proj;
        glc_.projSet = true;
    }

    // lighting
    bool lighting = st.lighting != 0 && (fvf & 0x010);  // needs normals
    if (!glc_.valid || glc_.lighting != (int)lighting) {
        glUniform1i(U.lighting, lighting);
        glc_.lighting = (int)lighting;
    }
    bool hasColor = (fvf & 0x040) != 0;
    if (!glc_.valid || glc_.hasColor != (int)hasColor) {
        glUniform1i(U.hasVertexColor, hasColor);
        glc_.hasColor = (int)hasColor;
    }
    bool matChanged = !mergedMaterial && (!glc_.valid || !glc_.matSet || glc_.matMode != 0 ||
                      memcmp(glc_.matD, &st.material.diffuse.r, sizeof glc_.matD) ||
                      memcmp(glc_.matA, &st.material.ambient.r, sizeof glc_.matA) ||
                      memcmp(glc_.matE, &st.material.emissive.r, sizeof glc_.matE));
    if (matChanged) {
        SetMatMode(0);
        glUniform4fv(U.matDiffuse, 1, &st.material.diffuse.r);
        glUniform4fv(U.matAmbient, 1, &st.material.ambient.r);
        glUniform4fv(U.matEmissive, 1, &st.material.emissive.r);
        memcpy(glc_.matD, &st.material.diffuse.r, sizeof glc_.matD);
        memcpy(glc_.matA, &st.material.ambient.r, sizeof glc_.matA);
        memcpy(glc_.matE, &st.material.emissive.r, sizeof glc_.matE);
        glc_.matSet = true;
    }
    if (!glc_.valid || !glc_.ambientSet || glc_.ambient != st.ambient) {
        DWORD a = st.ambient;
        glUniform4f(U.ambient, ((a >> 16) & 0xFF) / 255.f, ((a >> 8) & 0xFF) / 255.f, (a & 0xFF) / 255.f,
                    ((a >> 24) & 0xFF) / 255.f);
        glc_.ambient = st.ambient;
        glc_.ambientSet = true;
    }
    bool lightOn = st.lightEnabled && st.light.type == D3DLIGHT_DIRECTIONAL;
    if (!glc_.valid || glc_.lightOn != (int)lightOn) {
        glUniform1i(U.lightOn, lightOn);
        glc_.lightOn = (int)lightOn;
    }
    if (lightOn) {
        D3DVECTOR d = st.light.direction;
        float len = Magnitude(d);
        if (len > 0) d = d / len;
        float dir[3] = {-d.x, -d.y, -d.z};
        if (!glc_.valid || memcmp(glc_.lightDir, dir, sizeof dir) != 0) {
            glUniform3f(U.lightDir, dir[0], dir[1], dir[2]);
            memcpy(glc_.lightDir, dir, sizeof dir);
        }
        if (!glc_.valid || memcmp(glc_.lightDiffuse, &st.light.diffuse.r, sizeof glc_.lightDiffuse) != 0) {
            glUniform4fv(U.lightDiffuse, 1, &st.light.diffuse.r);
            memcpy(glc_.lightDiffuse, &st.light.diffuse.r, sizeof glc_.lightDiffuse);
        }
    }
    bool flat = st.shadeMode == D3DSHADE_FLAT;
    if (!glc_.valid || glc_.flat != (int)flat) {
        glUniform1i(U.flat, flat);
        glc_.flat = (int)flat;
    }

    // texture stage 0
    Texture* texture = st.texture;
    bool texOn = texture != nullptr && (fvf & 0xF00) != 0;
    bool stageChanged = !glc_.valid || glc_.texOn != (int)texOn || glc_.colorOp != (int)st.colorOp ||
                        glc_.colorArg1 != (int)st.colorArg1 || glc_.colorArg2 != (int)st.colorArg2 ||
                        glc_.alphaOp != (int)st.alphaOp || glc_.alphaArg1 != (int)st.alphaArg1 ||
                        glc_.alphaArg2 != (int)st.alphaArg2;
    if (stageChanged) {
        glUniform1i(U.texOn, texOn);
        glUniform1i(U.colorOp, st.colorOp);
        glUniform1i(U.colorArg1, st.colorArg1);
        glUniform1i(U.colorArg2, st.colorArg2);
        glUniform1i(U.alphaOp, st.alphaOp);
        glUniform1i(U.alphaArg1, st.alphaArg1);
        glUniform1i(U.alphaArg2, st.alphaArg2);
        glc_.texOn = (int)texOn;
        glc_.colorOp = (int)st.colorOp;
        glc_.colorArg1 = (int)st.colorArg1;
        glc_.colorArg2 = (int)st.colorArg2;
        glc_.alphaOp = (int)st.alphaOp;
        glc_.alphaArg1 = (int)st.alphaArg1;
        glc_.alphaArg2 = (int)st.alphaArg2;
    }
    if (glc_.alphaTest != 0) {
        glUniform1i(U.alphaTest, 0);
        glc_.alphaTest = 0;
    }
    int alphaFix = 0;
    if (texOn && texture->SoftAlpha() && st.blendEnable) {
        DWORD srcB = st.srcBlend, dstB = st.destBlend;
        if (srcB == D3DBLEND_ONE) alphaFix = 1;
        else if ((srcB == D3DBLEND_ZERO && dstB == D3DBLEND_SRCCOLOR) ||
                 (srcB == D3DBLEND_DESTCOLOR && dstB == D3DBLEND_ZERO))
            alphaFix = 2;
    }
    if (alphaFix) SetAlphaFix(alphaFix);
    if (texOn) {
        if (!glc_.valid || glc_.texture != texture->Id()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture->Id());
            SetTexUnit(0);
            glc_.texture = texture->Id();
            glc_.minFilter = -1;
            glc_.magFilter = -1;
            glc_.anisoSet = false;
        }
        bool linMag = st.magFilter >= D3DTFG_LINEAR || cfg.smoothTextures;
        bool linMin = st.minFilter >= D3DTFG_LINEAR || cfg.smoothTextures;
        GLint minF = linMin ? GL_LINEAR : GL_NEAREST;
        bool aniso = texture->HasMips() && cfg.anisotropy > 1;
        if (texture->HasMips() && (aniso || (linMin && cfg.smoothTextures))) minF = GL_LINEAR_MIPMAP_LINEAR;
        GLint magF = linMag ? GL_LINEAR : GL_NEAREST;
        GLfloat anisoVal = (maxAniso_ > 1.f && aniso) ? std::min((float)cfg.anisotropy, maxAniso_) : 1.f;
        if (glc_.minFilter != minF || glc_.magFilter != magF || !glc_.anisoSet || glc_.aniso != anisoVal) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
            if (maxAniso_ > 1.f) glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, anisoVal);
            glc_.minFilter = minF;
            glc_.magFilter = magF;
            glc_.aniso = anisoVal;
            glc_.anisoSet = true;
        }
    }
    glc_.valid = true;
}

static void SetupAttribs(DWORD fvf);

void Device::EnsureAttribs(DWORD fvf) {
    if (attribFvf_ == fvf) return;
    SetupAttribs(fvf);
    attribFvf_ = fvf;
}
void Device::BindMainVao() {
    if (boundVao_ == vao_) return;
    glBindVertexArray(vao_);
    boundVao_ = vao_;
    boundElement_ = 0;  // the element buffer is part of the VAO's state
    attribFvf_ = 0;     // the attributes live in the VAO: set them up for this one
}

void Device::BindMainArray() {
    if (boundArray_ == vbo_) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    boundArray_ = vbo_;
}

void Device::BindMainElement() {
    if (boundElement_ == ibo_) return;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    boundElement_ = ibo_;
}

void Device::SetConstColor(float r, float g, float b, float a) {
    const float c[4] = {r, g, b, a};
    if (glc_.valid && glc_.constColorSet && memcmp(glc_.constColor, c, sizeof c) == 0) return;
    glUniform4f(activeU_->constColor, r, g, b, a);
    memcpy(glc_.constColor, c, sizeof c);
    glc_.constColorSet = true;
}

void Device::SetAlphaFix(int v) {
    if (glc_.valid && glc_.alphaFix == v) return;
    glUniform1i(activeU_->alphaFix, v);
    glc_.alphaFix = v;
}

void Device::SetAOStrength(float v) {
    if (glc_.valid && glc_.aoStrength == v) return;
    glUniform1f(activeU_->aoStrength, v);
    glc_.aoStrength = v;
}

void Device::SetShadowKind(int v) {
    if (glc_.valid && glc_.shadowKind == v) return;
    glUniform1i(activeU_->shadowKind, v);
    glc_.shadowKind = v;
}

void Device::SetLightKind(int v) {
    if (glc_.valid && glc_.lightKind == v) return;
    glUniform1i(activeU_->lightKind, v);
    glc_.lightKind = v;
}

void Device::SetSceneDim(float v) {
    if (glc_.valid && glc_.sceneDim == v) return;
    glUniform1f(activeU_->sceneDim, v);
    glc_.sceneDim = v;
}

void Device::SetGlowMode(int v) {
    if (glc_.valid && glc_.glowMode == v) return;
    glUniform1i(activeU_->glowMode, v);
    glc_.glowMode = v;
}

void Device::SetFboSize(float w, float h) {
    if (glc_.valid && glc_.fboSizeSet && glc_.fboW == w && glc_.fboH == h) return;
    glUniform2f(activeU_->fboSize, w, h);
    glc_.fboW = w;
    glc_.fboH = h;
    glc_.fboSizeSet = true;
}

void Device::SetTexUnit(int v) {
    if (glc_.valid && glc_.texUnit == v) return;
    glUniform1i(activeU_->tex, v);
    glc_.texUnit = v;
}

static GLenum MapPrim(D3DPRIMITIVETYPE t, DWORD n, GLsizei* count) {
    switch (t) {
        case D3DPT_POINTLIST: *count = n; return GL_POINTS;
        case D3DPT_LINELIST: *count = n; return GL_LINES;
        case D3DPT_LINESTRIP: *count = n; return GL_LINE_STRIP;
        case D3DPT_TRIANGLESTRIP: *count = n; return GL_TRIANGLE_STRIP;
        case D3DPT_TRIANGLEFAN: *count = n; return GL_TRIANGLE_FAN;
        default: *count = n; return GL_TRIANGLES;
    }
}

static void SetupAttribs(DWORD fvf) {
    for (int i = 0; i < 6; i++) glDisableVertexAttribArray(i);
    if (fvf == D3DFVF_VERTEX) {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(D3DVERTEX), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(D3DVERTEX), (void*)12);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(D3DVERTEX), (void*)24);
        glVertexAttrib4f(2, 1, 1, 1, 1);
    } else {  // D3DFVF_LVERTEX
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(D3DLVERTEX), (void*)0);
        glEnableVertexAttribArray(2);
#if defined(MBN_GLES)
        // ES has no BGRA vertex-array form (desktop's ARB_vertex_array_bgra takes GL_BGRA as the
        // size): four normalised bytes, swizzled in the shader (uBgra)
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(D3DLVERTEX), (void*)16);
#else
        glVertexAttribPointer(2, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(D3DLVERTEX), (void*)16);
#endif
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(D3DLVERTEX), (void*)24);
        glVertexAttrib4f(1, 0, 0, 0, 0);
    }
}

MeshBuffer::~MeshBuffer() { Invalidate(); }

void MeshBuffer::Invalidate() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (matVbo_) glDeleteBuffers(1, &matVbo_);
    vao_ = vbo_ = ibo_ = matVbo_ = 0;
    valid_ = merged_ = false;
    vertexCount_ = indexCount_ = matCount_ = 0;
    fvf_ = 0;
}

void Device::EnsureMesh(MeshBuffer* mesh, DWORD fvf, const void* verts, size_t numVerts, const void* indices,
                        size_t numIndices) {
    if (mesh->valid_ && mesh->fvf_ == fvf && mesh->vertexCount_ == numVerts && mesh->indexCount_ == numIndices) return;
    if (!mesh->vao_) glGenVertexArrays(1, &mesh->vao_);
    if (!mesh->vbo_) glGenBuffers(1, &mesh->vbo_);
    if (!mesh->ibo_) glGenBuffers(1, &mesh->ibo_);
    glBindVertexArray(mesh->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(numVerts * 32), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo_);
    BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(numIndices * sizeof(WORD)), indices, GL_STATIC_DRAW);
    SetupAttribs(fvf);
    mesh->valid_ = true;
    mesh->fvf_ = fvf;
    mesh->vertexCount_ = numVerts;
    mesh->indexCount_ = numIndices;
    InvalidateAttribs();  // the bindings and attribute state changed outside the main VAO
}

void Device::BindMesh(MeshBuffer* mesh) {
    if (boundVao_ != mesh->vao_) {
        glBindVertexArray(mesh->vao_);
        boundVao_ = mesh->vao_;
        boundArray_ = mesh->vbo_;
        boundElement_ = mesh->ibo_;
    }
    attribFvf_ = mesh->fvf_;  // the VAO keeps the layout; no per-draw pointer setup
}

void Device::SetMatMode(int mode) {
    if (glc_.valid && glc_.matMode == mode) return;
    glUniform1i(activeU_->matMode, mode);
    glc_.matMode = mode;
}

void Device::SetMaterialArray(const D3DMATERIAL7* materials, DWORD numMaterials) {
    if (glc_.valid && glc_.matMode == 1 && glc_.matKey == materials && glc_.matCount == numMaterials) return;
    float d[16 * 4] = {}, a[16 * 4] = {}, e[16 * 4] = {};
    DWORD n = std::min<DWORD>(numMaterials, 16);
    for (DWORD i = 0; i < n; i++) {
        const D3DMATERIAL7& m = materials[i];
        memcpy(&d[i * 4], &m.diffuse.r, 4 * sizeof(float));
        memcpy(&a[i * 4], &m.ambient.r, 4 * sizeof(float));
        memcpy(&e[i * 4], &m.emissive.r, 4 * sizeof(float));
    }
    SetMatMode(1);
    glUniform4fv(activeU_->matDiffuse, 16, d);
    glUniform4fv(activeU_->matAmbient, 16, a);
    glUniform4fv(activeU_->matEmissive, 16, e);
    glc_.matKey = materials;
    glc_.matCount = numMaterials;
}

void Device::EnsureMergedMesh(MeshBuffer* mesh, DWORD fvf, const void* verts, size_t numVerts, const void* indices,
                              size_t numIndices, const unsigned char* matIndex) {
    EnsureMesh(mesh, fvf, verts, numVerts, indices, numIndices);
    if (mesh->merged_ && mesh->matCount_ == numVerts) return;
    if (!mesh->matVbo_) glGenBuffers(1, &mesh->matVbo_);
    glBindVertexArray(mesh->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->matVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)numVerts, matIndex, GL_STATIC_DRAW);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, 1, (void*)0);
    mesh->merged_ = true;
    mesh->matCount_ = numVerts;
    InvalidateAttribs();
}

void Device::DrawMergedMesh(D3DPRIMITIVETYPE type, DWORD fvf, MeshBuffer* mesh, const void* verts, DWORD numVerts,
                            const void* indices, DWORD numIndices, const unsigned char* matIndex,
                            const D3DMATERIAL7* materials, DWORD numMaterials) {
    if (!mesh || !verts || !indices || !numIndices || !matIndex) return;
    if (recording_) {
        RecordMesh(type, fvf, mesh, verts, numVerts, indices, numIndices, 0, numIndices);
        RecordedDraw& d = rec_.back();
        d.merged = true;
        d.batchable = modelBatchable_;
        d.matIndex = matIndex;
        d.materials = materials;
        d.numMaterials = numMaterials;
        return;
    }
    frameDraws_++;
    ApplyState(fvf, false);
    EnsureMergedMesh(mesh, fvf, verts, numVerts, indices, numIndices, matIndex);
    SetMaterialArray(materials, numMaterials);
    BindMesh(mesh);
    GLsizei n;
    GLenum mode = MapPrim(type, numIndices, &n);
    glDrawElements(mode, n, GL_UNSIGNED_SHORT, (void*)0);
}

// Native (R36S fragments): a run of identical opaque merged models with only the world matrix
// changing. The per-instance matrix feeds the same vertex shader arithmetic the uniform path uses,
// so the vertices are transformed exactly as they were; no other state differs, and the order of
// the instances is the recorded order.
void Device::SetInstanced(bool on) {
    if (glc_.valid && glc_.instanced == (int)on) return;
    glUniform1i(uBase_.instanced, on);
    glc_.instanced = (int)on;
}

void Device::SetLevelInstanced(bool on) {
    if (glc_.valid && glc_.instanced == (int)on) return;
    glUniform1i(uLevel_.instanced, on);
    glc_.instanced = (int)on;
}

int Device::DrawShadowBatch(const std::vector<size_t>& indices) {
    // One batch, in the recorded order. The canonical instance keeps the vertex arithmetic the
    // same as the uniform path; the only visible difference is the rounded multiply at shadow
    // edges when shadows overlap (measured and recorded in research/acceptance).
    return DrawShadowBatchOne(indices);
}

int Device::DrawShadowBatchOne(const std::vector<size_t>& indices) {
    if (indices.empty()) return 0;
    const RecordedDraw& first = rec_[indices[0]];
    // Every shadow shares this state; only the world matrix changes. The canonical unit quad and
    // the per-instance matrix keep the vertex arithmetic the same as the uniform path, and the
    // instances stay in the recorded order so overlapping shadows blend in the same order (the
    // 8-bit multiply is not associative).
    DrawState st = first.st;
    st.world = D3DMATRIX();
    st.world._11 = st.world._22 = st.world._33 = st.world._44 = 1.0f;
    ApplyDrawState(st, D3DFVF_LVERTEX);
    SetLevelInstanced(true);
    D3DLVERTEX q[4];
    std::memset(q, 0, sizeof q);
    static const float kPos[4][3] = {{-0.5f, 0, 0.5f}, {0.5f, 0, 0.5f}, {-0.5f, 0, -0.5f}, {0.5f, 0, -0.5f}};
    static const float kUV[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int i = 0; i < 4; i++) {
        q[i].x = kPos[i][0]; q[i].y = kPos[i][1]; q[i].z = kPos[i][2];
        q[i].tu = kUV[i][0]; q[i].tv = kUV[i][1]; q[i].color = 0xFFFFFFFF;
    }
    static const WORD kIdx[6] = {0, 1, 3, 0, 3, 2};
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    BindMainElement();
    BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof kIdx, kIdx, GL_STREAM_DRAW);
    EnsureAttribs(D3DFVF_LVERTEX);
    // 16 matrix floats, the quad's u0,u1,v0,v1, then its full width and height
    std::vector<float> inst(indices.size() * 22);
    for (size_t k = 0; k < indices.size(); k++) {
        const RecordedDraw& d = rec_[indices[k]];
        const D3DLVERTEX* v = reinterpret_cast<const D3DLVERTEX*>(recVerts_.data() + d.firstVertex * 32);
        float* o = &inst[k * 22];
        std::memcpy(o, d.st.world.m(), 16 * sizeof(float));
        o[16] = v[0].tu; o[17] = v[1].tu; o[18] = v[0].tv; o[19] = v[2].tv;
        o[20] = std::fabs(v[1].x - v[0].x);
        o[21] = std::fabs(v[2].z - v[0].z);
    }
    if (!instVbo_) glGenBuffers(1, &instVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(inst.size() * sizeof(float)), inst.data(), GL_STREAM_DRAW);
    for (int k = 0; k < 4; k++) {
        glEnableVertexAttribArray(5 + k);
        glVertexAttribPointer(5 + k, 4, GL_FLOAT, GL_FALSE, 88, (void*)(uintptr_t)(k * 16));
        glVertexAttribDivisor(5 + k, 1);
    }
    glEnableVertexAttribArray(9);
    glVertexAttribPointer(9, 4, GL_FLOAT, GL_FALSE, 88, (void*)(uintptr_t)64);
    glVertexAttribDivisor(9, 1);
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, 88, (void*)(uintptr_t)80);
    glVertexAttribDivisor(10, 1);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void*)0, (GLsizei)indices.size());
    for (int k = 5; k <= 10; k++) glDisableVertexAttribArray(k);
    SetLevelInstanced(false);
    InvalidateAttribs();
    return 1;
}

void Device::DrawMergedInstanced(const std::vector<RecordedDraw>& draws, const std::vector<size_t>& indices) {
    if (indices.empty()) return;
    const RecordedDraw& d = draws[indices[0]];
    MeshBuffer* mesh = d.mesh;
    if (!mesh || !d.matIndex) return;
    EnsureMergedMesh(mesh, d.fvf, d.meshVerts, d.meshNumVerts, d.meshIndices, d.meshNumIndices, d.matIndex);
    ApplyDrawState(d.st, d.fvf, true);
    SetMaterialArray(d.materials, d.numMaterials);
    SetInstanced(true);
    std::vector<float> inst(indices.size() * 16);
    for (size_t k = 0; k < indices.size(); k++)
        std::memcpy(&inst[k * 16], draws[indices[k]].st.world.m(), 16 * sizeof(float));
    if (!instVao_) glGenVertexArrays(1, &instVao_);
    if (!instVbo_) glGenBuffers(1, &instVbo_);
    glBindVertexArray(instVao_);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void*)12);
    glDisableVertexAttribArray(2);
    glVertexAttrib4f(2, 1, 1, 1, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 32, (void*)24);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->matVbo_);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_UNSIGNED_BYTE, GL_FALSE, 1, (void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ibo_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(inst.size() * sizeof(float)), inst.data(), GL_STREAM_DRAW);
    for (int k = 0; k < 4; k++) {
        glEnableVertexAttribArray(5 + k);
        glVertexAttribPointer(5 + k, 4, GL_FLOAT, GL_FALSE, 64, (void*)(uintptr_t)(k * 16));
        glVertexAttribDivisor(5 + k, 1);
    }
    glDrawElementsInstanced(d.mode, (GLsizei)d.meshCount, GL_UNSIGNED_SHORT,
                            (void*)(d.meshFirstIndex * sizeof(WORD)), (GLsizei)indices.size());
    SetInstanced(false);
    boundVao_ = instVao_;
    boundArray_ = 0;
    boundElement_ = 0;
    attribFvf_ = 0;
    frameDraws_++;
}

void Device::RecordMesh(D3DPRIMITIVETYPE type, DWORD fvf, MeshBuffer* mesh, const void* verts, DWORD numVerts,
                        const void* indices, DWORD numIndices, DWORD firstIndex, DWORD count) {
    RecordedDraw d;
    d.st = CurrentState();
    d.fvf = fvf;
    d.indexed = true;
    d.mode = MapPrim(type, count, &d.count);
    d.mesh = mesh;
    d.meshVerts = verts;
    d.meshIndices = indices;
    d.meshNumVerts = numVerts;
    d.meshNumIndices = numIndices;
    d.meshFirstIndex = firstIndex;
    d.meshCount = count;
    d.batchable = modelBatchable_;
    rec_.push_back(d);
    g_recordPending = true;
}

void Device::DrawMeshIndexed(D3DPRIMITIVETYPE type, DWORD fvf, MeshBuffer* mesh, const void* verts, DWORD numVerts,
                             const void* indices, DWORD numIndices, DWORD firstIndex, DWORD count) {
    if (!mesh || !verts || !indices || !count) return;
    if (recording_) {
        RecordMesh(type, fvf, mesh, verts, numVerts, indices, numIndices, firstIndex, count);
        return;
    }
    frameDraws_++;
    ApplyState(fvf, false);
    EnsureMesh(mesh, fvf, verts, numVerts, indices, numIndices);
    BindMesh(mesh);
    GLsizei n;
    GLenum mode = MapPrim(type, count, &n);
    glDrawElements(mode, n, GL_UNSIGNED_SHORT, (void*)(uintptr_t)((size_t)firstIndex * sizeof(WORD)));
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts,
                                     const WORD* indices, DWORD numIndices, DWORD) {
    if (!verts || !indices || !numIndices) return D3D_OK;
    if (recording_) {
        Record(type, fvf, verts, numVerts, indices, numIndices);
        return D3D_OK;
    }
    frameDraws_++;
    ApplyState(fvf, false);
    size_t stride = fvf == D3DFVF_VERTEX ? sizeof(D3DVERTEX) : sizeof(D3DLVERTEX);
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, stride * numVerts, verts, GL_STREAM_DRAW);
    BindMainElement();
    BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(WORD) * numIndices, indices, GL_STREAM_DRAW);
    EnsureAttribs(fvf);
    GLsizei count;
    GLenum mode = MapPrim(type, numIndices, &count);
    glDrawElements(mode, count, GL_UNSIGNED_SHORT, (void*)0);
    return D3D_OK;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts, DWORD) {
    if (!verts || !numVerts) return D3D_OK;
    if (recording_) {
        Record(type, fvf, verts, numVerts, nullptr, 0);
        return D3D_OK;
    }
    frameDraws_++;
    ApplyState(fvf, false);
    size_t stride = fvf == D3DFVF_VERTEX ? sizeof(D3DVERTEX) : sizeof(D3DLVERTEX);
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, stride * numVerts, verts, GL_STREAM_DRAW);
    EnsureAttribs(fvf);
    GLsizei count;
    GLenum mode = MapPrim(type, numVerts, &count);
    glDrawArrays(mode, 0, count);
    return D3D_OK;
}

// ---------------------------------------------------------------------------
// The level's 3D, recorded and sent together (graphics enhancements, G2)
// ---------------------------------------------------------------------------
static_assert(sizeof(D3DVERTEX) == 32 && sizeof(D3DLVERTEX) == 32, "both vertex formats are 32 bytes");

void Device::SetGameplayView(bool on) {
    if (on) {
        Flush2D();
        if (!gameplayView_) recording_ = recordEnabled_;
        gameplayView_ = true;
    } else {
        FlushRecording();
        recording_ = false;
        gameplayView_ = false;
    }
}

void Device::Record(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts, const WORD* indices,
                    DWORD numIndices) {
    RecordedDraw d;
    d.st = CurrentState();
    d.fvf = fvf;
    d.indexed = indices != nullptr;
    d.mode = MapPrim(type, d.indexed ? numIndices : numVerts, &d.count);
    d.firstVertex = recVerts_.size() / 32;
    d.numVerts = numVerts;
    d.firstIndex = recIdx_.size();
    d.numIndices = d.indexed ? numIndices : 0;
    const unsigned char* v = (const unsigned char*)verts;
    recVerts_.insert(recVerts_.end(), v, v + (size_t)numVerts * 32);
    if (d.indexed) recIdx_.insert(recIdx_.end(), indices, indices + numIndices);
    rec_.push_back(d);
    g_recordPending = true;
}

void Device::FlushRecording() {
    if (rec_.empty()) return;
    // the extra passes first (G3: ambient occlusion), each on the graphics card's stopwatch
    prepUploaded_ = false;
    aoActive_ = GetConfig().ambientOcclusion && ComputeAO();
    const int shadowQuality = std::clamp(GetConfig().dynamicShadows, 0, 3);
    shadowActive_ = shadowQuality > 0 && ComputeShadows(shadowQuality);
    if (shadowActive_) {
        // what every receiving draw reads (the program keeps them)
        static const int kGrid[4] = {1, 2, 3, 4};
        glUseProgram(prog_);
        glUniformMatrix4fv(u_.lightVP, 1, GL_FALSE, lightVP_);
        glUniform1f(u_.shadowTexel, 1.f / (float)shadowSize_);
        glUniform1f(u_.shadowOffset, 1.5f * shadowTexelWorld_);
        glUniform1f(u_.shadowStrength, std::clamp(GetConfig().shadowStrength, 50, 100) / 100.f);
        glUniform1i(u_.shadowGrid, kGrid[shadowQuality]);
        glUniform3f(u_.eye, shadowEye_.x, shadowEye_.y, shadowEye_.z);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glActiveTexture(GL_TEXTURE0);
    }
    lightsActive_ = GetConfig().dynamicLights && CollectLights() > 0;
    // Ambient light below 100% (with Dynamic lights on, lights in view or not): the level dimmed
    const float sceneDim = GetConfig().dynamicLights ? std::clamp(GetConfig().lightsAmbient, 50, 100) / 100.f : 1.f;
    if (lightsActive_) {
        glUseProgram(prog_);
        glUniform1i(u_.lightCount, lightCount_);
        glUniform3fv(u_.lightPos, lightCount_, lightPos_);
        glUniform3fv(u_.lightColor, lightCount_, lightColor_);
        glUniform1fv(u_.lightReach, lightCount_, lightReach_);
        glUniform3f(u_.eye, shadowEye_.x, shadowEye_.y, shadowEye_.z);
        glUniform4f(u_.matDiffuse, 1, 1, 1, 1);
    }
    const int bloomLevel = glowRb_ ? std::clamp(GetConfig().bloom, 0, 2) : 0;
    bloomActive_ = false;
    if (bloomLevel > 0)
        for (const RecordedDraw& d : rec_)
            if (d.st.gameplayView) {
                bloomActive_ = true;
                break;
            }
    if (bloomActive_) {
        static const GLenum kBoth[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        static const GLfloat kBlack[4] = {0, 0, 0, 0};
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
        glDrawBuffers(2, kBoth);
        glClearBufferfv(GL_COLOR, 1, kBlack);
    }
    // The console's Mali driver forgets the window's draw buffer when the extra passes bind their
    // own framebuffers: after that the level still reached the window, but the HUD/fade quads went
    // nowhere and the frame looked black. Put GL_BACK back before the scene's draws.
    // The Mali driver loses the window's draw buffer when the extra passes bind their own
    // framebuffers (the shadow map also sets GL_NONE): the level still reached the window, but the
    // HUD/fade quads went nowhere and the frame looked black. Bind the window and its GL_BACK draw
    // buffer again before the scene's draws.
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    if (sceneFbo_ == 0) {
        static const GLenum kBack[1] = {0x0405};  // GL_BACK: the window's own buffer
        glDrawBuffers(1, kBack);
        glReadBuffer(0x0405);
    }
    gpu3D_.Begin();
    // Each draw exactly as it would have been sent when it was asked for: its state, then its own
    // vertices and indices uploaded on their own. (Drawing them all from one shared upload, with a
    // base vertex or attribute offsets, was tried and gave two pixels of 1-6 one shade apart on
    // NVIDIA, every time: the driver doesn't treat the two ways alike to the last bit. The
    // pictures must stay byte for byte, so the game's own draws keep their own uploads.)
    InvalidateState();
    InvalidateAttribs();
    BindMainVao();
    // how much ambient occlusion the level and the solid models take (by eye: the level's colours
    // are mostly light, the models' lit sides keep most of theirs)
    static constexpr float kAOLevel = 0.9f, kAOModel = 0.7f;
    // The enhancement state every solid draw needs, whether it comes from a mesh or the frame's
    // own upload: the glow layer, the level dim, the shadows and the dynamic lights, then the AO.
    // (Before this was only applied to the draws that are not meshes, so a merged model, and the
    // level's merged geometry, took neither the shadows nor the AO.)
    auto applyEnhancements = [&](const RecordedDraw& d) {
        if (bloomActive_ && d.st.gameplayView) {
            // the glow layer: the game's glowing sprites light it, multiplied draws leave it,
            // anything else hides what is behind it
            bool added = d.st.blendEnable && (d.st.destBlend == D3DBLEND_DESTALPHA || d.st.destBlend == D3DBLEND_ONE);
            bool multiplies = d.st.blendEnable && (d.st.srcBlend == D3DBLEND_ZERO || d.st.srcBlend == D3DBLEND_DESTCOLOR);
            bool glows = false;
            if (added && d.st.texture)
                for (uint16_t id : {0x187, 0x15c, 0x184, 0x15d, 0x185, 0x194, 0x195, 0x190, 0x192, 0x1a5, 0x294})
                    if (d.st.texture->ResId() == id) glows = true;
            // (not smoke, dust or the water's glints; nor the torches' and candles' sparks, 0x188,
            // which made the glow pulse with them, as they did the lights in 0.4.42)
            SetGlowMode(glows ? 1 : (multiplies ? 2 : 0));
        }
        if (sceneDim < 1.f && d.st.gameplayView) {
            // not the glowing (added) sprites, nor the blob shadows (multiplied onto the floor)
            bool added = d.st.blendEnable && (d.st.destBlend == D3DBLEND_DESTALPHA || d.st.destBlend == D3DBLEND_ONE);
            bool multiplied = d.st.blendEnable && d.st.srcBlend == D3DBLEND_ZERO && d.st.destBlend == D3DBLEND_SRCCOLOR;
            if (!added && !multiplied) SetSceneDim(sceneDim);
        }
        if (shadowActive_ || lightsActive_) {
            int kind = DrawKind(d);
            int way = kind == 0 ? 0 : (kind == 2 && d.st.lighting ? 2 : 1);
            if (shadowActive_ && way) SetShadowKind(way);
            if (lightsActive_ && way) SetLightKind(way);
        }
        if (aoActive_) {
            int kind = DrawKind(d);
            if (kind) {
                SetAOStrength(kind == 1 ? kAOLevel : kAOModel);
                SetFboSize((float)layout_.fboW, (float)layout_.fboH);
            }
        }
    };
    // native (R36S fragments): a run of identical opaque merged models differs only in its world
    // matrix; everything else must match for the instanced draw to be exact.
    auto sameExceptWorld = [](const RecordedDraw& a, const RecordedDraw& b) {
        static const bool allModels = std::getenv("MBN_FENCE_ONLY") == nullptr;
        if (a.fvf != b.fvf || a.mode != b.mode || a.mesh != b.mesh || !a.merged || !b.merged ||
            (!allModels && (!a.batchable || !b.batchable)) || a.meshCount != b.meshCount ||
            a.meshFirstIndex != b.meshFirstIndex || a.matIndex != b.matIndex || a.materials != b.materials ||
            a.numMaterials != b.numMaterials)
            return false;
        const DrawState& x = a.st;
        const DrawState& y = b.st;
        if (x.lightEnabled != y.lightEnabled || x.gameplayView != y.gameplayView || x.texture != y.texture)
            return false;
        if (x.zEnable != y.zEnable || x.zWrite != y.zWrite || x.blendEnable != y.blendEnable ||
            x.srcBlend != y.srcBlend || x.destBlend != y.destBlend || x.cullMode != y.cullMode ||
            x.lighting != y.lighting || x.ambient != y.ambient || x.shadeMode != y.shadeMode)
            return false;
        if (x.colorOp != y.colorOp || x.colorArg1 != y.colorArg1 || x.colorArg2 != y.colorArg2 ||
            x.alphaOp != y.alphaOp || x.alphaArg1 != y.alphaArg1 || x.alphaArg2 != y.alphaArg2 ||
            x.magFilter != y.magFilter || x.minFilter != y.minFilter)
            return false;
        if (std::memcmp(&x.view, &y.view, sizeof x.view) != 0 || std::memcmp(&x.proj, &y.proj, sizeof x.proj) != 0 ||
            std::memcmp(&x.light, &y.light, sizeof x.light) != 0)
            return false;
        return true;  // (the single material is unused by a merged draw)
    };
    int actual3D = 0;
    // native (R36S fragments): all shadow draws are the same multiplicative floor decal; they are
    // drawn together at the first shadow's place, batched by identical local data. Their order
    // between themselves does not matter (multiplication is commutative), and drawing them before
    // the other objects is the enemies' own order (shadow, then model).
    auto isShadow = [](const RecordedDraw& d) {
        return !d.mesh && d.st.blendEnable && d.st.srcBlend == D3DBLEND_ZERO && d.st.destBlend == D3DBLEND_SRCCOLOR &&
               d.fvf == D3DFVF_LVERTEX && d.indexed;
    };
    std::vector<size_t> shadowIndices;
    static const bool shadowBatch = std::getenv("MBN_NO_SHADOW_BATCH") == nullptr;
    static const bool shadowsAfter = std::getenv("MBN_SHADOWS_BEFORE") == nullptr;
    static const bool noModelBatch = std::getenv("MBN_NO_MODEL_BATCH") != nullptr;
    // The picture is byte-identical only while the opaque run keeps its order; the wider grouping
    // (across other opaque draws) is a test hook, not the shipped path.
    static const bool wideBatch = std::getenv("MBN_WIDE_BATCH") != nullptr;
    if (shadowBatch && BasePath())
        for (size_t i = 0; i < rec_.size(); i++)
            if (isShadow(rec_[i])) shadowIndices.push_back(i);
    std::vector<char> handled(rec_.size(), 0);
    bool shadowsDrawn = false;
    auto flushShadows = [&] {
        if (shadowIndices.empty() || shadowsDrawn) return;
        int n = DrawShadowBatch(shadowIndices);
        for (size_t idx : shadowIndices) handled[idx] = 1;
        actual3D += n;
        frameShadow3D_ += n;
        shadowsDrawn = true;
    };
    for (size_t i = 0; i < rec_.size(); ++i) {
        if (handled[i]) continue;
        const RecordedDraw& d = rec_[i];
        if (!shadowIndices.empty() && isShadow(d)) {
            if (!shadowsAfter) flushShadows();
            continue;
        }
        if (shadowsAfter && (d.st.blendEnable || !d.st.zEnable || !d.st.zWrite)) flushShadows();
        if (!noModelBatch && BasePath() && d.mesh && d.merged && !d.st.blendEnable && d.st.zEnable &&
            d.st.zWrite && d.meshCount > 0) {
            // Group identical opaque models that are neighbours; a blended draw (and, in the wider
            // test-only mode, any other opaque draw) is a boundary. Keeping the opaque order is
            // what makes the instanced picture byte-identical.
            std::vector<size_t> group{i};
            for (size_t j = i + 1; j < rec_.size(); ++j) {
                if (!wideBatch && handled[j]) break;  // a shadow (or a claimed draw) ends the run
                if (handled[j]) continue;
                const RecordedDraw& e = rec_[j];
                if (e.mesh && e.merged && sameExceptWorld(d, e)) {
                    group.push_back(j);
                    handled[j] = 1;
                    continue;
                }
                if (!wideBatch) break;
                if (isShadow(e)) continue;
                if (e.st.blendEnable || !e.st.zEnable || !e.st.zWrite) break;
            }
            if (group.size() >= 2) {
                DrawMergedInstanced(rec_, group);
                actual3D++;
                frameOpaque3D_++;
                continue;
            }
        }
        if (d.mesh) {
            frameOpaque3D_++;
            if (d.merged)
                EnsureMergedMesh(d.mesh, d.fvf, d.meshVerts, d.meshNumVerts, d.meshIndices, d.meshNumIndices,
                                 d.matIndex);
            else
                EnsureMesh(d.mesh, d.fvf, d.meshVerts, d.meshNumVerts, d.meshIndices, d.meshNumIndices);
            ApplyDrawState(d.st, d.fvf, d.merged);
            if (d.merged) SetMaterialArray(d.materials, d.numMaterials);
            applyEnhancements(d);
            BindMesh(d.mesh);
            glDrawElements(d.mode, (GLsizei)d.meshCount, GL_UNSIGNED_SHORT,
                           (void*)(uintptr_t)(d.meshFirstIndex * sizeof(WORD)));
            actual3D++;
            continue;
        }
        if (!d.mesh) {
            bool shadow = d.st.blendEnable && d.st.srcBlend == D3DBLEND_ZERO && d.st.destBlend == D3DBLEND_SRCCOLOR;
            if (shadow) frameShadow3D_++;
            else if (d.st.blendEnable) frameBlended3D_++;
            else frameOpaque3D_++;
        }
        ApplyDrawState(d.st, d.fvf);
        applyEnhancements(d);
        BindMainVao();  // dynamic draws use the main layout, not the last model's VAO
        BindMainArray();
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(d.numVerts * 32), recVerts_.data() + d.firstVertex * 32,
                     GL_STREAM_DRAW);
        if (d.indexed) {
            BindMainElement();
            BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(d.numIndices * sizeof(WORD)),
                         recIdx_.data() + d.firstIndex, GL_STREAM_DRAW);
            EnsureAttribs(d.fvf);
            glDrawElements(d.mode, d.count, GL_UNSIGNED_SHORT, (void*)0);
        } else {
            EnsureAttribs(d.fvf);
            glDrawArrays(d.mode, 0, d.count);
        }
        actual3D++;
    }
    flushShadows();
    gpu3D_.End();
    if (bloomActive_) {
        static const GLenum kFirst[1] = {GL_COLOR_ATTACHMENT0};
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
        glDrawBuffers(1, kFirst);
        ComputeBloom(bloomLevel);
        bloomActive_ = false;
    }
    aoActive_ = false;
    shadowActive_ = false;
    lightsActive_ = false;
    frameDraws3D_ += actual3D;
    frameDraws_ += actual3D;
    rec_.clear();
    recVerts_.clear();
    recIdx_.clear();
    g_recordPending = false;
}

void Device::GpuTimer::Init() {
#if defined(MBN_GLES)
    // GL_TIME_ELAPSED is core on desktop GL; on ES 3.0 it needs the timer-query extension.
    if (!hgl::HasExtension("GL_EXT_disjoint_timer_query") &&
        !hgl::HasExtension("GL_EXT_disjoint_timer_query_webgl2"))
        return;
#endif
    glGenQueries(kQueries, q);
}
void Device::GpuTimer::Free() {
    if (q[0]) glDeleteQueries(kQueries, q);
    for (auto& x : q) x = 0;
}
void Device::GpuTimer::Begin() {
    running = false;
    if (busy[next] || !q[next]) return;
    glBeginQuery(GL_TIME_ELAPSED, q[next]);
    busy[next] = true;
    next = (next + 1) % kQueries;
    running = timedThisFrame = true;
}
void Device::GpuTimer::End() {
    if (running) glEndQuery(GL_TIME_ELAPSED);
    running = false;
}
void Device::GpuTimer::Poll() {
    for (int i = 0; i < kQueries; i++) {
        if (!busy[i]) continue;
#if defined(MBN_GLES)
        GLuint ready = 0;
        glGetQueryObjectuiv(q[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint ns32 = 0;
        glGetQueryObjectuiv(q[i], GL_QUERY_RESULT, &ns32);
        GLuint64 ns = ns32;  // one frame's 3D: nowhere near the 32-bit wrap
#else
        GLint ready = 0;
        glGetQueryObjectiv(q[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q[i], GL_QUERY_RESULT, &ns);
#endif
        sumMs += (double)ns / 1e6;
        busy[i] = false;
    }
}
void Device::GpuTimer::FrameDone() {
    if (timedThisFrame) frames++;
    timedThisFrame = false;
}
void Device::GpuTimer::Take(double* s, int* f) {
    *s = sumMs;
    *f = frames;
    sumMs = 0;
    frames = 0;
}

// ---------------------------------------------------------------------------
// Ambient occlusion (graphics enhancements, G3)
// ---------------------------------------------------------------------------

// The lens a 3D draw is drawn with: its projection, widened for widescreen and, in levels, scaled
// by the Field of view (the same arithmetic ApplyDrawState has always used)
D3DMATRIX Device::DrawProj(const DrawState& st) const {
    const Config& cfg = GetConfig();
    D3DMATRIX proj = st.proj;
    proj._11 *= 640.f / layout_.virtualW;
    if (cfg.fovScale != 1.f && st.gameplayView) {  // levels only: menus, title and ending keep the original lens
        proj._11 /= cfg.fovScale;
        proj._22 /= cfg.fovScale;
    }
    return proj;
}

// What a recorded draw is, from its state alone: the level's pre-lit geometry (and any other
// solid pre-lit surface), a solid model part, or anything see-through (blob shadows, particles,
// shots, water, see-through model parts), which neither hides others from the AO nor takes it.
int Device::DrawKind(const RecordedDraw& d) {
    const DrawState& st = d.st;
    if (st.blendEnable || !st.zEnable || !st.zWrite) return 0;
    return d.fvf == D3DFVF_VERTEX ? 2 : 1;
}

void Device::DestroyAOTargets() {
    for (GLuint& f : aoFbo_)
        if (f) glDeleteFramebuffers(1, &f), f = 0;
    for (GLuint& t : aoTex_)
        if (t) glDeleteTextures(1, &t), t = 0;
    if (aoZTex_) glDeleteTextures(1, &aoZTex_);
    if (aoDepthRb_) glDeleteRenderbuffers(1, &aoDepthRb_);
    aoZTex_ = aoDepthRb_ = 0;
    aoW_ = aoH_ = 0;
}

void Device::EnsureAOTargets(int w, int h) {
    if (w == aoW_ && h == aoH_ && aoFbo_[0]) return;
    DestroyAOTargets();
    aoW_ = w;
    aoH_ = h;
    auto tex = [&](GLuint* t, GLint ifmt, GLenum fmt, GLenum type, GLint filter) {
        glGenTextures(1, t);
        glBindTexture(GL_TEXTURE_2D, *t);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    };
    glActiveTexture(GL_TEXTURE0);
    tex(&aoZTex_, GL_R32F, GL_RED, GL_FLOAT, GL_NEAREST);
    tex(&aoTex_[0], GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
    tex(&aoTex_[1], GL_R8, GL_RED, GL_UNSIGNED_BYTE, GL_LINEAR);
    glGenRenderbuffers(1, &aoDepthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, aoDepthRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(3, aoFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoZTex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, aoDepthRb_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Log("AO depth framebuffer incomplete");
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[i + 1]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoTex_[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Log("AO framebuffer incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Log("Ambient occlusion targets %dx%d", w, h);
}

// The frame's vertices and indices in one upload (once a flush, for whichever extra pass comes
// first), the position pointed at for a depth-only draw
void Device::UploadPrep() {
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, prepVbo_);
    if (!prepUploaded_)
        BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)recVerts_.size(), recVerts_.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prepIbo_);
    if (!prepUploaded_ && !recIdx_.empty())
        BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(recIdx_.size() * sizeof(WORD)), recIdx_.data(),
                     GL_STREAM_DRAW);
    prepUploaded_ = true;
    for (int i = 0; i < 6; i++) glDisableVertexAttribArray(i);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void*)0);  // (the position leads both formats)
    InvalidateAttribs();
    // the cache follows the bindings made above, so a following mesh bind is not skipped
    boundVao_ = vao_;
    boundArray_ = prepVbo_;
    boundElement_ = prepIbo_;
}

// The extra passes (the shadow map, the AO's depth) draw the same recorded solids the main pass
// does. A merged model keeps its own buffers: its vertices are not in the frame's single upload,
// so it must be drawn from its mesh. Drawing it with the shared base vertex read unrelated
// vertices, which left the level out of both maps (the enhancements then had nothing to apply).
void Device::DrawRecordedSolid(const RecordedDraw& d) {
    if (d.mesh) {
        if (d.merged)
            EnsureMergedMesh(d.mesh, d.fvf, d.meshVerts, d.meshNumVerts, d.meshIndices, d.meshNumIndices, d.matIndex);
        else
            EnsureMesh(d.mesh, d.fvf, d.meshVerts, d.meshNumVerts, d.meshIndices, d.meshNumIndices);
        BindMesh(d.mesh);
        glDrawElements(d.mode, (GLsizei)d.meshCount, GL_UNSIGNED_SHORT,
                       (void*)(uintptr_t)(d.meshFirstIndex * sizeof(WORD)));
        return;
    }
    UploadPrep();
    if (d.indexed)
        glDrawElementsBaseVertex(d.mode, d.count, GL_UNSIGNED_SHORT, (void*)(d.firstIndex * sizeof(WORD)),
                                 (GLint)d.firstVertex);
    else
        glDrawArrays(d.mode, (GLint)d.firstVertex, d.count);
}

// ---------------------------------------------------------------------------
// Bloom (graphics enhancements, G6)
// ---------------------------------------------------------------------------

void Device::DestroyBloomTargets() {
    for (int k = 0; k < kBloomLevels; k++) {
        if (bloomFbo_[k]) glDeleteFramebuffers(1, &bloomFbo_[k]);
        if (bloomTex_[k]) glDeleteTextures(1, &bloomTex_[k]);
        bloomFbo_[k] = bloomTex_[k] = 0;
    }
    bloomW_ = bloomH_ = 0;
}

// Level 0 at the scene target's size (what the glow layer is resolved into: the same format, as a
// multisampled resolve needs), then each level half the one before, kept in half floats
void Device::EnsureBloomTargets(int w, int h) {
    if (w == bloomW_ && h == bloomH_ && bloomFbo_[0]) return;
    DestroyBloomTargets();
    bloomW_ = w;
    bloomH_ = h;
    glActiveTexture(GL_TEXTURE0);
    for (int k = 0; k < kBloomLevels; k++) {
        int lw = std::max(1, w >> k), lh = std::max(1, h >> k);
        glGenTextures(1, &bloomTex_[k]);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[k]);
        if (k == 0) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, lw, lh, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glGenFramebuffers(1, &bloomFbo_[k]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[k]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[k], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Log("Bloom framebuffer incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Log("Bloom targets %dx%d", w, h);
}

// The glow layer resolved, blurred down the chain and back up, and added onto the scene. Low goes
// 4 levels down (a sixteenth of the size), High 6 (a sixty-fourth: a wider, softer glow).
void Device::ComputeBloom(int level) {
    const int W = layout_.fboW, H = layout_.fboH;
    EnsureBloomTargets(W, H);
    gpuBloom_.Begin();
    // the glow layer into level 0 (a multisampled resolve)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, bloomFbo_[0]);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);  // (Present and screenshots read the first layer)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(emptyVao_);
    glActiveTexture(GL_TEXTURE0);
    const int depth = level >= 2 ? 6 : 4;
    auto pass = [&](GLuint prog, int src, int dst) {
        int dw = std::max(1, W >> dst), dh = std::max(1, H >> dst);
        int sw = std::max(1, W >> src), sh = std::max(1, H >> src);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[dst]);
        glViewport(0, 0, dw, dh);
        glUseProgram(prog);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[src]);
        glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
        glUniform2f(glGetUniformLocation(prog, "uTexel"), 1.f / dw, 1.f / dh);
        glUniform2f(glGetUniformLocation(prog, "uSrcTexel"), 1.f / sw, 1.f / sh);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    for (int k = 1; k <= depth; k++) pass(progBloomDown_, k - 1, k);
    // back up, each level adding the wider blur onto its own (so the glow stays strong near its
    // source and fades softly further out; overwriting it gave a glow too faint to see)
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int k = depth - 1; k >= 1; k--) pass(progBloomUp_, k + 1, k);
    glDisable(GL_BLEND);
    // onto the scene (the first layer), added
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(progBloomAdd_);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[1]);
    glUniform1i(glGetUniformLocation(progBloomAdd_, "uSrc"), 0);
    glUniform2f(glGetUniformLocation(progBloomAdd_, "uTexel"), 1.f / W, 1.f / H);
    glUniform1f(glGetUniformLocation(progBloomAdd_, "uStrength"), level >= 2 ? 0.45f : 0.3f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glBindVertexArray(vao_);
    gpuBloom_.End();
}

// ---------------------------------------------------------------------------
// Dynamic shadows (graphics enhancements, G4)
// ---------------------------------------------------------------------------

void Device::DestroyShadowTarget() {
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    shadowFbo_ = shadowTex_ = 0;
    shadowSize_ = 0;
}

void Device::EnsureShadowTarget(int size) {
    if (size == shadowSize_ && shadowFbo_) return;
    DestroyShadowTarget();
    shadowSize_ = size;
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    // GL_UNSIGNED_INT is the depth type both backends accept (ES rejects GL_FLOAT here).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);  // (the card blends 2 x 2 comparisons)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
#if defined(MBN_GLES)
    static const GLenum kNone[1] = {GL_NONE};  // no colour attachment on the shadow map
    glDrawBuffers(1, kNone);
#else
    glDrawBuffer(GL_NONE);
#endif
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) Log("Shadow framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    Log("Dynamic shadows map %dx%d", size, size);
}

// ---------------------------------------------------------------------------
// Dynamic lights (graphics enhancements, G5)
// ---------------------------------------------------------------------------

// The game's glowing sprites that give light, by texture: how far (world units) and how bright.
// Their colour is the texture's own average, times the sprite's colour and fade. Not lights: the
// smoke (0x198), the shots' trails (0x15d, 0x185), coin sparkles (0x294), landing dust (0x295),
// water waves (0x18f) and the water's glints. Nor the torches' and candles' sparks (0x188): a lit
// torch or candle keeps one steady flame (0x187, only its size flickers) and sends a spark up
// every 12 or 15 ticks that fades in about a quarter of a second; lit by the sparks, every torch
// strobed about 8 times a second (found by the owner in 0.4.42), so only the steady flame lights.
struct LightSource {
    uint16_t tex;
    float reach, bright;
};
static const LightSource kLightSources[] = {
    {0x187, 160.f, 1.0f},  // torch and candle flames, the steady ones (the candles hang high on the walls)
    {0x15c, 100.f, 1.1f},  // player 1's shot
    {0x184, 100.f, 1.1f},  // player 2's shot
    {0x194, 90.f, 0.9f},   // the shots' flash (player 1), the wizard's wand
    {0x195, 90.f, 0.9f},   // the shots' flash (player 2)
    {0x190, 140.f, 1.3f},  // enemy explosions
    {0x192, 120.f, 1.1f},  // the dragon's flame, bullet explosions
    {0x1a5, 80.f, 0.5f},   // the warp ring
};

// The frame's lights from the recorded additive sprites: each at its sprite's centre, merged with
// any already found within 20 units (so a torch's few flames make one light), the brightest 16
// kept. Returns how many.
int Device::CollectLights() {
    struct Found {
        D3DVECTOR pos;
        float w;            // the total weight (for the merged position)
        float rgb[3];
        float reach;
    };
    std::vector<Found> found;
    const float strength = std::clamp(GetConfig().lightStrength, 50, 150) / 100.f;
    for (const RecordedDraw& d : rec_) {
        const DrawState& st = d.st;
        if (!st.gameplayView || !st.blendEnable || !st.texture || d.fvf != D3DFVF_LVERTEX) continue;
        if (st.destBlend != D3DBLEND_DESTALPHA && st.destBlend != D3DBLEND_ONE) continue;  // additive only
        const LightSource* src = nullptr;
        for (const LightSource& l : kLightSources)
            if (l.tex == st.texture->ResId()) src = &l;
        if (!src || d.numVerts == 0) continue;
        // the sprite's centre in the level and its average colour and fade
        const D3DLVERTEX* v = (const D3DLVERTEX*)(recVerts_.data() + d.firstVertex * 32);
        float cx = 0, cy = 0, cz = 0, cr = 0, cg = 0, cb = 0, ca = 0;
        for (size_t k = 0; k < d.numVerts; k++) {
            cx += v[k].x, cy += v[k].y, cz += v[k].z;
            D3DCOLOR col = v[k].color;
            cr += ((col >> 16) & 0xFF) / 255.f, cg += ((col >> 8) & 0xFF) / 255.f, cb += (col & 0xFF) / 255.f;
            ca += ((col >> 24) & 0xFF) / 255.f;
        }
        float inv = 1.f / (float)d.numVerts;
        cx *= inv, cy *= inv, cz *= inv, cr *= inv, cg *= inv, cb *= inv, ca *= inv;
        const D3DMATRIX& W = st.world;  // (row vectors)
        D3DVECTOR pos(cx * W._11 + cy * W._21 + cz * W._31 + W._41, cx * W._12 + cy * W._22 + cz * W._32 + W._42,
                      cx * W._13 + cy * W._23 + cz * W._33 + W._43);
        // the texture's hue at full brightness, then the sprite's colour, its fade and the source's own
        const float* avg = st.texture->AvgColor();
        float peak = std::max({avg[0], avg[1], avg[2], 1e-3f});
        float k = src->bright * ca * strength;
        if (k <= 0.01f) continue;
        float rgb[3] = {avg[0] / peak * cr * k, avg[1] / peak * cg * k, avg[2] / peak * cb * k};
        Found* into = nullptr;
        for (Found& f : found)
            if (Magnitude(f.pos - pos) < 20.f) {
                into = &f;
                break;
            }
        if (!into) {
            found.push_back({pos, k, {rgb[0], rgb[1], rgb[2]}, src->reach});
            continue;
        }
        float tw = into->w + k;
        into->pos = (into->pos * into->w + pos * k) / tw;
        into->w = tw;
        for (int j = 0; j < 3; j++) into->rgb[j] = std::min(into->rgb[j] + rgb[j], 2.f);  // (a cluster, not a sun)
        into->reach = std::max(into->reach, src->reach);
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        return a.rgb[0] + a.rgb[1] + a.rgb[2] > b.rgb[0] + b.rgb[1] + b.rgb[2];
    });
    lightCount_ = std::min((int)found.size(), kMaxLights);
    for (int i = 0; i < lightCount_; i++) {
        const Found& f = found[(size_t)i];
        lightPos_[i * 3] = f.pos.x, lightPos_[i * 3 + 1] = f.pos.y, lightPos_[i * 3 + 2] = f.pos.z;
        for (int j = 0; j < 3; j++) lightColor_[i * 3 + j] = f.rgb[j];
        lightReach_[i] = f.reach;
    }
    if (lightCount_ > 0) lastLights_ = lightCount_;
    // the camera, for the side of a surface the lights shine on (ComputeShadows works it out too)
    for (const RecordedDraw& d : rec_)
        if (d.st.gameplayView) {
            const D3DMATRIX& V = d.st.view;
            D3DVECTOR t(V._41, V._42, V._43);
            shadowEye_ = D3DVECTOR(-(t.x * V._11 + t.y * V._12 + t.z * V._13), -(t.x * V._21 + t.y * V._22 + t.z * V._23),
                                   -(t.x * V._31 + t.y * V._32 + t.z * V._33));
            break;
        }
    return lightCount_;
}

// The shadow map for the recorded draws (false when there is no level or no directional light).
bool Device::ComputeShadows(int quality) {
    const RecordedDraw* lens = nullptr;
    for (const RecordedDraw& d : rec_)
        if (d.st.gameplayView && DrawKind(d)) {
            lens = &d;
            break;
        }
    if (!lens || !lens->st.lightEnabled || lens->st.light.type != D3DLIGHT_DIRECTIONAL) return false;
    D3DVECTOR travel = lens->st.light.direction;
    if (Magnitude(travel) <= 0.f) return false;
    static const int kSize[4] = {0, 1024, 2048, 4096};
    EnsureShadowTarget(kSize[quality]);

    // 1. What the camera sees of the level. The levels lie along z, between x -60 and 260 and y
    //    -260 and 300 (the floors, the pits and cliff faces, the walls; the camera is on the +x
    //    side, at y 315, looking down). Rays through a grid of the screen, from the near plane to
    //    the far (10 to 1000 along the view), clipped to that slab: the z range they cover, which
    //    follows any window shape, Field of view and the camera's shake.
    const D3DMATRIX& V = lens->st.view;
    const D3DMATRIX P = DrawProj(lens->st);
    D3DVECTOR t(V._41, V._42, V._43);
    D3DVECTOR eye(-(t.x * V._11 + t.y * V._12 + t.z * V._13), -(t.x * V._21 + t.y * V._22 + t.z * V._23),
                  -(t.x * V._31 + t.y * V._32 + t.z * V._33));
    const float x0 = -60.f, x1 = 260.f, y0 = -260.f, y1 = 300.f;
    float zMin = 1e9f, zMax = -1e9f;
    for (int j = 0; j <= 8; j++)
        for (int i = 0; i <= 8; i++) {
            float nx = -1.f + i / 4.f, ny = -1.f + j / 4.f;
            D3DVECTOR dv(nx / P._11, ny / P._22, 1.f);  // (in the view: z = the distance along it)
            D3DVECTOR d(dv.x * V._11 + dv.y * V._12 + dv.z * V._13, dv.x * V._21 + dv.y * V._22 + dv.z * V._23,
                        dv.x * V._31 + dv.y * V._32 + dv.z * V._33);
            float tIn = 10.f, tOut = 1000.f;
            auto slab = [&](float o, float dd, float lo, float hi) {
                if (std::fabs(dd) < 1e-6f) {
                    if (o < lo || o > hi) tOut = -1.f;
                    return;
                }
                float a = (lo - o) / dd, b = (hi - o) / dd;
                if (a > b) std::swap(a, b);
                tIn = std::max(tIn, a);
                tOut = std::min(tOut, b);
            };
            slab(eye.x, d.x, x0, x1);
            slab(eye.y, d.y, y0, y1);
            if (tIn >= tOut) continue;
            for (float tt : {tIn, tOut}) {
                float z = eye.z + d.z * tt;
                zMin = std::min(zMin, z);
                zMax = std::max(zMax, z);
            }
        }
    if (zMin > zMax) return false;
    // things nearer the light (further along -z: the light comes from +x, above and -z) can cast
    // into view: up to the slab's height times its slant
    zMin -= 280.f;
    zMax += 40.f;

    // 2. The light's view: an orthographic box round that part of the level, along the light. Its
    //    size rounded up to steps and its centre snapped to whole map pixels, so the shadows' edges
    //    stay put as the camera follows the mage.
    D3DVECTOR f = Normalize(travel);
    D3DVECTOR up = std::fabs(f.y) < 0.99f ? D3DVECTOR(0, 1, 0) : D3DVECTOR(1, 0, 0);
    D3DVECTOR r = Normalize(CrossProduct(up, f));
    D3DVECTOR u = CrossProduct(f, r);
    float lx0 = 1e9f, lx1 = -1e9f, ly0 = 1e9f, ly1 = -1e9f, lz0 = 1e9f, lz1 = -1e9f;
    for (int k = 0; k < 8; k++) {
        D3DVECTOR c((k & 1) ? x1 : x0, (k & 2) ? y1 : y0, (k & 4) ? zMax : zMin);
        float a = DotProduct(c, r), b = DotProduct(c, u), z = DotProduct(c, f);
        lx0 = std::min(lx0, a), lx1 = std::max(lx1, a);
        ly0 = std::min(ly0, b), ly1 = std::max(ly1, b);
        lz0 = std::min(lz0, z), lz1 = std::max(lz1, z);
    }
    const float step = 64.f;
    float w = std::ceil((lx1 - lx0) / step) * step, h = std::ceil((ly1 - ly0) / step) * step;
    float side = std::max(w, h);  // (square pixels)
    float texel = side / (float)shadowSize_;
    float cx = std::floor((lx0 + lx1) * 0.5f / texel) * texel, cy = std::floor((ly0 + ly1) * 0.5f / texel) * texel;
    float zn = lz0 - 10.f, zf = lz1 + 10.f;
    shadowTexelWorld_ = texel;
    shadowEye_ = eye;
    // clip = M * world, M column-major for OpenGL
    float sx = 2.f / side, sz = 2.f / (zf - zn);
    float rows[3][4] = {{r.x * sx, r.y * sx, r.z * sx, -cx * sx},
                        {u.x * sx, u.y * sx, u.z * sx, -cy * sx},
                        {f.x * sz, f.y * sz, f.z * sz, -zn * sz - 1.f}};
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 3; row++) lightVP_[col * 4 + row] = rows[row][col];
        lightVP_[col * 4 + 3] = col == 3 ? 1.f : 0.f;
    }

    // 3. The map: the solid draws' depth from the light (both sides of every face, pushed back a
    //    little by their slope so surfaces don't shadow themselves)
    gpuShadow_.Begin();
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
#if defined(MBN_GLES)
    if (glClearDepthf) glClearDepthf(1.f);
#else
    glClearDepth(1.0);
#endif
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.f);
    glUseProgram(progShadow_);
    glUniformMatrix4fv(shadowLoc_[1], 1, GL_FALSE, lightVP_);
    UploadPrep();
    for (const RecordedDraw& d : rec_) {
        if (!DrawKind(d)) continue;
        glUniformMatrix4fv(shadowLoc_[0], 1, GL_FALSE, d.st.world.m());
        DrawRecordedSolid(d);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    gpuShadow_.End();
    return true;
}

// The AO texture for the recorded draws (false when there is nothing solid in a level to shade).
// Leaves it on texture unit 1 for the main pass.
bool Device::ComputeAO() {
    const RecordedDraw* lens = nullptr;
    for (const RecordedDraw& d : rec_)
        if (d.st.gameplayView && DrawKind(d)) {
            lens = &d;
            break;
        }
    if (!lens) return false;
    // at the picture's size (more pixels from Render scale add nothing to a soft shading). (0.4.39
    // also had Low, at half the size with half the samples: removed, the owner found it looked bad.)
    // The console's Mali-G31 cannot afford 16 full-size occlusion samples (about 100 ms a frame).
    // Half the picture and half the samples is soft enough for the 640x480 screen and keeps the
    // setting usable; the desktop keeps the full-size version.
#if defined(MBN_GLES)
    static constexpr int kAOScale = 2, kAOSamples = 8;
#else
    static constexpr int kAOScale = 1, kAOSamples = 16;
#endif
    int w = std::max(1, std::min(layout_.destW, layout_.fboW) / kAOScale);
    int h = std::max(1, std::min(layout_.destH, layout_.fboH) / kAOScale);
    EnsureAOTargets(w, h);
    gpuAO_.Begin();

    // 1. the depth pass: the solid draws, from one upload of the frame's vertices (depth only, so
    //    the shared buffer's last-bit differences can't show)
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[0]);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(1, 1, 1, 1);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);
#if defined(MBN_GLES)
    if (glClearDepthf) glClearDepthf(1.f);
#else
    glClearDepth(1.0);
#endif
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(progDepth_);
    UploadPrep();
    for (const RecordedDraw& d : rec_) {
        if (!DrawKind(d)) continue;
        if (d.st.cullMode == D3DCULL_NONE) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glFrontFace(d.st.cullMode == D3DCULL_CCW ? GL_CW : GL_CCW);
            glCullFace(GL_BACK);
        }
        D3DMATRIX proj = DrawProj(d.st);
        glUniformMatrix4fv(depthLoc_[0], 1, GL_FALSE, d.st.world.m());
        glUniformMatrix4fv(depthLoc_[1], 1, GL_FALSE, d.st.view.m());
        glUniformMatrix4fv(depthLoc_[2], 1, GL_FALSE, proj.m());
        DrawRecordedSolid(d);
    }

    // 2. the occlusion, 3. the blur
    D3DMATRIX proj = DrawProj(lens->st);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(emptyVao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoZTex_);
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[1]);
    glUseProgram(progAO_);
    glUniform1i(aoLoc_[0], 0);
    glUniform2f(aoLoc_[1], 1.f / w, 1.f / h);
    glUniform2f(aoLoc_[2], proj._11, proj._22);
    glUniform1f(aoLoc_[3], 30.f);  // world units: about half a map cell (40), a floor step is 25
    glUniform1i(aoLoc_[4], kAOSamples);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, aoFbo_[2]);
    glUseProgram(progBlur_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, aoTex_[0]);
    glUniform1i(blurLoc_[0], 0);
    glUniform1i(blurLoc_[1], 1);
    glUniform2f(blurLoc_[2], 1.f / w, 1.f / h);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // the result on unit 1 for the main pass; unit 0 for everything else, as always
    glBindTexture(GL_TEXTURE_2D, aoTex_[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    gpuAO_.End();
    return true;
}

void Device::Draw2DQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Texture* t,
                        bool alphaTest, const float* color, bool linear) {
    if (batchTex_ != t || batchAlpha_ != alphaTest || batchLinear_ != linear) {
        Flush2D();
        batchTex_ = t;
        batchAlpha_ = alphaTest;
        batchLinear_ = linear;
    }
    uint32_t c = 0xFFFFFFFF;
    if (color) {
        c = (uint32_t)(color[3] * 255) << 24 | (uint32_t)(color[0] * 255) << 16 | (uint32_t)(color[1] * 255) << 8 |
            (uint32_t)(color[2] * 255);
    }
    D3DLVERTEX q[4];
    memset(q, 0, sizeof q);
    q[0].x = x0; q[0].y = y0; q[0].tu = u0; q[0].tv = v0;
    q[1].x = x1; q[1].y = y0; q[1].tu = u1; q[1].tv = v0;
    q[2].x = x0; q[2].y = y1; q[2].tu = u0; q[2].tv = v1;
    q[3].x = x1; q[3].y = y1; q[3].tu = u1; q[3].tv = v1;
    for (auto& v : q) v.color = c;
    batch2D_.push_back(q[0]); batch2D_.push_back(q[1]); batch2D_.push_back(q[2]);
    batch2D_.push_back(q[2]); batch2D_.push_back(q[1]); batch2D_.push_back(q[3]);
}

void Device::Flush2D() {
    if (batch2D_.empty()) return;
    Texture* t = batchTex_;
    bool alphaTest = batchAlpha_;
    bool linear = batchLinear_;
    std::vector<D3DLVERTEX> quads;
    quads.swap(batch2D_);
    batchTex_ = nullptr;
    ApplyState(0, true);  // also flushes any recorded 3D first, keeping the order
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    float sx = 2.f / layout_.virtualW, sy = -2.f / 480.f;
    glUniform4f(u_.viewport2D, sx, sy, layout_.offsetX * sx - 1.f, 1.f);
    glUniform1i(u_.lighting, 0);
    glUniform1i(u_.flat, 0);
    glUniform1i(u_.alphaTest, alphaTest);
    glUniform1i(u_.alphaFix, 0);
    glUniform1i(u_.texOn, t != nullptr);
    glUniform1i(u_.colorOp, D3DTOP_SELECTARG1);
    glUniform1i(u_.colorArg1, D3DTA_TEXTURE);
    glUniform1i(u_.alphaOp, D3DTOP_SELECTARG1);
    glUniform1i(u_.alphaArg1, D3DTA_TEXTURE);
    glUniform4f(u_.constColor, 1, 1, 1, 1);
    if (t) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t->Id());
        glUniform1i(u_.tex, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(quads.size() * sizeof(D3DLVERTEX)), quads.data(), GL_STREAM_DRAW);
    EnsureAttribs(D3DFVF_LVERTEX);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)quads.size());
    frameDraws_++;
}

HRESULT Device::BltToBackBuffer(const RECT* dst, Surface* src, const RECT* srcRect, bool useColorKey) {
    if (!src) return E_FAIL;
    RECT d = dst ? *dst : RECT{0, 0, 640, 480};
    RECT s = srcRect ? *srcRect : RECT{0, 0, src->Width(), src->Height()};
    // DirectDraw (no clipper) rejected any blit whose destination crossed the surface edge. That
    // hid sprites whose transparent padding poked past 640 px (the last fire LEDs, FPS digits).
    // We clip instead: the scene target itself bounds the drawing, so nothing visible changes
    // except that those partially-off-screen sprites now appear.
    if (d.right <= d.left || d.bottom <= d.top) return E_FAIL;
    Texture* t = src->Tex();
    // source rectangles are in the image's layout size (an HD replacement has more pixels)
    float tw = (float)src->Width(), th = (float)src->Height();
    float x0 = (float)d.left, y0 = (float)d.top, x1 = (float)d.right, y1 = (float)d.bottom;
    if (hudScale_ != 1.f || hudShiftX_ != 0.f) {
        x0 = hudAnchorX_ + (x0 - hudAnchorX_) * hudScale_ + hudShiftX_;
        x1 = hudAnchorX_ + (x1 - hudAnchorX_) * hudScale_ + hudShiftX_;
        y0 = hudAnchorY_ + (y0 - hudAnchorY_) * hudScale_;
        y1 = hudAnchorY_ + (y1 - hudAnchorY_) * hudScale_;
    }
    Draw2DQuad(x0, y0, x1, y1, s.left / tw, s.top / th, s.right / tw, s.bottom / th, t, useColorKey && src->KeyEnabled(),
               nullptr, GetConfig().smoothSprites);
    return D3D_OK;
}

HRESULT Device::ColorFill(const RECT* dst, uint32_t rgb) {
    RECT d = dst ? *dst : RECT{0, 0, 640, 480};
    float c[4] = {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, 1.f};
    // Untextured quad with constant colour
    Draw2DQuad((float)d.left, (float)d.top, (float)d.right, (float)d.bottom, 0, 0, 0, 0, nullptr, false, c, false);
    return D3D_OK;
}

void Device::BeginHudScale(float scale, float anchorX, float anchorY, float shiftX) {
    hudScale_ = scale;
    hudAnchorX_ = anchorX;
    hudAnchorY_ = anchorY;
    hudShiftX_ = shiftX;
}

void Device::EndHudScale() {
    hudScale_ = 1.f;
    hudShiftX_ = 0.f;
}

void Device::ViewXRange(int* left, int* right) const {
    int side = (int)std::ceil(layout_.offsetX);
    *left = -side;
    *right = 640 + side;
}

int Device::HudEdgeShift() const {
    const Config& cfg = GetConfig();
    if (cfg.hudLayout != 1 || layout_.offsetX <= 0.f) return 0;
    float shift = layout_.offsetX;
    if (cfg.hudMaxAspect > 0.f) shift = std::min(shift, std::max(0.f, (480.f * cfg.hudMaxAspect - 640.f) * 0.5f));
    return (int)shift;
}

// Many UI quads in one draw (two triangles each, six vertices a quad, in drawing order), with the
// same state as DrawUIQuad: the Statistics Overlay's letters, which one by one cost a draw each.
void Device::DrawUIQuads(const std::vector<D3DLVERTEX>& verts, Texture* t) {
    if (verts.empty()) return;
    Flush2D();
    frameDraws_++;
    ApplyState(0, true);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    float sx = 2.f / layout_.virtualW, sy = -2.f / 480.f;
    glUniform4f(u_.viewport2D, sx, sy, layout_.offsetX * sx - 1.f, 1.f);
    glUniform1i(u_.lighting, 0);
    glUniform1i(u_.flat, 0);
    glUniform1i(u_.alphaTest, 0);
    glUniform1i(u_.alphaFix, 0);
    glUniform1i(u_.texOn, t != nullptr);
    glUniform1i(u_.colorOp, D3DTOP_MODULATE);
    glUniform1i(u_.colorArg1, D3DTA_TEXTURE);
    glUniform1i(u_.colorArg2, D3DTA_DIFFUSE);
    glUniform1i(u_.alphaOp, D3DTOP_MODULATE);
    glUniform1i(u_.alphaArg1, D3DTA_TEXTURE);
    glUniform1i(u_.alphaArg2, D3DTA_DIFFUSE);
    glUniform4f(u_.constColor, 1, 1, 1, 1);
    if (t) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t->Id());
        glUniform1i(u_.tex, 0);
        GLint f = t->UILinear() ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(D3DLVERTEX)), verts.data(), GL_STREAM_DRAW);
    EnsureAttribs(D3DFVF_LVERTEX);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
}

void Device::DrawUIQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Texture* t,
                        const float rgba[4]) {
    frameDraws_++;
    ApplyState(0, true);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    float sx = 2.f / layout_.virtualW, sy = -2.f / 480.f;
    glUniform4f(u_.viewport2D, sx, sy, layout_.offsetX * sx - 1.f, 1.f);
    glUniform1i(u_.lighting, 0);
    glUniform1i(u_.flat, 0);
    glUniform1i(u_.alphaTest, 0);
    glUniform1i(u_.alphaFix, 0);
    glUniform1i(u_.texOn, t != nullptr);
    glUniform1i(u_.colorOp, D3DTOP_MODULATE);
    glUniform1i(u_.colorArg1, D3DTA_TEXTURE);
    glUniform1i(u_.colorArg2, D3DTA_DIFFUSE);
    glUniform1i(u_.alphaOp, D3DTOP_MODULATE);
    glUniform1i(u_.alphaArg1, D3DTA_TEXTURE);
    glUniform1i(u_.alphaArg2, D3DTA_DIFFUSE);
    glUniform4f(u_.constColor, 1, 1, 1, 1);
    if (t) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, t->Id());
        glUniform1i(u_.tex, 0);
        GLint f = t->UILinear() ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    auto b =[](float f) { return (uint32_t)(std::min(1.f, std::max(0.f, f)) * 255.f + 0.5f); };
    uint32_t c = b(rgba[3]) << 24 | b(rgba[0]) << 16 | b(rgba[1]) << 8 | b(rgba[2]);
    D3DLVERTEX q[4];
    memset(q, 0, sizeof q);
    q[0].x = x0; q[0].y = y0; q[0].tu = u0; q[0].tv = v0;
    q[1].x = x1; q[1].y = y0; q[1].tu = u1; q[1].tv = v0;
    q[2].x = x0; q[2].y = y1; q[2].tu = u0; q[2].tv = v1;
    q[3].x = x1; q[3].y = y1; q[3].tu = u1; q[3].tv = v1;
    for (auto& v : q) v.color = c;
    BindMainVao();
    BindMainArray();
    BufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    EnsureAttribs(D3DFVF_LVERTEX);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
}

// Buffer uploads go through one place so the benchmark can report the bytes a frame sends to the
// vertex and index buffers (the counters are reset in BeginFrame and captured in Present).
void Device::BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (size > 0) frameUploadBytes_ += (size_t)size;
    hgl::glBufferData(target, size, data, usage);
}
