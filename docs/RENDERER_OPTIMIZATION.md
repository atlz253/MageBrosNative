# The renderer's work per draw (an optimization for weak graphics hardware)

**Status: in progress (change `r36s-performance`, 2026-11-12).** Issue 6 of the review of
2026-11-09 ([SMOOTH_MOTION.md](SMOOTH_MOTION.md), "Review of 2026-11-09"). The owner (2026-11-10)
asked to come back to it when players on low-end hardware reported low frame rates; the R36S
report (30 fps, stutter as enemies and the 1-2 fences appear) started this change. R0 is
measured; R1-R4 are pending. The baseline is in
[MageBrosNative/research/acceptance/r36s-performance.md](../../research/acceptance/r36s-performance.md).
Written from reading the code at 0.4.32; the R0 numbers below are the first measured data.

**Since then (2026-11-11, on the `graphics-enhancements` branch, merged into `main` as 0.4.46):** the graphics enhancements'
G2 built a recorder in `Device` (`docs/GRAPHICS_ENHANCEMENTS.md`, "G2 as built"): a level's 3D
draws are now gathered with their state (`DrawState`) and sent together, `ApplyState` became
`ApplyDrawState`, and the Statistics Overlay shows the card's time for them (GPU 3D) and their
count (3D draws: about 100 in 1-1, 270 in 3-10), which covers much of R0's measuring. That is where
R1 and R2 would now go. The game's own draws still upload their vertices one draw at a time: one
shared upload changed two pixels by a shade in a test (see the warning under R3). The figures
below are from the RTX 3090 Ti; on the RTX 5080 (2026-11-11) 1-1 runs at about 2050 frames a
second and 3-10 at about 1110 (1280 x 960, VSync off, Smooth motion on).

## In short

- The port draws by imitating the original's Direct3D 7 calls on OpenGL, one for one. For every
  draw it sets every piece of drawing state again and uploads the vertices and indices again,
  including for models that never change.
- On the owner's card that costs nothing that shows: about 1,070 frames a second in the busiest
  level (the nest, 3-10), so under a millisecond for a whole frame. Smoothness there has nothing
  to gain.
- On weak graphics hardware (old integrated graphics), where each OpenGL call costs more in the
  driver, it could decide whether the game holds the monitor's refresh rate with Smooth motion
  on. That is the only reason to do this.
- **It changes no game logic.** `src/game` asks for the same things in the same order; only how
  `src/gfx/device.cpp` and `src/framework/d3dfile.cpp` (the port's own code) carry them out
  changes. Every picture must stay byte for byte the same.

## R0 measured (R36S, 2026-11-12)

The instrumented build counts draws, OpenGL calls, uploaded bytes and the frame's logic, render,
CPU and swap times; the numbers are in
[research/acceptance/r36s-performance.md](../../research/acceptance/r36s-performance.md).
The short version, at 640x480, VSync on, MSAA 0, anisotropy 1, Smooth motion off:

| Spot | Draws | GL calls | Render | Logic | CPU | FPS |
|---|---:|---:|---:|---:|---:|---:|
| 1-1 start | 137 | 7324 | 13.1 ms | 0.6 ms | 17.0 ms | 19.7 |
| 1-1 enemies | 155 | 8302 | 15.1 ms | 0.8 ms | 19.1 ms | 20.0 |
| 1-2 fences | 150 | 8063 | 15.7 ms | 1.1 ms | 20.0 ms | 20.0 |
| 1-10 boss | 185 | 10021 | 15.2 ms | 0.7 ms | 19.5 ms | 19.5 |

The GPU wait (Low latency) is 0.01 ms, and a blank `--swap-test` and the same FBO/blit/present
path in `--present-test` both run at 60 FPS on the console. The limit is therefore the CPU's
draw submission (about 55 OpenGL calls per draw), not the graphics card, the panel, the audio
synthesizer or VSync. Render scale 0.75 alone only moves the frame from 20 to 30 FPS because the
draw submission is unchanged. This confirms that R1/R2 and R3 are the work that matters.

## R1/R2 built (2026-11-12)

`Device` now records what it last sent and skips unchanged framebuffer, viewport, program,
depth/blend/cull, transforms, material, light, texture-stage state, uniforms and texture
filtering. Three long-lived vertex layouts replace the per-draw attribute reset. On the R36S,
1-1 went from 7,324 to 926 OpenGL calls a frame and the renderer's 13.1 ms to 9.2 ms; the 1-2
fences went from 8,063 to 1,065 calls and 15.7 to 11.6 ms. The picture is byte-identical in the
level viewer across the Graphics options. Upload bytes did not change, which is why R3 is next.

## R3/R4 built (2026-11-12/13)

Models and the level meshes now keep their geometry in `MeshBuffer`s; the recurring uploads fell
from 340 KB to about 12 KB a frame and the 1-1 render time from 9.2 to 4.8 ms. Consecutive HUD
quads are batched, removing about 25 more draws. The R36S frame is now 114 draws and 664 OpenGL
calls, down from 137 draws and 7,324 calls. The CPU is about 8 ms; the remaining 30 FPS is the
swap/pageflip wait (every second refresh), not draw submission. See the acceptance file for the
full numbers and the presentation-path follow-up.

## R5/R6 built (2026-11-13)

The R36S has an explicit performance profile (MSAA 0, anisotropy 1, render scale 100%) selected
on a fresh install and available in Options > Graphics. Torches, needle fans and hammers now skip
their draw when outside the visible range. At the fixed 1-1 spot the frame is 102 draws, 600
OpenGL calls, 12 KB uploaded and 6.4 ms of CPU; it is a stable 30 FPS. The remaining time is the
KMSDRM pageflip wait (about 26 ms), which presents every second 60 Hz refresh. The probe can
present the same kind of frame at 60 FPS, so this is a presentation-path follow-up, not a
renderer-cost one. The full measurements are in the acceptance file.

## What happens now

The game code draws through `Device` (`src/gfx/device.{h,cpp}`), a small copy of
`IDirect3DDevice7`: `SetRenderState`, `SetTextureStageState`, `SetTransform`, `SetMaterial`,
`SetLight`, `SetTexture`, then `DrawIndexedPrimitive` or `DrawPrimitive` with a pointer to the
vertices. The setters only store their values (`rs_`, `tss_`, `world_`, `material_`, ...).
Everything is handed to OpenGL at each draw:

1. **`Device::ApplyState`**, on every draw, about 43 OpenGL calls:
   - the scene framebuffer bound, the viewport set, the shader program selected, though all
     three are the same for the whole frame;
   - depth test, depth write, blending and its two factors, culling and its winding;
   - **26 shader values** (`glUniform*`): the world, view and projection matrices, the lighting
     switch, the material's three colours, the ambient colour, the light's direction and colour,
     flat shading, per-pixel lighting (removed in 0.4.39), the six texture stage settings, the alpha test and the
     2D and colour flags;
   - the texture bound, and its filtering set on the texture itself (`glTexParameteri` for the
     minifying and magnifying filters and the anisotropy), because the same texture may be drawn
     with different filtering by different settings;
   - `GetConfig()` read for the field of view, smooth textures and anisotropy (and per-pixel
     lighting until 0.4.39).
2. **The vertices and indices uploaded** (`glBufferData` into the one `vbo_` and the one `ibo_`),
   for every draw. A model with three materials uploads **all its vertices three times**, once
   per material, each time with that material's part of the indices
   (`CD3DFileObject::Render`).
3. **`SetupAttribs`**, 13 calls: all six vertex attributes switched off, then the three the
   vertex format uses switched on and pointed at the buffer.
4. The draw call itself.

So each draw is **about 60 OpenGL calls and one or two uploads**, whatever it draws.

Who draws, each frame:

| Caller | What | Vertices change? |
|---|---|---|
| `CD3DFileObject::Render` (`src/framework/d3dfile.cpp`), through `JObject3DX::Render` | Every model: the mages, enemies, bosses, platforms, objects, coins, bullets. One draw per material, drawn in two passes (solid, then see-through) | **Never after loading** (`JModelList::Get` rotates and scales the mesh once, then it is shared by every object that uses it) |
| `XLevelView::DrawMesh` (`src/game/XLevelView.cpp`) | The level itself: floors, walls, cliffs, the goal pads, markers; a few large meshes, one per texture | **Only when a level loads** (`MapGeometry`) |
| `JObject3DRect::Draw` (`src/game/JObject3D.cpp`) | Every flat rectangle: particles, shots, shadows, water, the ending's corridor, the logo's backdrop | **Often**: sizes, fades and texture positions are written into the vertices (`Scale`, `SetAlphaFactor`, `SetUV`), some every tick |
| `Device::Draw2DQuad` (through `BltToBackBuffer`, `ColorFill`) | All 2D: the HUD, menus, the tile background, the fade strips, the shop. One quad per blit, with its own 15 or so shader values | Every quad is new |
| `Device::DrawUIQuad` | The options menu's text and boxes, one quad per character | Every quad is new |
| `Device::Present` | The finished scene drawn into the window, once a frame | No |

A level frame is a few hundred draws. (Counted since by the Statistics Overlay's 3D draws row,
0.4.39: about 100 in 1-1 and 270 in 3-10, the level's 3D alone; the 2D is not counted.)

## What it costs

**Known:**

- On the owner's machine (then an RTX 3090 Ti; an RTX 5080 since 2026-11-11: its figures are in the note at the top), VSync off: about 1,070 frames a second in 3-10 with all
  40 birds, about 1,790 to 1,900 in 1-1 at 1280 x 960, 490 in 4K borderless (where the card's
  pixel work, not this, is the limit). So a whole frame, game ticks included, takes 0.5 to
  0.9 ms of processor time.
- A 240 Hz monitor needs a frame in 4.2 ms; a 60 Hz one in 16.7 ms.

**Not known:**

- How the time in a frame splits between the game's ticks, building the draws, the OpenGL calls
  and the driver's own thread.
- What it costs on integrated graphics. OpenGL drivers differ widely in what a call costs:
  NVIDIA's is among the cheapest, older Intel and AMD drivers can cost several times more per
  call, and uploads that replace a buffer many times a frame are a known slow path on some.
- Whether a weak machine would be held back by this at all, or by the pixel work instead
  (antialiasing 4x and texture filtering 8x are on by default, and Render scale multiplies it).
  If it is the pixel work, nothing here helps, and the advice to such a player is the Graphics
  page.

That is why the first chunk only measures.

## The rules any change must keep

1. **Pictures byte for byte the same**, at every setting, as before the change. This is a pure
   optimization: if a screenshot changes, it is a bug.
2. **`src/game` keeps calling the same `Device` functions in the same order.** The draw order is
   part of the picture (see-through things blend over what is already drawn), so nothing is
   sorted, merged or reordered.
3. **The emulated Direct3D state stays the source of truth** (`rs_`, `tss_`, and so on). What
   changes is how much of it is sent to OpenGL each time.
4. **One chunk at a time**, each measured before and after, each checked by the screenshots, and
   stopped as soon as the gain no longer shows: the later chunks carry more risk for less.

## The plan, in chunks

| Chunk | What | Risk | Expected to help |
|---|---|---|---|
| R0 | Measure: count the calls, time the frame, find a weak machine to test on | None (temporary code) | Decides everything after it |
| R1 | Send only the state that changed | Medium: a missed case shows as a wrong picture somewhere | Most of the calls: about 43 of the 60 per draw |
| R2 | One vertex layout object per vertex format | Low | 13 calls per draw |
| R3 | Keep the models and the level on the graphics card | Medium: needs a new way to hand a mesh to `Device` | All uploads of unchanging meshes |
| R4 | The flat rectangles and 2D quads | Low to medium | Only if R0 shows 2D-heavy screens are slow |

### R0. Measure first

- **Counters**, temporary, printed once a second (as the logs used for smooth motion): draws a
  frame, OpenGL calls a frame, bytes uploaded a frame, split by the callers in the table above.
  In 1-1, the nest (3-10), the wizard (4-10), the title, level select, the open shop and the
  options menu.
- **Time**: the processor time of `Render` and of the ticks, per frame; and with VSync off, the
  frames a second, which is the simplest overall measure.
- **A weak machine.** Without one, the gain can't be shown. Possibilities:
  - The owner's PC lists its second monitor (the PA278CGV) on AMD graphics, so it seems to have
    an integrated AMD chip. Windows can run a program on it: Settings > System > Display >
    Graphics, add `MageBrosNative.exe`, choose "Power saving". The log's first lines
    (`GL_RENDERER`) would confirm which chip drew. Not tried.
  - A player who reports the problem, with a test build that writes the counters to the log.
  - An older laptop.
- **Decide**: if the weak machine holds its refresh rate with Smooth motion on, stop here. If it
  doesn't, see whether lowering Antialiasing, Texture filtering and Render scale cures it (then
  it is the pixel work, and this plan doesn't apply). Only if neither: carry on.

### R1. Send only the state that changed

`ApplyState` keeps a copy of what it last told OpenGL and skips whatever is the same.

- **Once a frame, not once a draw**: the framebuffer, the viewport and the program
  (`Device::BeginFrame`), and the settings read from the config.
- **In groups, each with a "changed" flag set by the `Device` setter that touches it** (set only
  when the new value differs from the stored one):
  - transforms: world, view, projection. The world matrix changes for almost every draw; the
    view and projection only a few times a frame;
  - the material (`SetMaterial`);
  - the light and the ambient colour (set once at start and never again in the game);
  - the texture stage settings (`SetTextureStageState`);
  - the render states behind depth, blending, culling, lighting and shading (`SetRenderState`).
- **The texture and its filtering**: bind only when the texture changes. The filtering is the
  awkward part: today it is written onto the texture at every draw, because one texture can be
  wanted with different filtering. Either remember per `Texture` what filtering it was last
  given and write only a difference, or use OpenGL's sampler objects (in OpenGL 3.3, which the
  port already requires): one small sampler per filtering combination, bound beside the
  texture, and the textures themselves never touched.
- **Shader values that depend on the vertex format or the 2D switch** (`hasVertexColor`,
  `lighting`, `screen2D`, the alpha fix): compare with the last value sent.

**The trap**: anything that talks to OpenGL without going through `ApplyState` leaves the copy
wrong. Each of these must mark the copy as unknown (one `InvalidateState()` call, which makes
the next draw send everything, as today):

- `Device::Present`, `Draw2DQuad`, `DrawUIQuad`, `ColorFill`, `Clear`, `SaveScreenshot`;
- `CreateTargets` and `ComputeLayout` (a window resize, antialiasing or render scale changed);
- `Texture::Create`, `Texture::Update` and a texture's deletion (`D3DTextr_InvalidateTexture`,
  mods): they bind textures, and a deleted texture's number can be given to a new one;
- the options menu's own drawing;
- `Device::Init` and anything that makes the shader program again.

A safe way to build it: first make `ApplyState` able to do both (send everything, or only the
differences) behind a temporary switch, and in a test build have it do the differences and then
check, by asking OpenGL (`glGet*`), that the result is what "send everything" would have set.
Run the whole screenshot suite like that, then remove the check.

### R2. One vertex layout per vertex format

The game uses two vertex formats: `D3DFVF_VERTEX` (position, normal, texture position: models)
and `D3DFVF_LVERTEX` (position, colour, texture position: the level, rectangles, 2D). Today one
vertex layout object (`vao_`) is set up again for each draw. Make two, one per format, set up
once, and bind the one the draw needs. With R3, each kept mesh gets its own.

Small and low risk; worth doing with R1.

### R3. Keep the models and the level on the graphics card

**A warning from the graphics enhancements' G2 (2026-11-11):** drawing the game's draws from one
shared buffer, instead of each from its own upload, gave two pixels of 1-6 one shade apart on
NVIDIA, every time (`GRAPHICS_ENHANCEMENTS.md`, "G2 as built"). Keeping meshes on the card in
buffers of their own may well hit the same, so R3 would have to be checked pixel by pixel and
might not keep rule 1 at all. The recorder G2 built (a level's 3D gathered, then sent) is also
where R1 and R2 would now go.

The largest change, because `Device::DrawIndexedPrimitive` is given a bare pointer to vertices
and can't know whether they are the same as last time. Guessing from the pointer is not safe: a
new level's geometry can land at the same address as the last one's, and rectangles rewrite
their vertices in place.

**The way that fits**: the owner of an unchanging mesh says so.

- A small class in `src/gfx`, say `MeshBuffer`: it holds a vertex buffer, an index buffer and a
  vertex layout on the graphics card, made on its first draw from the vertices and indices it is
  given, and a `Invalidate()` for when those change.
- A new `Device::DrawMesh(MeshBuffer&, vertices, indices, first index, count, format)`, beside
  the two existing draw functions, which stay for everything that changes.
- **Models** (`CD3DFileObject` in `src/framework/d3dfile.cpp`, the port's code): each mesh owns
  one `MeshBuffer`. Its vertices are uploaded **once**, and each material draws its range of the
  one index buffer, where today the vertices go up once per material per frame per object.
  `CD3DFile::Scale`, `Shift` and `RotateX` (used when a model is loaded, and by the ending's
  floor and walls) call `Invalidate()`. Since models are shared (`JModelList`), 40 birds are one
  upload, ever.
- **The level** (`XLevelView::DrawMesh`): each `MapGeometry::Mesh` gets a `MeshBuffer`,
  invalidated when a level is loaded. `XLevelView` is in `src/game`, but this function is the
  port's own drawing of the original's map data, and what it asks to be drawn doesn't change.
- **Freeing**: the buffers must be deleted while the OpenGL context still exists
  (`JModelList::Clear` and the level's meshes before `Device::Shutdown`; the application's
  destructor already frees the textures and the menu first for the same reason).

`src/game`'s objects keep calling `JObject3DX::Render` exactly as now; they never see a
`MeshBuffer`.

### R4. The flat rectangles and the 2D (only if R0 calls for it)

- **Rectangles** (particles, shots, shadows, water): four vertices each, changed often. Uploads
  this small are cheap; with R1 and R2 done, most of their cost is already gone. If the counters
  still point at them: keep one larger buffer for the frame and write each rectangle's vertices
  at the next free place in it (`glBufferSubData`, or mapping it), starting again each frame,
  so the buffer isn't replaced several hundred times.
- **2D quads** (`Draw2DQuad`): each sets about 15 shader values to switch the one shader into its
  2D mode and back. A second, small shader program for 2D alone would need only the colour, the
  texture and the alpha test. The title's tile background (35 quads or more), the fade strips
  and the HUD are the screens to measure.
- **The options menu** (`DrawUIQuad`, a quad per character): many draws, but it runs at 60
  frames a second with the game paused. Leave it.

### Left out on purpose

- **Sorting or merging draws**, instancing, texture atlases: they change the draw order or the
  shader, and with them the picture. Rule 2.
- **A second thread for drawing**: far more risk than this game's few hundred draws justify.
- **Replacing OpenGL** (Vulkan, Direct3D 11): another project. (The paused `dxgi-present` branch
  only changes how the finished picture is shown, not how it is drawn.)

## How to check each chunk

The method used for smooth motion ([SMOOTH_MOTION.md](SMOOTH_MOTION.md), "How the game logic is
shown untouched"), wider, because this touches every picture:

1. **Byte-for-byte screenshots** against the build before the chunk, taken with `--auto`
   (`CLAUDE.md`, "Automation"): the 14 level shots and the shop sliding (24 shots) of smooth
   motion's suite; the logo, title, save slots and level select; the ending; a level at 16:9 and
   21:9; the Tab viewers.
2. **Every Graphics option**, since each changes what `ApplyState` sends: Antialiasing off and
   on, Texture filtering off and 8x, Smooth textures, Smooth 2D images, Render scale 100% and 200%, Brightness, Field of view 100% and 150%.
3. **The special paths**: a mage hit (the white material flash); the see-through pass of models;
   the additive particles (`DESTALPHA`); the water; the shop's panel over the level; Smooth
   motion on at `alpha=0.5`; the options menu opened over a level and closed again (it draws
   through its own path in between, the case most likely to leave the state copy wrong); a
   window resize and Alt+Enter mid-level (the render targets are made again).
4. **OpenGL's own error reports** (`KHR_debug`, a debug context) switched on in a test build, to
   catch a wrong call that happens to draw correctly on one driver.
5. **More than one driver.** A state copy that is wrong can be invisible on one driver and
   visible on another. NVIDIA, and at least one of AMD and Intel.
6. **The counters and the frame rate** of R0 again, on the owner's machine and on the weak one.
   The frame rate on the owner's machine must not fall.
7. The four tests (`ctest`), and the owner's play test.

When a byte-for-byte run fails everywhere, check first that the test folder's ini hasn't kept
`maximized = 1` (the trap noted in SMOOTH_MOTION.md).

## Files

| File | Change |
|---|---|
| `src/gfx/device.{h,cpp}` | R1 the state copy and its flags, `InvalidateState`; R2 two vertex layouts; R3 `DrawMesh`; R4 a frame buffer for rectangles, a 2D program |
| `src/gfx/mesh_buffer.{h,cpp}` (new) | R3 `MeshBuffer` |
| `src/gfx/gl.h` | The OpenGL functions newly used (sampler objects, `glDrawElementsBaseVertex` or offsets, buffer mapping, the debug callback for tests) |
| `src/framework/d3dfile.{h,cpp}` | R3 a `MeshBuffer` per mesh, invalidated by `Scale` / `Shift` / `RotateX` |
| `src/game/XLevelView.cpp`, `XMapGeometry.h` | R3 a `MeshBuffer` per level mesh |
| `src/game/JModel.cpp` | R3 freeing in `JModelList::Clear`, if not done by the meshes themselves |
| `src/framework/d3dapp.cpp` | The order of freeing at exit |

Nothing else in `src/game` changes.

## Size, and when to stop

- R0: an afternoon, plus finding the weak machine.
- R1 and R2 together: the bulk of the calls for a moderate change in one file. A day or two with
  the checks. **Measure again here; this may be all that is needed.**
- R3: about as much again, in more files.
- R4: only on evidence.

Each chunk a version of its own, as the other chunks of the port, each stopping for the owner's
test.
