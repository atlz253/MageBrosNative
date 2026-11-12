#include "d3dapp.h"
#include "interp.h"
#include "d3dtextr.h"
#include "../core/config.h"
#include "../core/log.h"
#include "../gfx/device.h"
#include "../input/input.h"
#include "../audio/midi.h"
#include "../ui/options_menu.h"
#include "../ui/stats_overlay.h"
#include "version.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

static float g_tickAlpha = 0.f;
static uint64_t g_timeBase = 0;  // ns
static constexpr uint64_t kTickNs = 10000000;  // a tick: 10 ms

uint64_t AppTimeNs() { return SDL_GetTicksNS() - g_timeBase + 1000000000ull; }
DWORD AppTimeMs() { return (DWORD)(AppTimeNs() / 1000000); }
float AppTickAlpha() { return g_tickAlpha; }
static float g_frameRate = 0.f;
float AppFrameRate() { return g_frameRate; }

HRESULT BackBuffer::Blt(const RECT* dst, Surface* src, const RECT* srcRect, bool keySrc) {
    return GetDevice().BltToBackBuffer(dst, src, srcRect, keySrc);
}
HRESULT BackBuffer::ColorFill(const RECT* dst, uint32_t rgb) { return GetDevice().ColorFill(dst, rgb); }

CD3DApplication::CD3DApplication()
    : menu_(std::make_unique<OptionsMenu>(this)), statsOverlay_(std::make_unique<StatsOverlay>()) {}
CD3DApplication::~CD3DApplication() {
    menu_.reset();  // owns a GL texture: free it while the context still exists
    statsOverlay_.reset();  // (the same)
    D3DTextr_FreeAll();
    Log("Shutdown: device");
    GetDevice().Shutdown();
    Log("Shutdown: GL context");
    if (glContext_) SDL_GL_DestroyContext((SDL_GLContext)glContext_);
    Log("Shutdown: window");
    if (window_) SDL_DestroyWindow(window_);
}

Device* CD3DApplication::GetD3DDevice() { return &GetDevice(); }
int CD3DApplication::DisplayFPS() const { return GetConfig().showFps ? 1 : 0; }

bool CD3DApplication::OptionsOpen() const { return menu_ && menu_->IsOpen(); }

void CD3DApplication::OpenOptions() {
    if (menu_) menu_->Open();
}

// The finished frame to the screen. With VSync and [video] low_latency, the graphics card is
// waited for afterwards: a driver may otherwise hold several finished frames in a queue, each a
// refresh of delay.
void CD3DApplication::SwapFrame() {
    // (the Statistics Overlay's times: the frame's work up to here, the swap, the wait for the card)
    uint64_t t0 = AppTimeNs();
    SDL_GL_SwapWindow(window_);
    uint64_t t1 = AppTimeNs();
    bool gpu = GetConfig().lowLatency && vsyncInEffect_ > 0;
    if (gpu) GetDevice().WaitForGpu();
    uint64_t t2 = gpu ? AppTimeNs() : t1;
    if (frameStartNs_ && t0 >= frameStartNs_) {
        statsWin_.cpuSum += (double)(t0 - frameStartNs_) / 1e6;
        statsWin_.swapSum += (double)(t1 - t0) / 1e6;
        statsWin_.swaps++;
        if (gpu) {
            statsWin_.gpuSum += (double)(t2 - t1) / 1e6;
            statsWin_.gpuWaits++;
        }
    }
}

void CD3DApplication::UpdateRefreshRate() {
    const SDL_DisplayMode* m = window_ ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)) : nullptr;
    if (!m) return;  // (keep the last one known)
    refreshHz_ = m->refresh_rate_denominator > 0 ? (double)m->refresh_rate_numerator / m->refresh_rate_denominator
                                                 : (double)m->refresh_rate;
}

double CD3DApplication::FrameLimit() const {
    int f = GetConfig().maxFps;
    if (f > 0) return f;
    if (f < 0 && refreshHz_ > 0.0) return refreshHz_ / -f;
    return 0.0;
}

bool CD3DApplication::SwapPaced() const {
    const Config& cfg = GetConfig();
    return cfg.interpolation && cfg.maxFps < 0 && vsyncInEffect_ > 0 && swapEvery_ == -cfg.maxFps && vsyncOverride_ <= 0;
}

void CD3DApplication::ApplyVSync() {
    const Config& cfg = GetConfig();
    int want = cfg.vsync;
    UpdateRefreshRate();
    // a limit that is a part of the refresh rate (max_fps -2..-4, Smooth motion on): a picture
    // every n-th refresh, paced by the driver
    int every = cfg.interpolation && cfg.maxFps < 0 ? -cfg.maxFps : 1;
    auto set = [&](int n) {
        bool ok = false;
        if (want == 2) ok = adaptiveSupported_ && SDL_GL_SetSwapInterval(-n);
        if (!ok) ok = SDL_GL_SetSwapInterval(want == 0 ? 0 : n);
        return ok;
    };
    bool ok = set(every);
    if (!ok && every > 1) ok = set(1);  // (the limiter then keeps the limit)
    if (!ok) Log("VSync: the driver refused it: %s", SDL_GetError());
    int got = 1;
    if (!SDL_GL_GetSwapInterval(&got)) got = want == 0 ? 0 : 1;
    vsyncInEffect_ = got < 0 ? 2 : got > 0 ? 1 : 0;
    swapEvery_ = got < 0 ? -got : got;
    vsyncOverride_ = vsyncVote_ = vsyncVotes_ = 0;  // judged afresh
    int key = want * 100 + vsyncInEffect_ * 10 + swapEvery_;
    if (key != vsyncLogged_) {
        static const char* names[] = {"off", "on", "adaptive"};
        char part[48] = "";
        if (swapEvery_ > 1) snprintf(part, sizeof part, ", a picture every %d refreshes", swapEvery_);
        Log("VSync: %s%s%s", names[vsyncInEffect_], part,
            want == 2 && vsyncInEffect_ != 2 ? " (adaptive isn't supported by the driver)" : "");
        vsyncLogged_ = key;
    }
}

void CD3DApplication::JudgeVSync() {
    const Config& cfg = GetConfig();
    // only while frames are drawn freely: no limit, or one the driver paces with VSync (keep the
    // verdict otherwise)
    bool limiter = cfg.maxFps > 0 || (cfg.maxFps < 0 && !SwapPaced());    if (!cfg.interpolation || limiter || (menu_ && menu_->IsOpen()) || !window_) return;
    float hz = (float)refreshHz_;
    if (hz <= 0.f) return;
    // VSync on (or adaptive) never goes above the refresh rate (or the part of it asked for); off,
    // drawing freely, it lands on it only when something else holds it there. (Seen above the
    // part asked for, the driver isn't pacing it: SwapPaced turns false and the limiter does.)
    int vote = 0;
    if (vsyncInEffect_ > 0 && m_fFPS > hz / (float)std::max(1, swapEvery_) * 1.15f) vote = 1;
    // (within half a percent: forced VSync lands on the refresh rate itself, while a frame cap set
    // just under it in the driver, 141 on 144 Hz as advised for G-Sync / FreeSync, is 2% away)
    else if (vsyncInEffect_ == 0 && std::fabs(m_fFPS - hz) < hz * 0.005f) vote = -1;
    vsyncVotes_ = vote == vsyncVote_ ? vsyncVotes_ + 1 : 1;
    vsyncVote_ = vote;
    if (vsyncVotes_ >= 2 && vsyncOverride_ != vote) {
        vsyncOverride_ = vote;
        if (vote) Log("VSync: %.0f frames a second on a %.0f Hz monitor: the graphics driver seems to have VSync %s", m_fFPS,
                      hz, vote > 0 ? "off" : "forced on");
    }
}

// A monitor by its [video] display number (0 = primary, 1..n in SDL's order; the primary if
// that one isn't connected).
static SDL_DisplayID DisplayFor(int idx) {
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (idx > 0) {
        int n = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&n);
        if (ids && idx <= n) d = ids[idx - 1];
        SDL_free(ids);
    }
    return d;
}

// Monitor selected in the config (0 = primary).
static SDL_DisplayID ConfigDisplay() { return DisplayFor(GetConfig().displayIndex); }

std::string CD3DApplication::DisplayName(int index) {
    const char* n = SDL_GetDisplayName(DisplayFor(index));
    return n ? n : "";
}

int CD3DApplication::DisplayCount() {
    int n = 0;
    SDL_free(SDL_GetDisplays(&n));
    return n;
}

// The [video] display number (1..n) of the one monitor called `name`; 0 for none, or for more
// than one (two identical monitors: then the number decides).
static int DisplayIndexForName(const std::string& name) {
    int n = 0, found = 0, count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    for (int i = 0; ids && i < n; i++) {
        const char* dn = SDL_GetDisplayName(ids[i]);
        if (dn && name == dn) found = i + 1, count++;
    }
    SDL_free(ids);
    return count == 1 ? found : 0;
}

void CD3DApplication::ResolveDisplay() {
    Config& cfg = GetConfig();
    if (cfg.displayName.empty()) {
        // an older ini with only the number: take that monitor's name from now on
        if (cfg.displayIndex > 0 && cfg.displayIndex <= DisplayCount()) SetDisplay(cfg.displayIndex);
        return;
    }
    int i = DisplayIndexForName(cfg.displayName);
    if (i > 0) {
        if (i != cfg.displayIndex) Log("Display: \"%s\" is now monitor %d (was %d)", cfg.displayName.c_str(), i, cfg.displayIndex);
        cfg.displayIndex = i;
    } else if (cfg.displayIndex != 0) {
        // not connected (or not told apart): the primary for now; the name stays, so the game goes
        // back to that monitor once it is there again
        Log("Display: \"%s\" not found, using the primary monitor", cfg.displayName.c_str());
        cfg.displayIndex = 0;
    }
}

void CD3DApplication::SetDisplay(int index) {
    Config& cfg = GetConfig();
    cfg.displayIndex = index;
    cfg.displayName = index > 0 ? DisplayName(index) : std::string();
}

static void LogDisplays() {
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; ids && i < n; i++) {
        const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(ids[i]);
        SDL_Rect b{};
        SDL_GetDisplayBounds(ids[i], &b);
        const char* name = SDL_GetDisplayName(ids[i]);
        Log("Monitor %d: \"%s\", %dx%d at %.2f Hz, scale %.0f%%, at (%d, %d)%s", i + 1, name ? name : "",
            m ? m->w : 0, m ? m->h : 0, m ? m->refresh_rate : 0.f, SDL_GetDisplayContentScale(ids[i]) * 100.f, b.x, b.y,
            ids[i] == primary ? ", primary" : "");
    }
    SDL_free(ids);
}

// A window whose inside starts at (x, y) and is w wide is on monitor d and can be grabbed there:
// the top of its inside lies on it, with at least 40 pixels of it across. (A saved position on
// another monitor than the one chosen, or on one no longer connected, isn't used.)
static bool WindowPosOn(SDL_DisplayID d, int x, int y, int w) {
    SDL_Rect b;
    if (!SDL_GetDisplayBounds(d, &b)) return false;
    return y >= b.y && y < b.y + b.h - 40 && x + w > b.x + 40 && x < b.x + b.w - 40;
}

void CD3DApplication::WindowFitArea(int display, int* w, int* h) const {
    SDL_DisplayID d = DisplayFor(display);
    SDL_Rect u{0, 0, 1920, 1080};
    SDL_GetDisplayUsableBounds(d, &u);
    int top = 0, left = 0, bottom = 0, right = 0;
    bool known = window_ && !(SDL_GetWindowFlags(window_) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS)) &&
                 SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right) && (top | left | bottom | right);
    if (!known) {
        // no bordered window to measure: a Windows 10/11 title bar and frame at that monitor's scale
        float s = SDL_GetDisplayContentScale(d);
        if (s <= 0.f) s = 1.f;
        top = (int)(40 * s + 0.5f);
        left = right = bottom = (int)(8 * s + 0.5f);
    }
    *w = std::max(320, u.w - left - right);
    *h = std::max(240, u.h - top - bottom);
}

void CD3DApplication::DefaultWindowSize(int display, int* w, int* h) const {
    int fw = 0, fh = 0;
    WindowFitArea(display, &fw, &fh);
    *w = 640;
    *h = 480;
    for (float s : kWindowScales) {
        int sw = (int)(640 * s + 0.5f), sh = (int)(480 * s + 0.5f);
        if (sw <= fw && sh <= fh) *w = sw, *h = sh;
    }
}

// The window at its saved size, where it was last if that is on the chosen monitor, otherwise
// centred on that monitor. Twice: moving to a monitor with another display scale makes Windows
// resize the window for that scale (keeping the old scale's frame), so the size is set again
// once it is there, and a centred window centred again at that size.
void CD3DApplication::PlaceWindow() {
    const Config& cfg = GetConfig();
    SDL_DisplayID target = ConfigDisplay();
    bool saved = cfg.windowPosSet && WindowPosOn(target, cfg.windowX, cfg.windowY, cfg.windowWidth);
    for (int pass = 0; pass < 2; pass++) {
        SDL_SetWindowSize(window_, cfg.windowWidth, cfg.windowHeight);
        if (saved) SDL_SetWindowPosition(window_, cfg.windowX, cfg.windowY);
        else SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
        SDL_SyncWindow(window_);
    }
}

void CD3DApplication::SaveWindowState() {
    windowChangedAt_ = 0;
    Config& cfg = GetConfig();
    if (!window_ || cfg.fullscreen != FullscreenMode::Windowed) return;
    SDL_WindowFlags f = SDL_GetWindowFlags(window_);
    if (f & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN)) return;
    bool changed = false;
    bool max = (f & SDL_WINDOW_MAXIMIZED) != 0;
    if (max != cfg.maximized) cfg.maximized = max, changed = true;
    if (!max) {
        // the size and place to come back to (a maximized window keeps the ones from before)
        int w = 0, h = 0, x = 0, y = 0;
        SDL_GetWindowSize(window_, &w, &h);
        SDL_GetWindowPosition(window_, &x, &y);
        if (w >= 320 && h >= 240 && (w != cfg.windowWidth || h != cfg.windowHeight))
            cfg.windowWidth = w, cfg.windowHeight = h, changed = true;
        if (!cfg.windowPosSet || x != cfg.windowX || y != cfg.windowY)
            cfg.windowPosSet = true, cfg.windowX = x, cfg.windowY = y, changed = true;
    }
    // the monitor it is on now (kept as 0 while that is the primary)
    SDL_DisplayID d = SDL_GetDisplayForWindow(window_);
    if (d && d != DisplayFor(cfg.displayIndex)) {
        int idx = 0;
        if (d != SDL_GetPrimaryDisplay()) {
            int n = 0;
            SDL_DisplayID* ids = SDL_GetDisplays(&n);
            for (int i = 0; ids && i < n; i++)
                if (ids[i] == d) idx = i + 1;
            SDL_free(ids);
        }
        SetDisplay(idx), changed = true;
    }
    if (!changed) return;
    if (!cfg.Save()) Log("Config: could not save %s", cfg.path.c_str());
    Log("Display: window %dx%d at (%d, %d)%s, monitor %d", cfg.windowWidth, cfg.windowHeight, cfg.windowX, cfg.windowY,
        cfg.maximized ? ", maximized" : "", cfg.displayIndex);
}

// Borderless mode on Windows is a plain borderless window rather than SDL's "desktop fullscreen":
// GPU drivers promote an OpenGL window that exactly covers a monitor to exclusive fullscreen,
// which makes Alt+Tab flicker, black out or minimize the game. One extra pixel of height keeps
// the window from matching the monitor exactly (that row is off-screen). Elsewhere SDL's
// desktop fullscreen already behaves as borderless.
static bool UseBorderlessWindow() {
#if defined(_WIN32)
    return true;
#else
    return SDL_getenv("MBN_BORDERLESS_WINDOW") != nullptr;  // for testing the Windows path
#endif
}

void CD3DApplication::ApplyDisplaySettings() {
    const Config& cfg = GetConfig();
    SDL_DisplayID target = ConfigDisplay();
    // leave fullscreen (and a maximized window) first so size and monitor changes take effect cleanly
    SDL_SetWindowFullscreen(window_, false);
    SDL_RestoreWindow(window_);
    GetDevice().SetHiddenBottomRows(0);  // (the borderless window sets its own)
    SDL_SyncWindow(window_);
    if (cfg.fullscreen != FullscreenMode::Windowed) GetConfig().lastFullscreen = cfg.fullscreen;  // for F11

    if (cfg.fullscreen == FullscreenMode::Windowed) {
        SDL_SetWindowBordered(window_, true);
        SDL_SetWindowResizable(window_, true);
        PlaceWindow();
        if (cfg.maximized) {
            SDL_MaximizeWindow(window_);
            SDL_SyncWindow(window_);
        }
        Log("Display: window %dx%d%s, monitor %d", cfg.windowWidth, cfg.windowHeight, cfg.maximized ? " (maximized)" : "",
            cfg.displayIndex);
    } else if (cfg.fullscreen == FullscreenMode::Borderless) {
        SDL_Rect b{0, 0, cfg.windowWidth, cfg.windowHeight};
        SDL_GetDisplayBounds(target, &b);
        if (UseBorderlessWindow()) {
            SDL_SetWindowResizable(window_, false);
            SDL_SetWindowBordered(window_, false);
            SDL_SetWindowPosition(window_, b.x, b.y);
            SDL_SetWindowSize(window_, b.w, b.h + 1);
            GetDevice().SetHiddenBottomRows(1);
            SDL_SyncWindow(window_);
            SDL_RaiseWindow(window_);
        } else {
            SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
            SDL_SyncWindow(window_);
            SDL_SetWindowFullscreenMode(window_, nullptr);
            SDL_SetWindowFullscreen(window_, true);
            SDL_SyncWindow(window_);
        }
        Log("Display: borderless %dx%d, monitor %d", b.w, b.h, cfg.displayIndex);
    } else {
        // exclusive: desktop resolution unless a specific mode was chosen
        SDL_SetWindowBordered(window_, true);
        SDL_SetWindowResizable(window_, true);
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
        SDL_SyncWindow(window_);
        // the chosen size (or the desktop's) at the chosen refresh rate (or the desktop's): SDL
        // picks the closest mode the monitor has
        SDL_DisplayMode mode{};
        bool have = false;
        const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(target);
        int w = cfg.fullscreenWidth, h = cfg.fullscreenHeight;
        if ((w <= 0 || h <= 0) && desk) w = desk->w, h = desk->h;
        if (w > 0 && h > 0) have = SDL_GetClosestFullscreenDisplayMode(target, w, h, cfg.fullscreenRefresh, true, &mode);
        if (!have && desk) {
            mode = *desk;
            have = true;
        }
        SDL_SetWindowFullscreenMode(window_, have ? &mode : nullptr);
        SDL_SetWindowFullscreen(window_, true);
        SDL_SyncWindow(window_);
        Log("Display: exclusive %dx%d @ %.0f Hz, monitor %d", mode.w, mode.h, mode.refresh_rate, cfg.displayIndex);
    }
    if (glContext_) ApplyVSync();  // (the monitor and its refresh rate may have changed: judged afresh)
}

// Alt+Enter / F11: from a window to the fullscreen mode used last (borderless, or exclusive at its
// chosen resolution), from either fullscreen back to the window; saved like a menu change.
void CD3DApplication::ToggleFullscreen() {
    Config& cfg = GetConfig();
    if (windowChangedAt_) SaveWindowState();  // a window just moved to another monitor goes fullscreen there
    cfg.fullscreen = cfg.fullscreen == FullscreenMode::Windowed ? cfg.lastFullscreen : FullscreenMode::Windowed;
    ApplyDisplaySettings();
    if (!cfg.Save()) Log("Config: could not save %s", cfg.path.c_str());
}

HRESULT CD3DApplication::Create(const std::vector<std::string>& args) {
    Config& cfg = GetConfig();
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--auto" && i + 1 < args.size()) {
            std::stringstream ss(args[++i]);
            std::string item;
            while (std::getline(ss, item, ';')) {
                size_t c = item.find(':');
                if (c == std::string::npos) continue;
                AutoStep s;
                s.t = (DWORD)atoi(item.substr(0, c).c_str());
                std::string act = item.substr(c + 1);
                size_t eq = act.find('=');
                s.action = act.substr(0, eq);
                s.arg = eq == std::string::npos ? "" : act.substr(eq + 1);
                s.done = false;
                auto_.push_back(s);
            }
        }
    }

#if defined(MBN_GLES)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    window_ = SDL_CreateWindow(m_strWindowTitle, cfg.windowWidth, cfg.windowHeight,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window_) {
        Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return E_FAIL;
    }
    SDL_SetWindowMinimumSize(window_, 320, 240);
    LogDisplays();
    {
        int before = cfg.displayIndex;
        std::string name = cfg.displayName;
        ResolveDisplay();
        if (cfg.displayIndex != before || cfg.displayName != name) cfg.Save();
    }
    if (cfg.displayIndex > 0) {
        int n = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&n);
        if (!ids || cfg.displayIndex > n)
            Log("Config: display %d not found (%d connected), using the primary monitor", cfg.displayIndex, n);
        SDL_free(ids);
    }
    if (cfg.firstRun) {
        // a fresh ini: the largest 4:3 window that fits the monitor, kept from now on
        DefaultWindowSize(cfg.displayIndex, &cfg.windowWidth, &cfg.windowHeight);
        SDL_SetWindowSize(window_, cfg.windowWidth, cfg.windowHeight);
        cfg.Save();
        Log("Display: first start, window %dx%d", cfg.windowWidth, cfg.windowHeight);
    }
    PlaceWindow();
    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        Log("OpenGL %s context creation failed: %s",
#if defined(MBN_GLES)
            "ES 3.0",
#else
            "3.3",
#endif
            SDL_GetError());
        return E_FAIL;
    }
    SDL_GL_MakeCurrent(window_, (SDL_GLContext)glContext_);
    adaptiveSupported_ = SDL_GL_SetSwapInterval(-1);  // can the driver do adaptive VSync?
    ApplyVSync();
    if (!GetDevice().Init(window_)) return E_FAIL;
    if (cfg.fullscreen != FullscreenMode::Windowed) ApplyDisplaySettings();
    SDL_ShowWindow(window_);
    if (cfg.fullscreen == FullscreenMode::Windowed && cfg.maximized) SDL_MaximizeWindow(window_);
    m_bActive = true;

    g_timeBase = SDL_GetTicksNS();
    HRESULT hr = OneTimeSceneInit();
    if (FAILED(hr)) return hr;
    hr = InitDeviceObjects();
    if (FAILED(hr)) return hr;
    m_bReady = true;
    SetLastTick((uint64_t)AppTimeMs() * 1000000);  // (ticks on whole milliseconds, as before)
    autoStart_ = AppTimeMs();
    return S_OK;
}

void CD3DApplication::Pause(bool pause) {
    (void)pause;  // nothing to do: after a long stop, XApplication::FrameMove takes at most 10 ticks and the rest are dropped
}

void CD3DApplication::UpdateStats() {
    // (in whole milliseconds: as seconds in a float the time loses precision over a long session,
    // and JudgeVSync compares the rate to within half a percent)
    DWORD t = AppTimeMs();
    const Device::FrameCounters& work = GetDevice().Counters();
    statsWin_.drawSum += (double)work.draws;
    statsWin_.glCallSum += (double)work.glCalls;
    statsWin_.uploadSum += (double)work.uploadedBytes;
    statsWin_.workFrames++;
    statsWin_.tickSum += frameTickMs_;
    statsWin_.renderSum += frameRenderMs_;
    statsWin_.timeFrames++;
    statsFrames_++;
    if (t - statsLastTime_ > 1000) {
        m_fFPS = statsFrames_ * 1000.f / (float)(t - statsLastTime_);
        g_frameRate = m_fFPS;
        SummarizeStats(t - statsLastTime_);
        const StatsSecond& s = statsSec_;
        Log("Frame work: %.1f draws, %.0f GL calls, %.1f KB uploaded, %.2f ms logic, %.2f ms render, "
            "%.2f ms CPU, %.2f ms swap, %.2f ms GPU, %.1f FPS",
            s.draws, s.glCalls, s.uploadKB, s.tick, s.render, s.cpu, s.swap, s.gpu, s.fps);
        statsFrames_ = 0;
        statsLastTime_ = t;
        UpdateRefreshRate();  // (the window may have been moved to another monitor)
        JudgeVSync();
    }
}

// ---------------------------------------------------------------- the Statistics Overlay (F9)

// A frame is about to be drawn, started at `nowNs`, having run `ticks` ticks; `late`: count how
// long after its latest tick it started (Smooth motion off, where each frame belongs to a tick).
void CD3DApplication::StatsFrameStart(uint64_t nowNs, DWORD ticks, bool late) {
    StatsWindow& w = statsWin_;
    StatsFrameRec rec;
    rec.ticks = (unsigned char)std::min<DWORD>(ticks, 255);
    if (w.lastStartNs && nowNs > w.lastStartNs && nowNs - w.lastStartNs < 1000000000ull) {
        double ms = (double)(nowNs - w.lastStartNs) / 1e6;
        rec.ms = (float)ms;
        if (w.frames == 0 || ms < w.frameMin) w.frameMin = ms;
        if (w.frames == 0 || ms > w.frameMax) w.frameMax = ms;
        w.frameSum += ms;
        w.frames++;
    }
    statsHist_[statsHistHead_] = rec;
    statsHistHead_ = (statsHistHead_ + 1) % kStatsHistory;
    statsHistCount_ = std::min(statsHistCount_ + 1, kStatsHistory);
    w.lastStartNs = nowNs;
    frameStartNs_ = nowNs;
    w.ticks += (int)ticks;
    w.tickFrames[std::min<DWORD>(ticks, 2)]++;
    if (late && nowNs >= lastTickNs_) {
        double ms = (double)(nowNs - lastTickNs_) / 1e6;
        w.lateSum += ms;
        w.lateMax = std::max(w.lateMax, ms);
        w.lateFrames++;
    }
}

// Once a second, with the frame rate: the second just measured becomes the one shown.
void CD3DApplication::SummarizeStats(DWORD elapsedMs) {
    StatsWindow& w = statsWin_;
    StatsSecond& s = statsSec_;
    s.valid = true;
    s.fps = m_fFPS;
    s.frameAvg = w.frames ? w.frameSum / w.frames : 0;
    s.frameMin = w.frameMin;
    s.frameMax = w.frameMax;
    s.cpu = w.swaps ? w.cpuSum / w.swaps : 0;
    s.swap = w.swaps ? w.swapSum / w.swaps : 0;
    s.gpu = w.gpuWaits ? w.gpuSum / w.gpuWaits : -1;
    s.draws = w.workFrames ? w.drawSum / w.workFrames : 0;
    s.glCalls = w.workFrames ? w.glCallSum / w.workFrames : 0;
    s.uploadKB = w.workFrames ? w.uploadSum / w.workFrames / 1024.0 : 0;
    s.tick = w.timeFrames ? w.tickSum / w.timeFrames : 0;
    s.render = w.timeFrames ? w.renderSum / w.timeFrames : 0;
    s.ticksPerSecond = elapsedMs ? (int)std::lround(w.ticks * 1000.0 / elapsedMs) : 0;
    int drawn = w.tickFrames[0] + w.tickFrames[1] + w.tickFrames[2];
    for (int i = 0; i < 3; i++) s.tickPct[i] = drawn ? (int)std::lround(w.tickFrames[i] * 100.0 / drawn) : 0;
    s.lateAvg = w.lateFrames ? w.lateSum / w.lateFrames : -1;
    s.lateMax = w.lateFrames ? w.lateMax : -1;
    s.alpha = g_tickAlpha;
    double gpuSum = 0;
    int gpuFrames = 0;
    GetDevice().TakeGpu3D(&gpuSum, &gpuFrames);
    s.gpu3D = gpuFrames > 0 ? gpuSum / gpuFrames : -1;
    s.draws3D = GetDevice().Draws3D();
    s.lights = GetDevice().DynamicLights();
    GetDevice().TakeGpuAO(&gpuSum, &gpuFrames);
    s.gpuAO = gpuFrames > 0 ? gpuSum / gpuFrames : -1;
    GetDevice().TakeGpuShadows(&gpuSum, &gpuFrames);
    s.gpuShadows = gpuFrames > 0 ? gpuSum / gpuFrames : -1;
    GetDevice().TakeGpuBloom(&gpuSum, &gpuFrames);
    s.gpuBloom = gpuFrames > 0 ? gpuSum / gpuFrames : -1;
    uint64_t last = w.lastStartNs;
    w = StatsWindow{};
    w.lastStartNs = last;  // (the next frame's time is counted from the last one drawn)
}

// F9: Off, Compact, Full, Off. Saved, so it is as it was at the next start.
void CD3DApplication::CycleStatsOverlay() {
    Config& cfg = GetConfig();
    cfg.statsOverlay = (cfg.statsOverlay + 1) % 3;
    if (!cfg.Save()) Log("Config: could not save %s", cfg.path.c_str());
}

void CD3DApplication::DrawStatsOverlay() {
    const Config& cfg = GetConfig();
    if (cfg.statsOverlay <= 0 || !statsOverlay_) return;
    const StatsSecond& s = statsSec_;
    char b[96];
    auto num = [&](const char* fmt, double v) {
        snprintf(b, sizeof b, fmt, v);
        return std::string(b);
    };
    auto ms = [&](double v) { return num("%.2f ms", v); };
    const std::string wait = s.valid ? "" : "...";  // the first second isn't measured yet

    StatsGroup perf{"PERFORMANCE", {}};
    perf.rows.push_back({"FPS", s.valid ? num("%.1f", s.fps) : wait});
    perf.rows.push_back({"Frame avg", s.valid ? ms(s.frameAvg) : wait});
    snprintf(b, sizeof b, "%.2f-%.2f ms", s.frameMin, s.frameMax);
    perf.rows.push_back({"Frame range", s.valid ? std::string(b) : wait});
    perf.rows.push_back({"Logic", s.valid ? ms(s.tick) : wait});
    perf.rows.push_back({"Render", s.valid ? ms(s.render) : wait});
    perf.rows.push_back({"CPU", s.valid ? ms(s.cpu) : wait});
    perf.rows.push_back({"Swap wait", s.valid ? ms(s.swap) : wait});
    if (s.gpu >= 0) perf.rows.push_back({"GPU wait", ms(s.gpu)});
    else perf.rows.push_back({"GPU wait", "- (Low lat.)", true});
    perf.rows.push_back({"Draw calls", s.valid ? num("%.1f", s.draws) : wait});
    perf.rows.push_back({"GL calls", s.valid ? num("%.0f", s.glCalls) : wait});
    perf.rows.push_back({"Uploaded", s.valid ? num("%.1f KB", s.uploadKB) : wait});

    StatsGroup timing{"GAME TIMING", {}};
    timing.rows.push_back({"Ticks/s", s.valid ? std::to_string(s.ticksPerSecond) : wait});
    snprintf(b, sizeof b, "%d%% %d%% %d%%", s.tickPct[0], s.tickPct[1], s.tickPct[2]);
    timing.rows.push_back({"Ticks 0/1/2+", s.valid ? std::string(b) : wait});
    if (cfg.interpolation) {
        timing.rows.push_back({"Tick late", "- (Smooth)", true});
        timing.rows.push_back({"Blend", num("%.2f", s.alpha)});
    } else {
        snprintf(b, sizeof b, "%.2f max %.2f", s.lateAvg, s.lateMax);
        timing.rows.push_back({"Tick late", s.lateAvg >= 0 ? std::string(b) : "-", s.lateAvg < 0});
        timing.rows.push_back({"Blend", "1 (Smooth off)", true});
    }

    std::vector<StatsGroup> left{perf}, right{timing}, extra;
    if (cfg.statsOverlay >= 2) {
        Device& d = GetDevice();
        const FrameLayout& L = d.Layout();
        static const char* modes[] = {"Window", "Borderless", "Exclusive"};
        static const char* vs[] = {"Off", "On", "Adaptive"};
        StatsGroup disp{"DISPLAY", {}};
        disp.rows.push_back({"Mode", modes[std::clamp((int)cfg.fullscreen, 0, 2)]});
        snprintf(b, sizeof b, "%dx%d", L.windowW, L.windowH);
        disp.rows.push_back({"Window", b});
        snprintf(b, sizeof b, "%dx%d", L.destW, L.destH);
        disp.rows.push_back({"Picture", b});
        snprintf(b, sizeof b, "%dx%d %d%%", L.fboW, L.fboH, (int)std::lround(cfg.renderScale * 100));
        disp.rows.push_back({"Rendered", b});
        disp.rows.push_back({"Refresh", refreshHz_ > 0 ? num("%.2f Hz", refreshHz_) : std::string("?")});
        const char* mon = window_ ? SDL_GetDisplayName(SDL_GetDisplayForWindow(window_)) : nullptr;
        disp.rows.push_back({"", mon ? mon : "?", true});
        disp.rows.push_back({"VSync", vs[std::clamp(cfg.vsync, 0, 2)]});
        std::string inUse = vs[std::clamp(vsyncInEffect_, 0, 2)];
        if (vsyncOverride_ > 0) inUse += ", forced off?";
        else if (vsyncOverride_ < 0) inUse += ", forced on?";
        disp.rows.push_back({"VSync in use", inUse});
        disp.rows.push_back({"Swap interval", vsyncInEffect_ > 0 ? std::to_string(swapEvery_) : std::string("-")});
        double lim = FrameLimit();
        if (!cfg.interpolation) disp.rows.push_back({"Frame limit", "- (Smooth off)", true});
        else if (lim <= 0) disp.rows.push_back({"Frame limit", "None"});
        else disp.rows.push_back({"Frame limit", num("%.1f ", lim) + (SwapPaced() ? "driver" : "limiter")});
        disp.rows.push_back({"Low latency", cfg.lowLatency ? (vsyncInEffect_ > 0 ? "On" : "On (no VSync)") : "Off"});
        disp.rows.push_back({"Smooth motion", cfg.interpolation ? "On" : "Off"});
        disp.rows.push_back({"Profile", cfg.graphicsProfile == 1 ? "Performance"
                                      : cfg.graphicsProfile == 2 ? "Quality" : "Manual"});

        StatsGroup sys{"SYSTEM", {}};
        sys.rows.push_back({"", d.GLRenderer()});
        sys.rows.push_back({"", d.GLVendor(), true});
        sys.rows.push_back({"", "OpenGL " + d.GLVersion(), true});
        sys.rows.push_back({"Antialiasing", d.Samples() > 0 ? std::to_string(d.Samples()) + "x" : std::string("Off")});
        int af = cfg.anisotropy > 1 && d.MaxAnisotropy() > 1.f ? (int)std::min((float)cfg.anisotropy, d.MaxAnisotropy()) : 0;
        sys.rows.push_back({"Tex filtering", af > 1 ? std::to_string(af) + "x" : std::string("Off")});

        // The graphics enhancements in effect (G7): what is chosen, dimmed when nothing runs them
        // (they draw only in a level's recorded 3D: not on the other screens, nor with
        // MBN_NO_RECORD), with a line saying why. Every value read from the config as it is now.
        StatsGroup enh{"ENHANCEMENTS", {}};
        const bool running = d.Recording3D() && s.gpu3D >= 0;
        if (!d.Recording3D()) enh.rows.push_back({"", "Not running (recorder off)", true});
        else if (s.valid && s.gpu3D < 0) enh.rows.push_back({"", "Not running (levels only)", true});
        static const char* shadowQ[] = {"Off", "Low", "Medium", "High"};
        static const char* bloomQ[] = {"Off", "Low", "High"};
        auto pct = [&](const char* what, int v) {
            snprintf(b, sizeof b, "%s, %d%%", what, v);
            return std::string(b);
        };
        const bool ao = cfg.ambientOcclusion;
        const int sh = std::clamp(cfg.dynamicShadows, 0, 3);
        const int bl = std::clamp(cfg.bloom, 0, 2);
        enh.rows.push_back({"Ambient occl.", ao ? "On" : "Off", !running || !ao});
        enh.rows.push_back({"Shadows", sh > 0 ? pct(shadowQ[sh], cfg.shadowStrength) : std::string("Off"),
                            !running || sh == 0});
        enh.rows.push_back({"Lights", cfg.dynamicLights ? pct("On", cfg.lightStrength) : std::string("Off"),
                            !running || !cfg.dynamicLights});
        if (cfg.dynamicLights) enh.rows.push_back({"Ambient light", num("%.0f%%", (double)cfg.lightsAmbient), !running});
        else enh.rows.push_back({"Ambient light", "- (lights off)", true});
        enh.rows.push_back({"Bloom", bloomQ[bl], !running || bl == 0});

        // the graphics card's time for a level's 3D (the recorded draws, G2), and how many
        if (!d.Recording3D()) perf.rows.push_back({"GPU 3D", "- (off)", true});  // (MBN_NO_RECORD)
        else if (s.gpu3D >= 0) perf.rows.push_back({"GPU 3D", ms(s.gpu3D)});
        else perf.rows.push_back({"GPU 3D", "- (no level)", true});
        if (s.gpu3D >= 0) perf.rows.push_back({"3D draws", std::to_string(s.draws3D)});
        if (cfg.ambientOcclusion && s.gpuAO >= 0) perf.rows.push_back({"GPU AO", ms(s.gpuAO)});
        if (cfg.dynamicShadows > 0 && s.gpuShadows >= 0) perf.rows.push_back({"GPU shadows", ms(s.gpuShadows)});
        if (cfg.bloom > 0 && s.gpuBloom >= 0) perf.rows.push_back({"GPU bloom", ms(s.gpuBloom)});
        if (cfg.dynamicLights && s.gpu3D >= 0) perf.rows.push_back({"Lights", std::to_string(s.lights)});

        // two columns: how the game runs on the left, on what on the right; one: all stacked
        left = {perf, timing};
        right = {disp, sys};
        extra = {enh};
    }
    // The graphs (O2): the frames kept, oldest first, and the frame time to expect: with Smooth
    // motion off a tick (10 ms: the original's 100 a second); on, the frame limit, else with VSync
    // the refresh (times the swap interval), else none (drawing freely), scaled by the average.
    StatsGraphs graphs;
    graphs.smooth = cfg.interpolation;
    for (int i = 0; i < statsHistCount_; i++) {
        const StatsFrameRec& r = statsHist_[(statsHistHead_ - statsHistCount_ + i + kStatsHistory) % kStatsHistory];
        graphs.frameMs.push_back(r.ms);
        graphs.alpha.push_back(r.alpha);
        graphs.ticks.push_back(r.ticks);
    }
    double lim = FrameLimit();
    if (!cfg.interpolation) {
        graphs.targetMs = 10.f;
        graphs.target = "tick";
    } else if (lim > 0 && !SwapPaced()) {
        graphs.targetMs = (float)(1000.0 / lim);
        graphs.target = "limit";
    } else if (vsyncInEffect_ > 0 && refreshHz_ > 0) {
        graphs.targetMs = (float)(1000.0 * std::max(1, swapEvery_) / refreshHz_);
        graphs.target = swapEvery_ > 1 ? "refreshes" : "refresh";
    }
    float base = graphs.targetMs > 0 ? graphs.targetMs : (float)std::max(0.5, s.frameAvg);
    graphs.scaleMs = std::max(2.f * base, 2.f);  // (the expected time halfway up; longer clips at the top)

    // below the HUD's level info (its two rows end at about 58, grown by the HUD size in a level);
    // the graphs' own panel, where it has one, below the HUD's top-right panel (ends at about 80)
    const float hud = InLevel() ? std::max(1.0f, cfg.hudScale) : 1.0f;
    float top = 4.f + 58.f * hud, sideTop = 4.f + 80.f * hud;
    std::string sub = std::string(cfg.statsOverlay >= 2 ? "FULL" : "COMPACT") + "  v" + MBN_VERSION_STRING;
    statsOverlay_->Render(&GetDevice(), top, sideTop, cfg.statsOverlay >= 2, "STATISTICS OVERLAY", sub, left, right,
                          extra, graphs);
}

// With Smooth motion on, the game standing still keeps the alpha of the last frame drawn: the
// latest tick is moved along so that the next one comes due that far on. (With it off, alpha is
// 1 anyway and the next tick comes due a full tick from the frame's time, as before.)
void CD3DApplication::HoldTickPhase(uint64_t nowNs, uint64_t tickNow) {
    if (!GetConfig().interpolation) {
        SetLastTick(tickNow);
        return;
    }
    uint64_t phase = (uint64_t)(g_tickAlpha * kTickNs);
    if (phase >= kTickNs) phase = kTickNs - 1;
    // on a whole millisecond, rounded down: the ticks are counted from the frame's time in
    // milliseconds, so a latest tick between two of them would make every later tick come due
    // up to 1 ms late, the picture standing still meanwhile (found in the review of 2026-11-09).
    // The picture carries on at most a tenth of a tick further along, never back.
    SetLastTick((nowNs - phase) / 1000000 * 1000000);
}

HRESULT CD3DApplication::Render3DEnvironment(DWORD now, bool forceRender) {
    // the ticks are counted from `now`, the frame's time in milliseconds (the time the test
    // automation and the menu see), as before; the nanosecond clock only places the picture
    // between two ticks
    uint64_t nowNs = AppTimeNs();
    if (nowNs < lastTickNs_) SetLastTick(nowNs / 1000000 * 1000000);  // (on a whole millisecond, as every tick)
    uint64_t tickNow = (uint64_t)now * 1000000;
    menu_->Update(now);
    if (menu_->IsOpen()) {
        // game paused underneath: keep showing the last frame, don't accumulate catch-up time
        HoldTickPhase(nowNs, tickNow);
        heldLastFrame_ = true;
        StatsFrameStart(nowNs, 0, false);
        StatsFrameAlpha(GetConfig().interpolation ? g_tickAlpha : 1.f);
        frameTickMs_ = 0;
        uint64_t renderStart = AppTimeNs();
        GetDevice().BeginFrame();
        HRESULT hr = Render();
        frameRenderMs_ = (double)(AppTimeNs() - renderStart) / 1e6;
        if (FAILED(hr)) return hr;
        UpdateStats();
        DrawStatsOverlay();
        menu_->Render(&GetDevice());
        GetDevice().Present();
        SwapFrame();
        return S_OK;
    }
    // with Smooth motion, the first frame after the menu or a pause carries on from the held
    // picture (the menu draws at 60 a second: up to 16 ms would otherwise be caught up at once)
    if (heldLastFrame_ && GetConfig().interpolation) {
        HoldTickPhase(nowNs, tickNow);
        tickNow = lastTickNs_;
    }
    heldLastFrame_ = !m_bFrameMoving;
    uint64_t elapsed = tickNow > lastTickNs_ ? tickNow - lastTickNs_ : 0;
    DWORD ticks = (DWORD)(elapsed / kTickNs);
    if (ticks == 0 && !forceRender) return S_OK;
    uint64_t tickStart = AppTimeNs();
    DWORD ran = 0;
    if (ticks > 0) {
        SetLastTick(tickNow - elapsed % kTickNs);
        if (m_bFrameMoving || m_bSingleStep) {
            HRESULT hr = FrameMove(ticks);
            if (FAILED(hr)) return hr;
            m_bSingleStep = false;
            ran = ticks;
        }
    }
    // (with Smooth motion off a frame is drawn for its tick: how long after the tick it started)
    StatsFrameStart(nowNs, ran, ran > 0 && !GetConfig().interpolation);
    if (m_bFrameMoving) {
        g_tickAlpha = nowNs > lastTickNs_ ? (float)(nowNs - lastTickNs_) / (float)kTickNs : 0.f;
        if (g_tickAlpha > 1.f) g_tickAlpha = 1.f;
    } else if (GetConfig().interpolation) {
        HoldTickPhase(nowNs, tickNow);  // P: the picture stays as it was
    } else {
        g_tickAlpha = 1.f;
    }
    StatsFrameAlpha(GetConfig().interpolation ? g_tickAlpha : 1.f);  // (the blend this frame is drawn at)
    uint64_t renderStart = AppTimeNs();
    frameTickMs_ = (double)(renderStart - tickStart) / 1e6;
    GetDevice().BeginFrame();
    HRESULT hr = Render();
    frameRenderMs_ = (double)(AppTimeNs() - renderStart) / 1e6;
    if (FAILED(hr)) return hr;
    UpdateStats();
    DrawStatsOverlay();
    GetDevice().Present();
    SwapFrame();
    return S_OK;
}

void CD3DApplication::HandleAutomation(DWORD now) {
    DWORD t = now - autoStart_;
    for (auto& s : auto_) {
        if (s.done || t < s.t) continue;
        s.done = true;
        if (s.action == "key" || s.action == "down" || s.action == "up") {
            SDL_Scancode sc = SDL_GetScancodeFromName(s.arg.c_str());
            int dik = Input::DikFromScancode(sc);
            if (s.action != "up") {
                Input::InjectKey(dik, true);
                OnKeyDown(dik);
            }
            if (s.action != "down") {
                // a "key" tap stays down for 50 ms so the 100 Hz polling sees it
                AutoStep rel{s.t + 50, "up", s.arg, false};
                if (s.action == "up") Input::InjectKey(dik, false);
                else auto_.push_back(rel);
            }
            return;  // vector may have grown
        } else if (s.action == "sdlkey") {
            // a real key press through the SDL event queue (reaches the options menu)
            SDL_Event ev{};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = SDL_GetScancodeFromName(s.arg.c_str());
            ev.key.down = true;
            ev.key.windowID = SDL_GetWindowID(window_);
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_KEY_UP;
            ev.key.down = false;
            SDL_PushEvent(&ev);
        } else if (s.action == "focus") {
            // test-only: simulate the window losing (focus=0) or regaining (focus=1) focus
            SDL_Event ev{};
            ev.type = s.arg == "0" ? SDL_EVENT_WINDOW_FOCUS_LOST : SDL_EVENT_WINDOW_FOCUS_GAINED;
            ev.window.windowID = SDL_GetWindowID(window_);
            SDL_PushEvent(&ev);
        } else if (s.action == "vpad") {
            // test-only virtual controller: "vpad", "padaxis=<axis>:<value>", "padbtn=<button>:<0|1>"
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "Automation pad";
            SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
            autoPad_ = SDL_OpenJoystick(id);
            Log("auto: virtual pad %u %s", (unsigned)id, autoPad_ ? "attached" : SDL_GetError());
        } else if ((s.action == "padaxis" || s.action == "padbtn") && autoPad_) {
            size_t c = s.arg.find(':');
            int idx = atoi(s.arg.substr(0, c).c_str()), val = atoi(s.arg.substr(c + 1).c_str());
            if (s.action == "padaxis") SDL_SetJoystickVirtualAxis((SDL_Joystick*)autoPad_, idx, (Sint16)val);
            else SDL_SetJoystickVirtualButton((SDL_Joystick*)autoPad_, idx, val != 0);
        } else if (s.action == "shot") {
            GetDevice().SaveScreenshot(s.arg.c_str());
            Log("auto: screenshot %s", s.arg.c_str());
        } else if (s.action == "quit") {
            quit_ = true;
        } else {
            OnAutomation(s.action, s.arg);
        }
    }
}

// The frame limiter's wait, to the moment itself: asleep until 1.5 ms before, then watching the
// clock. A sleep comes back up to a millisecond late (which made limited frames start 0.3 ms
// late on average and up to 1.3: the review of 2026-11-09, issue 9), so the last stretch isn't
// slept. Measured: frames start within 0.05 ms of when they are due, for 1.5 ms of processor
// time a frame (6% of one core at a limit of 60, 16% at 141). SDL_DelayPrecise alone costs
// less but still comes back up to a millisecond late now and then.
static void WaitNs(uint64_t ns) {
    constexpr uint64_t kWatchNs = 1500000;
    uint64_t end = SDL_GetTicksNS() + ns;
    if (ns > kWatchNs) SDL_DelayNS(ns - kWatchNs);
    while (SDL_GetTicksNS() < end) SDL_CPUPauseInstruction();
}

int CD3DApplication::Run() {
    const Config& cfg = GetConfig();
    uint64_t perfFreq = SDL_GetPerformanceFrequency();
    uint64_t lastPresent = SDL_GetPerformanceCounter();
    while (!quit_) {
        SDL_Event e;
        bool gotEvent = m_bActive ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 100);
        while (gotEvent) {
            Input::HandleEvent(e);
            switch (e.type) {
                case SDL_EVENT_QUIT: quit_ = true; break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED: Input::SetFocus(true); OnActivateApp(true); break;
                case SDL_EVENT_WINDOW_FOCUS_LOST: Input::SetFocus(false); OnActivateApp(false); break;
                case SDL_EVENT_WINDOW_MINIMIZED: m_bActive = false; break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    WindowChanged(AppTimeMs());
                    [[fallthrough]];
                case SDL_EVENT_WINDOW_SHOWN:
                    if (!m_bActive) SetLastTick((uint64_t)AppTimeMs() * 1000000);
                    m_bActive = true;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_MOVED:
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED: WindowChanged(AppTimeMs()); break;
                case SDL_EVENT_DISPLAY_ADDED:
                case SDL_EVENT_DISPLAY_REMOVED:
                    LogDisplays();
                    ResolveDisplay();  // (the numbers may have moved)
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (!e.key.repeat && ((e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
                                          e.key.scancode == SDL_SCANCODE_F9 || e.key.scancode == SDL_SCANCODE_F11 ||
                                          e.key.scancode == SDL_SCANCODE_F12)) {
                    } else if (menu_->HandleEvent(e)) {
                        break;
                    }
                    if (e.key.repeat) break;
                    if ((e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)) ||
                        e.key.scancode == SDL_SCANCODE_F11) {
                        ToggleFullscreen();
                        break;
                    }
                    if (e.key.scancode == SDL_SCANCODE_F9) {
                        CycleStatsOverlay();
                        break;
                    }
                    if (e.key.scancode == SDL_SCANCODE_F12) {
                        char name[1024];
                        const char* base = SDL_GetBasePath();
                        snprintf(name, sizeof name, "%smagebros_%u.bmp", base ? base : "", (unsigned)AppTimeMs());
                        GetDevice().SaveScreenshot(name);
                        Log("Saved screenshot %s", name);
                        break;
                    }
                    {
                        std::vector<int> diks;
                        Input::DiksForScancode(e.key.scancode, &diks);
                        for (int d : diks) OnKeyDown(d);
                    }
                    break;
                default:
                    menu_->HandleEvent(e);  // gamepad: Start opens the menu; navigation while open
                    break;
            }
            gotEvent = SDL_PollEvent(&e);
        }
        if (quit_) break;
        // Select+Start quits from anywhere, as the console's other ports do. Checked after the
        // events are pumped, so the chord is still seen while the options menu is open.
        if (Input::QuitChordDown()) {
            Log("Quit: Select+Start");
            quit_ = true;
            break;
        }
        if (Midi::PollFinished()) OnMidiNotifySuccessful();
        DWORD now = AppTimeMs();
        if (windowChangedAt_ && now - windowChangedAt_ >= kWindowSettleMs) SaveWindowState();
        if (!auto_.empty()) HandleAutomation(now);
        if (!m_bActive || !m_bReady) continue;

        // frame pacing for the interpolated (decoupled) path; the options menu always redraws (max 60 fps)
        bool menuOpen = menu_->IsOpen();
        bool force = cfg.interpolation || menuOpen;
        // (a limit that is a part of the refresh rate is the driver's to pace while VSync shows a
        // picture every n-th refresh: SwapPaced)
        double cap = menuOpen ? 60.0 : SwapPaced() ? 0.0 : FrameLimit();
        bool capped = force && cap > 0.0;
        if (capped) {
            double minDt = 1.0 / cap;
            uint64_t counter = SDL_GetPerformanceCounter();
            double dt = (double)(counter - lastPresent) / perfFreq;
            if (dt < minDt) {
                WaitNs((uint64_t)((minDt - dt) * 1e9));
                continue;  // (the events and the time are read afresh, right before the frame)
            }
            // the next frame is due one interval after this one was due, not after it is drawn
            // (else the drawing time adds to every frame: a limit of 60 gave 58); a frame more
            // than an interval late starts the count again from now
            lastPresent = dt < 2 * minDt ? lastPresent + (uint64_t)(minDt * perfFreq) : counter;
        }
        DWORD before = m_dwLastFrameTime;
        if (FAILED(Render3DEnvironment(now, force))) quit_ = true;
        if (!force && m_dwLastFrameTime == before) {
            // the original only draws on frames with a tick: wait for the next one to come due
            // (to the moment itself: waiting a millisecond at a time drew each frame up to 2 ms
            // after its tick, by a different amount each time)
            uint64_t due = lastTickNs_ + kTickNs, t = AppTimeNs();
            if (due > t) SDL_DelayPrecise(std::min(due - t, kTickNs));
        } else if (!capped) {
            lastPresent = SDL_GetPerformanceCounter();
        }
    }
    if (windowChangedAt_) SaveWindowState();  // a resize or move just before quitting
    DeleteDeviceObjects();
    FinalCleanup();
    return 0;
}
