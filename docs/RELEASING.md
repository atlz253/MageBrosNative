# Building and releasing

How to build Mage Bros. Native on Windows, macOS and Linux and publish a new version, step by
step.

## Quick reference

| You want to | Run (in the repository folder) | Version in `CMakeLists.txt` |
|---|---|---|
| Build the current version (to test, or to rebuild a release) | double-click `make_release.bat`, or `make_release.bat` | unchanged |
| Build the next small release (fixes, a chunk of work) | `make_release.bat patch` | 0.4.21 -> 0.4.22 |
| Build the first release of a new stage | `make_release.bat minor` | 0.4.21 -> 0.5.0 |
| Build 1.0 or a major release | `make_release.bat major` | 0.4.21 -> 1.0.0 |
| Build an exact version (to skip one) | `make_release.bat 0.4.23` | set to 0.4.23 |

**Building never publishes anything.** A release goes public only when you **push** to `main`
with a version that has no GitHub release yet (see step 6).

## What you need (once)

- **Visual Studio 2022 or newer** with the **"Desktop development with C++"** workload (the
  Community edition is fine). The script finds it on its own; CMake comes with it.
- **Git**, with this repository cloned (for example to
  `C:\Users\ExoJag\Documents\MageBros-Project\MageBrosNative`).
- Optional: **`MageBros.exe` in the folder above the repository**
  (`MageBros-Project\MageBros.exe`). Then the map and hero tests also run on all 40 levels of
  your copy. Without it only the tests that need no game files run.

The first build downloads SDL3 and takes a few minutes; later ones are much quicker.

## Running the script

Double-click `make_release.bat`, or open a Command Prompt or PowerShell in the repository folder:

```bat
make_release.bat patch
```

It then:

1. changes the version in `CMakeLists.txt`, if you asked for that;
2. configures and builds the Release build in `build\`;
3. runs the tests;
4. packages `build\MageBrosNative-<version>-windows-x64.zip`;
5. prints `Done: ...` and waits for a key.

The game itself is also at `build\Release\MageBrosNative.exe`, the same program as in the zip.

**What the zip holds** (the `install(...)` lines in `CMakeLists.txt` decide it): the game,
`MageBrosNative.exe`; the asset tool `MageBrosAssets.exe`; the tests `mbn_tests.exe`,
`mbn_soundfont_tests.exe` and `mbn_collision_tests.exe`; the `TimGM6mb.sf2` General MIDI bank;
`README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`; the `licenses` folder (ColDet's LGPL text and
TimGM6mb's GPL-2.0 text must travel with the program); and the `docs` folder. Players only need
the game; the README's "Getting started" says so in a table. If what's packaged changes, change
that table too.

**If something fails**, the script prints `Release failed.` and puts a raised version back, so
`CMakeLists.txt` is as it was. Scroll up to see the build error or the failed test.

A line saying `'vswhere.exe' is not recognized` is harmless.

## Building the macOS package

The Windows script is Windows-only. On an Apple Silicon Mac, with CMake 3.20+ and the Xcode
command line tools:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMBN_SDL_FROM_SOURCE=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

`-DMBN_SDL_FROM_SOURCE=ON` builds the pinned SDL3 from source instead of taking a system copy,
so the zip carries its own SDL3. The result is
`build/MageBrosNative-<version>-macos-arm64.zip`, holding `MageBrosNative.app`,
`TimGM6mb.sf2` inside `Contents/Resources/`, and the same licence files as the Windows zip. The
GitHub workflow publishes the Windows zip and the Linux tarball; the macOS zip is built by hand
until the release job covers it.

The bundle is never written to: settings, saves and the log live in
`~/Library/Application Support/MageBrosNative`. Music uses a user `.sf2` first, otherwise the
macOS DLS synthesizer, with the bundled TimGM6mb bank as the fallback and an explicit choice in
Options > Audio > Music synth. The original tracks were authored for Yamaha XG; an XG bank is
not shipped because it cannot be redistributed.

## Building the Linux package

The Linux package targets **Debian 13 (glibc 2.41) and newer**. The build runs in a Debian 13
x86_64 container on any machine with Docker, so an Apple Silicon or ARM machine does not need a
Linux toolchain or a hand-prepared sysroot:

```sh
tools/linux/build.sh
# build-linux/MageBrosNative
# build-linux/MageBrosNative-<version>-linux-x86_64.tar.gz
```

The script builds SDL3 3.4.18 from the pinned source and links it statically, enables X11,
Wayland, ALSA/Pulse/PipeWire and the OpenGL 3.3 backend, runs the tests that need no game files,
and packages a tarball with a top-level folder. The tarball holds the game, the tools and tests,
`TimGM6mb.sf2`, the README, the licence files and the docs; it contains no game content. A
user `soundfont.sf2`/`magebros.sf2` next to the binary takes precedence, then the built-in
TimGM6mb bank. The original tracks were authored for Yamaha XG; no XG bank is shipped because
it cannot be redistributed, and the engine does not reproduce XG SysEx effects. The game binary
keeps only the system C++ runtime, `libm`, `libgcc_s` and `libc` as dynamic dependencies;
inspect it with `readelf -d` or `ldd` in the container.

`MBN_LINUX_SKIP_TESTS=1` builds and packages without running `ctest`. `MBN_LINUX_BASE` changes
the base image for a contributor experiment; the released artifact and its documentation are
still Debian 13.

The GitHub `CI` workflow runs the same script on every push, and the `Release` workflow builds
the tarball after the Windows zip and attaches it to the same release. The live-session
acceptance is recorded in `research/acceptance/linux-desktop.md`.

## Building for the R36S

The console is aarch64 Debian 13 with glibc 2.41, no SDL3, and its EGL/GLES entry points in
`libMali.so`. The build therefore runs in a Debian 13 arm64 container (native on Apple
Silicon), links SDL3 3.4.18 statically, and compiles the GLES renderer backend:

```sh
tools/r36s/build.sh                       # build + tests, output in build-r36s/
tools/r36s/install.sh ark@192.168.0.104   # copy to /roms2/ports/ over SSH
```

`install.sh` writes `/roms2/ports/MageBros.sh`, `/roms2/ports/MageBrosBench.sh` (the performance
benchmark launcher) and `/roms2/ports/magebros/`; put your own
`MageBros.exe` and its `Music*.mid` files in that folder, plus a General MIDI `.sf2` named
`soundfont.sf2` if you want music (the console has no system synthesizer). The launcher needs
one environment hint — `SDL_EGL_LIBRARY` pointing at `libMali.so` — and nothing else:
`SDL_VIDEO_DRIVER`, `SDL_OPENGL_LIBRARY` and `SDL_OPENGL_ES_DRIVER` were each tested and are not
needed. The game writes its own log next to the launcher's. In game, **Start** opens the pause
menu with "Level select" and "Quit game", and **Select+Start** quits from anywhere.
`MageBrosBench.sh` stops EmulationStation if it is running (needed over SSH), runs the generated
benchmark level for 30 seconds, writes `benchmark-report.json` and `benchmark.log` beside the
game, then restarts the frontend. The report fields and the desktop command line are in
`README.md`; the measured results are in `research/acceptance/r36s-performance.md`.

The Ports menu is EmulationStation's; it scans `/roms2/ports` at start, so restart the frontend
(or reboot the console) after the first install.

## Making a new release, step by step

### 1. Start from an up-to-date `main`

```bat
git checkout main
git pull
git status
```

`git status` should say "nothing to commit, working tree clean" (apart from the changes you're
about to release).

### 2. Pick the new version number

- **patch** (0.4.21 -> 0.4.22): fixes and normal chunks of work. Almost every release.
- **minor** (0.4.x -> 0.5.0): the start of a new stage in `docs/ROADMAP.md`.
- **major** (-> 1.0.0): the big one.

**Skip any version a side branch has used**, even if that branch was never merged: its builds
went out under that number. For example, `dxgi-present` used **0.4.22**, so `main` went from
0.4.21 straight to **0.4.23** (`make_release.bat 0.4.23`). Check `CLAUDE.md` ("Where we are") and
`docs/ROADMAP.md` for any others.

To see which versions are already released:

```bat
git fetch --tags
git tag --list "v0.4.*"
```

### 3. Write the changelog

At the top of `CHANGELOG.md`, under `## [Unreleased]`, add a section for the new version in
**exactly** this form (the release workflow looks for it, and its text becomes the release
notes on GitHub):

```markdown
## [0.4.23] - 2026-11-02

- What changed, in plain words for players.
```

Move anything that was under `## [Unreleased]` into it, and leave `## [Unreleased]` empty above
it. Also bring `README.md` (the "**Version x.y.z.**" line near the top), `CLAUDE.md` ("Where we
are") and `docs/ROADMAP.md` up to the new version.

### 4. Build and test

```bat
make_release.bat 0.4.23
```

or `make_release.bat patch` when no version needs skipping. Check that it ends with
`Done: build\MageBrosNative-0.4.23-windows-x64.zip`, then play `build\Release\MageBrosNative.exe`
for a bit (copy it next to `MageBros.exe`, or run it with `--game <path to MageBros.exe>`).

### 5. Commit

```bat
git status
git add CMakeLists.txt CHANGELOG.md README.md CLAUDE.md docs
git commit -m "Short description (version 0.4.23)"
```

Check `git status` first: **never commit `MageBros.exe`, `MageBros.pdb`, the `.mid` files,
screenshots or anything exported from the game** (`.gitignore` covers them, but look anyway).
The `build\` folder is ignored too: the zip is not committed.

### 6. Push: this publishes the release

```bat
git push
```

Every push to `main` runs the **Release** workflow on GitHub
(`.github/workflows/release.yml`). It reads the version from `CMakeLists.txt`, and:

- if there is **no** release `v<version>` yet, it builds the zip again on GitHub's own Windows
  machine, runs the tests that need no game files, and publishes it as the release
  `v<version>`, with that version's `CHANGELOG.md` section as its notes;
- if that release **already exists** (a docs-only push, say), it does nothing.

Watch it under the repository's **Actions** tab if you like; it takes several minutes. The
release then shows on the **Releases** page with its zip.

## Common situations

- **I only changed docs.** Commit and push. Don't change the version: no release is made.
- **I want to rebuild a version that's already released** (to test it, or after a fresh clone).
  Run `make_release.bat` with nothing after it. The version stays as it is.
- **The build failed after a bump.** The version was put back automatically. Fix the error, then
  run the same command again.
- **I bumped the version by mistake** (the build worked, not committed yet). Put the file back
  with `git checkout -- CMakeLists.txt`, or edit the number on line 3 of `CMakeLists.txt`
  (`project(MageBrosNative VERSION x.y.z ...)`) by hand.
- **A push should have made a release but didn't.** The version in `CMakeLists.txt` probably
  wasn't raised, or that release already existed. Check the Actions tab for the run's log.
- **Releasing again under the same number.** Not possible: the workflow skips versions that are
  already released. Raise the version instead.
- **Building on a side branch** (as `graphics-enhancements` was): `make_release.bat <x.y.z>` works the
  same; pushing the branch publishes nothing (only `main` releases). Its versions take the next
  numbers, and `main` stays off them until the branch merges.
- **Windows Defender removes the new exe** (it happened to 0.4.39 as
  `Behavior:Win32/DefenseEvasion.A!ml`: a machine-learning guess about a new, unsigned program,
  not a known threat). Restore it in Windows Security > Virus & threat protection > Protection
  history, and if you like report it at microsoft.com/wdsi/filesubmission. Defender may also take
  the identical copies in `build\Release` and `build\fix-builds`. Signing the exe (a code-signing
  certificate) would make this rarer; not set up.

## Building the Android APK

The Android project lives in `android/`. It uses the SDK, NDK, CMake and Gradle versions pinned in
`android/gradle/libs.versions.toml` (SDK 36, NDK 28.2, CMake 3.31.6, AGP 8.13), and SDL3 3.4.18 is
fetched by `android/fetch-sdl.sh` on the first build.

```sh
cd android
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The game library (`libmain.so`) and SDL3 are inside the APK; no SDL3 is needed on the phone. The
APK contains no game content: on first start it asks for the player's `MageBros.exe` and music
folder.

On the phone the game is landscape-only. The system Back action opens the options menu during a
level, closes it when the menu is open, and leaves the app from the title screen. The music and
drawing stop in the background and resume where they were, and the screen stays awake while
playing. Android cannot change monitors, resolutions, refresh rates, display modes or VSync, so
those rows are absent from Options > Video; the graphics, sound and frame-limit settings remain.
The on-screen controls are described in the README (Options > Controls > Touch controls).

Release signing, version codes and the 16 KB page-size check are covered under the release steps
below.

### Signing and releasing the APK

Release signing uses a keystore outside the repository and four environment variables:

```sh
export MBN_ANDROID_KEYSTORE=/path/outside/the/repo/magebros-release.jks
export MBN_ANDROID_STORE_PASSWORD=...
export MBN_ANDROID_KEY_ALIAS=...
export MBN_ANDROID_KEY_PASSWORD=...
cd android
./gradlew :app:assembleRelease
```

The APK is `android/app/build/outputs/apk/release/app-release.apk`. `versionName` and
`versionCode` come from the same CMake version line as the desktop packages (`0.4.47` gives
version code 447). Only `arm64-v8a` is built.

Before publishing, check it:

```sh
sh android/check-apk.sh android/app/build/outputs/apk/release/app-release.apk
"$ANDROID_HOME/build-tools/36.1.0/zipalign" -c -P 16 -v 4 android/app/build/outputs/apk/release/app-release.apk
"$ANDROID_HOME/build-tools/36.1.0/apksigner" verify android/app/build/outputs/apk/release/app-release.apk
```

`check-apk.sh` fails on any game content and requires the bundled bank and licence texts. The
linker aligns the native libraries' load segments to 16 KB; `zipalign` checks the archive side.
The APK starts on a 16 KB-page Android 16 emulator image (`magebros16k`).

The CI workflow (`.github/workflows/ci.yml`) builds and checks the debug APK on every push and
pull request. The release workflow adds a signed APK to a release only when the repository
variable `MBN_ANDROID_RELEASE` is `true` and the four signing secrets
(`MBN_ANDROID_KEYSTORE_BASE64`, `MBN_ANDROID_STORE_PASSWORD`, `MBN_ANDROID_KEY_ALIAS`,
`MBN_ANDROID_KEY_PASSWORD`) are set; otherwise desktop releases are unchanged.

## Where the version lives

Line 3 of `CMakeLists.txt`, `project(MageBrosNative VERSION x.y.z LANGUAGES C CXX)`, is the only
place the number is kept. The build turns it into `build\generated\version.h` (from
`res\version.h.in`) for the program, and into the zip's name; the GitHub workflow reads the same
line. Don't type the version anywhere else in the code.
