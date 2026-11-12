# R36S performance: measurement baseline

Date: 2026-11-12. Change: `r36s-performance`.

This file records the measured frame work before and after each optimisation chunk. Screenshots
are not committed (they contain game material); the numbers come from the once-a-second
`Frame work:` log line and the same values in the Full Statistics Overlay.

## Method

- macOS baseline: Apple M5, a 1280x960 window (the driver reports the drawable as 3286x2082),
  VSync on, Smooth motion on, MSAA 4x, anisotropic filtering 8x, Full Statistics Overlay on.
- The counters are reset at the start of every frame and captured after the game and HUD are
  drawn, before the overlay and the present, so the overlay does not count itself.
- `Draw calls` counts `glDrawArrays`/`glDrawElements`. `GL calls` counts every OpenGL call made
  by `src/gfx/device.cpp`, including state setup and buffer uploads. `Uploaded` is vertex, index
  and texture data handed to the driver.
- The five fixed spots: title, start of 1-1, 1-1 at z = 4120 (enemy group), 1-2 at z = 2920
  (fence section), 1-10 at the dragon (z = 1000).

## macOS baseline (2026-11-12)

| Spot | Draw calls | GL calls | Uploaded | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|
| Title | 6.0 | 295 | 0.8 KB | 0.90 ms | 15.32 ms | 60.0 |
| 1-1 start | 153.2 | 8078 | 341.5 KB | 2.32 ms | 13.99 ms | 59.9 |
| 1-1 enemy group (z 4120) | 185.4 | 9827 | 419.3 KB | 2.74 ms | 13.53 ms | 60.1 |
| 1-2 fence section (z 2920) | 191.6 | 10291 | 603.5 KB | 2.25 ms | 14.01 ms | 60.0 |
| 1-10 boss (z 1000) | 226.9 | 12603 | 216.4 KB | 2.92 ms | 13.26 ms | 58.2 |

GL calls per draw are about 53-56 in the level spots, which matches the analysis in
`docs/RENDERER_OPTIMIZATION.md`: nearly the whole state is sent again and the vertices are
uploaded again for every material.

The M5 CPU time is not comparable with the R36S; it is recorded to show the shape of the work
(draws and GL calls grow with the scene, uploads grow with the visible models). The R36S baseline
and the A/B setting runs are recorded below once the instrumented build has run on the console.

## R36S baseline (2026-11-12)

Build: the instrumented `r36s-performance` build, Mali-G31, 640x480 at 60.02 Hz, VSync on,
Smooth motion off, MSAA 0, anisotropic filtering 1x, render scale 1.00, SoundFont music on,
Full Statistics Overlay on. The overlay is drawn after the counters are captured, so its own
draws are not in the numbers below.

| Spot | Draw calls | GL calls | Uploaded | Logic | Render | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-1 start | 137.4 | 7324 | 339.5 KB | 0.61 ms | 13.07 ms | 16.95 ms | 32.37 ms | 19.7 |
| 1-1 enemy group (z 4120) | 154.5 | 8302 | 427.0 KB | 0.84 ms | 15.09 ms | 19.14 ms | 30.77 ms | 20.0 |
| 1-2 fence section (z 2920) | 149.8 | 8063 | 516.6 KB | 1.09 ms | 15.71 ms | 20.04 ms | 29.87 ms | 20.0 |
| 1-10 boss (z 1000) | 184.9 | 10021 | 191.2 KB | 0.67 ms | 15.21 ms | 19.52 ms | 32.06 ms | 19.5 |

GL calls per draw are 53-55 here too. Logic is under 1.5 ms; the renderer's submission is 13-16 ms
of the frame. With Low latency on, the GPU wait was 0.01 ms, so the graphics card is not the
limit.

The title/logo spot measures 6 draws, 294 GL calls, 0.8 KB, 4.39 ms CPU, 28.86 ms swap and
30.0 fps. The logo screen is not representative of a level; it is kept as a check that the
counters also work outside a level.

### A/B runs (1-1 start, same build)

| Setting | Draw calls | GL calls | Uploaded | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|
| Baseline | 137.4 | 7324 | 339.5 KB | 16.95 ms | 32.37 ms | 19.7 |
| VSync off | 137.8 | 7342 | 339.5 KB | 16.94 ms | 32.41 ms | 19.7 |
| Render scale 0.75 | 138.3 | 7370 | 340.1 KB | 16.36 ms | 16.89 ms | 30.0 |
| Smooth motion on | 138.1 | 7362 | 340.3 KB | 16.93 ms | 32.98 ms | 20.0 |
| Smooth motion + scale 0.75 | 138.7 | 7394 | 340.2 KB | 16.29 ms | 16.95 ms | 30.0 |
| No SoundFont | 138.0 | 7355 | 340.3 KB | 16.69 ms | 33.22 ms | 20.0 |

### Probe checks

A new `mbn_r36s_probe --swap-test` (blank clear + swap) runs at 60.0 FPS with swap interval 0
and 1, and stays at 60.0 FPS when 2 to 14 ms of work is added before each swap.
`--present-test` (a 640x480 scene FBO, a blit to a resolve texture, then a full-screen gamma
quad) also runs at 60.0 FPS. So the KMSDRM/GLES path can present 60; the game's frame is held
back by its own work at the 16.7 ms boundary, not by a hard 30 Hz panel cap.

### R0 verification

- `ctest` on macOS: all 5 tests pass (`formats`, `synth`, `map_geometry`, `hero_movement`,
  `collision`).
- A deterministic screenshot of level 1-2 in the level viewer (Tab, no animation) was taken from
  the build before this change (git HEAD `4d075b3`, built in a separate worktree) and from the
  instrumented build, twice each. All four captures have the same SHA-256
  `ba8ca62de67e6f8ca9d6e94da1d1a1531c3488683a6a28b646b69bea73e4b452`; the counters and log do
  not change the picture.
- The title/logo screenshot is not deterministic in a time-based automation run (enemy and
  particle state depends on the tick count), so the in-level level-viewer capture is used for the
  comparison instead of an automatic gameplay shot.

## R1/R2: state cache and vertex layouts (2026-11-12)

The state cache sends only changed render state, uniforms, matrices, material, light and texture
filtering. The three long-lived vertex layouts (`D3DVERTEX`, `D3DLVERTEX`, the present quad)
replace the per-draw attribute reset. `InvalidateState()` covers the framebuffer, texture and
target paths that talk to OpenGL outside the draw helpers.

### R36S

| Spot | Draws | GL calls | Uploaded | Logic | Render | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-1 start, before R1/R2 | 137.4 | 7324 | 339.5 KB | 0.61 ms | 13.07 ms | 16.95 ms | 32.37 ms | 19.7 |
| 1-1 start, after R1/R2 | 138.4 | 926 | 340.2 KB | 0.47 ms | 9.15 ms | 12.64 ms | 21.16 ms | 29.5 |
| 1-2 fences, before R1/R2 | 149.8 | 8063 | 516.6 KB | 1.09 ms | 15.71 ms | 20.04 ms | 29.87 ms | 20.0 |
| 1-2 fences, after R1/R2 | 149.8 | 1065 | 516.6 KB | 1.06 ms | 11.56 ms | 15.72 ms | 34.20 ms | 20.0 |

OpenGL calls per draw fell from about 53 to about 6.7; the frame's render time fell by about
30%. The upload bytes are unchanged, so the remaining cost is now the per-draw vertex/index
upload and the draw call itself: R3 and R4 are the next levers.

### macOS

| Spot | Draws | GL calls | Uploaded | Render | CPU | FPS |
|---|---:|---:|---:|---:|---:|---:|
| 1-1 start, before R1/R2 | 137.4 | 7324 | 339.5 KB | 13.07 ms | 16.95 ms | 19.7 |
| 1-1 start, after R1/R2 | 138.6 | 929 | 339.9 KB | 1.20 ms | 1.77 ms | 59.9 |

The M5 result shows the shape: the same draws now cost about a tenth of the OpenGL calls. The
R36S still pays for the uploads and the draw itself, which the next chunks address.

### Picture checks

The deterministic level-viewer capture (level 1-2, Tab, overlay off) is byte-identical to the
git-HEAD build in every one of these settings: default, anisotropy 8x, smooth textures on,
per-pixel lighting on, 4x MSAA, and render scale 0.75. All five `ctest` tests pass.

## R3: MeshBuffer (static models and level geometry)

Models and the merged level meshes now keep their vertices and indices on the card. The first
draw uploads them; later frames upload only the changing HUD/2D rectangles.

| Spot | Draws | GL calls | Uploaded | Logic | Render | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-1 start, before R3 | 138.4 | 926 | 340.2 KB | 0.47 ms | 9.15 ms | 12.64 ms | 21.16 ms | 29.5 |
| 1-1 start, after R3 | 138.5 | 739 | 10.8 KB | 0.47 ms | 5.52 ms | 9.16 ms | 24.10 ms | 30.0 |
| 1-2 fences, before R3 | 149.8 | 1065 | 516.6 KB | 1.06 ms | 11.56 ms | 15.72 ms | 34.20 ms | 20.0 |
| 1-2 fences, after R3 | 150.1 | 873 | 11.7 KB | 0.82 ms | 6.12 ms | 10.11 ms | 23.14 ms | 30.0 |

The recurring uploads are gone: 340 KB a frame became 10.8 KB at 1-1 and 517 KB became 11.7 KB at
the 1-2 fences. The render time fell by about 40% and the fence section reached a stable 30 FPS.

## R4: 2D and HUD batching

Consecutive 2D quads with the same texture and state now go out as one draw. HUD sprites, digits,
hearts and magic lights batch within their texture runs; the batch is flushed at every state
boundary and before Present.

| Spot | Draws | GL calls | Uploaded | Logic | Render | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1-1 start, after R4 | 114.3 | 664 | 12.4 KB | 0.47 ms | 4.76 ms | 8.23 ms | 25.02 ms | 30.0 |
| 1-2 fences, after R4 | 124.0 | 783 | 13.4 KB | 0.78 ms | 5.50 ms | 9.52 ms | 23.73 ms | 30.0 |

HUD batching removed about 25 draws and 0.7-0.8 ms of render time. The picture of the level
viewer is byte-identical to the R1/R2 build (`b949d8db…`); in-game HUD and water visuals match.

## Where the frame now goes (R36S)

- CPU: about 8 ms a frame at 1-1 (logic 0.5 ms, renderer 4.8 ms, the rest stats/present).
- Swap/pageflip wait: about 25 ms. The frame is consistently displayed every second 60 Hz
  refresh (33.3 ms, 30 FPS).
- GPU wait after the swap: 0.01 ms with Low latency; a pre-swap `glFinish` also returned at once.
- VSync off, borderless, `SDL_VIDEO_DOUBLE_BUFFER=1`, render scale 0.75 and no SoundFont all
  leave the 30 FPS / ~25 ms swap wait unchanged.
- The probe's blank swap, 14 ms-work swap, and the same 640x480 FBO/blit/gamma-present path all
  run at 60 FPS on the console; the port's game window is the one that presents every second
  refresh. This is now a presentation/pacing question, not draw submission.

### R0 decision (kept as the starting point)

At the baseline the limit was **draw submission** (CPU), not GPU pixels, not the audio
synthesizer and not VSync. The render path was 13-16 ms for 137-150 draws, while the game logic
was under 1.5 ms. That is why R1/R2, R3 and R4 went ahead. The profile starts at MSAA 0,
anisotropy 1 and render scale 1.00; render scale 0.75 remains a fallback.

## R5: performance profile and pacing

`[video] profile` is now `manual` (default), `performance` or `quality`. A fresh GLES install
selects `performance`; the menu's Graphics page has the row, and changing any of the three
affected settings switches the profile back to `manual`. The R36S log reports:

```
Graphics profile: performance (MSAA 0, anisotropy 1, render scale 100%)
```

Pacing tests (1-1, 4 s each):

| Setting | FPS | CPU | Swap |
|---|---:|---:|---:|
| VSync on, unlimited limit | 30.0 | 6.4 ms | 26.9 ms |
| VSync off, limit 60 | 30.0 | 6.6 ms | 26.6 ms |
| VSync on, limit 30 | 30.0 | 6.6 ms | 26.0 ms |

KMSDRM only accepts swap intervals 0 and 1; a part-of-refresh limit is therefore software-paced.
The recommended console setting is VSync on with the unlimited limit: the pageflip is already
the stable 30 FPS pace.

## R5 acceptance (30 s, Smooth motion on, VSync on, performance profile)

| Spot | Draws | GL calls | Logic | Render | CPU | Swap | FPS |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1-1, moving through enemies | 110.9 | 688 | 0.6 ms | 5.0 ms | 6.9 ms | 26.4 ms | 30.0 |
| 1-2, moving through the fences | 162-180 | 1028-1182 | 0.9 ms | 8.5-9.0 ms | 11.0 ms | 22-24 ms | ~29-30 |

The fence section stays at 29-30 FPS while moving through its densest part; it no longer falls
to 20. The remaining frame time is the pageflip wait, as above.

The SoundFont synthesizer makes no measurable difference: with it removed, 1-1 is 30.0 FPS with
6.7-7.0 ms CPU, the same as with it.

## R6: visibility culling

Torches, needle fans and hammers now use the same `IsVisible` test as the other objects. At the
fixed 1-1 spot the frame went from 114.3 to 102.1 draws and from 8.2 to 6.4 ms CPU; at 1-2 from
124 to 102 draws and from 9.5 to 7.2 ms. The deterministic level-viewer capture is byte-identical
to the pre-culling build; the visible objects are unchanged.

### Decision after R3/R4

The optimisation removed the draw-submission limit: the same 1-1 frame is now 114 draws,
664 OpenGL calls, 12 KB uploaded, 4.8 ms of renderer work and 8.2 ms of CPU. The remaining
30 FPS is the **swap/pageflip wait**, not the game's work: the frame is presented every second
60 Hz refresh. The probe can present the same kind of 640x480 FBO/blit/gamma frame at 60 FPS, so
the next investigation is the game window's presentation path (SDL/KMSDRM swap), not more draw
batching. Until that is resolved, the R36S runs at a stable 30 FPS, which is the fallback the
specification allows.
