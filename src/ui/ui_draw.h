// Minimal immediate-mode drawing for overlay UI (the options menu, the Statistics Overlay, the
// CHEATS mark): text and filled rectangles, drawn in the game's 480-pixel-high virtual space on
// top of the finished frame. The font is Classic, the embedded 8x8 bitmap font (public-domain
// font8x8 by Daniel Hepper), unless [video] ui_font names a player's own .ttf / .otf in a "fonts"
// folder: that is drawn with stb_truetype at the screen's real size, so it stays sharp at any
// resolution. Every font fills the same 8 x 8 cell a character, so every layout is the same.
#pragma once
#include "../core/d3dtypes.h"
#include <string>
#include <vector>

class Device;
class Texture;

struct UIColor {
    float r, g, b, a;
};

class UIDraw {
public:
    ~UIDraw();
    void Begin(Device* d);   // call once per frame before drawing
    // Visible horizontal range of virtual space (wider than 0..640 in widescreen)
    float Left() const { return left_; }
    float Right() const { return right_; }
    void Rect(float x0, float y0, float x1, float y1, UIColor c);
    // Circles, in the same virtual space (the touch controller's stick and buttons).
    void Disc(float cx, float cy, float radius, UIColor c, int segments = 24);
    void Ring(float cx, float cy, float outer, float inner, UIColor c, int segments = 24);
    // scale: 1 = 8 px glyphs. Draws a soft drop shadow under the text.
    void Text(float x, float y, const std::string& s, float scale, UIColor c, bool shadow = true);
    float TextWidth(const std::string& s, float scale) const { return (float)s.size() * 8.f * scale; }
    // Batching (the Statistics Overlay): between BeginBatch and EndBatch, rectangles and letters
    // are gathered and drawn at EndBatch, every rectangle first, then every letter, each in the
    // order given. (One by one, each letter is a draw of its own.)
    void BeginBatch();
    void EndBatch();
    // The player's fonts: the .ttf / .otf file names in the "fonts" folders (next to MageBros.exe,
    // then next to this program), sorted; and whether one can be read (an empty name, Classic, can)
    static std::vector<std::string> AvailableFonts();
    static bool FontUsable(const std::string& name);

private:
    // An outline font's letters for one text scale at the current screen size: a 16 x 6 grid of
    // cells (characters 32 to 127), each a character's 8 x 8 cell plus room above and below
    struct Atlas {
        float scale = 0;
        Texture* tex = nullptr;
        float cellW = 0, cellH = 0;  // in pixels
        float padPx = 1;             // the empty pixel round each cell (for smooth sampling)
        float above = 0;             // virtual units the cell reaches above the 8 x 8 cell
    };
    void EnsureFont();
    Atlas* FontAtlas(float scale);
    void FreeAtlases();
    void Quad(std::vector<D3DLVERTEX>& to, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
              float v1, UIColor c);
    void Glyph(Texture* t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, UIColor c);
    Device* dev_ = nullptr;
    Texture* font_ = nullptr;
    float left_ = 0, right_ = 640;
    float pxPerUnit_ = 0;  // the screen's pixels per virtual unit (the render target's height / 480)
    std::string atlasFont_;  // the font the atlases were made from (empty: Classic)
    std::vector<Atlas> atlases_;
    bool batching_ = false;
    std::vector<D3DLVERTEX> rects_;
    std::vector<std::pair<Texture*, std::vector<D3DLVERTEX>>> glyphs_;  // in order, a run per texture
};
