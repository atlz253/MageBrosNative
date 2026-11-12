// Entry point (replaces WinMain from XAppMain.cpp).
//
// Stage 2 of docs/ROADMAP.md: the engine runs, but the game logic isn't there yet, so the program
// opens the asset viewer: every model, sound and music track of the user's MageBros.exe, drawn
// and played through the same renderer, mixer, MIDI player and input layer the game will use.
#include "audio/audio.h"
#include "audio/midi.h"
#include "audio/music_stream.h"
#include "core/assets.h"
#include "core/config.h"
#include "core/game_files.h"
#include "core/log.h"
#include "core/mods.h"
#include "core/saves.h"
#include "framework/d3dapp.h"
#include "framework/d3dfile.h"
#include "framework/interp.h"
#include "framework/d3dtextr.h"
#include "framework/d3dutil.h"
#include "game/XApplication.h"
#include "game/JModel.h"
#include "game/JSprite.h"
#include "game/XGameScene.h"
#include "game/XLevelView.h"
#include "gfx/device.h"
#include "input/input.h"
#include "ui/ui_draw.h"
#include "version.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// The 8-bit PCM data chunk of a WAVE resource (every sound in the game is 8-bit mono).
bool WaveData(const std::vector<uint8_t>& w, const uint8_t** pcm, size_t* len, int* rate) {
    if (w.size() < 44 || std::memcmp(w.data(), "RIFF", 4) || std::memcmp(w.data() + 8, "WAVE", 4)) return false;
    auto rd32 = [&](size_t o) { return (uint32_t)(w[o] | w[o + 1] << 8 | w[o + 2] << 16 | (uint32_t)w[o + 3] << 24); };
    size_t p = 12;
    int bits = 0, channels = 0;
    while (p + 8 <= w.size()) {
        uint32_t sz = rd32(p + 4);
        if (!std::memcmp(&w[p], "fmt ", 4) && p + 24 <= w.size()) {
            channels = w[p + 10] | w[p + 11] << 8;
            *rate = (int)rd32(p + 12);
            bits = w[p + 22] | w[p + 23] << 8;
        } else if (!std::memcmp(&w[p], "data", 4)) {
            *pcm = &w[p + 8];
            *len = std::min<size_t>(sz, w.size() - (p + 8));
            return bits == 8 && channels == 1;
        }
        p += 8 + ((size_t)sz + 1) / 2 * 2;
    }
    return false;
}

class AssetViewerApp : public CD3DApplication {
public:
    AssetViewerApp(bool twoPlayer, bool ending) : startAtEnding_(ending) {
        m_strWindowTitle = "Mage Bros. Native";
        app_.SetTwoPlayer(twoPlayer);
    }

protected:
    HRESULT OneTimeSceneInit() override {
        for (const auto& e : GameFiles::ListGameResources()) {
            if (e.kind == GameFiles::Kind::Model) models_.push_back(e.id);
            if (e.kind == GameFiles::Kind::Wave) sounds_.push_back(e.id);
        }
        std::sort(models_.begin(), models_.end());
        std::sort(sounds_.begin(), sounds_.end());
        // start on player 1's standing frame (MageA01)
        auto it = std::find(models_.begin(), models_.end(), 355u);
        modelIndex_ = it == models_.end() ? 0 : (int)(it - models_.begin());
        for (uint32_t id : sounds_) {
            const auto* w = Assets::Wave(id);
            const uint8_t* pcm = nullptr;
            size_t len = 0;
            int rate = 22050;
            soundHandles_.push_back(w && WaveData(*w, &pcm, &len, &rate) ? Audio::CreateBuffer(pcm, len, rate) : -1);
        }
        Log("Viewer: %zu models, %zu sounds", models_.size(), sounds_.size());
        LoadModel();
        levelView_.Load(0, 0);
        app_.Init();  // the original's screens, from the logo
        if (startAtEnding_) app_.SetState(XApplication::ENDING);  // test hook: --ending
        ResetCamera();
        PlayMusic(levelView_.MusicTrack());
        return S_OK;
    }

    HRESULT InitDeviceObjects() override {
        Device* d = GetD3DDevice();
        d->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
        d->SetRenderState(D3DRENDERSTATE_DITHERENABLE, 1);
        d->SetRenderState(D3DRENDERSTATE_LIGHTING, 1);
        d->SetRenderState(D3DRENDERSTATE_ZENABLE, 1);
        return S_OK;
    }

    // The level and model viewers' own lens and light (a viewer's, not the game's: a wider depth
    // range and a softer light to inspect models). The game view uses XGameScene::SetDeviceState.
    static void SetViewerDeviceState(Device* d) {
        D3DMATRIX proj;
        D3DUtil_SetProjectionMatrix(proj, 0.7330383f, 0.75f, 1.f, 5000.f);
        d->SetTransform(D3DTRANSFORMSTATE_PROJECTION, &proj);
        D3DLIGHT7 light;
        D3DUtil_InitLight(light, D3DLIGHT_DIRECTIONAL, 0.8f, 0.1f, 0.5f, -1.0f, 0.5f);
        d->SetLight(0, &light);
        d->LightEnable(0, 1);
        d->SetRenderState(D3DRENDERSTATE_AMBIENT, 0xff808080);
    }

    // test automation (--auto): "level=w,l" goes straight into the game at a level (from any
    // screen), "place=x,z" drops hero A there ("placeB=x,z" hero B), "players=1" or "players=2"
    // sets the number of players for the next level started, "ending" goes to the ending,
    // "alpha=0.5" holds smooth motion's blend there ("alpha=off" frees it)
    void OnAutomation(const std::string& action, const std::string& arg) override {
        if (action == "alpha") {
            Interp::HoldAlpha(arg == "off" ? -1.0f : (float)std::atof(arg.c_str()));
            return;
        }
        if (action == "ending") {
            app_.SetState(XApplication::ENDING);
            return;
        }
        if (action == "players") {
            app_.SetTwoPlayer(arg == "2");
            return;
        }
        float a = 0, b = 0;
        if (std::sscanf(arg.c_str(), "%f,%f", &a, &b) != 2) return;
        if (action == "place" && app_.InGame()) app_.Game().TestPlaceHero(a, b);
        else if (action == "placeB" && app_.InGame()) app_.Game().TestPlaceHero(a, b, false);
        else if (action == "level") app_.StartGameAt((int)a - 1, (int)b - 1);
    }

    HRESULT FrameMove(DWORD n) override {
        angle_ += 0.01f * n;
        if (mode_ == Mode::Game) {
            bool inGame = app_.InGame();
            app_.FrameMove(n);  // n ticks of 10 ms (the original takes at most 10 a frame)
            if (app_.QuitRequested()) RequestQuit();
            if (int track = app_.TakeMusicRequest()) PlayMusic(track);
            if (app_.Game().TakeLevelChanged() || inGame != app_.InGame()) UpdateTitle();
            return S_OK;
        }
        if (levelMode_) {
            // fly camera: held keys, scaled by elapsed ticks (10 ms each)
            uint8_t keys[256];
            Input::GetKeyboardState(keys);
            float step = 4.0f * n * (keys[DIK_LSHIFT] || keys[DIK_RSHIFT] ? 3.0f : 1.0f);
            if (keys[DIK_RIGHT]) camZ_ += step;
            if (keys[DIK_LEFT]) camZ_ -= step;
            if (keys[DIK_UP]) camDist_ = std::max(80.0f, camDist_ - step);
            if (keys[DIK_DOWN]) camDist_ = std::min(1200.0f, camDist_ + step);
            if (keys[DIK_PRIOR]) camPitch_ = std::min(1.5f, camPitch_ + 0.004f * n);
            if (keys[DIK_NEXT]) camPitch_ = std::max(-0.3f, camPitch_ - 0.004f * n);
            // XCamera keeps its look-at point between z = 160 and (rows - 4) * 40
            camZ_ = std::max(160.0f, std::min(levelView_.CameraMaxZ(), camZ_));
        }
        return S_OK;
    }

    // XCamera::ResetForGame: 340 units away, 35 degrees above the horizontal, looking at
    // (140, 120, 160) from the camera side (+x).
    void ResetCamera() {
        camZ_ = 160.0f;
        camDist_ = 340.0f;
        camPitch_ = 0.610865f;
    }

    HRESULT Render() override {
        Device* d = GetD3DDevice();
        // XApplication::Render clears to its background colour, which XApplication::XApplication
        // sets to 0 (black) and nothing changes: it is what shows through the water in the pits.
        // The viewers keep their own navy.
        d->Clear(0, nullptr, 3, mode_ == Mode::Game ? 0x000000 : 0x1a2a44, 1.f, 0);
        d->BeginScene();
        if (mode_ == Mode::Game) {
            XGameScene::SetDeviceState(d);
            app_.Render(d, GetBackBuffer(), DisplayFPS() != 0, GetFPS());
            d->EndScene();
            // native: CHEATS in the bottom-left corner on level select and in levels while any
            // cheat is on (the FPS counter has the bottom-right)
            if (GetConfig().AnyCheat() && (app_.InGame() || app_.OnLevelSelect())) {
                cheatsMark_.Begin(d);
                cheatsMark_.Text(cheatsMark_.Left() + 6, 466, "CHEATS", 1, UIColor{1.f, 0.8f, 0.25f, 0.9f});
            }
            return S_OK;
        }
        SetViewerDeviceState(d);
        if (levelMode_) {
            // XCamera::_UpdateViewMatrix with the horizontal angle at pi/2 (the camera side, +x)
            D3DMATRIX view;
            float flat = std::cos(camPitch_) * camDist_;
            D3DVECTOR at(140.0f, 120.0f, camZ_), up(0, 1, 0);
            D3DVECTOR from(at.x + flat, at.y + std::sin(camPitch_) * camDist_, at.z);
            D3DUtil_SetViewMatrix(view, from, at, up);
            d->SetTransform(D3DTRANSFORMSTATE_VIEW, &view);
            levelView_.Render(d);
            d->EndScene();
            return S_OK;
        }
        // orbit the model's bounding box, framed by its size
        float r = radius_ * zoom_;
        D3DMATRIX view;
        D3DVECTOR from(center_.x + std::sin(angle_) * r, center_.y + r * 0.45f, center_.z - std::cos(angle_) * r);
        D3DVECTOR up(0, 1, 0);
        D3DUtil_SetViewMatrix(view, from, center_, up);
        d->SetTransform(D3DTRANSFORMSTATE_VIEW, &view);
        D3DMATRIX world;
        D3DUtil_SetIdentityMatrix(world);
        d->SetTransform(D3DTRANSFORMSTATE_WORLD, &world);
        if (mesh_) mesh_->Render(d);
        d->EndScene();
        return S_OK;
    }

    void OnKeyDown(int dik) override {
        if (dik == DIK_P) {
            // XApplication::MsgProc: WM_KEYDOWN 'P' posts IDM_TOGGLESTART, the framework's pause
            // (nothing ticks, the last picture stays). The original also toggles on the key's
            // auto-repeat; here a held P toggles once.
            m_bFrameMoving = !m_bFrameMoving;
            return;
        }
        if (dik == DIK_TAB) {
            // game -> level viewer -> model viewer -> game
            mode_ = mode_ == Mode::Game ? Mode::LevelViewer : mode_ == Mode::LevelViewer ? Mode::ModelViewer : Mode::Game;
            levelMode_ = mode_ == Mode::LevelViewer;
            UpdateTitle();
            return;
        }
        if (mode_ == Mode::Game) {
            if (!app_.InGame()) return;  // (the logo takes no keys)
            const XLevelView& lv = app_.Game().Level();
            if (dik == DIK_N || dik == DIK_B) {
                // test hooks: the next or previous level, straight away
                int i = lv.World() * GameFiles::kLevelsPerWorld + lv.Level() + (dik == DIK_N ? 1 : -1);
                int n = GameFiles::kWorldCount * GameFiles::kLevelsPerWorld;
                i = (i % n + n) % n;
                app_.StartGameAt(i / GameFiles::kLevelsPerWorld, i % GameFiles::kLevelsPerWorld);
                UpdateTitle();
            } else if (dik == DIK_F3) {
                // test hook: straight to the ending (as --ending and --auto ending)
                app_.SetState(XApplication::ENDING);
            } else if (dik == DIK_F2) {
                // one or two players (the title's choice, until it exists): restarts the level
                app_.SetTwoPlayer(app_.OnePlayer());
                app_.StartGameAt(lv.World(), lv.Level());
                UpdateTitle();
            } else if (app_.OnePlayer() && dik >= DIK_1 && dik < DIK_1 + GameFiles::kMusicTrackCount) {
                PlayMusic(dik - DIK_1 + 1);  // (1 and 2 are player 2's fire and jump keys)
            }
            return;
        }
        if (levelMode_) {
            int w = levelView_.World(), l = levelView_.Level();
            if (dik == DIK_N || dik == DIK_B) {
                int i = w * GameFiles::kLevelsPerWorld + l + (dik == DIK_N ? 1 : -1);
                int n = GameFiles::kWorldCount * GameFiles::kLevelsPerWorld;
                i = (i % n + n) % n;
                if (levelView_.Load(i / GameFiles::kLevelsPerWorld, i % GameFiles::kLevelsPerWorld)) {
                    ResetCamera();
                    PlayMusic(levelView_.MusicTrack());
                }
                UpdateTitle();
            } else if (dik == DIK_HOME) {
                ResetCamera();
            } else if (dik >= DIK_1 && dik < DIK_1 + GameFiles::kMusicTrackCount) {
                PlayMusic(dik - DIK_1 + 1);
            } else if (dik == DIK_SPACE) {
                PlayNextSound();
            }
            return;
        }
        switch (dik) {
            case DIK_RIGHT: modelIndex_++; LoadModel(); break;
            case DIK_LEFT: modelIndex_--; LoadModel(); break;
            case DIK_UP: zoom_ = std::max(0.3f, zoom_ * 0.85f); break;
            case DIK_DOWN: zoom_ = std::min(6.f, zoom_ / 0.85f); break;
            case DIK_SPACE: PlayNextSound(); break;
            default:
                if (dik >= DIK_1 && dik < DIK_1 + GameFiles::kMusicTrackCount) PlayMusic(dik - DIK_1 + 1);
                break;
        }
    }

    void OnMidiNotifySuccessful() override {
        // MM_MCINOTIFY: JMidiList::Replay, the loop (MP3/FLAC replacements loop by themselves)
        if (streaming_) return;
        Midi::PlayFromStart();
        if (musicPaused_) Midi::Pause();
    }

    // XApplication::MsgProc, WM_ACTIVATEAPP: losing the focus pauses the music (and releases the
    // keyboard and gamepads, which Input::SetFocus does); getting it back resumes it
    void OnActivateApp(bool active) override {
        musicPaused_ = !active;
        if (streaming_) active ? MusicStream::Resume() : MusicStream::Pause();
        else active ? Midi::Resume() : Midi::Pause();
    }

    // ---- the options menu's hooks (native additions)
    // the title menu's own sounds: 0xec moving, 0xed choosing
    void PlayUISound(UISound s) override { app_.Sounds().PlayMono(s == UISound::Move ? 0xec : 0xed, 1.0f); }
    // "Main menu" goes to the title from any screen but the logo and the title themselves
    bool CanReturnToMainMenu() const override { return mode_ == Mode::Game && !app_.OnLogoOrTitle(); }
    void ReturnToMainMenu() override {
        if (!CanReturnToMainMenu()) return;
        m_bFrameMoving = true;  // (a P pause would leave the title frozen)
        app_.SetState(XApplication::TITLE);
    }
    bool InLevel() const override { return mode_ == Mode::Game && app_.InGame(); }
    // in a level the menu offers "Level select" instead of "Main menu"
    void ReturnToLevelSelect() override {
        if (!InLevel()) return;
        m_bFrameMoving = true;
        app_.LeaveLevel();
    }
    // F10 / Start: in the viewers at any time; in the game only while a screen waits for the
    // player (not the logo or the ending, not during a fade, in a level only while a mage is
    // under the player's control)
    bool CanOpenOptions() const override { return mode_ != Mode::Game || app_.PlayerHasControl(); }
    void ReloadMusic() override {
        int t = mode_ == Mode::Game ? app_.CurrentMusic() : track_;
        if (t > 0) PlayMusic(t);
    }
    const char* MusicSource() const override {
        if (streaming_ && MusicStream::IsOpen()) return "MP3/FLAC file";
        return Midi::IsLoaded() ? "MIDI" : "";
    }

private:
    void LoadModel() {
        if (models_.empty()) return;
        int n = (int)models_.size();
        modelIndex_ = ((modelIndex_ % n) + n) % n;
        mesh_ = std::make_unique<CD3DFile>();
        uint32_t id = models_[modelIndex_];
        if (FAILED(mesh_->LoadFromResource((WORD)id))) {
            Log("Viewer: model %u failed to load", id);
            mesh_.reset();
        }
        D3DVECTOR lo(-1, -1, -1), hi(1, 1, 1);
        if (mesh_) {
            mesh_->GetBoxMin(lo);
            mesh_->GetBoxMax(hi);
        }
        center_ = D3DVECTOR((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2);
        float dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z;
        radius_ = std::max(1.f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.1f);
        UpdateTitle();
    }

    void PlayNextSound() {
        if (soundHandles_.empty()) return;
        soundIndex_ = (soundIndex_ + 1) % (int)soundHandles_.size();
        int h = soundHandles_[soundIndex_];
        if (h >= 0) {
            Audio::SetPosition(h, 0);
            Audio::Play(h, false);
        }
        UpdateTitle();
    }

    // JMidiList::Play: Music01..06, or (a native addition, as Hopmon Native's) its MP3/FLAC
    // replacement from the "music" folder when the music source allows it
    void PlayMusic(int track) {
        MusicStream::Close();
        Midi::Close();
        streaming_ = false;
        track_ = track;
        char midiName[16];
        std::snprintf(midiName, sizeof midiName, "Music%02d.mid", track);
        std::string file = MusicStream::FindReplacement(track - 1, midiName);
        if (!file.empty() && MusicStream::Open(file)) {
            streaming_ = true;
            Log("Music: track %d plays %s (%s)", track, file.c_str(), MusicStream::Describe().c_str());
            MusicStream::PlayFromStart();
            if (musicPaused_) MusicStream::Pause();
        } else {
            if (!file.empty()) Log("Music: track %d falls back to %s", track, midiName);
            std::string path = GameFiles::MusicPath(track);
            if (path.empty()) {
                Log("Music: %s not found next to MageBros.exe", midiName);
            } else if (Midi::Open(path)) {
                Midi::PlayFromStart();
                if (musicPaused_) Midi::Pause();
            }
        }
        UpdateTitle();
    }

    void UpdateTitle() {
        if (!GetWindow()) return;
        std::string name;
        if (!models_.empty())
            if (const auto* d = Assets::RCData(models_[modelIndex_])) {
                std::string text(d->begin(), d->begin() + std::min<size_t>(d->size(), 256));
                size_t m = text.find("Mesh ");
                if (m != std::string::npos) name = text.substr(m + 5, text.find_first_of(" {\r\n", m + 5) - (m + 5));
            }
        char title[512];
        if (mode_ == Mode::Game && !app_.InGame()) {
            std::snprintf(title, sizeof title, "Mage Bros. Native %s   [Tab viewer, F10 options]", MBN_VERSION_STRING);
            SDL_SetWindowTitle(GetWindow(), title);
            return;
        }
        if (mode_ == Mode::Game) {
            const XLevelView& lv = app_.Game().Level();
            if (!app_.OnePlayer())
                std::snprintf(title, sizeof title,
                              "Mage Bros. Native %s - World %d-%d, 2 players   [P1: arrows, R.Shift / ] jump, "
                              "R.Ctrl / [ fire; P2: WASD, L.Shift / 2 jump, L.Ctrl / 1 fire; Esc give up, "
                              "N/B level, F2 one player, F3 ending, Tab viewer, F10 options]",
                              MBN_VERSION_STRING, lv.World() + 1, lv.Level() + 1);
            else
                std::snprintf(title, sizeof title,
                              "Mage Bros. Native %s - World %d-%d   [arrows move, Shift / ] jump, Ctrl / [ fire, "
                              "Esc give up, N/B level, F2 two players, F3 ending, Tab viewer, F10 options]",
                              MBN_VERSION_STRING, lv.World() + 1, lv.Level() + 1);
            SDL_SetWindowTitle(GetWindow(), title);
            return;
        }
        if (levelMode_) {
            std::snprintf(title, sizeof title,
                          "Mage Bros. Native %s - %s   [Left/Right scroll, Up/Down zoom, PgUp/PgDn angle, "
                          "Shift fast, N/B next/prev level, Home reset, Tab models, F10 options]",
                          MBN_VERSION_STRING, levelView_.Summary().c_str());
            SDL_SetWindowTitle(GetWindow(), title);
            return;
        }
        std::snprintf(title, sizeof title,
                      "Mage Bros. Native %s - model %d/%zu: %u %s - sound %u - music %d   "
                      "[Left/Right model, Up/Down zoom, Space sound, 1-6 music, Tab levels, F10 options]",
                      MBN_VERSION_STRING, modelIndex_ + 1, models_.size(),
                      models_.empty() ? 0u : models_[modelIndex_], name.c_str(),
                      sounds_.empty() || soundIndex_ < 0 ? 0u : sounds_[soundIndex_], track_);
        SDL_SetWindowTitle(GetWindow(), title);
    }

    std::vector<uint32_t> models_, sounds_;
    std::vector<int> soundHandles_;
    std::unique_ptr<CD3DFile> mesh_;
    int modelIndex_ = 0, soundIndex_ = -1, track_ = 0;
    bool streaming_ = false;    // the current track is an MP3/FLAC replacement
    bool musicPaused_ = false;  // the window is in the background
    float angle_ = 0, zoom_ = 1.f, radius_ = 10.f;
    D3DVECTOR center_{0, 0, 0};
    XLevelView levelView_;
    XApplication app_;  // the original's screens: the logo, the title, the game (the rest: stage 6)
    UIDraw cheatsMark_;  // the CHEATS mark (its font is a GL texture: freed before the context)
    enum class Mode { Game, LevelViewer, ModelViewer };
    Mode mode_ = Mode::Game;  // Tab cycles game -> level viewer -> model viewer
    bool levelMode_ = false;
    bool startAtEnding_ = false;  // --ending
    float camZ_ = 160.0f, camDist_ = 340.0f, camPitch_ = 0.610865f;
};

void ShowFatal(const char* title, const char* message) {
    if (!SDL_getenv("MBN_HEADLESS")) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string basePath = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    // On macOS the bundle usually lives in /Applications, which a normal user cannot write to:
    // the ini and the log go to the per-user folder, beside the saves. Windows and Linux keep
    // them portable, next to the executable.
    std::string dataPath = basePath;
#if defined(__APPLE__)
    if (std::string user = Saves::UserFolder(); !user.empty()) dataPath = user;
#endif
    LogInit((dataPath + "magebros_native.log").c_str());
    SetFatalHook(ShowFatal);
    Log("Mage Bros. Native %s", MBN_VERSION_STRING);
    Config& cfg = GetConfig();
    bool loaded = cfg.Load(dataPath + "magebros_native.ini");
    if (!loaded) {
        cfg.path = dataPath + "magebros_native.ini";
        cfg.firstRun = true;  // the window is then sized to the monitor (CD3DApplication::Create)
    }
#if defined(MBN_GLES)
    // a fresh console install starts on the performance preset; an existing ini keeps its choice
    if (cfg.firstRun && cfg.graphicsProfile == 0) cfg.graphicsProfile = 1;
#endif
    cfg.ApplyGraphicsProfile();
    if (!loaded || cfg.version < Config::kCurrentVersion || cfg.gameVersion != MBN_VERSION_STRING) {
        // rewrite older files with the current comments, keeping their values, and files from
        // another build with this build's game_version
        cfg.Save();
    }
    Log("Graphics profile: %s (MSAA %d, anisotropy %d, render scale %d%%)",
        cfg.graphicsProfile == 1 ? "performance" : cfg.graphicsProfile == 2 ? "quality" : "manual", cfg.msaa,
        cfg.anisotropy, (int)std::lround(cfg.renderScale * 100));
    std::string gamePath = cfg.gamePath;
    bool allowUnverified = false, twoPlayer = false, ending = false;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == "--game" && i + 1 < args.size()) gamePath = args[++i];
        else if (args[i] == "--allow-unverified") allowUnverified = true;
        else if (args[i] == "--two-player") twoPlayer = true;
        else if (args[i] == "--ending") ending = true;  // test hook: start at the ending
        else if (args[i] == "--windowed") cfg.fullscreen = FullscreenMode::Windowed;
        else if (args[i] == "--fullscreen") cfg.fullscreen = FullscreenMode::Borderless;
        else if (args[i] == "--size" && i + 2 < args.size()) {
            cfg.windowWidth = atoi(args[++i].c_str());
            cfg.windowHeight = atoi(args[++i].c_str());
            cfg.firstRun = false;  // (a size given on the command line wins)
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        FatalError("Mage Bros. Native", "SDL initialisation failed: %s", SDL_GetError());
    Saves::Init();  // the save folder (the ini's save_path, or the per-user one)
    std::string err;
    if (!Assets::Init(gamePath, allowUnverified, &err)) FatalError("Mage Bros. Native", "%s", err.c_str());
    // the tool library's view of the same file (resource listing, level table, music files)
    if (!GameFiles::Open(Assets::ExePath(), "", true, &err)) FatalError("Mage Bros. Native", "%s", err.c_str());
    Mods::Init();
    Audio::Init();
    Audio::SetMasterVolumes(cfg.masterVolume / 100.f, cfg.sfxVolume / 100.f, cfg.musicVolume / 100.f);
    Midi::Init(cfg.soundFont);
    MusicStream::Init();
    // The SoundFont synth is mixed through Audio (which applies master/music volume); the Windows
    // synth bypasses the mixer, so it gets the combined volume directly.
    Midi::SetVolume(cfg.masterVolume / 100.f * cfg.musicVolume / 100.f);
    Log("Audio: master %d%%, sfx %d%%, music %d%% (music: %s)", cfg.masterVolume, cfg.sfxVolume,
        cfg.musicVolume, Midi::BackendName());
    Input::Init();

    int res = 0;
    {
        AssetViewerApp app(twoPlayer, ending);
        if (FAILED(app.Create(args)))
            FatalError("Mage Bros. Native",
#if defined(MBN_GLES)
                       "Could not create the game window (OpenGL ES 3.0 is required)."
#else
                       "Could not create the game window (OpenGL 3.3 is required)."
#endif
            );
        res = app.Run();
        // The model and bitmap caches own GL buffers and textures. Their static destructors
        // would run after SDL_Quit, i.e. without a context, which crashes the Mali driver.
        JModelList::Clear();
        CD3DFile::ClearCache();  // the parsed roots own MeshBuffers too
        JSurfaceList::Clear();
    }
    Log("Shutdown: application");
    Input::Shutdown();
    MusicStream::Shutdown();
    Midi::Shutdown();
    Audio::Shutdown();
    Log("Shutdown: SDL");
    SDL_Quit();
    Log("Shutdown: done");
    return res;
}
