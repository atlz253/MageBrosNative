// A Direct3D 7 style immediate-mode device implemented on OpenGL 3.3.
// Only the subset of D3D7 / DirectDraw used by the Saito engine is provided, but it is
// emulated faithfully (fixed-function lighting, flat/gouraud shading, blend
// modes, cull mode, texture stage 0 ops). Rendering enhancements live here too.
#pragma once
#include "../core/d3dtypes.h"
#include "../core/assets.h"
#include "gl.h"
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
class MeshBuffer;

// The frame-work counters are shared with MeshBuffer, which uploads geometry outside Device.
namespace GfxFrameWork {
void CountGLCall();
void CountUploadedBytes(uint64_t bytes);
}  // namespace GfxFrameWork

class Texture {
public:
    ~Texture();
    static Texture* Create(const Image& img, bool colorKey, uint32_t keyRGB, bool mipmaps);
    void Update(const Image& img, bool colorKey, uint32_t keyRGB);
    int Width() const { return width_; }
    int Height() const { return height_; }
    GLuint Id() const { return id_; }
    bool HasMips() const { return mips_; }
    // A mod image with its own transparency (originals only have colour-key cut-outs).
    bool SoftAlpha() const { return softAlpha_; }
    // Overlay UI text drawn from a font at the screen's size: sampled smoothly, not as pixels
    void SetUILinear(bool on) { uiLinear_ = on; }
    bool UILinear() const { return uiLinear_; }
    // The game resource it was made from (0 if none), and the average colour of its visible pixels
    // (0..1): the dynamic lights (G5) find the game's glowing sprites and their colours by them
    void SetResId(uint16_t id) { resId_ = id; }
    uint16_t ResId() const { return resId_; }
    const float* AvgColor() const { return avg_; }
    // Sets only the sampling parameters this texture does not already have. Update() records what
    // it wrote, so the first draw after a texture upload sends the differences.
    void ApplySampling(GLint minF, GLint magF, float aniso, GLint wrapS, GLint wrapT);
    void ForgetSampling() { samplingValid_ = false; }

private:
    uint16_t resId_ = 0;
    float avg_[3] = {1.f, 1.f, 1.f};
    void Upload(const Image& img, bool colorKey, uint32_t keyRGB);  // (Update without sending the recording)
    bool uiLinear_ = false;
    int width_ = 0, height_ = 0;
    GLuint id_ = 0;
    bool mips_ = false;
    bool softAlpha_ = false;
    bool samplingValid_ = false;
    GLint minF_ = 0, magF_ = 0, wrapS_ = 0, wrapT_ = 0;
    float aniso_ = 0.f;
};

// Emulates a DirectDraw off-screen surface holding a sprite sheet.
class Surface {
public:
    explicit Surface(const Image& img);
    ~Surface();
    int Width() const { return image_.LogicalWidth(); }    // layout size (= the original's)
    int Height() const { return image_.LogicalHeight(); }
    // COLORREF semantics of the original DDSetColorKey: CLR_INVALID = use pixel (0,0)
    void SetColorKey(COLORREF key);
    void ClearColorKey();
    bool KeyEnabled() const { return keyEnabled_; }
    Texture* Tex();
    const Image& GetImage() const { return image_; }

private:
    Image image_;
    Texture* tex_ = nullptr;
    bool dirty_ = true;
    bool keyEnabled_ = false;
    uint32_t keyRGB_ = 0;
};

struct FrameLayout {
    int windowW = 640, windowH = 480;     // drawable size in pixels
    int destX = 0, destY = 0, destW = 640, destH = 480;  // where the game image goes in the window
    int fboW = 640, fboH = 480;           // internal render target size
    float virtualW = 640.f;               // logical width (>= 640 in widescreen), height is always 480
    float offsetX = 0.f;                  // logical x of the original 640x480 area inside virtual space
};

class Device {
public:
    friend class Texture;  // (a texture being changed sends the 2D quads that use it first)
    // The work one drawn frame asked of OpenGL, reset by BeginFrame. The counters are read by the
    // Statistics Overlay and the once-a-second log line (R36S performance work).
    struct FrameCounters {
        uint64_t draws = 0;
        uint64_t glCalls = 0;
        uint64_t uploadedBytes = 0;
    };
    const FrameCounters& Counters() const;

    bool Init(SDL_Window* window);
    void Shutdown();

    // Frame management (called by the application framework, not by game code)
    void BeginFrame();
    void Present();
    // Wait until the graphics card has done everything handed to it (after the swap: the frame
    // just finished is on its way to the screen before the next one is begun)
    void WaitForGpu();
    const FrameLayout& Layout() const { return layout_; }
    // For the Statistics Overlay: the graphics card, its maker and the OpenGL version as the
    // driver states them (read at Init), and the antialiasing and texture filtering in effect
    const std::string& GLRenderer() const { return glRenderer_; }
    const std::string& GLVendor() const { return glVendor_; }
    const std::string& GLVersion() const { return glVersion_; }
    int Samples() const { return samples_; }          // 0 = no antialiasing
    float MaxAnisotropy() const { return maxAniso_; }  // the card's most (1 = none)
    // Widescreen HUD placement: how many 640x480-space pixels edge-anchored HUD elements move
    // outward (left items by -HudEdgeShift(), right items by +HudEdgeShift()). 0 unless
    // aspect = widescreen and hud_layout = 1.
    int HudEdgeShift() const;
    // The x range the view shows this frame, in the original's 640 x 480 pixels: 0 to 640 at 4:3
    // and in Stretch, beyond both edges in widescreen (the window's shape as laid out, so any
    // window size counts, resized or not).
    void ViewXRange(int* left, int* right) const;
    // HUD size and position (enhancement): while set, 2D blits are scaled by `scale` around the
    // point (anchorX, anchorY) in the original's 640 x 480 pixels, then moved `shiftX` across. The
    // game's HUD code wraps each HUD group with these (scale 1 and shift 0 draw as without them).
    void BeginHudScale(float scale, float anchorX, float anchorY, float shiftX = 0.f);
    void EndHudScale();
    // fov_scale applies only while this is on: XGameScene::Render turns it on for the level's 3D
    // and off before the shop and the HUD, every frame. While it is on, the 3D draws are recorded
    // and sent to OpenGL together, in their order, when it is turned off (the graphics
    // enhancements' frame: docs/GRAPHICS_ENHANCEMENTS.md, G2), so passes can be drawn before them.
    void SetGameplayView(bool on);
    // Sends whatever has been recorded (in its order), then records on. Called before anything
    // that draws or changes OpenGL state outside the recording (2D, clears, the present, textures
    // being made, changed or deleted), so the picture is as if each draw had been sent at once.
    void FlushRecording();
    // For the Statistics Overlay: the graphics card's time for the level's 3D (the recorded
    // draws), summed over the frames since the last call, and how many frames drew one; and the
    // number of 3D draws in the last frame that had any. Read without waiting (a frame or two late).
    void TakeGpu3D(double* sumMs, int* frames) { gpu3D_.Take(sumMs, frames); }
    // The same for ambient occlusion (its depth pass, the occlusion and the blur, G3)
    void TakeGpuAO(double* sumMs, int* frames) { gpuAO_.Take(sumMs, frames); }
    // And for the dynamic shadows' map (G4)
    void TakeGpuShadows(double* sumMs, int* frames) { gpuShadow_.Take(sumMs, frames); }
    // And for the bloom's blur and adding it (G6)
    void TakeGpuBloom(double* sumMs, int* frames) { gpuBloom_.Take(sumMs, frames); }
    // The dynamic lights in the last frame that had any (G5)
    int DynamicLights() const { return lastLights_; }
    int Draws3D() const { return lastDraws3D_; }
    bool Recording3D() const { return recordEnabled_; }
    // Rows at the bottom of the window that are off the screen (the borderless window's extra
    // row): the picture is laid out on the rows above them (from the next frame on).
    void SetHiddenBottomRows(int n) { hiddenBottomRows_ = n; }
    // Screen area used by the top-left HUD group this frame (virtual space), so the FPS counter
    // can step out of its way at large HUD sizes. Reset every frame.
    void SetHudTopLeftExtent(float right, float bottom) { hudTLRight_ = right; hudTLBottom_ = bottom; }
    float HudTopLeftRight() const { return hudTLRight_; }
    float HudTopLeftBottom() const { return hudTLBottom_; }
    void OnSettingsChanged();
    bool SaveScreenshot(const char* path);

    // --- IDirect3DDevice7 subset ---
    HRESULT BeginScene();
    HRESULT EndScene();
    HRESULT Clear(DWORD count, const RECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    HRESULT SetRenderState(D3DRENDERSTATETYPE s, DWORD v);
    HRESULT GetRenderState(D3DRENDERSTATETYPE s, DWORD* v);
    HRESULT SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, DWORD v);
    HRESULT GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, DWORD* v);
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m);
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m);
    HRESULT SetMaterial(const D3DMATERIAL7* m);
    HRESULT GetMaterial(D3DMATERIAL7* m);
    HRESULT SetLight(DWORD index, const D3DLIGHT7* l);
    HRESULT LightEnable(DWORD index, BOOL enable);
    HRESULT SetTexture(DWORD stage, Texture* t);
    HRESULT GetTexture(DWORD stage, Texture** t);
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts,
                                 const WORD* indices, DWORD numIndices, DWORD flags);
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts, DWORD flags);
    // A mesh whose vertices and indices do not change: MeshBuffer keeps them on the card, and
    // this draws a range of its indices. The game's rendering code keeps its draw order.
    void DrawMesh(MeshBuffer& mesh, DWORD fvf, const void* verts, DWORD numVerts, const WORD* indices,
                  DWORD numIndices, DWORD firstIndex, DWORD indexCount);

    // --- DirectDraw back-buffer operations, in the original 640x480 pixel space ---
    HRESULT BltToBackBuffer(const RECT* dst, Surface* src, const RECT* srcRect, bool useColorKey);
    HRESULT ColorFill(const RECT* dst, uint32_t rgb);

    // State caching (the R36S performance work): send only what changed since the last draw.
    // InvalidateState() makes the next draw send everything, for paths that talk to OpenGL
    // outside ApplyState. The cache is enabled by the change; the switch keeps the old path
    // available for comparison.
    void SetStateCacheEnabled(bool on) { cacheEnabled_ = on; if (!on) forceAll_ = true; }
    bool StateCacheEnabled() const { return cacheEnabled_; }
    void InvalidateState() { forceAll_ = true; }

    // Overlay UI (options menu): quad in 480-high virtual space, alpha blended, texture x colour.
    void DrawUIQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Texture* t,
                    const float rgba[4]);
    // Many such quads in one draw: six vertices (two triangles) a quad, drawn in order
    void DrawUIQuads(const std::vector<D3DLVERTEX>& verts, Texture* t);

    int ScreenWidth() const { return 640; }
    int ScreenHeight() const { return 480; }

private:
    // Everything ApplyState reads of the emulated Direct3D state for a 3D draw: what the setters
    // last stored when the draw was asked for (copied into each recorded draw).
    struct DrawState {
        D3DMATRIX world, view, proj;
        D3DMATERIAL7 material;
        D3DLIGHT7 light;
        bool lightEnabled, gameplayView;
        Texture* texture;
        DWORD zEnable, zWrite, blendEnable, srcBlend, destBlend, cullMode, lighting, ambient, shadeMode;
        DWORD colorOp, colorArg1, colorArg2, alphaOp, alphaArg1, alphaArg2, magFilter, minFilter;
    };
    struct RecordedDraw {
        DrawState st;
        DWORD fvf;
        GLenum mode;
        GLsizei count;        // indices (indexed) or vertices
        size_t firstVertex, numVerts;   // in recVerts_ (32 bytes a vertex, both formats)
        size_t firstIndex, numIndices;  // in recIdx_ (indexed draws only)
        bool indexed;
        // Geometry kept on the card (MeshBuffer): the draw reads it from there, in this replay and
        // in the extra passes, instead of from the frame's CPU copy (R36S performance work).
        MeshBuffer* mesh = nullptr;
        DWORD meshFirstIndex = 0, meshIndexCount = 0;
    };
    DrawState CurrentState() const;
    void ApplyDrawState(const DrawState& st, DWORD fvf);
    void Record(D3DPRIMITIVETYPE type, DWORD fvf, const void* verts, DWORD numVerts, const WORD* indices,
                DWORD numIndices);
    void RecordMesh(MeshBuffer& mesh, DWORD fvf, DWORD numVerts, DWORD firstIndex, DWORD indexCount);
    // A graphics-card stopwatch: queries used in turn, read frames later without waiting (a query
    // still pending when its turn comes round leaves that run untimed)
    struct GpuTimer {
        static constexpr int kQueries = 8;
        GLuint q[kQueries] = {};
        bool busy[kQueries] = {};
        int next = 0;
        bool timedThisFrame = false;
        bool running = false;
        double sumMs = 0;  // every query read since Take (a frame may hold several)
        int frames = 0;    // frames timed since Take
        void Init();
        void Free();
        void Begin();
        void End();
        void Poll();
        void FrameDone();
        void Take(double* s, int* f);
    };
    GpuTimer gpu3D_, gpuAO_, gpuShadow_, gpuBloom_;
    // Ambient occlusion (G3): a depth pass of the recorded solid draws into a texture of view-space
    // depth, the occlusion worked out from it, then blurred; the main pass multiplies the level and
    // the solid models by it. Sized to the picture (not the render scale).
    D3DMATRIX DrawProj(const DrawState& st) const;  // the lens a 3D draw is drawn with
    static int DrawKind(const RecordedDraw& d);     // 1 level-like, 2 solid model, 0 anything else
    bool ComputeAO();
    void EnsureAOTargets(int w, int h);
    void DestroyAOTargets();
    // The frame's vertices in one upload, for the extra passes (depth only: AO's and the shadows')
    void UploadPrep();
    bool prepUploaded_ = false;
    // Dynamic shadows (G4): a depth map of the solid draws seen from the game's own light, fitted
    // each frame round the part of the level the camera sees and snapped to whole map pixels;
    // the main pass looks each pixel up in it. Lit models lose only their direct light in shadow,
    // pre-lit surfaces facing the light are darkened.
    bool ComputeShadows(int quality);
    void EnsureShadowTarget(int size);
    void DestroyShadowTarget();
    // Dynamic lights (G5): the frame's lights, found among the recorded draws (the game's own
    // glowing sprites: flames, shots, flashes, explosions), merged and given to the main pass
    static constexpr int kMaxLights = 16;
    int CollectLights();
    float lightPos_[kMaxLights * 3] = {}, lightColor_[kMaxLights * 3] = {}, lightReach_[kMaxLights] = {};
    int lightCount_ = 0, lastLights_ = 0;
    bool lightsActive_ = false;
    // Bloom (G6): with it on, the scene target has a second colour layer (glowRb_) that only the
    // game's glowing sprites light up during the level's 3D (solid things in front darken it,
    // as on screen); it is then resolved, blurred down and up a chain of halved textures and
    // added onto the scene, before the HUD.
    static constexpr int kBloomLevels = 7;
    void ComputeBloom(int level);
    void EnsureBloomTargets(int w, int h);
    void DestroyBloomTargets();
    GLuint glowRb_ = 0;
    GLuint bloomFbo_[kBloomLevels] = {}, bloomTex_[kBloomLevels] = {};
    int bloomW_ = 0, bloomH_ = 0;
    GLuint progBloomDown_ = 0, progBloomUp_ = 0, progBloomAdd_ = 0;
    bool bloomActive_ = false;
    bool shadowActive_ = false;
    int shadowSize_ = 0;
    GLuint progShadow_ = 0, shadowFbo_ = 0, shadowTex_ = 0;
    GLint shadowLoc_[2] = {-1, -1};                   // world, lightVP
    float lightVP_[16] = {};                          // world to the shadow map (column-major)
    float shadowTexelWorld_ = 1.f;                    // a map pixel's width in the level
    D3DVECTOR shadowEye_;                             // the camera, in the level
    bool aoActive_ = false;
    int aoW_ = 0, aoH_ = 0;
    GLuint progDepth_ = 0, progAO_ = 0, progBlur_ = 0, emptyVao_ = 0, prepVbo_ = 0, prepIbo_ = 0;
    GLuint aoFbo_[3] = {}, aoZTex_ = 0, aoDepthRb_ = 0, aoTex_[2] = {};
    GLint depthLoc_[3] = {-1, -1, -1};                // world, view, proj
    GLint aoLoc_[5] = {-1, -1, -1, -1, -1};          // z, texel, projScale, radius, samples
    GLint blurLoc_[3] = {-1, -1, -1};                 // z, ao, texel
    bool recordEnabled_ = true;   // off with the environment variable MBN_NO_RECORD=1 (for comparing)
    bool recording_ = false;
    std::vector<RecordedDraw> rec_;
    std::vector<unsigned char> recVerts_;
    std::vector<WORD> recIdx_;
    int frameDraws3D_ = 0, lastDraws3D_ = 0;

    float hudScale_ = 1.f, hudAnchorX_ = 0.f, hudAnchorY_ = 0.f, hudShiftX_ = 0.f;
    bool gameplayView_ = false;
    int hiddenBottomRows_ = 0;
    float hudTLRight_ = -1e9f, hudTLBottom_ = 0.f;
    void ComputeLayout();
    void CreateTargets();
    void DestroyTargets();
    void ApplyState(DWORD fvf, bool twoD);
    void Draw2DQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Texture* t,
                    bool alphaTest, const float* color, bool linear);
    void Flush2D();
    GLuint CompileProgram(const char* vs, const char* fs);

    // The state sent to OpenGL, kept so an unchanged value is not sent again. `forceAll_` is set
    // by InvalidateState() and by a path that changes state without using these helpers.
    struct SentState {
        GLuint fboDraw = ~0u, fboRead = ~0u, program = ~0u, vao = ~0u;
        GLint vpX = -1000000, vpY = -1000000, vpW = -1, vpH = -1;
        GLint screen2D = -1, lighting = -1, hasVertexColor = -1, flat = -1, lightOn = -1;
        GLint texOn = -1, tex = -1, colorOp = -1, colorArg1 = -1, colorArg2 = -1, alphaOp = -1, alphaArg1 = -1,
              alphaArg2 = -1, alphaTest = -1, alphaFix = -1;
        float constColor[4] = {-2, -2, -2, -2};
        float viewport2D[4] = {-2, -2, -2, -2};
        bool matValid = false;
        D3DMATERIAL7 material{};
        bool ambientValid = false;
        float ambient[4] = {-2, -2, -2, -2};
        bool lightValid = false;
        float lightDir[3] = {-2, -2, -2};
        float lightDiffuse[4] = {-2, -2, -2, -2};
        bool matricesValid = false;
        D3DMATRIX world{}, view{}, proj{};
        int depthTest = -1, depthMask = -1, blend = -1, cull = -1, frontFace = -1;
        GLenum depthFunc = 0, srcBlend = 0, dstBlend = 0;
        int scissor = -1, colorMask = -1;
    } sent_;
    bool cacheEnabled_ = false;
    bool forceAll_ = true;
    // Consecutive 2D quads with the same texture and state, drawn in one call at the next state
    // change or before Present.
    std::vector<D3DLVERTEX> quadBatch_;
    Texture* quadBatchTex_ = nullptr;
    bool quadBatchAlphaTest_ = false, quadBatchLinear_ = false;
    void BindFbo(GLenum target, GLuint id);
    void SetViewport(GLint x, GLint y, GLsizei w, GLsizei h);
    void UseProgram(GLuint p);
    void SetCap(GLenum cap, bool on, int& cache);
    void SetDepthFunc(GLenum f);
    void SetDepthMask(bool on);
    void SetBlend(bool on, GLenum src, GLenum dst);
    void SetCull(bool on, GLenum frontFace);
    void SetScissor(bool on);
    void SetColorMask(bool on);
    void BindVao(GLuint vao);
    void BindVertexLayout(DWORD fvf);
    void Uni1i(GLint loc, GLint& cache, GLint v);
    void Uni1f(GLint loc, float& cache, float v);
    void Uni3f(GLint loc, float (&cache)[3], const float* v);
    void Uni4f(GLint loc, float (&cache)[4], const float* v);
    void UniMat(GLint loc, D3DMATRIX& cache, const D3DMATRIX& m);

    SDL_Window* window_ = nullptr;
    FrameLayout layout_;
    GLuint sceneFbo_ = 0, sceneColorRb_ = 0, sceneDepthRb_ = 0;
    GLuint resolveFbo_ = 0, resolveTex_ = 0;
    // Render scale above 2x: the scene is shrunk to the window through mipmaps (made each frame),
    // so every rendered pixel counts; plain bilinear skips most of them past 2x. (Not reached
    // since 0.4.38, which caps the render scale at 2x; kept for if the cap is ever lifted.)
    bool resolveMips_ = false;
    int samples_ = 0;
    bool inScene_ = false;

    GLuint prog_ = 0, progPresent_ = 0;
    GLuint vbo_ = 0, ibo_ = 0;
    GLuint vao_ = 0, vaoVertex_ = 0, vaoLVertex_ = 0, vaoPresent_ = 0;
    struct Uniforms {
        GLint world, view, proj, lighting, matDiffuse, matAmbient, matEmissive, ambient, lightOn, lightDir,
            lightDiffuse, flat, texOn, colorOp, colorArg1, colorArg2, alphaOp, alphaArg1, alphaArg2, alphaTest,
            hasVertexColor, tex, constColor, screen2D, viewport2D, alphaFix, bgra, aoStrength, aoTex,
            fboSize, shadowKind, shadowMap, lightVP, shadowTexel, shadowOffset, shadowStrength, shadowGrid,
            eye, lightKind, lightCount, lightPos, lightColor, lightReach, sceneDim, glowMode;
    } u_{};
    GLint presentTexLoc_ = -1, presentGammaLoc_ = -1;

    DWORD rs_[D3DRENDERSTATE_MAX];
    DWORD tss_[D3DTSS_MAX];
    D3DMATRIX world_, view_, proj_;
    D3DMATERIAL7 material_{};
    D3DLIGHT7 light_{};
    bool lightEnabled_ = false;
    Texture* texture_ = nullptr;
    float maxAniso_ = 1.f;
    std::string glRenderer_, glVendor_, glVersion_;
};

Device& GetDevice();
