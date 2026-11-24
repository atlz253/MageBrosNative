<p align="center"><img src="docs/banner.png" alt="Mage Bros. Native" width="100%"></p>

# Mage Bros. Native

An unofficial native port of **Mage Bros.**, the 3D platformer by Saito Games (2003–2009), for
modern Windows, macOS and Linux. It plays exactly like the original, with optional extras:
widescreen, any resolution and refresh rate, smooth motion, key rebinding and cheats.

**Version 0.4.47.** The whole game is in: all 4 worlds and 40 levels, every enemy and boss, one
or two players, the shop, saving and the ending. What changed in each version:
[CHANGELOG.md](CHANGELOG.md).

> **You need your own copy of the original game.** This project contains none of the game's
> files: everything is read from your `MageBros.exe` and its music files when you play.

## What Native adds

The game itself is the original's: the same levels, enemies, physics, timing and even its quirks.
Around it, Native adds:

- **Runs natively on Windows 10 and 11, macOS and Linux** with OpenGL instead of the original's
  DirectX 7: no compatibility settings or wrappers needed.
- **Music that works out of the box:** your own SoundFont when you place one next to the game,
  otherwise the system synthesizer, with a built-in TimGM6mb General MIDI bank as the fallback
  (and the default on Linux); the Music synth setting chooses any of them explicitly.
- **Any resolution and screen mode:** a resizable window, borderless or exclusive fullscreen, any
  monitor, instead of the original's fixed 640 × 480. The window opens where you left it.
- **Widescreen** (16:9, 21:9 and wider): see more of each level at the sides, with nothing
  popping in at the edges and the sound panned to match. On the widest screens (21:9, 32:9)
  every enemy in view moves, even at the far edges. The HUD can move to the screen's edges.
- **High refresh rates:** Smooth motion draws the game between its steps for smooth movement at
  144 Hz and beyond, and VSync, adaptive VSync and a frame limit are built in. The game's speed
  and timing never change with the frame rate.
- **Sharper graphics, all optional:** antialiasing, texture filtering, higher render resolution,
  smoother textures and a brightness setting; and new effects, all off by default: ambient
  occlusion (soft shading in corners and under objects), real shadows, light from the flames and
  shots, and a glow round them (bloom).
- **An in-game options menu** (F10) for every setting, with changes applied at once. It shows the
  game's version at the top right.
- **Statistics Overlay** (F9): a see-through panel with the frame rate, the game's timing (with
  graphs), the display settings in effect, your graphics card, the graphics effects in use
  and what each costs it. Off, Compact or Full; stays as you left it.
- **Menu font:** the Classic pixel font, or a font of your own, sharp at any resolution
  ([how](#your-own-menu-font)).
- **Full controls:** rebind every key for both players, and use modern gamepads.
- **Replacement music:** play MP3 or FLAC versions of the six tracks instead of MIDI, or the MIDI
  through a SoundFont of your choice ([how](#your-own-music)).
- **Cheats:** twelve of them, from Unlock all levels to Moon jump, all off by default.
- **Saves you can back up:** your progress is kept in a plain file under `%APPDATA%`.
- **A few fixes:** small slips of the original, such as overlapping messages in two-player games,
  are fixed; each one is listed in [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md).

## Getting started

1. Get **Mage Bros. 2.0**, the freeware version, from [saitogames.com](http://www.saitogames.com/).
2. Download the latest `MageBrosNative-<version>-windows-x64.zip`,
   `...-macos-arm64.zip` or `...-linux-x86_64.tar.gz` from
   [Releases](https://github.com/Exo-MDR-CD2000/MageBrosNative/releases).
3. Windows and macOS: unzip and put `MageBrosNative.exe` (or the `.app`) in the same folder as
   `MageBros.exe` and its `Music01.mid` to `Music06.mid` files. Linux: unpack the tarball into a
   writable folder and keep `MageBrosNative` beside those files.
4. Run `MageBrosNative.exe` (`MageBrosNative` on Linux).

**`MageBrosNative.exe` is the only file you need to play.** The rest of the package is optional:

| In the package | What it is |
|---|---|
| `MageBrosAssets.exe` | A tool that lists, checks or exports the resources in your `MageBros.exe` |
| `mbn_tests.exe`, `mbn_soundfont_tests.exe`, `mbn_collision_tests.exe` | The project's automated tests |
| `TimGM6mb.sf2` | The built-in General MIDI bank, used on Linux and as the fallback or explicit choice on Windows and macOS; see [Your own music](#your-own-music) |
| `README.md`, `docs` | This guide, and documents for developers |
| `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses` | The licences of the port and of the libraries built into it |

You can leave these in the package. If you pass the game on to someone else, include the licence
files with it.

You need 64-bit Windows 10 or 11, macOS 12 or newer (Apple Silicon), or a Debian 13+ desktop
(OpenGL 3.3, X11 or Wayland). The game checks that `MageBros.exe` is the 2.0 freeware build
(SHA-256 `ac191080af002b8ad0615e432faa92a2a47ce76a9aaa667bacf628836c098c49`); other builds are
refused unless you start it with `--allow-unverified`. If the game is somewhere else, start it
with `--game "C:\path\to\MageBros.exe"` or set `path` in the settings file. Linux players
unpack the tarball into a writable folder and run `./MageBrosNative`; the steps are under
[For developers](#for-developers).

### Android

The APK contains none of the game. On first start it explains what is needed and asks for the
folder that holds your `MageBros.exe` and `Music01.mid` to `Music06.mid`. It copies only those
files and an optional `.sf2` into the app's own storage; the folder you chose is left untouched.
The executable is checked against the supported 2.0 build, and an unknown one is refused unless
you choose to start it anyway.

The game files can be replaced later from **Options > Game files**; the next start uses them.
They live with the configuration and saves under
`Android/data/com.magebros.game/files/MageBrosNative/` on the phone's storage.

The game pauses its music and drawing in the background and returns where it was; the screen
stays awake while you play. The system **Back** action opens the options menu during a level,
closes it when the menu is open, and leaves the app from the title screen. Android has one screen,
so **Options > Video** hides the monitor, resolution, refresh rate, display mode and VSync rows;
the graphics, sound and frame-limit settings remain.

## Controls

| | Player 1 | Player 2 |
|---|---|---|
| Move | Arrow keys | W A S D |
| Jump | Right Shift or `]` | Left Shift or `2` |
| Fire | Right Ctrl or `[` | Left Ctrl or `1` |
| Gamepad | first gamepad | second gamepad |

In a one-player game, Left Shift and Left Ctrl also jump and fire. Every key can be changed in
Options > Controls.

| Key | Action |
|---|---|
| P | Pause |
| F9 | Statistics Overlay: off, compact, full |
| F10 or gamepad Start | Options menu |
| Escape | Give up the level (you lose some gold, as in the original) |
| Alt+Enter or F11 | Switch between window and fullscreen |
| F12 | Screenshot (saved next to the program) |

**How to play:** light every torch in a level by jump-firing over its post, then stand on the goal
to warp to the next level. Shots cost magic, which refills over time. Defeated enemies drop coins;
spend them in the shops (levels 3, 6 and 9 of worlds 1 to 3, and 4-2, 4-5 and 4-8). World 4 opens
once worlds 1 to 3 are cleared.

**On a touch screen** the game shows a fixed stick at the lower left, Jump and Fire at the lower right and Start at
the bottom centre, at the very bottom edge. Fire confirms in the menus, Jump cancels, and Start opens and closes the options
menu. In the options menu the stick steps once per push along one axis and does not auto-repeat;
diagonal pushes are ignored.
**Options > Controls > Touch controls** is Off, Automatic or On; Automatic shows them on a
touch screen. While a physical gamepad is connected the on-screen controls hide and the gamepad
controls player 1; they return when it is disconnected. On a desktop build, `--touch` shows the
controls and the mouse acts as one finger (a test harness).

## Options

Press F10 (or Start) to open the options menu. With every option at its default the game looks
and plays like the original, apart from smoother edges and distant textures (Antialiasing and
Texture filtering, both on by default; turn them off for the original's exact picture).

- **Video:** window, borderless or exclusive fullscreen; resolution and refresh rate; VSync; a
  frame limit, including your monitor's refresh rate and its half, third and quarter, which keep
  movement even; Low latency; **Smooth motion** (off by default), which draws the game between its 100 steps a
  second so it moves smoothly on high refresh rate monitors, without changing the game's speed;
  **Aspect** (4:3, Stretch or Widescreen, which shows more of each level at the sides); HUD
  position and size; and **Field of view**, which is **experimental: we recommend leaving it at
  100%**, the view the original's levels were made for.
- **Graphics:** antialiasing, texture filtering, render scale (up to 200%), smoother textures,
  ambient occlusion, dynamic shadows and dynamic lights (each with its strength), the ambient
  light with dynamic lights on, bloom, and brightness. The new effects are all off by default:
  with them off the game looks as before.
- **Audio:** volume levels, where the music comes from, and the MIDI synth. See
  [Your own music](#your-own-music).
- **Menu font** (on the Video page): the font of the menus and the Statistics Overlay. See below.
- **Controls:** rebind both players' keys and the gamepad buttons.

### Your own music

The game plays its six original MIDI tracks. You can replace any of them with MP3 or FLAC files of
your own, for example a recording or a remix.

1. Make a folder called `music` next to `MageBros.exe`.
2. Put your files in it, named after the track they replace: `Music01.mp3` (or `Music01.flac`)
   replaces `Music01.mid`, and so on up to `Music06`. You can replace one track or all six.
3. Start the game. Options (F10) > Audio > **Music source** must be **Auto** (the default).

| Track | Plays on |
|---|---|
| Music01 | The title, save slots, level select and the ending |
| Music02 to Music05 | The levels of worlds 1 to 4 |
| Music06 | Boss levels |

A track without a file of its own plays its MIDI as usual. If both an MP3 and a FLAC are there,
the FLAC is used. Your music plays, loops, stops and pauses just like the original's. To make a
track loop seamlessly from a point other than its start, give the file `LOOPSTART` and
`LOOPLENGTH` (or `LOOPEND`) tags, in samples, as many game-music files already have; without them
it loops from the end back to the start. To use other file names, set `music_1` to `music_6` in
the settings file. To hear the original music again, set Music source to **MIDI only**.

The original MIDI music plays through a synthesizer. **Automatic** (the default) uses a user
SoundFont first: a file called `soundfont.sf2` or `magebros.sf2` next to the program or
`MageBros.exe`, or in a `soundfonts` folder in either place. Placing it there is an intentional
choice, so it takes precedence. With no such file the game uses the system synthesizer: the
Windows MIDI synth the original used, or the macOS DLS synth. If the system synthesizer cannot
start, the game uses the built-in **TimGM6mb** General MIDI bank. Linux has no system
synthesizer, so it uses a user SoundFont and then the built-in bank.

The original tracks were authored for **Yamaha XG**. The project does not include a Yamaha XG
bank because its samples cannot be redistributed; the bundled TimGM6mb bank is General MIDI. If
you have a legally obtained XG bank, you can use it as a user `.sf2`; the engine does not
reproduce Yamaha XG SysEx effects.

**Better-sounding MIDI instead:** a General MIDI SoundFont (`.sf2`) changes the instruments the
MIDI music is played with.

1. Put the `.sf2` file next to `MageBros.exe`, or keep them tidy in a folder called `soundfonts`
   next to it.
2. In the game, open Options (F10) > Audio > **Music synth** and press Left or Right to choose
   **Built-in** (the TimGM6mb bank) or your file. The music switches at once and the choice is
   remembered.

**System synth** always uses the platform's synthesizer. **Built-in** uses the TimGM6mb bank that
comes with the package. On Linux the system synthesizer is not offered; Automatic uses a user
file and then the built-in bank. The built-in bank is GPL-2.0; its licence travels in
`licenses/`.

### Your own menu font

The menus, the Statistics Overlay and the CHEATS mark use the game's Classic pixel font. You can
use any font you like instead; it is drawn sharp at your screen's resolution.

1. Make a folder called `fonts` next to `MageBros.exe`.
2. Put one or more font files in it (`.ttf` or `.otf`).
3. In the game, open Options (F10) > Video > **Menu font** and press Left or Right to go through
   your fonts. The change shows at once and is remembered.

To go back, choose **Classic**. Fixed-width (monospace) fonts look best, since every letter sits
in the same space; with other fonts, narrow letters like "i" stand a little apart. If a font
shows "(not found)", its file was moved or can't be read, and Classic is used meanwhile. No fonts
come with the game, so check that you're allowed to use the ones you add.

### Cheats

Options > Cheats. All are off by default. While any is on, **CHEATS** shows in the bottom-left
corner.

| Cheat | What it does |
|---|---|
| Unlock all levels | Every level can be picked on level select (set it from the title screen; your saved progress is not changed) |
| Infinite lives | Hits never take your last heart |
| Invincible | Enemies, bullets and flames can't hurt or push you |
| Infinite magic | Firing costs no magic |
| No falling deaths | Falling into a pit or the water puts you back where you last stood |
| Max magic level | Every level starts at magic level 7 |
| Choose your wand | Every level starts with the wand you pick |
| Max gold | Every level starts with 9999 gold |
| Moon jump | Jump 150% or 200% as high |
| Run speed | Walk 125%, 150% or 200% as fast |
| One-hit kills | Enemies and bosses fall to one shot (turtles and ghosts still can't be shot) |
| Rapid fire | Hold fire to keep shooting |

A fall ends a level whatever your hearts, so only No falling deaths protects you from falls. Your
progress is saved as usual with cheats on, and what Max magic level, Choose your wand and Max gold
give you stays in your save.

## Settings and saves

- **Settings:** `magebros_native.ini` next to the program, with a comment for every option. Delete
  it to restore the defaults. A log, `magebros_native.log`, is written next to it.
- **Saves:** `%APPDATA%\MageBrosNative\saves\original\progress.ini` on Windows,
  `~/Library/Application Support/MageBrosNative` on macOS, and
  `~/.local/share/MageBrosNative/saves/original/progress.ini` on Linux, or the folder set by
  `save_path` in the settings file. There are 8 save slots for one-player games and 8 for
  two-player games, as in the original. Saves from the original game are not carried over.

## For developers

The repository holds everything needed to build the port, but none of the game's files: to run
it, or to run the tests that need the game, you still need your own `MageBros.exe`.

- **Building:** any C++17 compiler and CMake 3.20 or newer. On Windows, double-click
  `make_release.bat` (needs Visual Studio 2022 or newer with "Desktop development with C++"); it
  builds, runs the tests and writes the release zip to `build\`. Step by step, including how a
  release is published: [docs/RELEASING.md](docs/RELEASING.md).
- **Android:** the `android/` Gradle project builds the APK. The SDK, NDK, CMake and Gradle
  versions are pinned in `android/gradle/libs.versions.toml`; SDL3 is fetched by
  `android/fetch-sdl.sh` on the first build.

  ```sh
  cd android
  ./gradlew :app:assembleDebug
  adb install -r app/build/outputs/apk/debug/app-debug.apk
  ```

  On first start the app asks for your `MageBros.exe` and music folder. Release signing, the
  16 KB page-size check and the CI jobs are described in [docs/RELEASING.md](docs/RELEASING.md).
- **macOS:** Apple Silicon, macOS 12 or newer, with CMake 3.20+ and the Xcode command line tools
  (`xcode-select --install`). Nothing else: SDL3 is built from the pinned source, so the zip
  needs no Homebrew and no separately installed SDL3.

  ```sh
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMBN_SDL_FROM_SOURCE=ON
  cmake --build build                          # build/MageBrosNative.app
  ctest --test-dir build                       # the tests that need no game files
  ./build/MageBrosNative.app/Contents/MacOS/MageBrosNative --game /path/to/MageBros.exe
  cmake --build build --target package         # MageBrosNative-<version>-macos-arm64.zip
  ```

  Settings, saves and the log live in `~/Library/Application Support/MageBrosNative`; the
  application bundle itself is never written to. Music uses a user `.sf2` next to the program or
  `MageBros.exe` first, otherwise the macOS system synthesizer (the Apple DLS synth), with the
  built-in TimGM6mb as the fallback; the original's `Music*.mid` files are used in every case.
  **F10** (or Fn+F10: Apple keyboards use F10 for Mute by default, or Start on a gamepad)
  opens the pause menu — Resume, video/graphics/audio/controls, Level select, Main menu and
  Quit game. **P** freezes the game, as in the original; Esc is "back" inside menus.
- **R36S (aarch64 handheld):** Docker on any machine, no cross toolchain to prepare.

  ```sh
  tools/r36s/build.sh                    # Debian 13 container, static SDL3, GLES backend
  tools/r36s/install.sh ark@192.168.0.104   # writes /roms2/ports/ on the console
  ```

  Put your own `MageBros.exe` in `/roms2/ports/magebros/`, then start **MageBros** from the
  console's Ports menu. Copy the game's own `Music*.mid` files there too, and any General MIDI
  `.sf2` as `soundfont.sf2`: the console has no system synthesizer, so without a SoundFont the
  effects play but the music stays silent. (The desktop packages carry TimGM6mb; the console
  installer does not copy it.) The launcher sets the one environment hint this
  device needs (`SDL_EGL_LIBRARY=libMali.so`, because the console's EGL/GLES live in libMali and
  SDL does not find them by itself), runs the game through OpenGL ES 3.0 on KMSDRM at 640x480,
  and writes a log to `/roms2/ports/magebros/magebros.log`. Nothing on the system is replaced.

  **Performance benchmark.** Start **MageBrosBench** from the Ports menu (or run
  `/roms2/ports/MageBrosBench.sh` over SSH) to play the generated dense benchmark level for
  30 seconds and write `benchmark-report.json` and `benchmark.log` in the game folder. The same
  run is available from a desktop command line:

  ```sh
  ./MageBrosNative --game /path/to/MageBros.exe --benchmark --benchmark-seconds 30       --benchmark-report reports/run.json
  ```

  The report names the build, renderer, display, VSync, Smooth motion, MSAA, anisotropy, render
  scale and enhancements, and carries the frame times plus the average FPS, 1% low, p95/p99,
  CPU/tick/render/swap split, draws and uploaded bytes. `--benchmark-level 1,1` runs the same
  harness on an original level (for comparing the benchmark with the busiest campaign levels).
  `--benchmark-fragment w,l,x,z` starts at a campaign position (for example
  `--benchmark-fragment 1,5,140,3600`); `--benchmark-sweep w,l` steps through a level and records
  each second against camera Z. The launcher takes the same runs over SSH:
  `MAGEBROS_FRAGMENT=1,5,140,3600 /roms2/ports/MageBrosBench.sh`. The benchmark level is generated from the port's own data, never
  appears in the campaign, and writes no saves. Every named campaign fragment (1-5, 1-8, 1-9,
  2-2, 2-4, 2-5, 2-7, 2-8, 3-2, 3-3) now holds 59.4-60.0 fps at the R36S defaults. Off-screen
  effects and torches are skipped, consecutive identical opaque models share one instanced draw
  (byte-identical), and the shadows are batched after the models; the shadow blend is rounded to
  8 bits, so the batching changes 35-354 pixels at shadow edges (max 53), recorded with the table
  and the pixel checks in
  [`research/acceptance/r36s-performance.md`](research/acceptance/r36s-performance.md).
- **Linux desktop (Debian 13, x86_64):** Docker on any machine; no Linux toolchain or sysroot to
  prepare. The build runs in a Debian 13 container that matches the package's glibc 2.41, with
  SDL3 3.4.18 built from the pinned source and linked statically, X11 and Wayland, ALSA/Pulse,
  and the OpenGL 3.3 desktop backend.

  ```sh
  tools/linux/build.sh
  # build-linux/MageBrosNative
  # build-linux/MageBrosNative-<version>-linux-x86_64.tar.gz
  ```

  Unpack the tarball into a writable folder, put your own `MageBros.exe` and `Music*.mid` files
  beside `MageBrosNative`, and run `./MageBrosNative`. The package includes the TimGM6mb bank, so
  music works without a separately installed SoundFont. Settings and the log stay beside the
  program; saves live in `~/.local/share/MageBrosNative/`. The artifact targets Debian 13 and
  newer (glibc 2.41); Debian 12 and older are not supported by it. The same source builds for
  other Linux baselines by setting `MBN_LINUX_BASE`, and a GLES-only device can use
  `-DMBN_GLES=ON`. The Windows game cross-compiles with MinGW-w64
  (`-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake`).
- **What gets built:**

  | Program | What it does | Needs the game | In the package |
  |---|---|---|---|
  | `MageBrosNative.exe` | The game | Yes | Yes |
  | `MageBrosAssets.exe` | Lists (`--inventory`), checks (`--selftest`) or exports (`--export FOLDER`) the resources in your `MageBros.exe` | Yes | Yes |
  | `mbn_tests.exe` | Tests the file-format readers on small built-in samples | No | Yes |
  | `mbn_soundfont_tests.exe` | Loads the bundled TimGM6mb bank and checks that it renders a note | No | Yes |
  | `mbn_collision_tests.exe` | Tests that the ColDet collision library answers the game's kinds of query | No | Yes |
  | `mbn_map_tests.exe` | Builds all 40 levels' geometry from your copy and checks it | Yes | No |
  | `mbn_hero_tests.exe` | Runs player 1's movement on all 40 levels: landing on each start floor, the original's jump height, and long random play that must never leave the hero inside a wall | Yes | No |

  `ctest --test-dir build` runs the tests (add `-C Release` for a Visual Studio build); the last
  two only when CMake was configured with
  `-DMBN_GAME_EXE=<path to MageBros.exe>` (or run them with `--game <path>`). Each prints
  "passed" or what failed, and exits with 1 on a failure. The two tests in the zip only show
  that the build works on your machine; players never need them. Files exported by
  `MageBrosAssets.exe` are Saito Games' work: don't share them.
- **Test keys** (games started with them aren't saved): N / B change level, F2 switches between
  one and two players, F3 jumps to the ending, Tab opens the level and model viewers.
- **More:** [docs/ROADMAP.md](docs/ROADMAP.md) (status and plans),
  [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) (how the original works, and its bugs
  and quirks), [docs/SMOOTH_MOTION.md](docs/SMOOTH_MOTION.md),
  [docs/GRAPHICS_ENHANCEMENTS.md](docs/GRAPHICS_ENHANCEMENTS.md) (how the graphics effects work),
  and [CLAUDE.md](CLAUDE.md) (the
  contributor handoff: project rules, source layout, workflow).

The port keeps the original's game logic: the code in `src/game` is reconstructed from the
original program, one file per original module, and collision uses the original's own collision
library, [ColDet](https://sourceforge.net/projects/coldet/), built from its published source. The
engine code (`src/gfx`, `src/audio`, `src/input`, `src/ui`, `src/framework`) comes from
[Hopmon Native](https://github.com/Exo-MDR-CD2000/HopmonNative), by the same author, since both
games run on the same Saito Games engine.

## Legal

Mage Bros., its name, artwork, levels, sounds and music are © 2003–2009 Saito Games; the MIDI
music is © Yamaha Corporation. This project isn't affiliated with or endorsed by Saito Games and
distributes none of the game's files.

The port's own code is MIT-licensed. The reconstructed game logic in `src/game` isn't covered by
the MIT license. ColDet is LGPL-2.0-or-later (in `third_party/coldet` with its license), built as a
separate library that anyone can rebuild with a modified ColDet. See [LICENSE](LICENSE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
