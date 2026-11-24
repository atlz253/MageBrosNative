#include "midi.h"
#include "audio.h"
#include "../core/app_paths.h"
#include "../core/assets.h"
#include "../core/log.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>

#define TSF_IMPLEMENTATION
#include "../../third_party/tsf.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#endif

namespace {

struct Event {
    double time;       // seconds from start
    uint8_t status, d1, d2;
    std::vector<uint8_t> sysex;
};

struct Song {
    std::vector<Event> events;
    double length = 0;
};

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool ParseSMF(const std::vector<uint8_t>& d, Song* song) {
    if (d.size() < 14 || memcmp(d.data(), "MThd", 4) != 0) return false;
    uint32_t hlen = be32(&d[4]);
    uint16_t ntrk = be16(&d[10]);
    uint16_t div = be16(&d[12]);
    if (div & 0x8000) return false;  // SMPTE timing not used by these files
    struct Raw { uint64_t tick; int order; uint8_t st, a, b; std::vector<uint8_t> sx; uint32_t tempo; bool isTempo; };
    std::vector<Raw> raw;
    size_t p = 8 + hlen;
    int order = 0;
    for (int t = 0; t < ntrk && p + 8 <= d.size(); t++) {
        if (memcmp(&d[p], "MTrk", 4) != 0) break;
        uint32_t len = be32(&d[p + 4]);
        size_t q = p + 8, end = std::min(d.size(), q + len);
        uint64_t tick = 0;
        uint8_t running = 0;
        auto vlq = [&](uint32_t* out) {
            uint32_t v = 0;
            for (int i = 0; i < 4 && q < end; i++) {
                uint8_t c = d[q++];
                v = (v << 7) | (c & 0x7F);
                if (!(c & 0x80)) break;
            }
            *out = v;
        };
        while (q < end) {
            uint32_t delta;
            vlq(&delta);
            tick += delta;
            if (q >= end) break;
            uint8_t st = d[q];
            if (st & 0x80) q++;
            else st = running;
            if (st == 0xFF) {
                if (q >= end) break;
                uint8_t type = d[q++];
                uint32_t l;
                vlq(&l);
                if (type == 0x51 && l == 3 && q + 3 <= end) {
                    Raw r{tick, order++, 0, 0, 0, {}, (uint32_t)(d[q] << 16 | d[q + 1] << 8 | d[q + 2]), true};
                    raw.push_back(r);
                }
                q += l;
                if (type == 0x2F) break;
            } else if (st == 0xF0 || st == 0xF7) {
                uint32_t l;
                vlq(&l);
                Raw r{tick, order++, st, 0, 0, {}, 0, false};
                if (st == 0xF0) r.sx.push_back(0xF0);
                for (uint32_t i = 0; i < l && q + i < end; i++) r.sx.push_back(d[q + i]);
                raw.push_back(r);
                q += l;
            } else {
                running = st;
                uint8_t hi = st & 0xF0;
                int n = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                Raw r{tick, order++, st, 0, 0, {}, 0, false};
                if (q < end) r.a = d[q];
                if (n == 2 && q + 1 < end) r.b = d[q + 1];
                q += n;
                raw.push_back(r);
            }
        }
        p = 8 + p + len;
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
    double tempo = 500000.0, sec = 0;
    uint64_t lastTick = 0;
    for (auto& r : raw) {
        sec += (double)(r.tick - lastTick) * tempo / 1e6 / div;
        lastTick = r.tick;
        if (r.isTempo) {
            tempo = r.tempo;
            continue;
        }
        Event e{sec, r.st, r.a, r.b, std::move(r.sx)};
        song->events.push_back(std::move(e));
    }
    song->length = sec;
    return true;
}

// ---------------------------------------------------------------------------
std::mutex g_mutex;
Song g_song;
bool g_loaded = false;
bool g_playing = false;
double g_pos = 0;        // seconds
size_t g_next = 0;
std::atomic<bool> g_finished{false};
float g_volume = 1.f;

enum class Backend { None, System, Soft } g_backend = Backend::None;
bool g_usingBuiltin = false;  // the Soft backend is playing the bundled TimGM6mb bank
tsf* g_tsf = nullptr;

#if defined(_WIN32)
HMIDIOUT g_hmo = nullptr;
#elif defined(__APPLE__)
AUGraph g_graph = nullptr;
AUNode g_synthNode = 0, g_outputNode = 0;
AudioUnit g_synthUnit = nullptr;
#endif

// The system synthesizer plays outside our mixer, so music/master volume is applied by scaling
// each channel's volume controller (CC7). midiOutSetVolume is not used: the MIDI mapper usually
// rejects it, and where it works it changes the synth's volume for every program.
#if defined(_WIN32) || defined(__APPLE__)
std::thread g_thread;
std::atomic<bool> g_quit{false};
uint8_t g_chanVol[16] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};

uint8_t ScaledChannelVolume(uint8_t songValue) {
    // GM synths map CC7 to amplitude roughly as (cc/127)^2, so scale by sqrt(volume) to get a
    // linear amplitude change matching the sound-effect volume.
    float v = std::clamp(g_volume, 0.f, 1.f);
    int out = (int)(songValue * std::sqrt(v) + 0.5f);
    return (uint8_t)std::clamp(out, 0, 127);
}
#endif

#if defined(_WIN32) || defined(__APPLE__)
void SendSystemShort(uint8_t st, uint8_t a, uint8_t b) {
#if defined(_WIN32)
    if (g_hmo) midiOutShortMsg(g_hmo, (DWORD)st | (DWORD)a << 8 | (DWORD)b << 16);
#else
    if (g_synthUnit) MusicDeviceMIDIEvent(g_synthUnit, st, a, b, 0);
#endif
}
#endif

void SendShort(uint8_t st, uint8_t a, uint8_t b) {
    if (g_backend == Backend::Soft && g_tsf) {
        int ch = st & 0x0F;
        switch (st & 0xF0) {
            case 0x80: tsf_channel_note_off(g_tsf, ch, a); break;
            case 0x90: tsf_channel_note_on(g_tsf, ch, a, b / 127.f); break;
            case 0xB0: tsf_channel_midi_control(g_tsf, ch, a, b); break;
            case 0xC0: tsf_channel_set_presetnumber(g_tsf, ch, a, ch == 9); break;
            case 0xE0: tsf_channel_set_pitchwheel(g_tsf, ch, a | (b << 7)); break;
        }
        return;
    }
#if defined(_WIN32) || defined(__APPLE__)
    if (g_backend != Backend::System) return;
    if ((st & 0xF0) == 0xB0 && a == 7) {
        // Channel volume: remember the song's value and send it scaled by our volume.
        g_chanVol[st & 0x0F] = b;
        b = ScaledChannelVolume(b);
    }
    SendSystemShort(st, a, b);
    if ((st & 0xF0) == 0xB0 && a == 121) {
        // Some synths reset channel volume on "reset all controllers": re-apply ours.
        uint8_t ch = st & 0x0F;
        SendSystemShort((uint8_t)(0xB0 | ch), 7, ScaledChannelVolume(g_chanVol[ch]));
    }
#endif
}

#if defined(_WIN32) || defined(__APPLE__)
// A GM/GS/XG reset puts every channel back to volume 100 at full scale.
bool IsSynthReset(const std::vector<uint8_t>& sx) {
    bool gmReset = sx.size() >= 5 && sx[1] == 0x7E && sx[3] == 0x09;
    bool gsReset = sx.size() >= 9 && sx[1] == 0x41 && sx[3] == 0x42 && sx[5] == 0x40 && sx[6] == 0x00 && sx[7] == 0x7F;
    bool xgReset = sx.size() >= 8 && sx[1] == 0x43 && sx[3] == 0x4C && sx[4] == 0x00 && sx[5] == 0x00 && sx[6] == 0x7E;
    return gmReset || gsReset || xgReset;
}
#endif

void SendSysex(const std::vector<uint8_t>& sx) {
#if defined(_WIN32) || defined(__APPLE__)
    if (g_backend != Backend::System || sx.empty()) return;
#if defined(_WIN32)
    {
        std::vector<uint8_t> buf = sx;
        MIDIHDR h{};
        h.lpData = (LPSTR)buf.data();
        h.dwBufferLength = h.dwBytesRecorded = (DWORD)buf.size();
        if (midiOutPrepareHeader(g_hmo, &h, sizeof h) == MMSYSERR_NOERROR) {
            midiOutLongMsg(g_hmo, &h, sizeof h);
            for (int i = 0; i < 200 && !(h.dwFlags & MHDR_DONE); i++) SDL_Delay(1);
            midiOutUnprepareHeader(g_hmo, &h, sizeof h);
        }
    }
#else
    if (g_synthUnit) {
        // MusicDeviceSysEx wants a complete message with both markers. The parser keeps the
        // file's bytes, and some files carry F0 without a trailing F7.
        std::vector<uint8_t> msg;
        msg.reserve(sx.size() + 2);
        if (sx.front() != 0xF0) msg.push_back(0xF0);
        msg.insert(msg.end(), sx.begin(), sx.end());
        if (msg.back() != 0xF7) msg.push_back(0xF7);
        MusicDeviceSysEx(g_synthUnit, msg.data(), (UInt32)msg.size());
    }
#endif
    // Re-apply our volume after the reset flattened every channel to 100.
    if (IsSynthReset(sx))
        for (int ch = 0; ch < 16; ch++) SendShort((uint8_t)(0xB0 | ch), 7, 100);
#else
    (void)sx;
#endif
}

void AllNotesOff() {
    for (int ch = 0; ch < 16; ch++) {
        SendShort((uint8_t)(0xB0 | ch), 123, 0);
        SendShort((uint8_t)(0xB0 | ch), 120, 0);
        SendShort((uint8_t)(0xB0 | ch), 64, 0);
    }
    if (g_backend == Backend::Soft && g_tsf) tsf_note_off_all(g_tsf);
}

void ResetControllers() {
    for (int ch = 0; ch < 16; ch++) {
        SendShort((uint8_t)(0xB0 | ch), 121, 0);
        SendShort((uint8_t)(0xE0 | ch), 0, 0x40);
    }
    if (g_backend == Backend::Soft && g_tsf) {
        for (int ch = 0; ch < 16; ch++) {
            tsf_channel_set_presetnumber(g_tsf, ch, 0, ch == 9);
            tsf_channel_set_volume(g_tsf, ch, 100.f / 127.f);
        }
    }
}

// Dispatch all events up to g_pos. Caller holds g_mutex.
void DispatchUpTo(double t) {
    while (g_next < g_song.events.size() && g_song.events[g_next].time <= t) {
        const Event& e = g_song.events[g_next++];
        if (!e.sysex.empty()) SendSysex(e.sysex);
        else SendShort(e.status, e.d1, e.d2);
    }
    if (g_next >= g_song.events.size() && t >= g_song.length) {
        g_playing = false;
        g_finished = true;
        AllNotesOff();
    }
}

void SoftRender(float* out, int frames, int rate) {
    // Called from the audio thread with Audio's lock held; we only take our own lock.
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_tsf) return;
    const int block = 64;
    int done = 0;
    while (done < frames) {
        int n = std::min(block, frames - done);
        if (g_playing) {
            DispatchUpTo(g_pos);
            g_pos += (double)n / rate;
        }
        tsf_render_float(g_tsf, out + done * 2, n, 1);
        done += n;
    }
}

#if defined(_WIN32) || defined(__APPLE__)
void SystemThread() {
#if defined(_WIN32)
    timeBeginPeriod(1);
#endif
    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t last = SDL_GetPerformanceCounter();
    while (!g_quit) {
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            uint64_t now = SDL_GetPerformanceCounter();
            double dt = (double)(now - last) / freq;
            last = now;
            if (g_playing) {
                g_pos += dt;
                DispatchUpTo(g_pos);
            }
        }
        SDL_Delay(1);
    }
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}
#endif

#if defined(__APPLE__)
// The platform synthesizer: the DLS synth playing through the default output unit. It is the
// macOS counterpart of the Windows MIDI mapper and plays outside the game's mixer, like it.
bool StartSystemSynth(std::string* error) {
    auto fail = [&](const char* what, OSStatus st) {
        *error = std::string(what) + " (OSStatus " + std::to_string((int)st) + ")";
        return false;
    };
    OSStatus st = NewAUGraph(&g_graph);
    if (st != noErr) return fail("NewAUGraph failed", st);
    AudioComponentDescription synth{};
    synth.componentType = kAudioUnitType_MusicDevice;
    synth.componentSubType = kAudioUnitSubType_DLSSynth;
    synth.componentManufacturer = kAudioUnitManufacturer_Apple;
    st = AUGraphAddNode(g_graph, &synth, &g_synthNode);
    if (st != noErr) return fail("the DLS synthesizer is not available", st);
    AudioComponentDescription output{};
    output.componentType = kAudioUnitType_Output;
    output.componentSubType = kAudioUnitSubType_DefaultOutput;
    output.componentManufacturer = kAudioUnitManufacturer_Apple;
    st = AUGraphAddNode(g_graph, &output, &g_outputNode);
    if (st != noErr) return fail("the default audio output is not available", st);
    st = AUGraphOpen(g_graph);
    if (st != noErr) return fail("AUGraphOpen failed", st);
    st = AUGraphNodeInfo(g_graph, g_synthNode, nullptr, &g_synthUnit);
    if (st != noErr) return fail("AUGraphNodeInfo failed", st);
    st = AUGraphConnectNodeInput(g_graph, g_synthNode, 0, g_outputNode, 0);
    if (st != noErr) return fail("AUGraphConnectNodeInput failed", st);
    st = AUGraphInitialize(g_graph);
    if (st != noErr) return fail("AUGraphInitialize failed", st);
    st = AUGraphStart(g_graph);
    if (st != noErr) return fail("AUGraphStart failed", st);
    return true;
}

void StopSystemSynth() {
    if (!g_graph) return;
    AUGraphStop(g_graph);
    AUGraphUninitialize(g_graph);
    AUGraphClose(g_graph);
    DisposeAUGraph(g_graph);
    g_graph = nullptr;
    g_synthUnit = nullptr;
}
#endif

}  // namespace

namespace Midi {

std::vector<SoundFontDir> SoundFontDirs() {
    std::vector<SoundFontDir> out;
    std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    std::string game = Assets::GameDir();
    if (game == "./" || game == "." || game == ".\\") {
        // The game is in the program folder. Assets::GameDir() kept the caller's relative
        // spelling; resolve it so the folder is not listed twice (and does not make a second
        // menu entry for every .sf2).
        const char* cwd = SDL_GetCurrentDirectory();
        std::string current = cwd ? cwd : "";
        SDL_free((void*)cwd);
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
            return s;
        };
        if (!current.empty() && trim(current) == trim(base)) game.clear();
    }
    auto add = [&](const std::string& dir, const std::string& name) {
        if (dir.empty()) return;
        for (const SoundFontDir& d : out)
            if (d.dir == dir) return;
        out.push_back({dir, name});
    };
#if defined(__ANDROID__)
    // The unpacked bundled bank (and any user .sf2 imported) lives in the app's data root, and
    // is preferred over the copy inside the APK.
    add(AppPaths::DataRoot(), "");
#endif
    add(base, "");
    add(game, game);
#if defined(__APPLE__)
    // In a bundle SDL_GetBasePath is Contents/Resources; also accept a bank beside the executable.
    if (!base.empty()) add(base + "../MacOS/", "../MacOS/");
#endif
    // then a "soundfonts" folder in each, matched case-insensitively (as the music folder)
    for (const std::string& parent : {base, game}) {
        if (parent.empty()) continue;
        int n = 0;
        char** names = SDL_GlobDirectory(parent.c_str(), "soundfonts", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; i < n; i++) {
            std::string d = parent + names[i];
            SDL_PathInfo info;
            if (SDL_GetPathInfo(d.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
                add(d + "/", parent == base ? std::string(names[i]) + "/" : d + "/");
        }
        SDL_free(names);
    }
    return out;
}

namespace {

std::string BasePath() {
    // SDL caches this string and returns the same pointer on later calls; it must not be freed.
    const char* p = SDL_GetBasePath();
    return p ? p : "";
}

bool FileExists(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::string FindUserSoundFont() {
    // automatic: soundfont.sf2, then magebros.sf2, each next to the program, next to MageBros.exe,
    // then in their "soundfonts" folders
    for (const char* cand : {"soundfont.sf2", "magebros.sf2"}) {
        for (const SoundFontDir& d : SoundFontDirs()) {
            std::string p = d.dir + cand;
            if (FileExists(p)) return p;
        }
    }
    return "";
}

std::string FindBuiltinSoundFont() {
    // The packaged bank is called TimGM6mb.sf2: it must not be confused with a player's
    // soundfont.sf2, which is the automatic user SoundFont.
    for (const SoundFontDir& d : SoundFontDirs()) {
        std::string p = d.dir + "TimGM6mb.sf2";
        if (FileExists(p)) return p;
    }
    return "";
}

bool LoadSoftSynth(const std::string& path, bool builtin) {
    // loaded through SDL, which takes UTF-8 paths on every system
    size_t n = 0;
    void* data = SDL_LoadFile(path.c_str(), &n);
    g_tsf = data ? tsf_load_memory(data, (int)n) : nullptr;
    SDL_free(data);
    if (!g_tsf) {
        Log("MIDI: failed to load SoundFont %s", path.c_str());
        return false;
    }
    tsf_set_output(g_tsf, TSF_STEREO_INTERLEAVED, Audio::OutputRate(), 0.0f);
    tsf_set_max_voices(g_tsf, 256);
    g_backend = Backend::Soft;
    g_usingBuiltin = builtin;
    Audio::SetMusicRenderer(SoftRender);
    if (builtin) Log("MIDI: using the built-in TimGM6mb bank with %s", path.c_str());
    else Log("MIDI: using SoundFont %s", path.c_str());
    return true;
}

bool TrySystemSynth() {
#if defined(_WIN32)
    if (midiOutOpen(&g_hmo, MIDI_MAPPER, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR) {
        g_backend = Backend::System;
        g_quit = false;
        g_thread = std::thread(SystemThread);
        Log("MIDI: using the Windows system synthesizer");
        return true;
    }
    Log("MIDI: the Windows system synthesizer could not start");
#elif defined(__APPLE__)
    std::string why;
    if (StartSystemSynth(&why)) {
        g_backend = Backend::System;
        g_quit = false;
        g_thread = std::thread(SystemThread);
        Log("MIDI: using the macOS system synthesizer");
        return true;
    }
    StopSystemSynth();
    Log("MIDI: the macOS system synthesizer could not start: %s", why.c_str());
#endif
    return false;
}

}  // namespace

bool Init(const std::string& soundFontPath) {
    g_backend = Backend::None;
    g_usingBuiltin = false;

    if (soundFontPath == "system") {
        if (TrySystemSynth()) return true;
        Log("MIDI: the system synthesizer is unavailable (explicit choice)");
        return false;
    }
    if (soundFontPath == "builtin") {
        std::string sf = FindBuiltinSoundFont();
        if (!sf.empty() && LoadSoftSynth(sf, true)) return true;
        Log("MIDI: the built-in TimGM6mb bank is missing or unreadable");
        return false;
    }
    if (!soundFontPath.empty()) {
        std::string sf = soundFontPath;
        bool absolute = sf[0] == '/' || sf[0] == '\\' || (sf.size() > 1 && sf[1] == ':');
        if (!absolute) sf = BasePath() + sf;
        if (LoadSoftSynth(sf, false)) return true;
        Log("MIDI: the selected SoundFont is unavailable; music stays silent");
        return false;
    }

    // Automatic: a player's SoundFont is an intentional override, then the system synthesizer,
    // then the bundled bank.
    std::string sf = FindUserSoundFont();
    if (!sf.empty() && LoadSoftSynth(sf, false)) return true;
    if (TrySystemSynth()) return true;
    sf = FindBuiltinSoundFont();
    if (!sf.empty() && LoadSoftSynth(sf, true)) return true;

    Log("MIDI: no synthesizer available (no system synthesizer, user SoundFont or built-in bank)");
    return false;
}

std::string BuiltinSoundFontPath() { return FindBuiltinSoundFont(); }

static void ShutdownBackend() {
#if defined(_WIN32) || defined(__APPLE__)
    if (g_backend == Backend::System) {
        g_quit = true;
        if (g_thread.joinable()) g_thread.join();
#if defined(_WIN32)
        midiOutReset(g_hmo);
        midiOutClose(g_hmo);
        g_hmo = nullptr;
#else
        StopSystemSynth();
#endif
    }
#endif
    Audio::SetMusicRenderer(nullptr);
    if (g_tsf) tsf_close(g_tsf);
    g_tsf = nullptr;
    g_usingBuiltin = false;
    g_backend = Backend::None;
}

void Shutdown() {
    Close();
    ShutdownBackend();
}

bool SwitchSynth(const std::string& soundFontPath) {
    // Keep the song and its position, swap the synthesizer underneath, then replay the song's
    // controller/program state up to the current position so instruments and volumes match.
    Song song;
    bool loaded, playing;
    double pos;
    size_t next;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_playing) AllNotesOff();
        song = std::move(g_song);
        loaded = g_loaded;
        playing = g_playing;
        pos = g_pos;
        next = g_next;
        g_playing = false;
        g_loaded = false;
        g_song = Song();
    }
    ShutdownBackend();
    bool ok = Init(soundFontPath);
    std::lock_guard<std::mutex> lk(g_mutex);
    g_song = std::move(song);
    g_loaded = loaded;
    g_pos = pos;
    g_next = next;
    if (loaded) {
        ResetControllers();
        for (size_t i = 0; i < next && i < g_song.events.size(); i++) {
            const Event& e = g_song.events[i];
            if (!e.sysex.empty()) SendSysex(e.sysex);
            else if ((e.status & 0xF0) == 0xB0 || (e.status & 0xF0) == 0xC0 || (e.status & 0xF0) == 0xE0)
                SendShort(e.status, e.d1, e.d2);
        }
    }
    g_playing = playing;
    return ok;
}

bool Open(const std::string& path) {
    std::vector<uint8_t> bytes;
    Song song;
    bool ok = Assets::ReadFile(path, &bytes) && ParseSMF(bytes, &song);
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_playing) AllNotesOff();
    g_playing = false;
    g_finished = false;
    g_loaded = ok;
    g_song = ok ? std::move(song) : Song();
    g_pos = 0;
    g_next = 0;
    if (!ok) Log("MIDI: could not load %s", path.c_str());
    return ok;
}

void PlayFromStart() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_loaded) return;
    AllNotesOff();
    ResetControllers();
#if defined(_WIN32) || defined(__APPLE__)
    for (auto& c : g_chanVol) c = 100;  // GM default until the song sets its own
#endif
    for (int ch = 0; ch < 16; ch++) SendShort((uint8_t)(0xB0 | ch), 7, 100);
    g_pos = 0;
    g_next = 0;
    g_finished = false;
    g_playing = true;
}

void Pause() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_playing) AllNotesOff();
    g_playing = false;
}

void Resume() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (!g_loaded) return;
    if (g_next >= g_song.events.size()) {  // MCI: playing at end restarts nothing; behave like "from current"
        g_pos = 0;
        g_next = 0;
    }
    g_playing = true;
}

void Close() {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_playing) AllNotesOff();
    g_playing = false;
    g_loaded = false;
    g_song = Song();
    g_pos = 0;
    g_next = 0;
}

bool IsLoaded() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_loaded;
}

bool PollFinished() { return g_finished.exchange(false); }

void SetVolume(float v) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_volume = v;
#if defined(_WIN32) || defined(__APPLE__)
    if (g_backend == Backend::System)
        for (int ch = 0; ch < 16; ch++) SendShort((uint8_t)(0xB0 | ch), 7, g_chanVol[ch]);
#endif
}

const char* BackendName() {
    switch (g_backend) {
        case Backend::System:
#if defined(_WIN32)
            return "Windows system synth";
#elif defined(__APPLE__)
            return "macOS system synth";
#else
            return "system synth";
#endif
        case Backend::Soft:
            return g_usingBuiltin ? "Built-in synth (TimGM6mb)" : "SoundFont synth";
        default: return "unavailable";
    }
}

}  // namespace Midi
