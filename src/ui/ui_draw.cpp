#include "ui_draw.h"
#include "../core/config.h"
#include "../gfx/device.h"
#include "../../third_party/font8x8_basic.h"
#include "../core/assets.h"
#include "../core/log.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace {
constexpr int kPad = 2;             // transparent border around each glyph
constexpr int kCell = 8 + 2 * kPad;  // atlas cell size

// An outline font in the 8 x 8 cell: capitals 7 high, standing on the cell's 7th row (the 8x8
// font's own capitals and baseline), each letter centred in its 8-wide cell (fixed-width fonts
// fill it evenly; in a proportional one narrow letters stand apart). The cell reaches kAbove
// above the 8 x 8 for accents and brackets and kBelow below it for descenders.
constexpr float kCapUnits = 7.f, kBaseUnits = 7.f, kAbove = 2.f, kBelow = 4.f;

// The player's own fonts: .ttf / .otf files in "fonts" folders (any capitalisation) next to
// MageBros.exe, then next to this program, as the music folder. None ship with the game: Classic,
// the built-in 8x8, is the default, and [video] ui_font names the file to use instead.
std::vector<std::string> FontDirs() {
    std::vector<std::string> parents{Assets::GameDir()};
    if (const char* b = SDL_GetBasePath())
        if (std::find(parents.begin(), parents.end(), std::string(b)) == parents.end()) parents.push_back(b);
    std::vector<std::string> out;
    for (const auto& parent : parents) {
        if (parent.empty()) continue;
        int n = 0;
        char** names = SDL_GlobDirectory(parent.c_str(), "fonts", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; i < n; i++) {
            std::string d = parent + names[i];
            SDL_PathInfo info;
            if (SDL_GetPathInfo(d.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY) out.push_back(d + "/");
        }
        SDL_free(names);
    }
    return out;
}

// The font in use, read once from its file and kept while it stays chosen
struct PlayerFont {
    std::string name;
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

stbtt_fontinfo* FontInfo(const std::string& name) {
    if (name.empty()) return nullptr;
    static PlayerFont f;
    if (f.name != name) {
        f = PlayerFont{};
        f.name = name;
        std::string path;
        for (const std::string& dir : FontDirs()) {
            SDL_PathInfo info;
            if (SDL_GetPathInfo((dir + name).c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
                path = dir + name;
                break;
            }
        }
        size_t size = 0;
        void* p = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &size);
        if (p) {
            f.data.assign((unsigned char*)p, (unsigned char*)p + size);
            SDL_free(p);
            int at = stbtt_GetFontOffsetForIndex(f.data.data(), 0);
            f.ok = at >= 0 && stbtt_InitFont(&f.info, f.data.data(), at);
        }
        Log("Menu font: %s %s", name.c_str(),
            f.ok ? "loaded" : path.empty() ? "is not in a fonts folder: Classic is used" : "could not be read: Classic is used");
    }
    return f.ok ? &f.info : nullptr;
}
}  // namespace

UIDraw::~UIDraw() {
    delete font_;
    FreeAtlases();
}

void UIDraw::FreeAtlases() {
    for (Atlas& a : atlases_) delete a.tex;
    atlases_.clear();
}

void UIDraw::EnsureFont() {
    if (font_) return;
    // 16 x 8 glyph atlas. Each 8x8 glyph sits in a 12x12 cell with a 2-pixel transparent border:
    // with antialiasing, pixels on a glyph's edge can be shaded from just outside its quad, and
    // without the border they would pick up the neighbouring glyph (a thin light seam).
    // White glyph pixels, everything else black (colour-keyed out).
    Image img;
    img.width = 16 * kCell;
    img.height = 8 * kCell;
    img.bpp = 32;
    img.pixels.assign((size_t)img.width * img.height, 0xFF000000u);
    for (int ch = 0; ch < 128; ch++) {
        int gx = (ch % 16) * kCell + kPad, gy = (ch / 16) * kCell + kPad;
        for (int row = 0; row < 8; row++) {
            unsigned bits = (unsigned char)font8x8_basic[ch][row];
            for (int col = 0; col < 8; col++)
                if (bits & (1u << col)) img.pixels[(size_t)(gy + row) * img.width + gx + col] = 0xFFFFFFFFu;
        }
    }
    font_ = Texture::Create(img, true, 0x000000, false);
}

void UIDraw::Begin(Device* d) {
    dev_ = d;
    EnsureFont();
    const FrameLayout& L = d->Layout();
    left_ = -L.offsetX;
    right_ = 640.f + L.offsetX;
    // a player's font is drawn for the screen's real size: made again when that changes (the
    // window resized, another render scale) or another font is chosen
    float px = (float)L.fboH / 480.f;
    const std::string& font = GetConfig().uiFont;
    if (px != pxPerUnit_ || font != atlasFont_) {
        atlasFont_ = font;
        FreeAtlases();
        pxPerUnit_ = px;
    }
}

// The letters of one text scale, rasterized from the font's outlines at the screen's size, with
// their antialiasing as transparency (white letters, so the text colour tints them).
UIDraw::Atlas* UIDraw::FontAtlas(float scale) {
    for (Atlas& a : atlases_)
        if (a.scale == scale) return a.tex ? &a : nullptr;
    Atlas a;
    a.scale = scale;
    stbtt_fontinfo* f = FontInfo(atlasFont_);
    const float k = pxPerUnit_ * scale;  // pixels a virtual unit, at this scale
    if (f && k > 0) {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBox(f, 'H', &x0, &y0, &x1, &y1);
        const float em = (y1 > y0) ? kCapUnits * k / (float)(y1 - y0) : 0.f;  // font units to pixels
        a.above = kAbove;
        a.cellW = std::ceil(8.f * k) + 2 * a.padPx;
        a.cellH = std::ceil((kAbove + 8.f + kBelow) * k) + 2 * a.padPx;
        const int cw = (int)a.cellW, ch = (int)a.cellH;
        Image img;
        img.width = 16 * cw;
        img.height = 6 * ch;
        img.bpp = 32;
        img.hasAlpha = true;
        img.pixels.assign((size_t)img.width * img.height, 0x00FFFFFFu);
        std::vector<unsigned char> glyph;
        for (int c = 32; c < 128 && em > 0; c++) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(f, c, &adv, &lsb);
            // the pen: the letter centred in its 8-wide cell, standing on the baseline
            float penX = a.padPx + (8.f * k - adv * em) * 0.5f;
            float baseY = a.padPx + (kAbove + kBaseUnits) * k;
            int ix = (int)std::floor(penX), iy = (int)std::floor(baseY);
            float sx = penX - ix, sy = baseY - iy;
            int bx0, by0, bx1, by1;
            stbtt_GetCodepointBitmapBoxSubpixel(f, c, em, em, sx, sy, &bx0, &by0, &bx1, &by1);
            int gx = ix + bx0, gy = iy + by0, gw = bx1 - bx0, gh = by1 - by0;
            if (gw <= 0 || gh <= 0) continue;
            glyph.assign((size_t)gw * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(f, glyph.data(), gw, gh, gw, em, em, sx, sy, c);
            // copied into its cell, clipped so the cell's 1-pixel border stays clear
            int ox = ((c - 32) % 16) * cw, oy = ((c - 32) / 16) * ch;
            for (int yy = std::max(gy, 1); yy < std::min(gy + gh, ch - 1); yy++)
                for (int xx = std::max(gx, 1); xx < std::min(gx + gw, cw - 1); xx++) {
                    unsigned cov = glyph[(size_t)(yy - gy) * gw + (xx - gx)];
                    if (cov) img.pixels[(size_t)(oy + yy) * img.width + ox + xx] = (uint32_t)cov << 24 | 0xFFFFFFu;
                }
        }
        a.tex = Texture::Create(img, false, 0, false);
        a.tex->SetUILinear(true);
    }
    atlases_.push_back(a);
    return atlases_.back().tex ? &atlases_.back() : nullptr;
}

void UIDraw::Quad(std::vector<D3DLVERTEX>& to, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                  float v1, UIColor c) {
    auto b = [](float f) { return (uint32_t)(std::min(1.f, std::max(0.f, f)) * 255.f + 0.5f); };
    uint32_t col = b(c.a) << 24 | b(c.r) << 16 | b(c.g) << 8 | b(c.b);
    D3DLVERTEX v[4];
    memset(v, 0, sizeof v);
    v[0].x = x0; v[0].y = y0; v[0].tu = u0; v[0].tv = v0;
    v[1].x = x1; v[1].y = y0; v[1].tu = u1; v[1].tv = v0;
    v[2].x = x0; v[2].y = y1; v[2].tu = u0; v[2].tv = v1;
    v[3].x = x1; v[3].y = y1; v[3].tu = u1; v[3].tv = v1;
    for (auto& q : v) q.color = col;
    // two triangles: 0 1 2 and 2 1 3 (the strip DrawUIQuad draws)
    for (int i : {0, 1, 2, 2, 1, 3}) to.push_back(v[i]);
}

void UIDraw::BeginBatch() {
    batching_ = true;
    rects_.clear();
    glyphs_.clear();
}

void UIDraw::EndBatch() {
    batching_ = false;
    if (!dev_) return;
    dev_->DrawUIQuads(rects_, nullptr);
    for (auto& run : glyphs_) dev_->DrawUIQuads(run.second, run.first);
}

void UIDraw::Glyph(Texture* t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                   UIColor c) {
    if (batching_) {
        if (glyphs_.empty() || glyphs_.back().first != t) glyphs_.push_back({t, {}});
        Quad(glyphs_.back().second, x0, y0, x1, y1, u0, v0, u1, v1, c);
        return;
    }
    float rgba[4] = {c.r, c.g, c.b, c.a};
    dev_->DrawUIQuad(x0, y0, x1, y1, u0, v0, u1, v1, t, rgba);
}

void UIDraw::Rect(float x0, float y0, float x1, float y1, UIColor c) {
    if (batching_) {
        Quad(rects_, x0, y0, x1, y1, 0, 0, 0, 0, c);
        return;
    }
    float rgba[4] = {c.r, c.g, c.b, c.a};
    dev_->DrawUIQuad(x0, y0, x1, y1, 0, 0, 0, 0, nullptr, rgba);
}

namespace {
uint32_t PackColor(UIColor c) {
    auto b = [](float f) { return (uint32_t)(std::min(1.f, std::max(0.f, f)) * 255.f + 0.5f); };
    return b(c.a) << 24 | b(c.r) << 16 | b(c.g) << 8 | b(c.b);
}

void Triangle(std::vector<D3DLVERTEX>& to, float x0, float y0, float x1, float y1, float x2, float y2,
              uint32_t color) {
    D3DLVERTEX v[3];
    memset(v, 0, sizeof v);
    v[0].x = x0; v[0].y = y0;
    v[1].x = x1; v[1].y = y1;
    v[2].x = x2; v[2].y = y2;
    for (auto& q : v) q.color = color;
    for (int i = 0; i < 3; i++) to.push_back(v[i]);
}
}  // namespace

void UIDraw::Disc(float cx, float cy, float radius, UIColor c, int segments) {
    if (!dev_ || radius <= 0 || segments < 3) return;
    const uint32_t color = PackColor(c);
    std::vector<D3DLVERTEX> tris;
    tris.reserve((size_t)segments * 3);
    for (int i = 0; i < segments; i++) {
        float a0 = 6.2831853f * i / segments, a1 = 6.2831853f * (i + 1) / segments;
        Triangle(tris, cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                 cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color);
    }
    if (batching_) {
        rects_.insert(rects_.end(), tris.begin(), tris.end());
        return;
    }
    dev_->DrawUIQuads(tris, nullptr);
}

void UIDraw::Ring(float cx, float cy, float outer, float inner, UIColor c, int segments) {
    if (!dev_ || outer <= inner || segments < 3) return;
    const uint32_t color = PackColor(c);
    std::vector<D3DLVERTEX> tris;
    tris.reserve((size_t)segments * 6);
    for (int i = 0; i < segments; i++) {
        float a0 = 6.2831853f * i / segments, a1 = 6.2831853f * (i + 1) / segments;
        float o0x = cx + std::cos(a0) * outer, o0y = cy + std::sin(a0) * outer;
        float o1x = cx + std::cos(a1) * outer, o1y = cy + std::sin(a1) * outer;
        float i0x = cx + std::cos(a0) * inner, i0y = cy + std::sin(a0) * inner;
        float i1x = cx + std::cos(a1) * inner, i1y = cy + std::sin(a1) * inner;
        Triangle(tris, o0x, o0y, o1x, o1y, i1x, i1y, color);
        Triangle(tris, o0x, o0y, i1x, i1y, i0x, i0y, color);
    }
    if (batching_) {
        rects_.insert(rects_.end(), tris.begin(), tris.end());
        return;
    }
    dev_->DrawUIQuads(tris, nullptr);
}

void UIDraw::Text(float x, float y, const std::string& s, float scale, UIColor c, bool shadow) {
    if (shadow) Text(x + scale, y + scale, s, scale, UIColor{0, 0, 0, c.a * 0.7f}, false);
    float g = 8.f * scale;
    Atlas* a = atlasFont_.empty() ? nullptr : FontAtlas(scale);  // (a font that can't be read: Classic)
    if (a) {
        // the player's font: each cell's quad in virtual units (its pixels over the screen's pixels a
        // unit), so its texels land on the screen's pixels one to one
        const float k = pxPerUnit_;
        const float aw = 16.f * a->cellW, ah = 6.f * a->cellH;
        const float qw = a->cellW / k, qh = a->cellH / k, pad = a->padPx / k;
        for (unsigned char ch : s) {
            if (ch > 32 && ch < 128) {
                float u0 = ((ch - 32) % 16) * a->cellW / aw, v0 = ((ch - 32) / 16) * a->cellH / ah;
                float qx = x - pad, qy = y - a->above * scale - pad;
                Glyph(a->tex, qx, qy, qx + qw, qy + qh, u0, v0, u0 + a->cellW / aw, v0 + a->cellH / ah, c);
            }
            x += g;
        }
        return;
    }
    for (unsigned char ch : s) {
        if (ch > 32 && ch < 128) {
            const float aw = 16.f * kCell, ah = 8.f * kCell;
            float u0 = ((ch % 16) * kCell + kPad) / aw, v0 = ((ch / 16) * kCell + kPad) / ah;
            Glyph(font_, x, y, x + g, y + g, u0, v0, u0 + 8 / aw, v0 + 8 / ah, c);
        }
        x += g;
    }
}

std::vector<std::string> UIDraw::AvailableFonts() {
    std::vector<std::string> out;
    for (const std::string& dir : FontDirs())
        for (const char* pattern : {"*.ttf", "*.otf"}) {
            int n = 0;
            char** names = SDL_GlobDirectory(dir.c_str(), pattern, SDL_GLOB_CASEINSENSITIVE, &n);
            for (int i = 0; i < n; i++)
                if (std::find(out.begin(), out.end(), names[i]) == out.end()) out.push_back(names[i]);
            SDL_free(names);
        }
    std::sort(out.begin(), out.end());
    return out;
}

bool UIDraw::FontUsable(const std::string& name) { return name.empty() || FontInfo(name) != nullptr; }
