#include "app_paths.h"
#include "log.h"
#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif

namespace {
std::string g_root;
bool g_resolved = false;

bool Exists(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info);
}

void EnsureFolder(const std::string& path) {
    if (Exists(path)) return;
    if (!SDL_CreateDirectory(path.c_str()))
        Log("Data: could not create %s (%s)", path.c_str(), SDL_GetError());
}
}  // namespace

namespace AppPaths {

std::string DataRoot() {
    if (g_resolved) return g_root;
    g_resolved = true;
#if defined(__ANDROID__)
    if (const char* external = SDL_GetAndroidExternalStoragePath()) {
        g_root = std::string(external) + "/MageBrosNative/";
    } else if (char* pref = SDL_GetPrefPath("", "MageBrosNative")) {
        g_root = pref;
        SDL_free(pref);
    }
    if (!g_root.empty()) {
        EnsureFolder(g_root);
        Log("Data folder: %s", g_root.c_str());
    } else {
        Log("Data folder: unavailable, using the program's folder");
    }
#endif
    return g_root;
}

std::string GameFolder() {
    const std::string root = DataRoot();
    if (root.empty()) return root;
    const std::string dir = root + "game/";
    EnsureFolder(dir);
    return dir;
}

void UnpackBundledBank() {
    const std::string root = DataRoot();
    if (root.empty()) return;
    const std::string dest = root + "TimGM6mb.sf2";
    if (Exists(dest)) return;
    size_t size = 0;
    void* data = SDL_LoadFile("TimGM6mb.sf2", &size);  // the APK's assets on Android
    if (!data) {
        Log("Bundled bank: TimGM6mb.sf2 is not in the package (%s)", SDL_GetError());
        return;
    }
    if (SDL_SaveFile(dest.c_str(), data, size))
        Log("Bundled bank: unpacked to %s", dest.c_str());
    else
        Log("Bundled bank: could not write %s (%s)", dest.c_str(), SDL_GetError());
    SDL_free(data);
}

}  // namespace AppPaths
