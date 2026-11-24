// The Android import's file-name rules (no game files needed).
#include "core/import_names.h"
#include <cstdio>
#include <string>

static int g_fail = 0;
#define CHECK(c)                                                                 \
    do {                                                                         \
        if (!(c)) {                                                              \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);    \
            g_fail++;                                                            \
        }                                                                        \
    } while (0)

static void Expect(const char* input, const char* expected) {
    std::string got = ImportNames::TargetName(input);
    if (got != expected) {
        std::printf("TargetName(\"%s\") = \"%s\", expected \"%s\"\n", input, got.c_str(), expected);
        g_fail++;
    }
}

int main() {
    Expect("MageBros.exe", "MageBros.exe");
    Expect("magebros.EXE", "MageBros.exe");
    Expect("  MageBros.exe  ", "MageBros.exe");
    Expect("MageBros.pdb", "");

    Expect("Music01.mid", "Music01.mid");
    Expect("music1.mid", "Music01.mid");
    Expect("MUSIC6.MID", "Music06.mid");
    Expect("Music 3.mid", "Music03.mid");
    Expect("Music07.mid", "");
    Expect("Music10.mid", "");
    Expect("Music01.mp3", "");

    Expect("soundfont.sf2", "soundfont.sf2");
    Expect("My Bank.SF2", "My Bank.SF2");
    Expect("../evil.sf2", ".._evil.sf2");
    Expect("readme.txt", "");
    Expect("", "");

    if (g_fail) {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("import name tests passed\n");
    return 0;
}
