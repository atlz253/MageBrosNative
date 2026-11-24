#include "game_files.h"
#include "known_builds.h"
#include "log.h"
#include "sha256.h"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {

PEResources g_res;
bool g_open = false, g_verified = false;
std::string g_exePath, g_gameDir, g_label;

// Builds of MageBros.exe this port has been checked against are in known_builds.h.

// GetMapResId(world, level) of the original, as a table. Level 9 of each world is its boss level.
const uint16_t kLevelIds[GameFiles::kWorldCount][GameFiles::kLevelsPerWorld] = {
    {284, 277, 278, 279, 280, 281, 282, 283, 276, 265},
    {285, 286, 287, 288, 289, 290, 291, 292, 293, 294},
    {300, 301, 302, 303, 304, 305, 306, 307, 308, 309},
    {595, 597, 598, 599, 600, 601, 602, 603, 604, 575},
};

bool FileExists(const std::string& p) {
    FILE* f = OpenFileUtf8(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool IsDirSep(char c) { return c == '/' || c == '\\'; }

std::string DirOf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string("./") : p.substr(0, s + 1);
}

std::string Join(const std::string& dir, const char* name) {
    if (dir.empty()) return name;
    return IsDirSep(dir.back()) ? dir + name : dir + "/" + name;
}

}  // namespace

namespace GameFiles {

bool Open(const std::string& explicitPath, const std::string& programDir, bool allowUnverified,
          std::string* error) {
    std::vector<std::string> candidates;
    const char* names[] = {"MageBros.exe", "magebros.exe", "MAGEBROS.EXE"};
    if (!explicitPath.empty()) {
        candidates.push_back(explicitPath);
        for (const char* n : names) candidates.push_back(Join(explicitPath, n));
    }
    for (const std::string& dir : {programDir, std::string("./")})
        if (!dir.empty())
            for (const char* n : names) candidates.push_back(Join(dir, n));

    std::string found;
    for (const auto& c : candidates) {
        // an explicit folder is not a file: skip anything that won't open as one
        if (FileExists(c) && c.size() >= 4) {
            std::string tail = c.substr(c.size() - 4);
            for (char& ch : tail) ch = (char)std::tolower((unsigned char)ch);
            if (tail == ".exe") { found = c; break; }
        }
    }
    if (found.empty()) {
        if (error)
            *error = "MageBros.exe was not found. Put this program in the Mage Bros folder, or "
                     "start it with --game \"path\\to\\MageBros.exe\".";
        return false;
    }

    std::string err;
    if (!g_res.Load(found, &err)) {
        if (error) *error = found + ": " + err;
        return false;
    }

    const auto& bytes = g_res.FileBytes();
    std::string hash = Sha256Hex(bytes.data(), bytes.size());
    g_verified = false;
    g_label = "unknown build";
    for (const auto& kb : KnownBuilds::kBuilds)
        if (hash == kb.sha256) { g_verified = true; g_label = kb.label; }
    Log("Game executable: %s", found.c_str());
    Log("SHA-256: %s (%s)", hash.c_str(), g_label.c_str());
    if (!g_verified && !allowUnverified) {
        if (error)
            *error = "This MageBros.exe is not a build the port knows (SHA-256 " + hash +
                     "). Start with --allow-unverified to try it anyway.";
        return false;
    }

    g_exePath = found;
    g_gameDir = DirOf(found);
    g_open = true;
    return true;
}

bool IsOpen() { return g_open; }
bool IsVerified() { return g_verified; }
const std::string& ExePath() { return g_exePath; }
const std::string& GameDir() { return g_gameDir; }
const std::string& BuildLabel() { return g_label; }
const PEResources& Resources() { return g_res; }

const std::vector<uint8_t>* Bitmap(uint32_t id) { return g_res.Find(2u, id); }
const std::vector<uint8_t>* RCData(uint32_t id) { return g_res.Find(10u, id); }
const std::vector<uint8_t>* Wave(uint32_t id) { return g_res.Find(std::string("WAVE"), id); }

uint16_t LevelResourceId(int world, int level) {
    if (world < 0 || world >= kWorldCount || level < 0 || level >= kLevelsPerWorld) return 0;
    return kLevelIds[world][level];
}

std::string MusicPath(int track) {
    char name[16];
    std::snprintf(name, sizeof name, "Music%02d.mid", track);
    std::string p = g_gameDir + name;
    return FileExists(p) ? p : std::string();
}

std::vector<Entry> ListGameResources() {
    std::vector<Entry> out;
    for (const auto& kv : g_res.IntTyped()) {
        Kind k = Kind::Other;
        const auto& d = kv.second;
        if (kv.first.first == 2) k = Kind::Bitmap;
        else if (kv.first.first == 10) {
            if (d.size() >= 4 && std::memcmp(d.data(), "xof ", 4) == 0) k = Kind::Model;
            else if (d.size() >= 2 && d[1] == '|') k = Kind::Level;
        }
        out.push_back({k, kv.first.first, kv.first.second, &d});
    }
    for (const auto& kv : g_res.NameTyped())
        if (kv.first.first == "WAVE") out.push_back({Kind::Wave, 0, kv.first.second, &kv.second});
    return out;
}

const char* KindName(Kind k) {
    switch (k) {
        case Kind::Bitmap: return "bitmap";
        case Kind::Wave: return "sound";
        case Kind::Level: return "level";
        case Kind::Model: return "model";
        default: return "other";
    }
}

}  // namespace GameFiles
