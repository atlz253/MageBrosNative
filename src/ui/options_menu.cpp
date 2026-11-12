#include "options_menu.h"
#include "../audio/audio.h"
#include "../audio/midi.h"
#include "../core/assets.h"
#include "../core/config.h"
#include "../core/log.h"
#include "../core/mods.h"
#include "../framework/d3dapp.h"
#include "../framework/d3dtextr.h"
#include "../gfx/device.h"
#include "version.h"
#include "../input/input.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace {

const UIColor kWhite{1, 1, 1, 1};
const UIColor kGrey{0.55f, 0.55f, 0.62f, 1};
const UIColor kGold{1.0f, 0.82f, 0.25f, 1};
const UIColor kValue{0.62f, 0.9f, 1.0f, 1};
const UIColor kSelValue{1.0f, 0.95f, 0.55f, 1};
const UIColor kPanel{0.05f, 0.04f, 0.16f, 0.92f};
const UIColor kBorder{0.45f, 0.6f, 1.0f, 1};
const UIColor kSelBar{0.22f, 0.3f, 0.75f, 0.75f};
const UIColor kDim{0, 0, 0, 0.5f};

std::string OnOff(bool b) { return b ? "On" : "Off"; }

std::string Fit(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n - 2) + "..";
}

// Cycle through a list of values: returns the index after moving `d` from the closest match.
template <class T>
int CycleIndex(const std::vector<T>& list, const T& cur, int d) {
    int idx = 0;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i] == cur) idx = (int)i;
    int n = (int)list.size();
    return ((idx + d) % n + n) % n;
}

int NearestIndex(const std::vector<float>& list, float v) {
    int best = 0;
    for (size_t i = 0; i < list.size(); i++)
        if (std::fabs(list[i] - v) < std::fabs(list[best] - v)) best = (int)i;
    return best;
}

float StepFloat(const std::vector<float>& list, float cur, int d) {
    int i = NearestIndex(list, cur) + d;
    i = std::clamp(i, 0, (int)list.size() - 1);
    return list[(size_t)i];
}

std::string Pct(int pct) { return std::to_string(pct) + "%"; }

// The keys the game reads (XHeroA / XHeroB::_UpdateInput, the menus' CheckButtonPressed and
// friends, XApplication::MsgProc), in the manual's groups. Each row rebinds one original key:
// every place the game reads that key follows it (player 2's W A S D also move the menus' cursor,
// and in a one-player game player 1 also fires and jumps with player 2's Left Ctrl and Left Shift).
struct KeyAction { const char* group; const char* label; int dik; };
enum { kDik2 = 0x03, kDikLBracket = 0x1A, kDikRBracket = 0x1B };
const KeyAction kKeyActions[] = {
    {"Player 1", "Move up", DIK_UP},          {"Player 1", "Move down", DIK_DOWN},
    {"Player 1", "Move left", DIK_LEFT},      {"Player 1", "Move right", DIK_RIGHT},
    {"Player 1", "Fire", DIK_RCONTROL},       {"Player 1", "Fire (second key)", kDikLBracket},
    {"Player 1", "Jump", DIK_RSHIFT},         {"Player 1", "Jump (second key)", kDikRBracket},
    {"Player 2", "Move up", DIK_W},           {"Player 2", "Move down", DIK_S},
    {"Player 2", "Move left", DIK_A},         {"Player 2", "Move right", DIK_D},
    {"Player 2", "Fire", DIK_LCONTROL},       {"Player 2", "Fire (second key)", DIK_1},
    {"Player 2", "Jump", DIK_LSHIFT},         {"Player 2", "Jump (second key)", kDik2},
    {"General", "Select", DIK_RETURN},        {"General", "Pause", DIK_P},
    {"General", "Back / give up", DIK_ESCAPE},
};

std::string BaseName(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

}  // namespace

OptionsMenu::OptionsMenu(CD3DApplication* app) : app_(app) {}

// ------------------------------------------------------------------ open / close / save
void OptionsMenu::Open() {
    if (open_) return;
    open_ = true;
    mode_ = Mode::Normal;
    stack_.clear();
    stack_.push_back(MainPage());
    Input::SetBlocked(true);
    ResetStick();  // stick movements made while the menu was closed never reached it
    message_.clear();
    Sound(true);
}

void OptionsMenu::Close() {
    if (!open_) return;
    if (modsChanged_ && !ModsLocked()) {
        // mods or the level pack changed: reload everything and restart the title screen
        modsChanged_ = false;
        app_->ApplyModChanges();
    }
    if (mode_ == Mode::ConfirmDisplay) RevertDisplay();
    DiscardPending(false);
    open_ = false;
    mode_ = Mode::Normal;
    ResetStick();
    stack_.clear();
    Input::SetBlocked(false);
}

void OptionsMenu::NotYet(Item& it) {
    it.enabled = [] { return false; };
    it.focusWhenDisabled = true;
    it.helpFn = [] { return std::string("Not available yet: this comes in a later version of Mage Bros. Native."); };
}

void OptionsMenu::Save() {
    if (!GetConfig().Save()) Log("Options: could not save %s", GetConfig().path.c_str());
}

// Every setting back to its default, applied at once and saved. Kept: the path to MageBros.exe
// (needed to find the game data), the save folder, the ini location, and saved progress
// (stored separately).
void OptionsMenu::ResetAllSettings() {
    Config& c = GetConfig();
    const Config old = c;
    Config fresh;
    fresh.path = old.path;
    fresh.version = old.version;
    fresh.gamePath = old.gamePath;
    fresh.savePath = old.savePath;  // resetting must not move where progress is saved
    fresh.mods = old.mods;          // installed mods and the chosen level pack stay as they are
    fresh.modPack = old.modPack;
    app_->DefaultWindowSize(fresh.displayIndex, &fresh.windowWidth, &fresh.windowHeight);  // as a first start
    c = fresh;
    pendingActive_ = false;
    // apply everything that is not read live every frame
    if (old.fullscreen != c.fullscreen || old.windowWidth != c.windowWidth || old.windowHeight != c.windowHeight ||
        old.displayIndex != c.displayIndex || old.fullscreenWidth != c.fullscreenWidth ||
        old.fullscreenHeight != c.fullscreenHeight || old.maximized != c.maximized ||
        old.windowPosSet != c.windowPosSet)
        app_->ApplyDisplaySettings();
    app_->ApplyVSync();
    GetDevice().OnSettingsChanged();  // antialiasing, render scale, aspect
    Audio::SetMasterVolumes(c.masterVolume / 100.f, c.sfxVolume / 100.f, c.musicVolume / 100.f);
    if (old.soundFont != c.soundFont) Midi::SwitchSynth(c.soundFont);
    Midi::SetVolume(c.masterVolume / 100.f * c.musicVolume / 100.f);
    bool musicFilesChanged = false;
    for (int i = 0; i < 6; i++) musicFilesChanged |= old.musicFiles[i] != c.musicFiles[i];
    if (old.musicSource != c.musicSource || musicFilesChanged) app_->ReloadMusic();
    Input::ReloadBindings();
    Save();
    // rebuild the menu so every page shows the new values, staying on the main page
    int cursor = stack_.empty() ? 0 : stack_.front().cursor;
    stack_.clear();
    stack_.push_back(MainPage());
    stack_.back().cursor = cursor;
    SetMessage("All settings reset to defaults.");
    Log("Options: all settings reset to defaults");
}

void OptionsMenu::SetMessage(const std::string& m, DWORD ms) {
    message_ = m;
    messageUntil_ = now_ + ms;
}

// ------------------------------------------------------------------ display changes
void OptionsMenu::BeginDisplayChange() {
    const Config& c = GetConfig();
    if (mode_ != Mode::ConfirmDisplay)
        snapshot_ = {(int)c.fullscreen, c.windowWidth, c.windowHeight, c.fullscreenWidth, c.fullscreenHeight,
                     c.displayIndex, (int)c.lastFullscreen, c.fullscreenRefresh};
}

void OptionsMenu::ApplyDisplay() {
    app_->ApplyDisplaySettings();
    Save();
    mode_ = Mode::ConfirmDisplay;
    confirmDeadline_ = now_ + 10000;
    confirmChoice_ = 1;
}

void OptionsMenu::RevertDisplay() {
    Config& c = GetConfig();
    c.fullscreen = (FullscreenMode)snapshot_.fullscreen;
    c.windowWidth = snapshot_.w;
    c.windowHeight = snapshot_.h;
    c.fullscreenWidth = snapshot_.fw;
    c.fullscreenHeight = snapshot_.fh;
    CD3DApplication::SetDisplay(snapshot_.display);
    c.fullscreenRefresh = snapshot_.hz;
    app_->ApplyDisplaySettings();
    c.lastFullscreen = (FullscreenMode)snapshot_.lastFullscreen;  // (a reverted exclusive mode isn't F11's)
    Save();
    mode_ = Mode::Normal;
    SetMessage("Display settings reverted.");
}

static SDL_DisplayID DisplayIdFor(int index) {
    SDL_DisplayID d = SDL_GetPrimaryDisplay();
    if (index > 0) {
        int n = 0;
        SDL_DisplayID* ids = SDL_GetDisplays(&n);
        if (ids && index <= n) d = ids[index - 1];
        SDL_free(ids);
    }
    return d;
}

// "16:9", "4:3", "21:9" ... for a resolution
static std::string AspectName(int w, int h) {
    if (w <= 0 || h <= 0) return "";
    double r = (double)w / h;
    struct A { double r; const char* name; double tol; };
    static const A known[] = {{5.0 / 4, "5:4", 0.01},   {4.0 / 3, "4:3", 0.01},  {3.0 / 2, "3:2", 0.01},
                              {16.0 / 10, "16:10", 0.01}, {16.0 / 9, "16:9", 0.01}, {21.0 / 9, "21:9", 0.06},
                              {32.0 / 9, "32:9", 0.02}};
    for (const A& a : known)
        if (std::fabs(r - a.r) <= a.tol) return a.name;
    int x = w, y = h;
    while (y) { int t = x % y; x = y; y = t; }
    return std::to_string(w / x) + ":" + std::to_string(h / x);
}

// "1920x1440 4:3 3x": a whole multiple of the game's 640 x 480 says so (its 2D art then scales evenly)
static std::string SizeName(int w, int h) {
    std::string s = std::to_string(w) + "x" + std::to_string(h) + " " + AspectName(w, h);
    if (w % 640 == 0 && h % 480 == 0 && w / 640 == h / 480 && w > 640) s += " " + std::to_string(w / 640) + "x";
    return s;
}

static bool Is4x3(int w, int h) { return w * 3 == h * 4; }

std::vector<std::pair<int, int>> OptionsMenu::ResolutionList(FullscreenMode mode, int display) const {
    const Config& c = GetConfig();
    std::vector<std::pair<int, int>> out;
    SDL_DisplayID disp = DisplayIdFor(display);
    auto byArea = [](auto& a, auto& b) {
        return a.first * a.second < b.first * b.second || (a.first * a.second == b.first * b.second && a.first < b.first);
    };
    if (mode == FullscreenMode::Exclusive) {
        // the monitor's modes, one per size (the refresh rate has its own row), none smaller than
        // the game's 640 x 480 and none the same size as Desktop, which ends the list
        const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(disp);
        int n = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp, &n);
        for (int i = 0; i < n; i++) {
            std::pair<int, int> r{modes[i]->w, modes[i]->h};
            if (r.first < 640 || r.second < 480 || (desk && r.first == desk->w && r.second == desk->h)) continue;
            if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
        }
        SDL_free(modes);
        if (c.fullscreenWidth > 0 && std::find(out.begin(), out.end(), std::pair<int, int>{c.fullscreenWidth, c.fullscreenHeight}) == out.end())
            out.push_back({c.fullscreenWidth, c.fullscreenHeight});
        std::sort(out.begin(), out.end(), byArea);
        out.push_back({0, 0});  // "Desktop": the monitor's own resolution, the largest choice
    } else {
        // the window sizes that fit the monitor with the title bar and frame: the game's own 4:3
        // first (multiples of 640 x 480), then wider ones; each group smallest first, and the
        // current size (dragged to anything) in its group
        int fitW = 0, fitH = 0;
        app_->WindowFitArea(display, &fitW, &fitH);
        for (float s : CD3DApplication::kWindowScales) {
            std::pair<int, int> r{(int)(640 * s + 0.5f), (int)(480 * s + 0.5f)};
            if (r.first <= fitW && r.second <= fitH) out.push_back(r);
        }
        static const int wide[][2] = {{1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080},
                                      {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160}};
        for (auto& r : wide)
            if (r[0] <= fitW && r[1] <= fitH) out.push_back({r[0], r[1]});
        std::pair<int, int> cur{c.windowWidth, c.windowHeight};
        if (std::find(out.begin(), out.end(), cur) == out.end()) out.push_back(cur);
        std::sort(out.begin(), out.end(), [&](auto& a, auto& b) {
            bool a43 = Is4x3(a.first, a.second), b43 = Is4x3(b.first, b.second);
            return a43 != b43 ? a43 : byArea(a, b);
        });
    }
    return out;
}

std::vector<float> OptionsMenu::RefreshList(int fw, int fh, int display) const {
    SDL_DisplayID disp = DisplayIdFor(display);
    if (fw <= 0 || fh <= 0) {
        const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(disp);
        if (!desk) return {0.f};
        fw = desk->w, fh = desk->h;
    }
    std::vector<float> rates;
    int n = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp, &n);
    for (int i = 0; i < n; i++) {
        float r = modes[i]->refresh_rate;
        if (modes[i]->w != fw || modes[i]->h != fh || r <= 0.f) continue;
        bool seen = false;
        for (float o : rates) seen |= std::fabs(o - r) < 0.01f;
        if (!seen) rates.push_back(r);
    }
    SDL_free(modes);
    std::sort(rates.begin(), rates.end());
    rates.insert(rates.begin(), 0.f);  // "Desktop"
    return rates;
}

// "144 Hz", or "59.94 Hz" for a rate that isn't a whole number
static std::string RateName(float hz) {
    char b[32];
    if (std::fabs(hz - std::round(hz)) < 0.05f) snprintf(b, sizeof b, "%d Hz", (int)std::lround(hz));
    else snprintf(b, sizeof b, "%.2f Hz", hz);
    return b;
}

// ------------------------------------------------------------------ display settings
OptionsMenu::DisplayCfg OptionsMenu::CurrentDisplay() const {
    const Config& c = GetConfig();
    return {c.fullscreen, c.fullscreenWidth > 0 ? c.fullscreenWidth : 0, c.fullscreenWidth > 0 ? c.fullscreenHeight : 0,
            c.displayIndex, c.fullscreenRefresh};
}

OptionsMenu::DisplayCfg OptionsMenu::ShownDisplay() const { return pendingActive_ ? pending_ : CurrentDisplay(); }

void OptionsMenu::ProposeDisplay(const DisplayCfg& d) {
    if (d.mode != FullscreenMode::Exclusive) {
        // Window / Borderless can't leave the screen unusable: apply straight away, no confirmation
        Config& c = GetConfig();
        pendingActive_ = false;
        if (d.display != c.displayIndex) c.windowPosSet = false;  // centred on the new monitor
        c.fullscreen = d.mode;
        CD3DApplication::SetDisplay(d.display);
        app_->ApplyDisplaySettings();
        Save();
        return;
    }
    // ends in exclusive fullscreen: hold it until Enter
    pending_ = d;
    pendingActive_ = !(d == CurrentDisplay());
    if (pendingActive_) SetMessage("Press Enter to apply.", 4000);
}

void OptionsMenu::ApplyPending() {
    if (!pendingActive_) return;
    BeginDisplayChange();
    Config& c = GetConfig();
    c.fullscreen = pending_.mode;
    c.fullscreenWidth = pending_.fw;
    c.fullscreenHeight = pending_.fh;
    c.fullscreenRefresh = pending_.hz;
    if (pending_.display != c.displayIndex) c.windowPosSet = false;
    CD3DApplication::SetDisplay(pending_.display);
    pendingActive_ = false;
    ApplyDisplay();  // asks "Keep these display settings?"
}

void OptionsMenu::DiscardPending(bool tell) {
    if (!pendingActive_) return;
    pendingActive_ = false;
    if (tell) SetMessage("Display change not applied.");
}

std::string OptionsMenu::DisplayHelp(const std::string& base) const {
    if (pendingActive_) return "Not applied yet (*). Press Enter to switch; you'll be asked to keep it.";
    return base;
}

// ------------------------------------------------------------------ synth list
std::vector<std::string> OptionsMenu::SynthList() const {
    std::vector<std::string> out{""};  // automatic
#if defined(_WIN32) || defined(__APPLE__)
    out.push_back("system");
#endif
    // next to the program, next to MageBros.exe, then their "soundfonts" folders (Midi::SoundFontDirs);
    // files in the program's own folders are stored relative to it (portable), others in full
    for (const Midi::SoundFontDir& d : Midi::SoundFontDirs()) {
        int n = 0;
        char** files = SDL_GlobDirectory(d.dir.c_str(), "*.sf2", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; i < n; i++) {
            std::string v = d.name + files[i];
            if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
        }
        SDL_free(files);
    }
    const std::string& cur = GetConfig().soundFont;
    if (std::find(out.begin(), out.end(), cur) == out.end()) out.push_back(cur);
    return out;
}

std::string OptionsMenu::KeyNamesFor(int dik) const {
    const Config& c = GetConfig();
    std::vector<std::string> names;
    auto it = c.keyBinds.find(dik);
    if (it != c.keyBinds.end()) {
        names = it->second;
    } else {
        std::vector<int> scs;
        Input::DefaultScancodesForDik(dik, &scs);
        for (int sc : scs) names.push_back(SDL_GetScancodeName((SDL_Scancode)sc));
    }
    std::string s;
    for (auto& n : names) s += (s.empty() ? "" : ", ") + n;
    return s.empty() ? "(none)" : s;
}

std::string OptionsMenu::PadNamesFor(int button) const {
    const Config& c = GetConfig();
    auto it = c.padBinds.find(button);
    if (it == c.padBinds.end()) return button == 0 ? "A / bottom" : button == 1 ? "B / right" : "(default)";
    std::string s;
    for (auto& n : it->second) s += (s.empty() ? "" : ", ") + n;
    return s.empty() ? "(none)" : s;
}

// ------------------------------------------------------------------ pages
std::string OptionsMenu::LostLine() const {
    return app_->InLevel() ? "Progress in the current level will be lost." : "";
}

OptionsMenu::Page OptionsMenu::MainPage() {
    Page p;
    // On the title screen (where "Main menu" has nowhere to go) this is an options menu, not a pause.
    const bool onTitle = !app_->CanReturnToMainMenu();
    p.title = onTitle ? "OPTIONS" : "PAUSED";
    if (onTitle)
        p.items.push_back({"Back", "Close this menu (F10 / Start).", nullptr, nullptr, [this] { Close(); }, nullptr});
    else
        p.items.push_back({"Resume game", "Close this menu and continue (F10 / Start).", nullptr, nullptr,
                           [this] { Close(); }, nullptr});
    p.items.push_back({"Video", "Window, resolution, frame rate, widescreen and HUD.", nullptr, nullptr,
                       [this] { PushPage(VideoPage()); }, nullptr});
    p.items.push_back({"Graphics", "Antialiasing, sharpness and visual upgrades.", nullptr, nullptr,
                       [this] { PushPage(GraphicsPage()); }, nullptr});
    p.items.push_back({"Audio", "Volume levels and the music synthesizer.", nullptr, nullptr,
                       [this] { PushPage(AudioPage()); }, nullptr});
    p.items.push_back({"Controls", "Keyboard and gamepad bindings, stick dead zone.", nullptr, nullptr,
                       [this] { PushPage(ControlsPage()); }, nullptr});
    p.items.push_back({"Cheats", "Game modifiers, all off by default.", nullptr, nullptr,
                       [this] { PushPage(CheatsPage()); }, nullptr});
    p.items.push_back({"Mods", "", nullptr, nullptr, [this] { PushPage(ModsPage()); }, nullptr});
    NotYet(p.items.back());
    p.items.push_back({"Reset all settings",
                       "Restore every setting to its default. Saved progress, the game path and the save folder are kept.", nullptr,
                       nullptr,
                       [this] {
                           AskConfirm("Reset all settings?", "Video, graphics, audio and controls go back to defaults.",
                                      [this] { ResetAllSettings(); });
                       },
                       nullptr});
    if (app_->InLevel())
        p.items.push_back({"Level select", "Leave this level and return to level select, as you were when it started.",
                           nullptr, nullptr,
                           [this] {
                               AskConfirm("Return to level select?", LostLine(), [this] {
                                   Close();
                                   app_->ReturnToLevelSelect();
                               });
                           },
                           nullptr});
    if (!onTitle)
        p.items.push_back({"Main menu", "Leave the current screen and return to the title screen.", nullptr, nullptr,
                           [this] {
                               AskConfirm("Return to the main menu?", LostLine(), [this] {
                                   Close();
                                   app_->ReturnToMainMenu();
                               });
                           },
                           [this] { return app_->CanReturnToMainMenu(); }});
    p.items.push_back({"Quit game", "Close Mage Bros. Native. Settings are already saved.", nullptr, nullptr,
                       [this] {
                           AskConfirm("Quit the game?", LostLine(), [this] {
                               Close();
                               app_->RequestQuit();
                           });
                       },
                       nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::VideoPage() {
    Page p;
    p.title = "OPTIONS > VIDEO";
    auto& v = p.items;
    auto modeName = [](FullscreenMode m) {
        switch (m) {
            case FullscreenMode::Windowed: return std::string("Window");
            case FullscreenMode::Borderless: return std::string("Borderless");
            default: return std::string("Exclusive");
        }
    };
    {
        Item it{"Display mode", "",
                [this, modeName] { return modeName(ShownDisplay().mode) + PendingMark(ShownDisplay().mode != CurrentDisplay().mode); },
                [this](int d) {
                    DisplayCfg p = ShownDisplay();
                    p.mode = (FullscreenMode)((((int)p.mode + d) % 3 + 3) % 3);
                    ProposeDisplay(p);
                },
                [this] {
                    if (pendingActive_) { ApplyPending(); return; }
                    DisplayCfg p = ShownDisplay();
                    p.mode = (FullscreenMode)(((int)p.mode + 1) % 3);
                    ProposeDisplay(p);
                },
                nullptr};
        it.helpFn = [this] {
            return DisplayHelp("Window and Borderless switch at once. Exclusive: choose, then press Enter.");
        };
        v.push_back(it);
    }
    {
        Item it{"Resolution", "",
                [this] {
                    DisplayCfg p = ShownDisplay(), cur = CurrentDisplay();
                    const Config& c = GetConfig();
                    if (p.mode == FullscreenMode::Borderless) return std::string("Desktop");
                    if (p.mode == FullscreenMode::Exclusive)
                        return (p.fw <= 0 ? std::string("Desktop") : SizeName(p.fw, p.fh)) +
                               PendingMark(p.fw != cur.fw || p.fh != cur.fh);
                    if (c.maximized) return std::string("Maximized");
                    return SizeName(c.windowWidth, c.windowHeight);
                },
                [this](int d) {
                    DisplayCfg p = ShownDisplay();
                    Config& c = GetConfig();
                    auto list = ResolutionList(p.mode, p.display);
                    if (p.mode == FullscreenMode::Exclusive) {
                        // stops at both ends (Desktop is the last), never wrapping round to the smallest
                        int i = std::clamp(CycleIndex(list, std::pair<int, int>{p.fw, p.fh}, 0) + d, 0,
                                           (int)list.size() - 1);
                        p.fw = list[(size_t)i].first;
                        p.fh = list[(size_t)i].second;
                        // a refresh rate this size doesn't have goes back to the desktop's
                        auto rates = RefreshList(p.fw, p.fh, p.display);
                        bool have = false;
                        for (float r : rates) have |= std::fabs(r - p.hz) < 0.01f;
                        if (!have) p.hz = 0.f;
                        ProposeDisplay(p);
                    } else {  // window size: resize right away (a maximized window first goes back to its own size)
                        int i = CycleIndex(list, std::pair<int, int>{c.windowWidth, c.windowHeight}, 0);
                        if (!c.maximized) i = std::clamp(i + d, 0, (int)list.size() - 1);
                        c.maximized = false;
                        c.windowWidth = list[(size_t)i].first;
                        c.windowHeight = list[(size_t)i].second;
                        app_->ApplyDisplaySettings();
                        Save();
                    }
                },
                [this] { ApplyPending(); },
                [this] { return ShownDisplay().mode != FullscreenMode::Borderless; }};
        it.helpFn = [this] {
            DisplayCfg p = ShownDisplay();
            const Config& c = GetConfig();
            const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(DisplayIdFor(p.display));
            std::string deskNote;
            if (desk) {
                int w = p.mode == FullscreenMode::Exclusive ? p.fw : c.windowWidth;
                int h = p.mode == FullscreenMode::Exclusive ? p.fh : c.windowHeight;
                if (p.mode == FullscreenMode::Exclusive && w <= 0)
                    deskNote = "Desktop = your monitor's own resolution, " + std::to_string(desk->w) + "x" +
                               std::to_string(desk->h) + ".";
                else if (w == desk->w && h == desk->h)
                    deskNote = "This is your desktop resolution.";
            }
            std::string base = p.mode == FullscreenMode::Exclusive
                                   ? "Screen mode for exclusive fullscreen; press Enter to apply. " + deskNote
                                   : "Window size in screen pixels, at once. 2x, 3x...: the game's 640x480 scaled evenly.";
            return DisplayHelp(base);
        };
        v.push_back(it);
    }
    {
        // exclusive fullscreen only: Window and Borderless run at the desktop's rate
        Item it{"Refresh rate", "",
                [this] {
                    DisplayCfg p = ShownDisplay();
                    std::string mark = PendingMark(p.hz != CurrentDisplay().hz);
                    if (p.mode == FullscreenMode::Exclusive && p.hz > 0.f) return RateName(p.hz) + mark;
                    const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(DisplayIdFor(p.display));
                    return (desk && desk->refresh_rate > 0.f ? "Desktop " + RateName(desk->refresh_rate) : std::string("Desktop")) +
                           mark;
                },
                [this](int d) {
                    DisplayCfg p = ShownDisplay();
                    auto rates = RefreshList(p.fw, p.fh, p.display);
                    int i = 0;
                    for (size_t k = 0; k < rates.size(); k++)
                        if (std::fabs(rates[k] - p.hz) < 0.01f) i = (int)k;
                    p.hz = rates[(size_t)std::clamp(i + d, 0, (int)rates.size() - 1)];
                    ProposeDisplay(p);
                },
                [this] { ApplyPending(); }, [this] { return ShownDisplay().mode == FullscreenMode::Exclusive; }};
        it.focusWhenDisabled = true;
        it.helpFn = [this] {
            if (ShownDisplay().mode != FullscreenMode::Exclusive)
                return std::string("Exclusive fullscreen only: Window and Borderless use the desktop's refresh rate.");
            return DisplayHelp("How often the screen refreshes in exclusive fullscreen; press Enter to apply.");
        };
        v.push_back(it);
    }
    {
        Item it{"Monitor", "",
                // "Primary" or "2: 2560x1440" (names are long; the help line has the whole of it)
                [this] {
                    int idx = ShownDisplay().display;
                    std::string s = "Primary";
                    if (idx > 0) {
                        s = std::to_string(idx);
                        if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(DisplayIdFor(idx)))
                            s += ": " + std::to_string(m->w) + "x" + std::to_string(m->h);
                    }
                    return s + PendingMark(idx != CurrentDisplay().display);
                },
                [this](int d) {
                    int n = 0;
                    SDL_DisplayID* ids = SDL_GetDisplays(&n);
                    SDL_free(ids);
                    if (n < 1) n = 1;
                    DisplayCfg p = ShownDisplay();
                    p.display = ((p.display + d) % (n + 1) + (n + 1)) % (n + 1);
                    ProposeDisplay(p);
                },
                [this] { ApplyPending(); }, nullptr};
        it.helpFn = [this] {
            // "Monitor 2 of 3: DELL U2723QE, 2560x1440, 144 Hz." (the primary: Windows' main monitor)
            int idx = ShownDisplay().display, n = CD3DApplication::DisplayCount();
            SDL_DisplayID id = DisplayIdFor(idx);
            std::string s = idx == 0 ? "Windows' main monitor: " : "Monitor " + std::to_string(idx) + " of " + std::to_string(n) + ": ";
            const char* name = SDL_GetDisplayName(id);
            s += name && *name ? name : "(no name)";
            if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(id))
                s += ", " + std::to_string(m->w) + "x" + std::to_string(m->h) + ", " + RateName(m->refresh_rate);
            return DisplayHelp(s + ".");
        };
        v.push_back(it);
    }
    {
        // Off / On / Adaptive (Adaptive only where the driver can do it)
        Item it{"VSync", "",
                [] {
                    int m = GetConfig().vsync;
                    return std::string(m == 2 ? "Adaptive" : m == 1 ? "On" : "Off");
                },
                [this](int d) {
                    std::vector<int> l{0, 1};
                    if (app_->AdaptiveVSyncSupported() || GetConfig().vsync == 2) l.push_back(2);
                    GetConfig().vsync = l[(size_t)CycleIndex(l, GetConfig().vsync, d)];
                    app_->ApplyVSync();
                    Save();
                },
                nullptr, nullptr};
        it.helpFn = [this] {
            int want = GetConfig().vsync, got = app_->VSyncInEffect(), over = app_->VSyncOverride();
            if (over > 0) return std::string("VSync is on here, but your graphics driver's own setting seems to keep it off.");
            if (over < 0) return std::string("VSync is off here, but your graphics driver's own setting seems to force it on.");
            if (want == 2 && got != 2) return std::string("Your graphics driver can't do adaptive VSync, so it is on (as On).");
            if (want == 2) return std::string("As On, but a late picture shows at once: a little tearing instead of a stutter.");
            if (want == 1) return std::string("Waits for each screen refresh: no tearing. Off: shows pictures at once (tearing).");
            return std::string("Off: pictures show as soon as drawn (tearing). With Smooth motion, set a Frame limit.");
        };
        v.push_back(it);
    }
    v.push_back({"Smooth motion",
                 "On: drawn between game ticks, smooth at any refresh rate; up to 10 ms behind. Off: as the original.",
                 [] { return OnOff(GetConfig().interpolation); },
                 [this](int) {
                     GetConfig().interpolation = !GetConfig().interpolation;
                     app_->ApplyVSync();  // (a part-of-refresh Frame limit only counts with it on)
                     Save();
                 },
                 nullptr, nullptr});
    {
        // Unlimited; the monitor's refresh rate and its half, third and quarter (down to 30: every
        // picture then stays the same number of refreshes); then the common numbers
        Item it{"Frame limit", "",
                [this] {
                    int f = GetConfig().maxFps;
                    if (f == 0) return std::string("Unlimited");
                    if (f > 0) return std::to_string(f);
                    std::string s = f == -1 ? "Refresh rate" : "1/" + std::to_string(-f) + " refresh rate";
                    double hz = app_->RefreshRate();
                    if (hz > 0.0) s += " (" + std::to_string((int)(hz / -f + 0.5)) + ")";
                    return s;
                },
                [this](int d) {
                    std::vector<int> l{0};
                    double hz = app_->RefreshRate();
                    for (int n = 1; n <= 4; n++)
                        if (hz <= 0.0 || hz / n >= 29.5 || GetConfig().maxFps == -n) l.push_back(-n);
                    for (int f : {30, 50, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240, 360}) l.push_back(f);
                    GetConfig().maxFps = l[(size_t)CycleIndex(l, GetConfig().maxFps, d)];
                    app_->ApplyVSync();
                    Save();
                },
                nullptr, [] { return GetConfig().interpolation; }};
        it.helpFn = [] {
            if (GetConfig().maxFps < 0)
                return std::string("A part of the monitor's refresh rate: every picture shows equally long. Smooth motion only.");
            return std::string("Most frames a second, with Smooth motion only (off: 100 at most). VSync also caps it.");
        };
        v.push_back(it);
    }
    v.push_back({"Low latency", "With VSync: no finished pictures wait in the graphics driver, so keys answer sooner.",
                 [] { return OnOff(GetConfig().lowLatency); },
                 [this](int) {
                     GetConfig().lowLatency = !GetConfig().lowLatency;
                     Save();
                 },
                 nullptr, [] { return GetConfig().vsync != 0; }});
    v.push_back({"Aspect", "4:3 as original, stretched, or widescreen (wider 3D view).",
                 [] {
                     switch (GetConfig().aspect) {
                         case AspectMode::Original4x3: return std::string("4:3");
                         case AspectMode::Stretch: return std::string("Stretch");
                         default: return std::string("Widescreen");
                     }
                 },
                 [this](int d) {
                     Config& c = GetConfig();
                     c.aspect = (AspectMode)((((int)c.aspect + d) % 3 + 3) % 3);
                     GetDevice().OnSettingsChanged();
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"HUD position", "Widescreen only: the HUD and FPS counter in the original's 4:3 area or at the screen edges.",
                 [] { return std::string(GetConfig().hudLayout ? "Edges" : "Centered"); },
                 [this](int) {
                     GetConfig().hudLayout = GetConfig().hudLayout ? 0 : 1;
                     Save();
                 },
                 nullptr, [] { return GetConfig().aspect == AspectMode::Widescreen; }});
    v.push_back({"HUD max width", "How far the edge HUD may spread on ultrawide screens (21:9 keeps it off a 32:9 screen's far corners).",
                 [] {
                     float a = GetConfig().hudMaxAspect;
                     if (a <= 0) return std::string("Unlimited");
                     if (std::fabs(a - 1.78f) < 0.05f) return std::string("16:9");
                     if (std::fabs(a - 1.6f) < 0.05f) return std::string("16:10");
                     if (std::fabs(a - 2.4f) < 0.05f) return std::string("21:9");
                     if (std::fabs(a - 3.56f) < 0.05f) return std::string("32:9");
                     char b[16];
                     snprintf(b, sizeof b, "%.2f", a);
                     return std::string(b);
                 },
                 [this](int d) {
                     static const std::vector<float> l{1.6f, 1.78f, 2.4f, 3.56f, 0.f};
                     int i = NearestIndex(l, GetConfig().hudMaxAspect <= 0 ? 0.f : GetConfig().hudMaxAspect);
                     int n = (int)l.size();
                     GetConfig().hudMaxAspect = l[(size_t)(((i + d) % n + n) % n)];
                     Save();
                 },
                 nullptr,
                 [] { return GetConfig().aspect == AspectMode::Widescreen && GetConfig().hudLayout == 1; }});
    v.push_back({"HUD size", "Size of the in-game HUD and FPS counter, from their corners. Limited when parts would meet. 100% = original.",
                 [] { return std::to_string((int)std::lround(GetConfig().hudScale * 100)) + "%"; },
                 [this](int d) {
                     Config& c = GetConfig();
                     c.hudScale = std::clamp(std::round((c.hudScale + d * 0.1f) * 10.f) / 10.f, 0.5f, 2.0f);
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Field of view", "Experimental: best left at 100% (the original, 45 degrees). Zooms levels; above 100% shows unbuilt edges.",
                 [] {
                     // the percentage, and the vertical angle it gives (the original's 45 degrees at 100%)
                     float f = GetConfig().fovScale;
                     int deg = (int)std::lround(2.0 * std::atan(f * std::tan(0.392699)) * 57.29578);
                     return std::to_string((int)std::lround(f * 100)) + "% (" + std::to_string(deg) + " deg)";
                 },
                 [this](int d) {
                     Config& c = GetConfig();
                     c.fovScale = std::clamp(std::round((c.fovScale + d * 0.05f) * 20.f) / 20.f, 0.5f, 2.0f);
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Show FPS", "The original's frames-per-second counter, at the bottom right of every screen.",
                 [] { return OnOff(GetConfig().showFps); },
                 [this](int) {
                     GetConfig().showFps = !GetConfig().showFps;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Statistics Overlay",
                 "F9: frame rate, timing, display and graphics card figures, see-through. Full adds the details.",
                 [] {
                     static const char* names[] = {"Off", "Compact", "Full"};
                     return std::string(names[std::clamp(GetConfig().statsOverlay, 0, 2)]);
                 },
                 [this](int d) {
                     Config& c = GetConfig();
                     c.statsOverlay = ((c.statsOverlay + d) % 3 + 3) % 3;
                     Save();
                 },
                 nullptr, nullptr});
    // Classic, then the player's own fonts (.ttf / .otf files in a "fonts" folder), by file name
    v.push_back({"Menu font",
                 "Classic, or a font of your own: put .ttf or .otf files in a \"fonts\" folder next to MageBros.exe.",
                 [] {
                     const std::string& f = GetConfig().uiFont;
                     if (f.empty()) return std::string("Classic");
                     std::string name = f.substr(0, f.find_last_of('.'));
                     // (shown without a "-Regular" ending, to leave the row room)
                     for (const char* tail : {"-Regular", "_Regular", " Regular"})
                         if (name.size() > strlen(tail) && name.compare(name.size() - strlen(tail), std::string::npos, tail) == 0)
                             name.erase(name.size() - strlen(tail));
                     return UIDraw::FontUsable(f) ? name : name + " (not found)";
                 },
                 [this](int d) {
                     std::vector<std::string> fonts = UIDraw::AvailableFonts();
                     fonts.insert(fonts.begin(), std::string());  // Classic first
                     int n = (int)fonts.size();
                     int i = (int)(std::find(fonts.begin(), fonts.end(), GetConfig().uiFont) - fonts.begin());
                     if (i >= n) i = 0;  // (a font no longer in the folder: start again from Classic)
                     GetConfig().uiFont = fonts[(size_t)(((i + d) % n + n) % n)];
                     Save();
                 },
                 nullptr, nullptr});

    v.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::GraphicsPage() {
    Page p;
    p.title = "OPTIONS > GRAPHICS";
    auto& v = p.items;
    v.push_back({"Graphics profile", "Manual, performance (the R36S: no antialiasing or anisotropy) or quality (the desktop defaults).",
                 [] {
                     int k = GetConfig().graphicsProfile;
                     return k == 1 ? std::string("Performance") : k == 2 ? std::string("Quality") : std::string("Manual");
                 },
                 [this](int d) {
                     static const std::vector<int> l{0, 1, 2};
                     GetConfig().graphicsProfile = l[(size_t)CycleIndex(l, GetConfig().graphicsProfile, d)];
                     GetConfig().ApplyGraphicsProfile();
                     GetDevice().OnSettingsChanged();
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Antialiasing", "Smooths polygon edges (MSAA). Lowered automatically if unsupported.",
                 [] {
                     int m = GetConfig().msaa;
                     return m < 2 ? std::string("Off") : std::to_string(m) + "x";
                 },
                 [this](int d) {
                     static const std::vector<int> l{0, 2, 4, 8, 16};
                     GetConfig().msaa = l[(size_t)CycleIndex(l, GetConfig().msaa, d)];
                     GetConfig().graphicsProfile = 0;
                     GetDevice().OnSettingsChanged();
                     Save();
                 },
                 nullptr, nullptr});
    // (up to 200%, the owner's cap from 0.4.38: 300% and 400% cost far more for little to see)
    v.push_back({"Render scale", "Renders the 3D scene larger (smoother, slower) or smaller (faster) than the screen. Up to 200%.",
                 [] { return std::to_string((int)std::lround(GetConfig().renderScale * 100)) + "%"; },
                 [this](int d) {
                     static const std::vector<float> l{0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
                     GetConfig().renderScale = StepFloat(l, GetConfig().renderScale, d);
                     GetConfig().graphicsProfile = 0;
                     GetDevice().OnSettingsChanged();
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Texture filtering", "Anisotropic: far and angled floors and walls stay sharp, no flicker. Up close: the original pixels.",
                 [] {
                     int a = GetConfig().anisotropy;
                     return a <= 1 ? std::string("Off") : std::to_string(a) + "x";
                 },
                 [this](int d) {
                     static const std::vector<int> l{1, 2, 4, 8, 16};
                     GetConfig().anisotropy = l[(size_t)CycleIndex(l, GetConfig().anisotropy, d)];
                     GetConfig().graphicsProfile = 0;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Smooth textures", "Bilinear filtering on textures the original drew pixelated.",
                 [] { return OnOff(GetConfig().smoothTextures); },
                 [this](int) {
                     GetConfig().smoothTextures = !GetConfig().smoothTextures;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Smooth 2D images", "Smooth scaling of menus, logo and HUD (off = sharp pixels).",
                 [] { return OnOff(GetConfig().smoothSprites); },
                 [this](int) {
                     GetConfig().smoothSprites = !GetConfig().smoothSprites;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Ambient occlusion", "Soft shading in corners and under objects, in levels.",
                 [] { return OnOff(GetConfig().ambientOcclusion); },
                 [this](int) {
                     GetConfig().ambientOcclusion = !GetConfig().ambientOcclusion;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Dynamic shadows", "Real shadows from the game's light, in levels. Higher is sharper and costs more.",
                 [] {
                     static const char* names[] = {"Off", "Low", "Medium", "High"};
                     return std::string(names[std::clamp(GetConfig().dynamicShadows, 0, 3)]);
                 },
                 [this](int d) {
                     static const std::vector<int> l{0, 1, 2, 3};
                     GetConfig().dynamicShadows = l[(size_t)CycleIndex(l, GetConfig().dynamicShadows, d)];
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Shadow strength", "How dark the dynamic shadows are.",
                 [] { return Pct(GetConfig().shadowStrength); },
                 [this](int d) {
                     GetConfig().shadowStrength = std::clamp(GetConfig().shadowStrength + d * 10, 50, 100);
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Dynamic lights", "Lit torches, candles, shots, flames and explosions light their surroundings, in levels.",
                 [] { return OnOff(GetConfig().dynamicLights); },
                 [this](int) {
                     GetConfig().dynamicLights = !GetConfig().dynamicLights;
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Light strength", "How bright the dynamic lights are.",
                 [] { return Pct(GetConfig().lightStrength); },
                 [this](int d) {
                     GetConfig().lightStrength = std::clamp(GetConfig().lightStrength + d * 10, 50, 150);
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Bloom", "A soft glow round flames, shots and explosions, in levels.",
                 [] {
                     static const char* names[] = {"Off", "Low", "High"};
                     return std::string(names[std::clamp(GetConfig().bloom, 0, 2)]);
                 },
                 [this](int d) {
                     static const std::vector<int> l{0, 1, 2};
                     GetConfig().bloom = l[(size_t)CycleIndex(l, GetConfig().bloom, d)];
                     GetDevice().OnSettingsChanged();  // (the scene target gains or loses its glow layer)
                     Save();
                 },
                 nullptr, nullptr});
    v.push_back({"Ambient light", "With Dynamic lights on: lower dims the level so the lights stand out. 100% = original.",
                 [] { return Pct(GetConfig().lightsAmbient); },
                 [this](int d) {
                     static const std::vector<int> l{70, 85, 100};
                     GetConfig().lightsAmbient = l[(size_t)CycleIndex(l, GetConfig().lightsAmbient, d)];
                     Save();
                 },
                 nullptr, nullptr});
    {
        Item it{"Brightness", "Adjusts how bright the picture looks. 100% = original.",
                [] { return Pct(GetConfig().brightness); },
                [this](int d) {
                    GetConfig().brightness = std::clamp(GetConfig().brightness + d * 5, 50, 150);
                    Save();
                },
                [] {}, nullptr};
        it.bar = [] { return GetConfig().brightness - 50; };  // 50..150 % shown as an empty..full bar
        v.push_back(it);
    }
    v.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::AudioPage() {
    Page p;
    p.title = "OPTIONS > AUDIO";
    auto& v = p.items;
    auto applyVolumes = [] {
        const Config& c = GetConfig();
        Audio::SetMasterVolumes(c.masterVolume / 100.f, c.sfxVolume / 100.f, c.musicVolume / 100.f);
        Midi::SetVolume(c.masterVolume / 100.f * c.musicVolume / 100.f);
    };
    struct Vol { const char* label; const char* help; int Config::*field; int (*bar)(); };
    static const Vol vols[] = {
        {"Master volume", "Overall volume of sound effects and music.", &Config::masterVolume,
         [] { return GetConfig().masterVolume; }},
        {"Sound effects", "Volume of sound effects.", &Config::sfxVolume, [] { return GetConfig().sfxVolume; }},
        {"Music", "Volume of the music.", &Config::musicVolume, [] { return GetConfig().musicVolume; }}};
    for (const Vol& vol : vols) {
        int Config::*f = vol.field;
        Item it{vol.label, vol.help, [f] { return Pct(GetConfig().*f); },
                [this, f, applyVolumes](int d) {
                    GetConfig().*f = std::clamp(GetConfig().*f + d * 5, 0, 100);
                    applyVolumes();
                    Save();
                },
                [] {}, nullptr};
        it.bar = vol.bar;
        v.push_back(it);
    }
    v.push_back({"Music source",
                 "Auto: MP3/FLAC from the music folder replace the matching MIDI track. MIDI: original music only.",
                 [] { return std::string(GetConfig().musicSource == 1 ? "MIDI only" : "Auto"); },
                 [this](int) {
                     Config& c = GetConfig();
                     c.musicSource = c.musicSource == 1 ? 0 : 1;
                     app_->ReloadMusic();
                     Save();
                     const char* src = app_->MusicSource();
                     SetMessage(*src ? std::string("Now playing: ") + src : std::string("Music source changed."));
                 },
                 nullptr, nullptr});
    v.push_back({"Music synth",
                 "Automatic uses soundfont.sf2/magebros.sf2 if present, else the system synthesizer. Or pick a .sf2 file.",
                 [] {
                     const std::string& s = GetConfig().soundFont;
                     if (s.empty()) return std::string("Automatic");
                     if (s == "system") return std::string("System synth");
                     return Fit(BaseName(s), 13);
                 },
                 [this, applyVolumes](int d) {
                     auto list = SynthList();
                     Config& c = GetConfig();
                     c.soundFont = list[(size_t)CycleIndex(list, c.soundFont, d)];
                     Midi::SwitchSynth(c.soundFont);
                     applyVolumes();
                     Save();
                     SetMessage(std::string("Music now plays through: ") + Midi::BackendName());
                 },
                 nullptr, nullptr});
    v.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::ControlsPage() {
    Page p;
    p.title = "OPTIONS > CONTROLS";
    auto& v = p.items;
    v.push_back({"Keyboard", "Change each player's keys to move, fire and jump, and the pause and back keys.",
                 nullptr, nullptr, [this] { PushPage(KeyGroupsPage()); }, nullptr});
    v.push_back({"Gamepad buttons", "Change the fire and jump buttons. Movement is the stick and d-pad.",
                 nullptr, nullptr, [this] { PushPage(PadPage()); }, nullptr});
    v.push_back({"Stick dead zone", "How far the analog stick must move before it counts.",
                 [] { return std::to_string(GetConfig().gamepadDeadzone) + "%"; },
                 [this](int d) {
                     GetConfig().gamepadDeadzone = std::clamp(GetConfig().gamepadDeadzone + d * 5, 0, 90);
                     Save();
                 },
                 [] {}, nullptr});
    v.push_back({"Reset controls", "Restore the original keys and buttons (as in the manual) and the default dead zone.", nullptr,
                 nullptr,
                 [this] {
                     Config& c = GetConfig();
                     c.keyBinds.clear();
                     c.padBinds.clear();
                     c.gamepadDeadzone = 25;
                     Input::ReloadBindings();
                     Save();
                     SetMessage("Controls reset to defaults.");
                 },
                 nullptr});
    v.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

// ------------------------------------------------------------------ mods
bool OptionsMenu::ModsLocked() const { return app_->InLevel(); }

namespace {
std::string Contents(const Mods::ModInfo& m) {
    std::string s;
    auto add = [&](bool b, const char* w) {
        if (b) s += (s.empty() ? "" : ", ") + std::string(w);
    };
    add(m.hasLevels && !m.isPack, "levels");
    add(m.hasTextures, "textures");
    add(m.hasModels, "models");
    add(m.hasSounds, "sounds");
    add(m.hasMusic, "music");
    return s.empty() ? "nothing yet" : s;
}
const Mods::ModInfo* FindMod(const std::string& id) {
    for (const auto& m : Mods::Installed())
        if (m.id == id) return &m;
    return nullptr;
}
std::string LevelCount(size_t n) { return std::to_string(n) + (n == 1 ? " level" : " levels"); }
const char* kLockedHelp = "Change mods from OPTIONS on the title screen.";
}  // namespace

// OPTIONS > MODS: the level pack in play, the add-ons, and the mods folder.
OptionsMenu::Page OptionsMenu::ModsPage() {
    Page p;
    p.title = "OPTIONS > MODS";
    auto unlocked = [this] { return !ModsLocked(); };
    {
        Item it{"Level pack", "", [] {
                    const Mods::Pack* pk = Mods::FindPack(Mods::ActivePackId());
                    return pk && pk->id != "original" ? pk->name : std::string("Original game");
                },
                nullptr, [this] { PushPage(LevelPackPage()); }, unlocked};
        it.focusWhenDisabled = true;
        it.submenu = true;
        it.helpFn = [this] {
            if (ModsLocked()) return std::string(kLockedHelp);
            size_t n = Mods::LevelPackMods().size();
            return "The campaign you play: the original game or one of " + std::to_string(n) + " installed level pack" +
                   (n == 1 ? "" : "s") + ".";
        };
        p.items.push_back(it);
    }
    {
        Item it{"Add-ons", "", [] {
                    auto a = Mods::Addons();
                    if (a.empty()) return std::string("None installed");
                    int on = 0;
                    for (auto* m : a)
                        if (m->enabled && m->problem.empty()) on++;
                    return std::to_string(on) + " of " + std::to_string(a.size()) + " on";
                },
                nullptr, [this] { PushPage(AddonsPage()); }, unlocked};
        it.focusWhenDisabled = true;
        it.submenu = true;
        it.helpFn = [this] {
            return ModsLocked() ? std::string(kLockedHelp)
                                : std::string("New textures, models, sounds and music for whatever you play.");
        };
        p.items.push_back(it);
    }
    p.items.push_back({"Open the mods folder",
                       "Level packs go in its 'levelpacks' folder, add-ons in 'addons'. Folders or .zip files.", nullptr,
                       nullptr,
                       [] {
                           Mods::CreateFolders();
                           std::string url = "file:///" + Mods::ModsFolder();
                           for (char& c : url)
                               if (c == '\\') c = '/';
                           SDL_OpenURL(url.c_str());
                       },
                       nullptr});
    p.items.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

// MODS > LEVEL PACK: pick the campaign. Choosing one asks, then reloads at the title screen.
OptionsMenu::Page OptionsMenu::LevelPackPage() {
    Page p;
    p.title = "MODS > LEVEL PACK";
    auto unlocked = [this] { return !ModsLocked(); };
    auto choose = [this](const std::string& id, const std::string& name) {
        return [this, id, name] {
            if (id == Mods::ActivePackId()) {
                SetMessage("You're already playing " + name + ".");
                return;
            }
            AskConfirm("Play " + name + "?", "The game reloads and returns to the title screen.", [this, id] {
                Mods::SetActivePack(id);
                modsChanged_ = true;
                Save();
                Close();  // applies it
            });
        };
    };
    {
        const Mods::Pack* orig = Mods::FindPack("original");
        size_t n = orig ? orig->levels.size() : 40;
        Item it{"Original game", "", [n] { return Mods::ActivePackId() == "original" ? std::string("Playing") : LevelCount(n); },
                nullptr, choose("original", "the original game"), unlocked};
        it.focusWhenDisabled = true;
        it.helpFn = [this, n] {
            if (ModsLocked()) return std::string(kLockedHelp);
            return "Mage Bros. by Saito Games. " + LevelCount(n) + " in 4 worlds." +
                   (Mods::ActivePackId() == "original" ? " You're playing it." : " Enter: play it.");
        };
        p.items.push_back(it);
    }
    auto packs = Mods::LevelPackMods();
    for (const Mods::ModInfo* m : packs) {
        std::string id = m->id, name = m->name;
        Item it{name, "", [id] {
                    const Mods::ModInfo* mi = FindMod(id);
                    if (!mi || !mi->problem.empty()) return std::string("Can't use");
                    if (id == Mods::ActivePackId()) return std::string("Playing");
                    const Mods::Pack* pk = Mods::FindPack(id);
                    return pk ? LevelCount(pk->levels.size()) : std::string();
                },
                nullptr, nullptr, nullptr};
        if (m->problem.empty()) {
            it.activate = choose(id, name);
            it.enabled = unlocked;
        } else {
            it.enabled = [] { return false; };
        }
        it.focusWhenDisabled = true;
        it.helpFn = [this, id] {
            if (ModsLocked()) return std::string(kLockedHelp);
            const Mods::ModInfo* mi = FindMod(id);
            if (!mi) return std::string();
            if (!mi->problem.empty()) return "Can't be used: " + mi->problem + ".";
            const Mods::Pack* pk = Mods::FindPack(id);
            std::string s = mi->author.empty() ? "" : "By " + mi->author + ". ";
            if (pk) {
                size_t worlds = 0;
                std::vector<bool> used(pk->worlds.size(), false);
                for (int w : pk->levelWorld) used[(size_t)w] = true;
                for (bool u : used) worlds += u;
                s += LevelCount(pk->levels.size()) + " in " + std::to_string(worlds) + (worlds == 1 ? " world." : " worlds.");
            }
            if (!mi->description.empty()) s += " " + mi->description;
            s += id == Mods::ActivePackId() ? " You're playing it." : " Enter: play it.";
            return s;
        };
        p.items.push_back(it);
    }
    if (packs.empty()) {
        Item none{"No level packs installed", "Put level packs in the 'levelpacks' folder inside the 'mods' folder.",
                  nullptr, nullptr, nullptr, [] { return false; }};
        none.focusWhenDisabled = true;
        p.items.push_back(none);
    }
    p.items.push_back({"Back", "Each level pack keeps its own progress.", nullptr, nullptr, [this] { Back(false); }, nullptr});
    // start on the pack in play
    for (size_t i = 0; i < p.items.size(); i++)
        if ((i == 0 && Mods::ActivePackId() == "original") ||
            (i > 0 && i <= packs.size() && packs[i - 1]->id == Mods::ActivePackId()))
            p.cursor = (int)i;
    return p;
}

// MODS > ADD-ONS: the add-ons in load order, the top one winning. Space / X ticks one on or off,
// Shift+Up/Down (PgUp/PgDn, LB/RB) moves it, Enter opens its details.
OptionsMenu::Page OptionsMenu::AddonsPage() {
    Page p;
    p.title = "MODS > ADD-ONS";
    auto unlocked = [this] { return !ModsLocked(); };
    auto addons = Mods::Addons();
    if (!addons.empty()) {
        p.note = "Higher in the list wins when two add-ons change the same thing.";
        p.controls = "Space/X: on/off  Enter/A: details  Shift+Up/Down, LB/RB: move";
    }
    int n = 0;
    for (const Mods::ModInfo* m : addons) {
        std::string id = m->id;
        Item it{m->name, "", nullptr, nullptr, [this, id] { PushPage(AddonDetailPage(id)); }, unlocked};
        it.number = ++n;
        it.check = [id] {
            const Mods::ModInfo* mi = FindMod(id);
            return mi && mi->enabled && mi->problem.empty() ? 1 : 0;
        };
        it.toggle = [this, id] {
            const Mods::ModInfo* mi = FindMod(id);
            if (!mi) return;
            if (!mi->problem.empty()) {
                SetMessage("Can't be used: " + mi->problem + ".");
                return;
            }
            Mods::SetEnabled(id, !mi->enabled);
            modsChanged_ = true;
            Save();
        };
        it.change = [this](int) {  // Left/Right too
            auto f = Cur().items[(size_t)Cur().cursor].toggle;
            f();
        };
        it.reorder = [this, id](int delta) {
            auto list = Mods::Addons();
            int pos = -1;
            for (size_t k = 0; k < list.size(); k++)
                if (list[k]->id == id) pos = (int)k;
            int to = pos + delta;
            if (pos < 0 || to < 0 || to >= (int)list.size()) {
                Sound(false);
                return;
            }
            std::string other = list[(size_t)to]->name, name = list[(size_t)pos]->name;
            Mods::Move(id, delta);
            modsChanged_ = true;
            Save();
            // rebuild the list in its new order, keeping the cursor on the moved add-on
            Page fresh = AddonsPage();
            fresh.cursor = to;
            fresh.scroll = Cur().scroll;
            Cur() = fresh;
            Sound(false);
            SetMessage(delta < 0 ? name + " moved up: it now wins over " + other + "."
                                 : name + " moved down: " + other + " now wins over it.");
        };
        it.focusWhenDisabled = true;
        it.helpFn = [this, id] {
            if (ModsLocked()) return std::string(kLockedHelp);
            const Mods::ModInfo* mi = FindMod(id);
            if (!mi) return std::string();
            if (!mi->problem.empty()) return "Can't be used: " + mi->problem + ".";
            std::string s = mi->description.empty() ? "" : mi->description + " ";
            if (!mi->author.empty()) s += "By " + mi->author + ". ";
            return s + "Replaces " + Contents(*mi) + ".";
        };
        p.items.push_back(it);
    }
    if (addons.empty()) {
        Item none{"No add-ons installed", "Put add-ons in the 'addons' folder inside the 'mods' folder.", nullptr, nullptr,
                  nullptr, [] { return false; }};
        none.focusWhenDisabled = true;
        p.items.push_back(none);
    }
    p.items.push_back({"Back", "Changes apply when you close the menu: the game reloads and returns to the title screen.",
                       nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::AddonDetailPage(const std::string& id) {
    Page p;
    const Mods::ModInfo* m = FindMod(id);
    p.title = "ADD-ONS > " + (m ? m->name : id);
    p.controls = "Enter/A, Space/X: on/off  Esc/B: back to the list";
    auto unlocked = [this] { return !ModsLocked(); };
    auto info = [&](const std::string& label, const std::string& value) {
        if (value.empty()) return;
        Item it{label, "", [value] { return value; }, nullptr, nullptr, [] { return false; }};
        it.focusWhenDisabled = true;
        it.helpFn = [id] {
            const Mods::ModInfo* mi = FindMod(id);
            return mi && !mi->description.empty() ? mi->description : std::string();
        };
        p.items.push_back(it);
    };
    if (m) {
        Item on{"On", "", nullptr, nullptr, nullptr, unlocked};
        on.check = [id] {
            const Mods::ModInfo* mi = FindMod(id);
            return mi && mi->enabled && mi->problem.empty() ? 1 : 0;
        };
        on.toggle = [this, id] {
            const Mods::ModInfo* mi = FindMod(id);
            if (!mi) return;
            if (!mi->problem.empty()) {
                SetMessage("Can't be used: " + mi->problem + ".");
                return;
            }
            Mods::SetEnabled(id, !mi->enabled);
            modsChanged_ = true;
            Save();
        };
        on.change = [this](int) {
            auto f = Cur().items[(size_t)Cur().cursor].toggle;
            f();
        };
        on.focusWhenDisabled = true;
        on.helpFn = [this, id] {
            if (ModsLocked()) return std::string(kLockedHelp);
            const Mods::ModInfo* mi = FindMod(id);
            return mi && !mi->description.empty() ? mi->description : std::string();
        };
        p.items.push_back(on);
        auto addons = Mods::Addons();
        int pos = 0;
        for (size_t i = 0; i < addons.size(); i++)
            if (addons[i]->id == id) pos = (int)i + 1;
        info("Load order", std::to_string(pos) + " of " + std::to_string(addons.size()));
        info("Author", m->author);
        info("Version", m->version);
        info("Replaces", Contents(*m));
        info("License", m->license);
        if (!m->problem.empty()) info("Problem", m->problem);
        info("Installed as", m->isZip ? "zip file" : "folder");
    }
    p.items.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::CheatsPage() {
    Page p;
    p.title = "OPTIONS > CHEATS";
    struct Cheat { const char* label; const char* help; bool Config::*field; };
    static const Cheat cheats[] = {
        {"Unlock all levels",
         "Every level of all four worlds can be picked on level select. Your saved progress is not changed.",
         &Config::unlockAllLevels},
        {"Infinite lives",
         "Hits still hurt and push you, but never take your last life. Falling out of a level still loses it.",
         &Config::infiniteLives},
        {"Invincible", "Enemies, bullets and flames can't hurt or push you. Falling out of a level still loses it.",
         &Config::invincible},
        {"Infinite magic", "Firing costs no magic.", &Config::infiniteMagic},
        {"No falling deaths", "Falling into a pit or the water puts you back where you last stood on the floor.",
         &Config::noFallingDeaths},
        {"Max magic level", "Every level starts at magic level 7, the fastest magic recovery. It stays in your save.",
         &Config::maxMagicLevel},
        {"Choose your wand", "", nullptr},  // (its own row below)
        {"Max gold", "Every level starts with 9999 gold. It stays in your save.", &Config::maxGold},
        {"Moon jump", "", nullptr},  // (percent rows below)
        {"Run speed", "", nullptr},
        {"One-hit kills",
         "Every enemy and boss your shots can hurt falls to one shot. Turtles and ghosts still can't be shot.",
         &Config::oneHitKills},
        {"Rapid fire", "Hold fire to keep firing, 8 shots a second. Each shot still costs magic.", &Config::rapidFire},
    };
    // a row stepping through percentages, the first being off
    auto percentRow = [this](const char* label, const char* help, int Config::*field, std::vector<int> steps) {
        return Item{label, help,
                    [field, steps] {
                        int v = GetConfig().*field;
                        return v == steps[0] ? std::string("Off") : std::to_string(v) + "%";
                    },
                    [this, field, steps](int d) {
                        GetConfig().*field = steps[(size_t)CycleIndex(steps, GetConfig().*field, d)];
                        Save();
                    },
                    nullptr, nullptr};
    };
    for (const Cheat& ch : cheats) {
        bool Config::*f = ch.field;
        if (!f && std::string(ch.label) == "Moon jump") {
            p.items.push_back(percentRow(ch.label, "Jump higher. Falling is unchanged.", &Config::moonJump, {100, 150, 200}));
            continue;
        }
        if (!f && std::string(ch.label) == "Run speed") {
            p.items.push_back(percentRow(ch.label, "Walk and run faster.", &Config::runSpeed, {100, 125, 150, 200}));
            continue;
        }
        if (!f) {
            static const char* const kWands[] = {"Off",          "Single",        "Double",       "Triple",
                                                 "Homing single", "Homing double", "Homing triple"};
            // -1 (Off) and the six wand types, as XHeroStatus::wandType
            p.items.push_back({ch.label,
                               "Every level starts with this wand (Off keeps yours). It stays in your save.",
                               [] { return std::string(kWands[std::clamp(GetConfig().cheatWand, -1, 5) + 1]); },
                               [this](int d) {
                                   int& w = GetConfig().cheatWand;
                                   w = (std::clamp(w, -1, 5) + 1 + d + 7) % 7 - 1;
                                   Save();
                               },
                               nullptr, nullptr});
            continue;
        }
        Item it{ch.label, ch.help, [f] { return OnOff(GetConfig().*f); },
                [this, f](int) {
                    GetConfig().*f = !(GetConfig().*f);
                    Save();
                },
                nullptr, nullptr};
        if (f == &Config::unlockAllLevels) {
            // only has a visible effect on level select, so it can't be changed during a level
            it.enabled = [this] { return !app_->InLevel(); };
            it.focusWhenDisabled = true;
            std::string help = ch.help;
            it.helpFn = [this, help] {
                return app_->InLevel() ? std::string("Change this from OPTIONS on the title screen.") : help;
            };
        }
        p.items.push_back(it);
    }
    p.items.push_back({"Back", "All cheats are off by default; with them off the game plays like the original. "
                               "CHEATS shows in the corner while any is on.",
                       nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

// OPTIONS > CONTROLS > KEYBOARD: one page for each group of keys
OptionsMenu::Page OptionsMenu::KeyGroupsPage() {
    Page p;
    p.title = "OPTIONS > KEYBOARD";
    struct G { const char* group; const char* help; };
    static const G groups[] = {
        {"Player 1", "Player 1's keys: the arrows, fire Right Ctrl or [, jump Right Shift or ]."},
        {"Player 2", "Player 2's keys: W A S D, fire Left Ctrl or 1, jump Left Shift or 2."},
        {"General", "Select (Enter), pause (P) and back or give up the level (Esc)."},
    };
    for (const G& g : groups) {
        std::string group = g.group;
        Item it{group, g.help, nullptr, nullptr, [this, group] { PushPage(KeysPage(group)); }, nullptr};
        p.items.push_back(it);
    }
    p.items.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::KeysPage(const std::string& group) {
    Page p;
    std::string upper = group;
    for (char& c : upper) c = (char)std::toupper((unsigned char)c);
    p.title = "KEYBOARD > " + upper;
    if (group == "Player 2") p.note = "In a one-player game player 1 also fires and jumps with these.";
    else if (group == "General") p.note = "Menus also select with Shift or Ctrl and move with W / S.";
    for (const KeyAction& a : kKeyActions) {
        if (group != a.group) continue;
        int dik = a.dik;
        std::string label = a.label, capture = group + ": " + a.label;
        p.items.push_back({label, "Enter: press a new key.  Backspace: restore the default.",
                           [this, dik] { return Fit(KeyNamesFor(dik), 13); }, nullptr,
                           [this, dik, capture] {
                               mode_ = Mode::CaptureKey;
                               captureDik_ = dik;
                               captureLabel_ = capture;
                           },
                           nullptr});
    }
    p.items.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

OptionsMenu::Page OptionsMenu::PadPage() {
    Page p;
    p.title = "OPTIONS > GAMEPAD";
    p.note = "Gamepad 1 plays player 1 and gamepad 2 player 2. Both use these.";
    struct B { const char* label; int button; };
    static const B buttons[] = {{"Fire / select", 0}, {"Jump / select", 1}};
    for (const B& b : buttons) {
        int btn = b.button;
        std::string label = b.label;
        p.items.push_back({label, "Enter: press a gamepad button.  Backspace: restore the default.",
                           [this, btn] { return Fit(PadNamesFor(btn), 13); }, nullptr,
                           [this, btn, label] {
                               mode_ = Mode::CaptureButton;
                               captureButton_ = btn;
                               captureLabel_ = label;
                           },
                           nullptr});
    }
    p.items.push_back({"Back", "", nullptr, nullptr, [this] { Back(false); }, nullptr});
    return p;
}

// ------------------------------------------------------------------ navigation
void OptionsMenu::PushPage(Page p) { stack_.push_back(std::move(p)); }

void OptionsMenu::ResetStick() {
    stickDir_ = stickX_ = stickY_ = 0;
    stickPad_ = 0;
}

void OptionsMenu::Sound(bool confirm) {
    app_->PlayUISound(confirm ? CD3DApplication::UISound::Confirm : CD3DApplication::UISound::Move);
}

void OptionsMenu::AskConfirm(const std::string& title, const std::string& line, std::function<void()> yes) {
    askTitle_ = title;
    askLine_ = line;
    askYes_ = std::move(yes);
    confirmChoice_ = 1;  // "No" is the default
    mode_ = Mode::ConfirmAction;
}

void OptionsMenu::Back(bool sound) {
    if (sound) Sound(false);
    DiscardPending(true);
    if (stack_.size() > 1) stack_.pop_back();
    else Close();
}

void OptionsMenu::Move(int d) {
    Page& p = Cur();
    int n = (int)p.items.size();
    int before = p.cursor;
    for (int tries = 0; tries < n; tries++) {
        p.cursor = ((p.cursor + d) % n + n) % n;
        const Item& it = p.items[(size_t)p.cursor];
        if (!it.enabled || it.enabled() || it.focusWhenDisabled) break;
    }
    if (p.cursor != before) Sound(false);
}

void OptionsMenu::Change(int d) {
    Item& it = Cur().items[(size_t)Cur().cursor];
    if (it.enabled && !it.enabled()) return;
    if (!it.change) return;
    std::string before = it.value ? it.value() : "";
    auto value = it.value;   // copies: the action may rebuild the page, destroying `it`
    auto change = it.change;
    change(d);
    // play after the change, so a volume change is heard at the new level
    if (!value || value() != before) Sound(false);
}

void OptionsMenu::Activate() {
    Item& it = Cur().items[(size_t)Cur().cursor];
    if (it.enabled && !it.enabled()) return;
    if (it.activate) {
        Sound(true);
        auto f = it.activate;  // a copy: opening a page can move the item it belongs to
        f();
    } else if (it.change) {
        Change(+1);
    }
}

// ------------------------------------------------------------------ input
bool OptionsMenu::HandleEvent(const SDL_Event& e) {
    bool isKey = e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP;
    bool isPad = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_UP ||
                 e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    if (!isKey && !isPad) return false;

    if (!open_) {
        bool request = (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F10) ||
                       (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_START);
        if (!request) return false;
        // during the logo, the ending, a fade, or while no mage is under the player's control the press is ignored
        if (app_->CanOpenOptions()) Open();
        return true;
    }

    // ---- capture modes
    if (mode_ == Mode::CaptureKey) {
        if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return true;
        SDL_Scancode sc = e.key.scancode;
        Config& c = GetConfig();
        if (sc == SDL_SCANCODE_ESCAPE && captureDik_ != DIK_ESCAPE) {
            mode_ = Mode::Normal;
        } else if (sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_DELETE) {
            c.keyBinds.erase(captureDik_);
            Input::ReloadBindings();
            Save();
            mode_ = Mode::Normal;
            SetMessage(captureLabel_ + " restored to " + KeyNamesFor(captureDik_) + ".");
        } else if (sc == SDL_SCANCODE_F9 || sc == SDL_SCANCODE_F10 || sc == SDL_SCANCODE_F11 || sc == SDL_SCANCODE_F12) {
            SetMessage("F9 to F12 are reserved.");
        } else {
            std::string name = SDL_GetScancodeName(sc);
            c.keyBinds[captureDik_] = {name};
            Input::ReloadBindings();
            Save();
            mode_ = Mode::Normal;
            // warn if that key also does something else
            std::string also;
            for (const KeyAction& a : kKeyActions) {
                if (a.dik == captureDik_) continue;
                std::string names = ", " + KeyNamesFor(a.dik) + ",";
                if (names.find(", " + name + ",") != std::string::npos) also = std::string(a.group) + ": " + a.label;
            }
            if (!also.empty()) SetMessage(name + " is also used for: " + also + ".", 5000);
            else SetMessage(captureLabel_ + " set to " + name + ".");
        }
        return true;
    }
    if (mode_ == Mode::CaptureButton) {
        Config& c = GetConfig();
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode == SDL_SCANCODE_ESCAPE) mode_ = Mode::Normal;
            if (e.key.scancode == SDL_SCANCODE_BACKSPACE || e.key.scancode == SDL_SCANCODE_DELETE) {
                c.padBinds.erase(captureButton_);
                Input::ReloadBindings();
                Save();
                mode_ = Mode::Normal;
                SetMessage(captureLabel_ + " restored to the default button.");
            }
        } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            SDL_GamepadButton b = (SDL_GamepadButton)e.gbutton.button;
            if (b == SDL_GAMEPAD_BUTTON_START) {
                mode_ = Mode::Normal;
            } else if (const char* name = SDL_GetGamepadStringForButton(b)) {
                c.padBinds[captureButton_] = {name};
                Input::ReloadBindings();
                Save();
                mode_ = Mode::Normal;
                SetMessage(captureLabel_ + " set to " + name + ".");
            }
        }
        return true;
    }

    // ---- translate to menu actions
    enum { None, Up, Down, Left, Right, Ok, Cancel, Exit, Toggle, MoveUp, MoveDown } act = None;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        switch (e.key.scancode) {
            case SDL_SCANCODE_UP: act = shift ? MoveUp : Up; break;
            case SDL_SCANCODE_DOWN: act = shift ? MoveDown : Down; break;
            case SDL_SCANCODE_PAGEUP: act = MoveUp; break;
            case SDL_SCANCODE_PAGEDOWN: act = MoveDown; break;
            case SDL_SCANCODE_LEFT: act = Left; break;
            case SDL_SCANCODE_RIGHT: act = Right; break;
            case SDL_SCANCODE_SPACE: if (!e.key.repeat) act = Toggle; break;  // Ok on rows without a tick box
            // select with Enter or Ctrl, as the game's own menus (Shift, which they also take, is
            // the menu's reorder modifier here)
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
            case SDL_SCANCODE_LCTRL:
            case SDL_SCANCODE_RCTRL: if (!e.key.repeat) act = Ok; break;
            case SDL_SCANCODE_ESCAPE:
            case SDL_SCANCODE_BACKSPACE: if (!e.key.repeat) act = Cancel; break;
            case SDL_SCANCODE_F10: if (!e.key.repeat) act = Exit; break;
            default: break;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (e.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP: act = Up; break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: act = Down; break;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: act = Left; break;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: act = Right; break;
            case SDL_GAMEPAD_BUTTON_SOUTH: act = Ok; break;
            case SDL_GAMEPAD_BUTTON_EAST: act = Cancel; break;
            case SDL_GAMEPAD_BUTTON_WEST: act = Toggle; break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: act = MoveUp; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: act = MoveDown; break;
            case SDL_GAMEPAD_BUTTON_START: act = Exit; break;
            default: break;
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
               (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)) {
        // Each axis is tracked on its own, with hysteresis: it counts as pushed past 50% and as
        // released again below 35%. Releasing the axis that drives the auto-repeat stops it.
        bool isY = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY;
        int& axisDir = isY ? stickY_ : stickX_;
        int v = e.gaxis.value;
        int nd = axisDir;
        if (v < -kStickPress) nd = -1;
        else if (v > kStickPress) nd = 1;
        else if (std::abs(v) < kStickRelease) nd = 0;
        if (nd != axisDir) {
            axisDir = nd;
            int scaled = isY ? nd : nd * 2;
            if (nd != 0) {
                stickDir_ = scaled;
                stickPad_ = e.gaxis.which;
                stickNext_ = now_ + 400;
                act = scaled == -1 ? Up : scaled == 1 ? Down : scaled == -2 ? Left : Right;
            } else if ((std::abs(stickDir_) == 1) == isY) {
                stickDir_ = 0;  // the axis driving the repeat was released
            }
        }
    }

    // the tick-box/reorder actions only mean something on rows that have them
    if (mode_ != Mode::Normal || stack_.empty()) {
        if (act == Toggle) act = e.type == SDL_EVENT_KEY_DOWN ? Ok : None;
        if (act == MoveUp) act = e.type == SDL_EVENT_KEY_DOWN ? Up : None;
        if (act == MoveDown) act = e.type == SDL_EVENT_KEY_DOWN ? Down : None;
    }
    if (mode_ == Mode::ConfirmAction) {
        if (act == Left || act == Right || act == Up || act == Down) {
            confirmChoice_ ^= 1;
            Sound(false);
        }
        if (act == Ok) {
            Sound(true);
            mode_ = Mode::Normal;
            if (confirmChoice_ == 0 && askYes_) {
                auto yes = std::move(askYes_);
                askYes_ = nullptr;
                yes();
            }
        }
        if (act == Cancel) {
            Sound(false);
            mode_ = Mode::Normal;
        }
        if (act == Exit) Close();
        return true;
    }
    if (mode_ == Mode::ConfirmDisplay) {
        if (act == Left || act == Right || act == Up || act == Down) {
            confirmChoice_ ^= 1;
            Sound(false);
        }
        if (act == Ok) Sound(true);
        if (act == Ok) {
            if (confirmChoice_ == 0) {
                mode_ = Mode::Normal;
                SetMessage("Display settings kept.");
            } else {
                RevertDisplay();
            }
        }
        if (act == Cancel || act == Exit) RevertDisplay();
        return true;
    }

    if (act == Toggle || act == MoveUp || act == MoveDown) {
        const Item& it = Cur().items[(size_t)Cur().cursor];
        bool en = !it.enabled || it.enabled();
        // run copies: an action may rebuild the page, destroying the item it belongs to
        if (act == Toggle) {
            if (it.toggle) {
                if (en) {
                    Sound(true);
                    auto f = it.toggle;
                    f();
                }
                return true;
            }
            act = e.type == SDL_EVENT_KEY_DOWN ? Ok : None;  // Space works as Enter elsewhere
        } else {
            if (it.reorder) {
                if (en) {
                    auto f = it.reorder;
                    f(act == MoveUp ? -1 : +1);
                }
                return true;
            }
            act = e.type != SDL_EVENT_KEY_DOWN ? None : act == MoveUp ? Up : Down;
        }
    }
    switch (act) {
        case Up: Move(-1); break;
        case Down: Move(+1); break;
        case Left: Change(-1); break;
        case Right: Change(+1); break;
        case Ok: Activate(); break;
        case Cancel: Back(); break;
        case Exit:
            Sound(false);
            Close();
            break;
        default: break;
    }
    return true;
}

void OptionsMenu::Update(DWORD nowMs) {
    now_ = nowMs;
    if (!open_) return;
    if (mode_ == Mode::ConfirmDisplay && now_ >= confirmDeadline_) RevertDisplay();
    // analog stick auto-repeat. Before each repeat, re-read the stick directly so a lost
    // "released" event (or a disconnected controller) can never leave it scrolling.
    if (stickDir_ != 0) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(stickPad_);
        bool held = false;
        if (pad) {
            int v = SDL_GetGamepadAxis(pad, std::abs(stickDir_) == 1 ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_LEFTX);
            held = (stickDir_ < 0) ? v < -kStickRelease : v > kStickRelease;
        }
        if (!held) ResetStick();
    }
    if (stickDir_ != 0 && mode_ == Mode::Normal && now_ >= stickNext_) {
        stickNext_ = now_ + 120;
        if (stickDir_ == -1) Move(-1);
        else if (stickDir_ == 1) Move(+1);
        else Change(stickDir_ > 0 ? +1 : -1);
    }
}

// ------------------------------------------------------------------ drawing
// A 16 x 16 tick box at (x, y): an outline, with a green tick when on.
void OptionsMenu::DrawTickBox(float x, float y, bool on, bool enabled) {
    const UIColor edge = enabled ? kWhite : kGrey;
    ui_.Rect(x, y, x + 16, y + 2, edge);
    ui_.Rect(x, y + 14, x + 16, y + 16, edge);
    ui_.Rect(x, y, x + 2, y + 16, edge);
    ui_.Rect(x + 14, y, x + 16, y + 16, edge);
    if (!on) return;
    const UIColor tick = enabled ? UIColor{0.45f, 0.95f, 0.55f, 1} : kGrey;
    // two strokes drawn as small squares: down-right from (4,8) to (7,11), then up-right to (12,4)
    for (int i = 0; i <= 3; i++) ui_.Rect(x + 3 + i, y + 7 + i, x + 5.5f + i, y + 9.5f + i, tick);
    for (int i = 0; i <= 6; i++) ui_.Rect(x + 6 + i, y + 10 - i, x + 8.5f + i, y + 12.5f - i, tick);
}

void OptionsMenu::Render(Device* d) {
    if (!open_) return;
    ui_.Begin(d);
    ui_.Rect(ui_.Left(), 0, ui_.Right(), 480, kDim);
    const float x0 = 40, x1 = 600, y0 = 36, y1 = 444;
    ui_.Rect(x0 - 2, y0 - 2, x1 + 2, y1 + 2, kBorder);
    ui_.Rect(x0, y0, x1, y1, kPanel);

    Page& p = Cur();
    // the build's version at the right of the title row, on every page, so a player can tell which
    // build is running (the same MBN_VERSION_STRING as the window's title bar)
    const std::string version = std::string("v") + MBN_VERSION_STRING;
    const float versionW = ui_.TextWidth(version, 1);
    ui_.Text(x1 - 16 - versionW, y0 + 30, version, 1, kGrey);
    ui_.Text(x0 + 16, y0 + 14, Fit(p.title, (size_t)((x1 - x0 - 48 - versionW) / 24)), 3, kGold);
    ui_.Rect(x0 + 16, y0 + 46, x1 - 16, y0 + 48, UIColor{kBorder.r, kBorder.g, kBorder.b, 0.6f});

    float y = y0 + 60;
    if (!p.note.empty()) {
        ui_.Text(x0 + 20, y - 4, p.note, 1, kGrey);
        y += 14;
    }
    const float rowH = 22;
    // rows that fit above the help text; longer pages scroll to keep the cursor visible
    const int visible = (int)((y1 - 52 - y) / rowH);
    const int count = (int)p.items.size();
    if (count <= visible) p.scroll = 0;
    else {
        if (p.cursor < p.scroll) p.scroll = p.cursor;
        if (p.cursor >= p.scroll + visible) p.scroll = p.cursor - visible + 1;
        p.scroll = std::clamp(p.scroll, 0, count - visible);
    }
    const bool scrollable = count > visible && visible > 0;
    if (scrollable) {
        // scrollbar along the right edge of the list (the same look as the level-select one): the
        // thumb shows which part of the page is on screen and how much of it fits
        const float tx0 = x1 - 11, tx1 = x1 - 4, ty0 = y - 4, ty1 = y + visible * rowH - 4;
        const UIColor track{0.055f, 0.04f, 0.19f, 1}, thumb{1.0f, 0.925f, 0.55f, 1}, shine{1.0f, 1.0f, 0.86f, 1};
        ui_.Rect(tx0 + 1, ty0, tx1 - 1, ty1, kBorder);  // outline with cut corners
        ui_.Rect(tx0, ty0 + 1, tx1, ty1 - 1, kBorder);
        ui_.Rect(tx0 + 1, ty0 + 1, tx1 - 1, ty1 - 1, track);
        const float h = ty1 - ty0 - 2;
        float th = std::max(h * visible / count, 10.f);
        float ty = ty0 + 1 + (h - th) * p.scroll / (count - visible);
        ui_.Rect(tx0 + 2, ty + 1, tx1 - 2, ty + th - 1, thumb);
        ui_.Rect(tx0 + 1.5f, ty + 2, tx1 - 1.5f, ty + th - 2, thumb);
        ui_.Rect(tx0 + 2, ty + 1, tx0 + 3, ty + th - 1, shine);  // highlight along the left side
    }
    for (size_t i = (size_t)p.scroll; i < p.items.size() && (int)i < p.scroll + std::max(visible, 1); i++) {
        const Item& it = p.items[i];
        bool sel = (int)i == p.cursor;
        bool en = !it.enabled || it.enabled();
        if (sel) ui_.Rect(x0 + 8, y - 3, scrollable ? x1 - 14 : x1 - 8, y + 19, kSelBar);
        float labelX = x0 + 20;
        bool ticked = true;
        if (it.number > 0) {
            // numbers right-aligned in a column as wide as the largest one, so names line up
            int maxNum = 0;
            for (const Item& o : p.items) maxNum = std::max(maxNum, o.number);
            float col = ui_.TextWidth(std::to_string(maxNum), 2);
            std::string num = std::to_string(it.number);
            ui_.Text(x0 + 20 + col - ui_.TextWidth(num, 2), y, num, 2, sel ? kSelValue : kGrey);
            labelX = x0 + 20 + col + 10;
        }
        if (it.check && it.number > 0) {  // list row: number, tick box, name (greyed when off)
            ticked = it.check() == 1;
            DrawTickBox(labelX, y, ticked, en);
            labelX += 24;
        } else if (it.check) {  // setting row: the tick box is its value
            DrawTickBox(x1 - 20 - 16, y, it.check() == 1, en);
        }
        // long names (mods can be called anything) are shortened to fit beside the value
        const bool arrow = !it.value && it.activate && it.label != "Back" && it.label != "Resume game" &&
                           it.label != "Reset controls" && it.label != "Main menu" && it.label != "Level select" && it.label != "Quit game" &&
                           it.label != "Reset all settings";
        std::string val;
        if (it.value) {
            val = it.value();
            if (sel && en && it.change) val = "< " + val + " >";
            if (it.submenu) val += " >";
        }
        const float right = x1 - 20;
        std::string label = it.label;
        float room = right - labelX - (it.value ? ui_.TextWidth(val, 2) + 16 : arrow ? 28 : 0);
        if (ui_.TextWidth(label, 2) > room) label = Fit(label, (size_t)std::max(room / 16.f, 6.f));
        if (it.value) {
            float valRoom = right - labelX - ui_.TextWidth(label, 2) - 16;
            if (ui_.TextWidth(val, 2) > valRoom) val = Fit(val, (size_t)std::max(valRoom / 16.f, 4.f));
        }
        ui_.Text(labelX, y, label, 2, en && ticked ? kWhite : kGrey);
        if (it.value) {
            float w = ui_.TextWidth(val, 2);
            float vx = x1 - 20 - w;
            ui_.Text(vx, y, val, 2, !en ? kGrey : sel ? kSelValue : kValue);
            if (it.bar) {
                // level bar left of the number
                float bx1 = x1 - 20 - ui_.TextWidth("< 100% >", 2) - 8, bx0 = bx1 - 120;
                float f = std::clamp(it.bar(), 0, 100) / 100.f;
                ui_.Rect(bx0, y + 3, bx1, y + 13, UIColor{0.1f, 0.1f, 0.25f, 1});
                ui_.Rect(bx0, y + 3, bx0 + (bx1 - bx0) * f, y + 13, sel ? kSelValue : kValue);
            }
        } else if (arrow) {
            ui_.Text(x1 - 20 - 16, y, ">", 2, sel ? kSelValue : kGrey);
        }
        y += rowH;
    }

    // help line for the selected item (wrapped to two lines), or a status message
    const Item& selItem = p.items[(size_t)p.cursor];
    std::string help = selItem.helpFn ? selItem.helpFn() : selItem.help;
    UIColor helpColor{0.75f, 0.78f, 0.9f, 1};
    if (!message_.empty() && now_ < messageUntil_) {
        help = message_;
        helpColor = kGold;
    }
    const size_t perLine = 66;
    std::string l1 = help, l2;
    if (help.size() > perLine) {
        size_t cut = help.rfind(' ', perLine);
        if (cut == std::string::npos) cut = perLine;
        l1 = help.substr(0, cut);
        l2 = help.substr(cut + 1);
    }
    ui_.Text(x0 + 16, y1 - 46, l1, 1, helpColor);
    if (!l2.empty()) ui_.Text(x0 + 16, y1 - 34, Fit(l2, perLine), 1, helpColor);
    ui_.Text(x0 + 16, y1 - 16,
             p.controls.empty() ? std::string("Arrows/D-pad  Enter/A: select  Esc/B: back  F10/Start: close") : p.controls,
             1, kGrey);

    // modal overlays
    auto box = [&](const std::string& title, const std::string& line2) {
        float bx0 = 60, bx1 = 580, by0 = 170, by1 = 300;
        ui_.Rect(bx0 - 2, by0 - 2, bx1 + 2, by1 + 2, kGold);
        ui_.Rect(bx0, by0, bx1, by1, UIColor{0.08f, 0.06f, 0.2f, 0.98f});
        ui_.Text(320 - ui_.TextWidth(title, 2) / 2, by0 + 22, title, 2, kWhite);
        ui_.Text(320 - ui_.TextWidth(line2, 1) / 2, by0 + 52, line2, 1, kValue);
    };
    if (mode_ == Mode::CaptureKey) {
        box("Press a key for", captureLabel_);
        std::string hint = captureDik_ == DIK_ESCAPE ? "Backspace: restore default" : "Esc: cancel   Backspace: restore default";
        ui_.Text(320 - ui_.TextWidth(hint, 1) / 2, 270, hint, 1, kGrey);
    } else if (mode_ == Mode::CaptureButton) {
        box("Press a gamepad button for", captureLabel_);
        std::string hint = "Start/Esc: cancel   Backspace: restore default";
        ui_.Text(320 - ui_.TextWidth(hint, 1) / 2, 270, hint, 1, kGrey);
    } else if (mode_ == Mode::ConfirmDisplay) {
        int secs = (int)((confirmDeadline_ > now_ ? confirmDeadline_ - now_ : 0) + 999) / 1000;
        box("Keep these display settings?", "Reverting in " + std::to_string(secs) + " seconds");
        std::string keep = confirmChoice_ == 0 ? "> Keep <" : "  Keep  ";
        std::string rev = confirmChoice_ == 1 ? "> Revert <" : "  Revert  ";
        ui_.Text(220 - ui_.TextWidth(keep, 2) / 2, 258, keep, 2, confirmChoice_ == 0 ? kSelValue : kGrey);
        ui_.Text(420 - ui_.TextWidth(rev, 2) / 2, 258, rev, 2, confirmChoice_ == 1 ? kSelValue : kGrey);
    } else if (mode_ == Mode::ConfirmAction) {
        box(askTitle_, askLine_);
        std::string yes = confirmChoice_ == 0 ? "> Yes <" : "  Yes  ";
        std::string no = confirmChoice_ == 1 ? "> No <" : "  No  ";
        ui_.Text(220 - ui_.TextWidth(yes, 2) / 2, 258, yes, 2, confirmChoice_ == 0 ? kSelValue : kGrey);
        ui_.Text(420 - ui_.TextWidth(no, 2) / 2, 258, no, 2, confirmChoice_ == 1 ? kSelValue : kGrey);
    }
}
