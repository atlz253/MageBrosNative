#include "import_names.h"
#include <cctype>

namespace {

std::string Trim(const std::string& s) {
    size_t first = 0, last = s.size();
    while (first < last && (unsigned char)s[first] <= ' ') first++;
    while (last > first && (unsigned char)s[last - 1] <= ' ') last--;
    return s.substr(first, last - first);
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool EndsWith(const std::string& s, const char* tail) {
    size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

}  // namespace

namespace ImportNames {

std::string TargetName(const std::string& displayName) {
    const std::string name = Trim(displayName);
    const std::string lower = Lower(name);
    if (lower == "magebros.exe") return "MageBros.exe";

    // MusicNN.mid, accepting any capitalisation and an optional leading zero and spaces.
    if (lower.compare(0, 5, "music") == 0) {
        size_t i = 5;
        while (i < lower.size() && (lower[i] == ' ' || lower[i] == '\t')) i++;
        if (i < lower.size() && lower[i] == '0') i++;
        if (i + 5 == lower.size() && lower[i] >= '1' && lower[i] <= '6' &&
            lower.compare(i + 1, 4, ".mid") == 0)
            return std::string("Music0") + lower[i] + ".mid";
        return "";
    }

    if (EndsWith(lower, ".sf2")) {
        std::string safe = name;
        for (char& c : safe)
            if (c == '/' || c == '\\') c = '_';
        return safe;
    }
    return "";
}

}  // namespace ImportNames
