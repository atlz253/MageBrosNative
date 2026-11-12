// Replacement for the DX7 SDK CD3DApplication (d3dapp.cpp) on SDL3.
#pragma once
#include "../core/d3dtypes.h"
#include <memory>
#include <string>
#include <vector>

class Device;
class Surface;
class OptionsMenu;
class StatsOverlay;
struct SDL_Window;

// What the game sees as IDirectDrawSurface7* for the back buffer.
class BackBuffer {
public:
    // DDBLT_KEYSRC is the only flag that matters for the game
    HRESULT Blt(const RECT* dst, Surface* src, const RECT* srcRect, bool keySrc);
    HRESULT ColorFill(const RECT* dst, uint32_t rgb);
};

#define DDBLT_WAIT 0x01000000
#define DDBLT_KEYSRC 0x00008000

class CD3DApplication {
public:
    CD3DApplication();
    virtual ~CD3DApplication();

    virtual HRESULT Create(const std::vector<std::string>& args);
    virtual int Run();
    virtual void Pause(bool pause);

    Device* GetD3DDevice();
    BackBuffer* GetBackBuffer() { return &backBuffer_; }
    int GetScreenWidth() const { return 640; }
    int GetScreenHeight() const { return 480; }
    int GetFPS() const { return (int)(m_fFPS + 0.5f); }
    int DisplayFPS() const;
    SDL_Window* GetWindow() const { return window_; }
    void RequestQuit() { quit_ = true; }
    // Re-apply window mode, size and monitor from the config (used by the options menu).
    void ApplyDisplaySettings();
    // [video] vsync to the swap interval: 0 off, 1 on, 2 adaptive (-1), which falls back to on where
    // the driver refuses it. What took effect is read back (VSyncInEffect).
    void ApplyVSync();
    bool AdaptiveVSyncSupported() const { return adaptiveSupported_; }
    int VSyncInEffect() const { return vsyncInEffect_; }  // 0, 1 or 2
    // What the frame rate says about the driver (NVIDIA Control Panel, AMD Software) overriding
    // VSync: +1 = VSync is on here but frames come faster than the monitor refreshes; -1 = it is
    // off here but frames stop at the refresh rate; 0 = nothing seen. Judged only while frames are
    // drawn freely (Smooth motion on, no frame limit, the menu closed), from two seconds running.
    int VSyncOverride() const { return vsyncOverride_; }
    // The refresh rate of the monitor the window is on, as its mode states it (143.86, not 144);
    // 0 while unknown. Read at every display or VSync change and once a second.
    double RefreshRate() const { return refreshHz_; }
    // [video] max_fps as frames a second: the number itself, or for -1..-4 the refresh rate, a
    // half, a third or a quarter of it (a picture then stays the same number of refreshes, which
    // a limit like 60 on a 144 Hz monitor can't give); 0 = none.
    double FrameLimit() const;
    // The room a window's inside has on a monitor (0 = primary, as [video] display): its usable
    // area (without the taskbar) less the window's title bar and borders.
    void WindowFitArea(int display, int* w, int* h) const;
    // The largest 4:3 window from kWindowScales (multiples of the game's 640 x 480) that fits
    // that monitor; at least 640 x 480. The first start and "Reset all settings" use it.
    void DefaultWindowSize(int display, int* w, int* h) const;
    // Monitors by [video] display number (1..n in SDL's order; 0 = the primary).
    static std::string DisplayName(int index);
    static int DisplayCount();
    // Choose a monitor: its number and its name (kept, so it is found again under another number).
    static void SetDisplay(int index);
    // display_name, when that monitor is connected, decides the number (at start and whenever a
    // monitor is plugged in or out); when it isn't, the primary is used for now.
    static void ResolveDisplay();
    static constexpr float kWindowScales[] = {1.f, 1.25f, 1.5f, 1.6f, 2.f, 2.5f, 3.f, 3.5f, 4.f, 4.5f, 5.f, 6.f};
    bool OptionsOpen() const;
    void OpenOptions();  // e.g. from the title screen's OPTIONS entry
    // Hooks for the options/pause menu, implemented by the game.
    enum class UISound { Move, Confirm };
    virtual void PlayUISound(UISound s) { (void)s; }
    virtual bool CanReturnToMainMenu() const { return false; }
    virtual void ReturnToMainMenu() {}
    virtual void ReturnToLevelSelect() {}  // from a level (offered beside the main menu when InLevel)
    virtual bool InLevel() const { return false; }
    // Whether F10 / Start may open the options menu right now (false during the intro logo,
    // the ending and the fades, and in a level whenever no mage is under the player's control).
    virtual bool CanOpenOptions() const { return true; }
    virtual void ReloadMusic() {}                       // music source changed: restart the current track
    virtual void ApplyModChanges() {}                   // mods or level pack changed (title screen only)
    virtual const char* MusicSource() const { return ""; }

protected:
    virtual HRESULT OneTimeSceneInit() { return S_OK; }
    virtual HRESULT InitDeviceObjects() { return S_OK; }
    virtual HRESULT DeleteDeviceObjects() { return S_OK; }
    virtual HRESULT Render() { return S_OK; }
    virtual HRESULT FrameMove(DWORD) { return S_OK; }
    virtual HRESULT RestoreSurfaces() { return S_OK; }
    virtual HRESULT FinalCleanup() { return S_OK; }
    // Window-message equivalents for derived classes
    virtual void OnActivateApp(bool active) { (void)active; }
    virtual void OnKeyDown(int dik) { (void)dik; }
    virtual void OnMidiNotifySuccessful() {}
    virtual void OnAutomation(const std::string& action, const std::string& arg) { (void)action; (void)arg; }

    bool m_bActive = false;
    bool m_bReady = false;
    bool m_bFrameMoving = true;
    bool m_bSingleStep = false;
    DWORD m_dwLastFrameTime = 0;  // when the latest tick came due (ms; lastTickNs_ to the ns)
    float m_fFPS = 0.f;
    const char* m_strWindowTitle = "D3D Application";
    bool m_bAppUseZBuffer = false;
    bool m_bShowStats = false;

private:
    HRESULT Render3DEnvironment(DWORD now, bool forceRender);
    // Ticks are counted from the frame's time in whole milliseconds (exactly 10 ms each), and the
    // latest one is kept in nanoseconds, always on a whole millisecond; the smooth motion alpha is
    // measured from it on the nanosecond clock, so it has no millisecond steps. HoldTickPhase
    // keeps the blend where it is while the game stands still (the options menu, P), so the
    // picture doesn't jump when it moves again.
    void SetLastTick(uint64_t ns) {
        lastTickNs_ = ns;
        m_dwLastFrameTime = (DWORD)(ns / 1000000);
    }
    void HoldTickPhase(uint64_t nowNs, uint64_t tickNow);
    uint64_t lastTickNs_ = 0;
    bool heldLastFrame_ = false;  // the last frame was drawn with the game standing still
    void UpdateStats();
    void ToggleFullscreen();
    void HandleAutomation(DWORD now);
    // A window event (resized, moved, maximized, restored, another monitor): once things have
    // been still for kWindowSettleMs, the window's state is read and saved (SaveWindowState).
    void WindowChanged(DWORD now) { windowChangedAt_ = now ? now : 1; }
    void SaveWindowState();
    void PlaceWindow();
    static constexpr DWORD kWindowSettleMs = 500;
    DWORD windowChangedAt_ = 0;  // 0 = nothing to save

    SDL_Window* window_ = nullptr;
    void* glContext_ = nullptr;
    BackBuffer backBuffer_;
    bool quit_ = false;
    DWORD statsFrames_ = 0;
    DWORD statsLastTime_ = 0;  // ms
    void JudgeVSync();  // each second's frame rate against the monitor's refresh rate
    bool adaptiveSupported_ = false;
    int vsyncInEffect_ = 1;
    // With VSync and a limit that is a part of the refresh rate, the driver itself shows a picture
    // every n-th refresh (the swap interval), exact where the limiter's own clock would drift
    // across a refresh now and then. swapEvery_ is the n in effect (0 with VSync off); SwapPaced
    // says the driver is pacing that limit, so the limiter stands aside.
    int swapEvery_ = 1;
    bool SwapPaced() const;
    void UpdateRefreshRate();
    void SwapFrame();
    double refreshHz_ = 0.0;
    int vsyncOverride_ = 0;
    int vsyncVote_ = 0, vsyncVotes_ = 0;  // the last second's verdict and how many in a row
    int vsyncLogged_ = -1;

    // scripted run for automated testing: "time_ms:action[=arg]" entries
    struct AutoStep { DWORD t; std::string action, arg; bool done; };
    std::vector<AutoStep> auto_;
    DWORD autoStart_ = 0;
    std::vector<int> autoHeldKeys_;
    void* autoPad_ = nullptr;  // SDL_Joystick* of the test-only virtual controller
    std::unique_ptr<OptionsMenu> menu_;

    // The Statistics Overlay (F9, [video] stats_overlay). The figures are gathered on every
    // frame drawn (a few clock reads, whether it shows or not) and summed up each second with the
    // frame rate (UpdateStats); the panel shows the last second's. Gathering and drawing only read
    // the frame loop: nothing here changes the ticks or when a frame starts.
    struct StatsWindow {  // the second being measured
        uint64_t lastStartNs = 0;
        int frames = 0;
        double frameSum = 0, frameMin = 0, frameMax = 0;  // ms between frame starts
        double cpuSum = 0, swapSum = 0, gpuSum = 0;      // ms
        int swaps = 0, gpuWaits = 0;
        int ticks = 0;
        int tickFrames[3] = {0, 0, 0};  // frames that ran 0, 1, 2 or more ticks
        double lateSum = 0, lateMax = 0;  // ms, Smooth motion off: a frame's start after its tick
        int lateFrames = 0;
        double drawSum = 0, glCallSum = 0, uploadSum = 0;  // the renderer's work, per frame
        int workFrames = 0;
        double tickSum = 0, renderSum = 0;  // ms spent in FrameMove and Render
        int timeFrames = 0;
    } statsWin_;
    struct StatsSecond {  // the last second, as shown
        bool valid = false;
        float fps = 0;
        double frameAvg = 0, frameMin = 0, frameMax = 0, cpu = 0, swap = 0, gpu = -1;
        int ticksPerSecond = 0;
        int tickPct[3] = {0, 0, 0};
        double lateAvg = -1, lateMax = -1;
        float alpha = 0;
        double gpu3D = -1;  // the graphics card's time for a level's 3D a frame (-1: none drawn)
        int draws3D = 0;    // the level's 3D draws in the last frame that had any
        int lights = 0;     // the dynamic lights in the last frame that had any (G5)
        double gpuAO = -1;  // the graphics card's time for ambient occlusion a frame (-1: none)
        double gpuShadows = -1;  // the same for the dynamic shadows' map
        double gpuBloom = -1;    // and for the bloom's blur (G6)
        double draws = 0, glCalls = 0, uploadKB = 0;  // averaged over the second
        double tick = 0, render = 0;                  // ms a frame, averaged
    } statsSec_;
    double frameTickMs_ = 0, frameRenderMs_ = 0;  // the frame just drawn, before UpdateStats
    uint64_t frameStartNs_ = 0;
    // The last frames drawn, for the overlay's graphs (O2): the time since the frame before, the
    // blend it was drawn at (1 with Smooth motion off) and the ticks it ran. Kept whether the
    // overlay shows or not, so the graphs are full the moment it is turned on.
    static constexpr int kStatsHistory = 512;
    struct StatsFrameRec {
        float ms = 0, alpha = 1;
        unsigned char ticks = 0;
    };
    StatsFrameRec statsHist_[kStatsHistory];
    int statsHistHead_ = 0, statsHistCount_ = 0;  // head: the next slot
    void StatsFrameAlpha(float alpha) {
        if (statsHistCount_) statsHist_[(statsHistHead_ + kStatsHistory - 1) % kStatsHistory].alpha = alpha;
    }
    std::unique_ptr<StatsOverlay> statsOverlay_;
    void StatsFrameStart(uint64_t nowNs, DWORD ticks, bool late);
    void SummarizeStats(DWORD elapsedMs);
    void DrawStatsOverlay();
    void CycleStatsOverlay();
};

// Timer used by the framework (milliseconds, like timeGetTime)
DWORD AppTimeMs();
// The same clock in nanoseconds (AppTimeMs() == AppTimeNs() / 1,000,000)
uint64_t AppTimeNs();
// Fraction (0..1) of the current 10 ms logic tick that has elapsed at render time.
float AppTickAlpha();
// The frames drawn a second, as last measured (the FPS counter's number; 0 before the first second)
float AppFrameRate();
// Inject a DIK key state for automation (pressed = true/false)
void AppInjectKey(int dik, bool pressed);
bool AppInjectedKey(int dik);
