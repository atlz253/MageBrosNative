# Android: build and acceptance

Date: 2026-11-25. Change: `android-touch-port`. Status: built and accepted on the Android
emulator (30/32 tasks); the audible and physical-gamepad checks need the owner's phone (6.4 and
6.5, listed at the end).

## Environment

| Item | Value |
|---|---|
| Host | Apple M5, macOS 27, 24 GB |
| Emulator | Android 16 (API 36), `arm64-v8a`, Pixel 9, 1080×2424, 420 dpi |
| 16 KB image | `system-images;android-36;google_apis_ps16k;arm64-v8a`, `PAGE_SIZE=16384` |
| GPU | Android Emulator OpenGL ES Translator on Apple M5, GLES 3.0 (Metal 4.1) |
| SDK / NDK / CMake | 36 / 28.2.13676358 / 3.31.6 |
| Game data | the owner's `MageBros.exe` (SHA-256 `ac191080…8c49`) and `Music01..06.mid` |

## Build and package

- Debug: `android/app/build/outputs/apk/debug/app-debug.apk`, 16 MB,
  SHA-256 `97f173df466d19f83801dd5c76bb615198cda68bf6511699b0d0cd71390b8eed`.
- Release, signed with the out-of-repo test keystore: `app-release.apk`, 10 MB,
  SHA-256 `c28c346157218d606a680f3bd3e207fe2aa10cb1b5f56578659385588df6e327`.
- `aapt2`: `versionCode 447`, `versionName 0.4.47`, `native-code: arm64-v8a` only.
- `apksigner verify`: certificate `CN=Mage Bros. Native, OU=Test, O=MageBros, C=US`.
- `zipalign -c -P 16 4`: successful.
- The ELF load segments of `libmain.so` are aligned to `0x4000` (16 KB).
- `android/check-apk.sh`: contents OK. Adding a fake `assets/MageBros.exe` makes it fail, as
  intended.
- Installed and started on the 16 KB image (`getconf PAGE_SIZE` = 16384): the game reached its
  log and rendered.

## Runtime

Startup log (16 KB image, release APK):

```text
Mage Bros. Native 0.4.47
Data folder: /storage/emulated/0/Android/data/com.magebros.game/files/MageBrosNative/
Bundled bank: unpacked to …/MageBrosNative/TimGM6mb.sf2
Found Mage Bros. 2.0 (2022 rebuild, freeware) at …/game//MageBros.exe
SHA-256: ac191080af002b8ad0615e432faa92a2a47ce76a9aaa667bacf628836c098c49
MIDI: using the built-in TimGM6mb bank with …/TimGM6mb.sf2
OpenGL OpenGL ES 3.0 (4.1 Metal - 91.7) | Android Emulator OpenGL ES Translator (Apple M5)
Renderer backend: gles
Limits: renderbuffer 16384, texture 16384
Render target 2424x1080, MSAA 4x
Display: borderless 2424x1080, monitor 0
Touch controls: virtual pad attached (SDL id 2)
Gamepad connected: Mage Bros. touch controls
Touch controls: overlay layout, safe insets left 63 top 61 right 35 bottom 37 (virtual units)
```

No GL errors in the session log. The Android default `aspect = widescreen` and
`hud_layout = edges` were chosen on first run and appear in the config and the 2424×1080 render
target.

## Game-data import

- First start shows the explanation and the folder picker; cancelling returns to the explanation,
  and relaunching offers it again.
- SAF folder from Downloads (`content://…/tree/primary:Download/magebros-saf`) copied, with the
  permission dialog; all six MIDI files appeared with their expected names.
- The raw-path test hook copied a folder containing the executable, six music files, an `.sf2` and
  a `readme.txt`: only the executable, music and SoundFont were copied.
- Executable-only folder: the game starts, the log says `Music: Music01.mid not found next to
  MageBros.exe`.
- Known build: accepted and logged with its label. Modified executable: refused with the SHA-256
  and a "Start anyway" choice; choosing it starts the game with `--allow-unverified`. A truncated
  executable: refused; "Choose another folder" removed it.
- A second import (import-only mode) replaced the files; the next start used the new
  `soundfont.sf2`.
- The Android-only **Game files** row is present in Options and opens the same import screen.

## Touch controls

- Overlay drawn at 4:3 and 16:9, and on the phone with the emulator's safe-area insets
  `63/61/35/37` (the Start pill and buttons move inside the safe area).
- Stick response: `0.13 → 0.66`, `0.50 → 0.81`, `1.00 → 1.00`, release `→ 0.00` (axis values
  logged by the scripted runs). The 0.66 floor clears the game's 0.5 walk threshold after its own
  25% dead zone.
- Three fingers at once: the virtual pad reported `S=1 E=1` while the stick was at `999,0`; the
  screenshot shows the hero airborne, a shot in flight and MP spent. Releasing Jump left Fire and
  the stick unchanged; sliding off Fire released only Fire.
- Menu path: stick moves the title selection (1 PLAYER → 2 PLAYERS), Fire confirms (title → save
  slots → world map), Start opens the options menu, Jump closes it.
- Input blocking: while the options menu is open the game sees no pad state; a Fire press held
  through the menu's closing is not seen until it is released and pressed again.
- Physical-pad precedence (test hook): connecting a pad logs `Touch controls: hidden (a physical
  gamepad is connected)` and removes the overlay; disconnecting shows it again.
- `Options > Controls > Touch controls` cycles Off / Automatic / On at once; the setting is saved.
- Real Android touch: `adb shell input swipe` on the stick moved the hero in level 1-01. Touch
  coordinates include the widescreen offset: a tap at the drawn Fire button (window 1978,723 on
  the 2424×1080 emulator) maps to virtual 660,321 and fires (a shot and one MP bar); Jump sends
  the hero airborne. The stick base is fixed in the lower left: only a touch inside its grab
  circle moves the knob, and the base no longer follows the finger. Start sits at the bottom
  centre, flush with the bottom edge. In the options menu the stick is a D-pad: a 1.5-second
  hold moved exactly one row, a held Right changed one value once (HUD position Edges ↔
  Centered), and a diagonal down-right push moved nothing, so settings neither jump several
  steps nor move on two axes at once.
- `adb shell input keyevent 4` (Back) opens the pause menu from a level and closes it when the
  menu is open; from the title it leaves the app. The system immersive-confirmation sheet must be
  dismissed once on a fresh emulator image before Back reaches the app.
- In the headless emulator a scripted touch press did not start a level from the world-map screen
  (the screen was reached and drawn, but the transition did not run to completion); the level,
  gameplay, options and Back checks used `--auto "0:level=1,1"` for the entry. A continuous
  touch-only session on the owner's phone is the remaining check.

## Screenshot comparison (level 1-1)

Both builds ran `--auto "0:level=1,1;10000:shot=…"` at 2424×1080, widescreen, HUD at the edges,
no overlay, MSAA 4, anisotropy 1.

- The two frames show the same camera, hero, level geometry and HUD at the same positions.
- Full frame: SSIM 0.861, PSNR 21.0 dB; centre crop: PSNR 19.3 dB; edge comparison: PSNR 20.7 dB.
- The difference image shows a broad colour/gamma and texture-filtering shift from the emulator's
  GLES translator (the Mac uses desktop GL / Metal), not a layout or geometry difference. The
  emulator reports maximum anisotropy 1.0 while the Mac uses 8 in normal play.

## Performance

The built-in benchmark on the 16 KB emulator image, 20 s after a 2 s warm-up, generated level,
2424×1080, MSAA 4×, VSync on, render scale 1.0:

```text
fps_avg 60.00, fps_1pct_low 54.87
frame_ms_avg 16.666, p95 17.624, p99 18.225, max 31.871
cpu_ms_avg 2.754, render_ms_avg 1.248, swap_ms_avg 13.903
draws_avg 105.4, draws3d_avg 51.4
```

This is emulator evidence only; frame times on a phone depend on its GPU, thermals and display
resolution.

## CI

`.github/workflows/ci.yml` gained an Android job: JDK 21, the pinned SDK/NDK/CMake, the debug
APK, `check-apk.sh`, `zipalign -c -P 16` and an artifact upload. `release.yml` gained a signed
APK job gated by the repository variable `MBN_ANDROID_RELEASE` and the four signing secrets.
`actionlint` passes for both files; the exact commands were run locally (build, contents check,
alignment, signing). No GitHub Actions run was available from this machine.

## Remaining physical-device checks

The emulator cannot provide these; they need the owner and the phone:

- 60 fps, heat and battery on the phone at its native resolution over a long session.
- Audible music and effects through the phone's speaker/headphones, and the background/resume
  behaviour while a call interrupts play.
- A real Bluetooth/USB gamepad taking over from the overlay, and a second pad for player two.
- A display with a real cut-out/rounded corners (the emulator reports insets and the layout was
  checked with simulated and emulator insets).
- Android 15/16 gesture Back on a physical device.

The acceptance report should be extended with those results before the change is archived.
The two remaining tracked tasks are the checks that need a person and the phone:

1. **6.4 Audio and backgrounding.** Install the current APK, enter level 1-01 with the phone's
   speaker or headphones, and confirm music and effects; press Home and return, and confirm the
   music resumes with no note left sounding.
2. **6.5 Physical gamepads.** Connect a Bluetooth/USB gamepad and confirm the overlay hides and
   the pad controls player 1; connect a second pad and confirm two-player play.

With those confirmed, mark 6.4 and 6.5, archive the change and add the phone's model to this
report.
