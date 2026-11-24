// Builds of MageBros.exe this port has been checked against, and their SHA-256. Shared by the
// asset loader, the game-file check and the Android import's verification step.
#pragma once
#include <string>

namespace KnownBuilds {

struct Build {
    const char* sha256;
    const char* label;
};

inline constexpr Build kBuilds[] = {
    {"ac191080af002b8ad0615e432faa92a2a47ce76a9aaa667bacf628836c098c49",
     "Mage Bros. 2.0 (2022 rebuild, freeware)"},
};

inline const Build* Find(const std::string& hash) {
    for (const Build& b : kBuilds)
        if (hash == b.sha256) return &b;
    return nullptr;
}

}  // namespace KnownBuilds
