# Changelog

What changed in each version of Mage Bros. Native. Every version is published as a
[GitHub release](https://github.com/Exo-MDR-CD2000/MageBrosNative/releases) with its Windows zip.
You still need your own copy of Mage Bros. 2.0 (the freeware build) to play: put
`MageBrosNative.exe` next to `MageBros.exe`.

Where we are and what comes next: [docs/ROADMAP.md](docs/ROADMAP.md).

## [Unreleased]

- **Much less drawing work per frame, especially on the R36S.** Render state is sent only when it
  changes, static models and level geometry stay on the graphics card, and consecutive HUD/2D
  quads are batched. In level 1-1 the R36S frame went from about 137 draws and 7,300 OpenGL calls
  to about 102 draws and 600 calls, with the renderer's CPU time roughly halved. The dense parts
  of 1-1 and 1-2 now hold a stable 30 FPS instead of dipping to 20. The console currently
  presents every second 60 Hz refresh through its KMSDRM swap path; Smooth motion, VSync,
  borderless mode and the frame limit all leave that 30 FPS pace unchanged.
- **Graphics profile** (Options > Graphics): Manual (default), Performance (the R36S: no
  antialiasing or anisotropy) or Quality (the desktop defaults). A fresh console install starts
  on Performance; changing one of the three settings switches back to Manual.
- Measured frame work in the log and the Full Statistics Overlay: draw calls, OpenGL calls,
  uploaded bytes, logic/render time and GPU wait.
- **R36S:** off-screen torches, needle fans and hammers are no longer drawn; the `mbn_r36s_probe`
  gained `--swap-test` and `--present-test` for checking the console's display path.

## [0.4.47] - 2026-11-13

- **Music plays on macOS without a SoundFont.** The game now uses the Mac's built-in General MIDI
  synthesizer when no `.sf2` is installed, as it already did with the Windows synthesizer on
  Windows. A SoundFont still takes precedence when one is present. The R36S has no system
  synthesizer and still needs one.

## [0.4.46] - 2026-11-12

- The Statistics Overlay (Full) lists the graphics enhancements in effect: ambient occlusion,
  shadows, lights, ambient light and bloom, each as you set it, greyed out where it doesn't run
  (outside levels).

## [0.4.45] - 2026-11-11

- **Bloom** (Options > Graphics, off by default): a soft glow round the flames, the mages' shots,
  flashes, explosions and the warp. **Low** is a gentle halo, **High** a clear glow. Only the
  glowing effects glow, and walls in front of them hide their glow. Off, the game looks exactly
  as before. The Statistics Overlay (Full) shows what it costs (GPU bloom).

## [0.4.44] - 2026-11-11

- **Ambient light** (Options > Graphics, with Dynamic lights on): 100% keeps the level as bright as
  the original; 85% and 70% dim it, so the candles, torches and shots stand out more. The HUD and
  the glowing effects themselves are not dimmed. A trial: tell us which you prefer.
- No preset row on the Graphics page: every option is set on its own.

## [0.4.43] - 2026-11-11

- **Dynamic lights: no more strobing torches.** The light of lit torches and wall candles
  flickered several times a second, worst with more than one on screen: it came from the sparks
  the original sends up from every flame. It now comes from the steady flame alone, so each torch
  gives an even glow. The fire itself looks as it always did.

## [0.4.42] - 2026-11-11

- **Dynamic lights** (Options > Graphics, off by default): the wall candles, lit torches, the
  mages' shots, the dragon's flame, explosions and the warp light the floors, walls and models
  around them, in their own colours, flickering with their flames. **Light strength** (50% to
  150%) sets how bright. In the bright daylight levels the effect is gentle. Off, the game looks
  exactly as before. The Statistics Overlay (Full) shows how many lights are in view.

## [0.4.41] - 2026-11-11

- **Dynamic shadows** (Options > Graphics, off by default): real shadows from the game's own
  light. The walls, the mages, enemies, pillars, spikes, fences and everything solid shade the
  floor, the walls and each other. **Low**, **Medium** and **High** (sharper, a little more
  work for the graphics card), and **Shadow strength** (50% to 100%, 80% to start) for how dark
  they are. The original blob shadows under the mages stay. Off, the game looks exactly as
  before. The Statistics Overlay (Full) shows what the shadows cost (GPU shadows).

## [0.4.40] - 2026-11-11

- **Ambient occlusion is now just Off or On.** The Low setting looked poor, so it is gone; On is
  the former High. If you had chosen Low or High, it is now On.

## [0.4.39] - 2026-11-11

The first test build of the graphics upgrades (from the `graphics-enhancements` branch).

- **Ambient occlusion** (Options > Graphics, off by default): soft shading where floors meet walls,
  at the feet of walls, pillars and fences, round barrels and pots and under the enemies, in
  levels. **Low** works at half resolution and costs less; **High** is sharper. Off, the game
  looks exactly as before. The Statistics Overlay (Full) shows what it costs (GPU AO).
- **Per-pixel lighting and Soft shadows are gone** from Options > Graphics. They made very
  little difference to how the game looks; better shadows and lighting are being worked on to
  take their place. Both were off by default, so the game looks exactly as before; if you had
  turned them on, they are simply ignored.
- **Statistics Overlay** (F9, Full): two new rows, **GPU 3D**, the graphics card's time for a
  level's 3D each frame, and **3D draws**. Behind the scenes a level's 3D is now gathered before
  it is drawn, to make room for the graphics upgrades to come; the picture is exactly the same.
## [0.4.38] - 2026-11-10

- **Render scale now goes up to 200%** (Options > Graphics). 300% and 400% asked far more of your
  graphics card for very little difference on screen. If you had chosen one of them, the game now
  uses 200%.
- The README now says which files of the download you need: only `MageBrosNative.exe`.

## [0.4.37] - 2026-11-10

- **SoundFonts in their own folder**: MIDI SoundFonts (`.sf2`) can now also go in a `soundfonts`
  folder next to `MageBros.exe`, as well as next to it. Options > Audio > Music synth lists them
  all, and `soundfont.sf2` or `magebros.sf2` is still picked up automatically from either place.

## [0.4.36] - 2026-11-10

- **Your own menu font** (Options > Video > Menu font): the options menu, the Statistics Overlay
  and the CHEATS mark use the Classic 8x8 pixel font, as always, or a font of your own. Put
  `.ttf` or `.otf` font files in a `fonts` folder next to `MageBros.exe`, then pick one in the
  menu: it is drawn sharp at your screen's resolution. Fixed-width (monospace) fonts fit best;
  everything stays in the same place with any font. No fonts are added to the game itself.

## [0.4.35] - 2026-11-10

- **The Statistics Overlay has graphs**: the time between pictures (an even line is smooth, a red
  bar is a stutter, the gold line is what to expect from your refresh rate or frame limit), Smooth
  motion's blend (an even pattern means even movement) and the game's steps in each picture, for
  the last couple of seconds. They sit under the figures; in Full on a 4:3 screen they get a small
  panel of their own beside it.

## [0.4.34] - 2026-11-10

- **Statistics Overlay** (F9, or Options > Video): a see-through panel at the left of the screen
  showing how the game is running and on what. **Compact** shows the frame rate, frame times,
  processor time and the game's own timing (its 100 steps a second); **Full** adds the display
  settings actually in effect (window, picture size, refresh rate, VSync, frame limit) and your
  graphics card, driver and antialiasing. F9 cycles Off, Compact and Full, and the game remembers
  your choice. It shows in F12 screenshots, so you can send one with a report. It only shows
  figures: the game plays exactly the same with it on, and it costs next to nothing.
- F9 can no longer be bound to a game action (F9 to F12 are reserved).

## [0.4.33] - 2026-11-10

- **Every enemy in view moves on the widest screens**: the original keeps some enemies (slimes,
  bees, wallabies, mushrooms, squids, kiwis, turtles, ghosts and fish) still until you come near.
  Its 4:3 screen never showed them waiting, but at 32:9, and in the pits and against the back
  wall at 21:9, they could be seen frozen at the screen's edges. They now start moving as soon
  as they can be seen. Nothing changes up to 16:9 (or with Field of view at 100% and Aspect at
  4:3 or Stretch): there the game is exactly as before.
- The options menu shows the game's version at the top right of every page (as the window's
  title bar does), so you can tell which build you are playing.

## [0.4.32] - 2026-11-10

- **A much more even frame limit**: with a Frame limit set, pictures now start within a tenth of
  a millisecond of their turn; before, they could be more than a millisecond late and the next
  one early. This shows most on G-Sync and FreeSync monitors, which follow each picture as it
  comes. It uses a little more processor time while a limit is on.

## [0.4.31] - 2026-11-10

- With Smooth motion off, each picture is now drawn right when its game step is due (within
  a twentieth of a millisecond; it was about a millisecond late, by a different amount each
  time), so the original's 100 pictures a second come more evenly.

## [0.4.30] - 2026-11-09

- Fixed: with VSync off and a frame cap set a little under the refresh rate in your graphics
  driver (141 on a 144 Hz monitor, as often advised for G-Sync and FreeSync), the VSync row
  wrongly said the driver was forcing VSync on.

## [0.4.29] - 2026-11-09

- **Low latency** (Options > Video, off by default): with VSync on, graphics drivers may keep a
  few finished pictures waiting in a queue, each adding one screen refresh between pressing a
  key and seeing the result. With Low latency on, the game waits for each picture to be on its
  way to the screen before starting the next, so none wait. The frame rate stays the same.

## [0.4.28] - 2026-11-09

- **Frame limits that fit your monitor**: Options > Video > Frame limit now offers your
  monitor's refresh rate and its half, third and quarter (on a 144 Hz monitor: 144, 72, 48 and
  36). With one of these every picture stays on screen equally long, so movement is even; a
  limit that doesn't divide the refresh rate (60 on 144 Hz) shows pictures for two or three
  refreshes in turn, which looks choppy on a monitor without G-Sync or FreeSync. With VSync on,
  the graphics driver keeps these limits exactly in step with the monitor. As every frame
  limit, they apply with Smooth motion on.
- The Frame limit list also has 50, 100, 180 and 200 now.

## [0.4.27] - 2026-11-09

The first of a set of fixes to frame timing, found in a review of refresh rates, VSync, the
frame limit and Smooth motion (versions 0.4.27 to 0.4.32).

- Fixed: with Smooth motion on, after opening the options menu or pausing once, the game's steps
  ran up to a millisecond late for the rest of the session, so now and then a picture stood
  still for a moment. Movement is now as even after the menu or a pause as before it.
- **`MageBrosNative.exe` has its own icon**: a blue wizard hat on a tiled floor before a
  torchlit brick wall, shown in Explorer, on the taskbar and in the game's title bar. Its
  version now shows under Properties > Details. The README has a banner in the same style.

## [0.4.26] - 2026-11-09

The last four cheats in Options > Cheats, for moving and fighting. Off by default, and they can be
turned on or off at any time:

- **Moon jump**: jump 150% or 200% as high. Falling is unchanged.
- **Run speed**: walk 125%, 150% or 200% as fast.
- **One-hit kills**: every enemy and boss your shots can hurt falls to one shot. Turtles and ghosts
  still can't be shot, as in the original.
- **Rapid fire**: hold fire to keep firing, 8 shots a second. Each shot still costs magic unless
  Infinite magic is on.

That completes the cheats planned for Mage Bros. Native.

## [0.4.25] - 2026-11-09

Three more cheats in Options > Cheats, for items. They apply when each level starts, to every
player in the game, and what they give stays in your save, as if you had bought it:

- **Max magic level**: magic level 7, the fastest magic recovery.
- **Choose your wand**: start with any of the six wands (single, double, triple, or the homing
  single, double and triple), or Off to keep the one you have.
- **Max gold**: 9999 gold.

## [0.4.24] - 2026-11-09

Four more cheats in Options > Cheats, for staying alive. Like every cheat they are off by default,
and they can be turned on or off at any time, in a level too:

- **Infinite lives**: enemies, bullets and flames still hit you (you flash and are knocked back),
  but never take your last life.
- **Invincible**: enemies, bullets and flames can't hurt you or push you at all.
- **Infinite magic**: firing costs no magic.
- **No falling deaths**: falling into a pit or the water puts you back where you last stood on the
  level's floor (not a moving platform), flashing for a moment, after the splash on water levels.
  In Mage Bros. a fall ends the level outright whatever your life, so Infinite lives and Invincible
  don't cover falls: this one does.

## [0.4.23] - 2026-11-09

The first of the cheats:

- **Options > Cheats is open**, with **Unlock all levels**: every level of all four worlds can be
  picked on level select. Turning it on writes nothing to your save, so turning it off again
  brings back exactly the levels you had reached (levels you clear while it is on count as
  usual). Change it from the options on the title screen. More cheats come in the next versions;
  all are off by default, and with them off the game plays exactly as the original.
- While any cheat is on, a small **CHEATS** mark shows in the bottom-left corner on level select
  and in levels.
- **The settings file names the game's version**: `magebros_native.ini` now has a
  `game_version` line at the top, the version of Mage Bros. Native that last wrote it, so you can
  tell which build a settings file came from. Starting a different version updates it; your
  settings are kept.
- For building it yourself: `make_release.bat` now builds the current version as it is when run
  with nothing after it (or double-clicked), instead of raising the version. Raise it with
  `make_release.bat patch`, `minor` or `major`, or set one with `make_release.bat 0.4.23`. Step
  by step: [docs/RELEASING.md](docs/RELEASING.md).

## [0.4.21] - 2026-10-29

A second look at Options > Graphics:

- **Texture filtering now works on the game's own textures.** The floors, walls and objects are
  drawn with the original's sharp, pixelated textures, and the setting didn't reach them: its
  levels (2x to 16x) all looked the same. Now, with it on, floors and walls far away or seen at an
  angle stay sharp without the flickering pixel noise, while up close the textures keep the
  original's pixels. Off, the game looks exactly like the original. (On by default, at 8x, as
  before; the difference shows most on walls seen at a steep angle and while the screen scrolls.)
- **Render scale above 200% now pays off.** The larger picture is now shrunk to your screen
  properly, so 300% and 400% give smoother edges and detail than 200%; before, most of the extra
  work was thrown away.
- **Screenshots (F12) now include the Brightness setting**, as you see it on screen.
- The help lines for Texture filtering and Render scale say what they do more plainly.

## [0.4.20] - 2026-10-28

**Smooth motion** (Options > Video > Smooth motion, off by default). This release brings together
versions 0.4.16 to 0.4.20, which were built and tested before being released; each one's own
notes are below.

- **What it does.** The game runs in steps, 100 a second, as the original. Without Smooth motion,
  a picture is drawn only when a step has run, so on a 144 Hz screen some pictures are shown
  twice and movement judders slightly. With Smooth motion on, a picture is drawn for every refresh
  of your monitor, showing the game between its last two steps, so everything moves smoothly at
  any refresh rate: 60, 144, 240 Hz or more.
- **What moves smoothly**: the camera, your mage (or both), their shadows, enemies, every boss
  (the dragon's whole body, the genie, the nest and its birds, the wizard), platforms and anyone
  riding them, cannons, coins, bullets, smoke, sparkles, explosions, your shots, the dragon's
  flame, the water's ripples, the shop sliding open and closed, and the ending's credits.
- **The game itself doesn't change**: the same speed, timing, jumps, enemies and controls, with
  Smooth motion on or off. The only cost is that the picture trails the game by at most 10 ms
  (about 5 on average).
- **Kept as the original on purpose**: animation frames, effects growing and fading, and things
  that move a whole pixel at a time (the menus' scrolling background, the ending's film strips,
  the screen transitions). The ending's corridor keeps the speed it had at your frame rate,
  scrolling evenly.
- **Smooth motion is off by default**, so the game looks and feels like the original until you
  turn it on. If you had it on before (it used to be on by default, but only repeated pictures),
  it is switched off once when you update.
- Also: a mage's shadow no longer lags a step behind while a moving platform carries it (with
  Smooth motion on or off).

New in 0.4.20 itself:

- The help lines in Options > Video, the notes in `magebros_native.ini` and the README describe
  Smooth motion in full, and what it costs.
- The Frame limit's help line says it only applies with Smooth motion, and that VSync also caps
  the frame rate.

## [0.4.19] - 2026-10-28

- **Smooth motion, fourth part**: with Smooth motion on, the shop now slides open and closed
  smoothly (the board, the items, the shopkeeper and the dimmed background), and the ending's
  credits glide in and out evenly.
- With Smooth motion on, the ending's corridor now scrolls evenly, at the same speed as before.
  On a 144 Hz screen it used to stand still on about one picture in three, which looked jittery.
- The menus' scrolling backgrounds, the ending's film strips and the venetian-blind screen
  transitions keep the original's look, moving a whole pixel at a time.

## [0.4.18] - 2026-10-28

- **Smooth motion, third part**: with Smooth motion on, the rest of a level now moves smoothly
  too: smoke, sparkles, flashes, explosions and the other effects, your shots and their trails,
  the dragon's flame, and the water's drifting ripples. Everything that moves in a level is now
  drawn between the game's steps.
- Effects that grow, shrink or fade still do so in the game's steps (in small steps of a few
  percent), as do animated pictures such as the flame's flicker.
- As before, Smooth motion changes only what you see: the game plays exactly the same.

## [0.4.17] - 2026-10-28

- **Smooth motion, second part**: with Smooth motion on, everything with a model in a level is now
  drawn between the game's steps too: enemies, every boss (the dragon's whole body, the genie,
  the nest and its birds, the wizard with its blade and wand), platforms, cannons, the genie's
  lamp, coins, and cannonballs, needle balls, rockets and the wizard's fireballs. Their shadows
  follow them smoothly.
- A mage riding a moving platform now moves smoothly with it, with its shadow on the platform.
- Things that appear (a coin popping out, a new cannonball, the wizard's wand, the genie leaving
  its lamp) show up where they are, instead of gliding in.
- Still in the game's steps for now: particle effects, the mages' shots, the dragon's flame and
  the water's ripples (next part).
- Fixed (0.4.16, Smooth motion on or off): a mage's shadow could sit up to a step behind the mage
  while a moving platform carried it.

## [0.4.16] - 2026-10-27

- **Smooth motion, first part** (Options > Video > Smooth motion): when it's on, your mage (or
  both mages), their shadows and the camera are drawn between the game's steps, so walking,
  jumping, falling and the screen's scrolling are smooth at any refresh rate (144 Hz, 240 Hz,
  and 60 Hz too). Everything else in a level still moves in the game's steps for now; it follows
  in the next parts. A mage riding a moving platform can drift slightly against it until then.
- Smooth motion changes only what you see: the game runs exactly as before, at the same speed,
  with your keys read at the same moments. The picture trails the game by at most 10 ms (about
  5 ms on average).
- **Smooth motion is now off by default**, so the game looks and feels like the original unless
  you turn it on. It was on before but only repeated pictures. Your setting is switched off once
  when you update; turn it back on in Options > Video if you want it.
- Pausing (P) and the options menu no longer make the picture jump when the game carries on.

## [0.4.15] - 2026-10-26

- **Field of view (experimental)**: Options > Video > Field of view now works, from 50% (zoomed in) to 200%
  (zoomed out), in 5% steps; the menu shows the lens angle too (100% = the original's 45
  degrees). It changes the view in levels only: the title, level select, the shop's panel, the
  HUD and the ending keep the original view. Zoomed out, objects keep showing to the screen's
  edges and sounds are panned to match the wider view.
- The original's levels are built only as far as its own view reaches, so above 100% their
  unbuilt edges come into view: black below the level's near edge (from about 105%), and at
  stronger zoom-out above the back wall and past the level's ends. Far-away enemies, which the
  original keeps still until you are near, can also be seen standing still. The help line says
  so. **The setting is experimental: we recommend leaving it at 100%**, the view the original's
  levels were made for.

## [0.4.14] - 2026-10-26

- Fixed: at the start of level 3-9 a black strip showed on the wall to the left, just above the
  water, because the level opens on a row of pits and the original game never built the wall
  below floor level there (a slip in the original game too). The wall now carries on down to the
  water in the same brick as every other pit edge. No other level is affected.

## [0.4.13] - 2026-10-26

- **HUD settings**: Options > Video > HUD position, HUD max width and HUD size now work.
  - **HUD position** (widescreen): Centered keeps the HUD where the original has it, inside the
    middle 4:3 area (the default); Edges moves the level information to the left edge, the
    players' panels to the right edge and the FPS counter to the bottom-right corner.
  - **HUD max width**: with Edges, how far the HUD may spread on very wide screens (21:9 by
    default, so a 32:9 screen keeps it off the far corners).
  - **HUD size**: 50% to 200%, each part growing from its corner (the FPS counter too). When the
    level information and the panels would meet (two players on a boss level, or a narrow
    screen), the HUD stops at the largest size that fits.
- HUD position now starts as Centered. Settings files from earlier versions are set back to
  Centered once (the setting did nothing before).
- Fixed: in a two-player game, when both mages lost gold at once, player 1's "you have lost ...
  gold" message covered the top of player 2's, clipping its mage icon (a slip in the original
  game too). Player 2's message is now a little lower, so both show in full.
- The original game's known slips and quirks, and what this port does about each, are now listed
  at the end of `docs/REVERSE_ENGINEERING.md`.

## [0.4.12] - 2026-10-26

- **Test shortcut to the ending**: press F3 during a level to jump straight to the ending, or
  start the game with `--ending` to open on it. Like the other test keys (N / B, F2), nothing
  is saved.

## [0.4.11] - 2026-10-26

- **The ending in widescreen**: the black film-strip bands at the top and bottom of the ending now
  run the whole width of a wide screen, where before the corridor showed through above and below
  them at the sides. The credits now slide in from the edge of the wide screen and out past the
  other edge, instead of waiting in view at the right and vanishing at the left. In 4:3 nothing
  changes.

## [0.4.10] - 2026-10-25

- **The Saito Games logo fills widescreen**: the white behind the logo at start-up now reaches
  the edges of a wide screen (16:9, 21:9 and wider) instead of stopping with black bars at the
  sides. The logo itself is unchanged, and in 4:3 nothing changes.

## [0.4.9] - 2026-10-25

- **The shop takes your usual keys**: buying an item or choosing EXIT now works with any key
  that fires or jumps for you in the level, and with Enter. Before, only Right Shift and Right
  Ctrl worked on the keyboard (player 2: Left Shift and Left Ctrl), as in the original, so the
  `[` and `]` keys, the one-player Left Shift / Left Ctrl and Enter did nothing in the shop.
  Keys you changed in Options > Controls count too.

## [0.4.8] - 2026-10-25

- **The shop in widescreen**: the shop's board and items now slide in from the edges of a wide
  screen (and back out to them) instead of popping up at the sides, and the level behind the shop
  is dimmed across the whole screen while it is open, so the menu stands out. The life, gold and
  magic display stays bright. In 4:3 the shop is exactly the original, without the dimming.

## [0.4.7] - 2026-10-25

- **The green background fills widescreen**: the scrolling green tiles behind the title, the save
  slots and level select now cover the whole screen in widescreen (16:9, 21:9 and wider), where
  before the left edge was black and the right edge partly so. The middle is exactly the
  original's, and in 4:3 nothing changes.
- **Fades cover the whole screen**: the striped fade between screens (and when a level starts or
  ends) now reaches the edges of a wide screen instead of only the middle.

## [0.4.6] - 2026-10-25

- **Widescreen sound**: sounds are panned left and right by where they happen, as in the
  original. In widescreen, things at the edges of the wider screen sounded much quieter than at
  the edges of the original screen, and on 21:9 some objects you could see made no sound at all.
  The panning now follows the shape of the window (any size, even while you resize it), so a
  sound at the edge of a wide screen is as loud and as far to that side as one at the edge of the
  original. In 4:3 nothing changes.

## [0.4.5] - 2026-10-25

- **No more pop-in at the edges in widescreen**: tall pillars, hammers and other objects near the
  back of a level or down in its pits could appear and vanish at the left and right edges of a
  wide screen. The original only draws objects close to the middle of its 4:3 view; the game now
  draws as far as the wider view reaches (at 4:3 it is exactly the original). What objects do,
  and when enemies wake up, is unchanged.
- Ultrawide (21:9) now plays properly: every object, enemy and boss shows and behaves normally
  right to the edges of the screen.

## [0.4.4] - 2026-10-25

- **Adaptive VSync**: Options > Video > VSync is now Off, On or Adaptive. Adaptive waits for the
  screen like On, but a picture that comes too late shows at once (a little tearing instead of
  a stutter). It is offered when your graphics driver supports it.
- **VSync tells you when it isn't in charge**: your graphics driver's own setting (NVIDIA
  Control Panel, AMD Software) can override the game's. When the frame rate shows that it does,
  the VSync row's help line says so. The log records what took effect.
- Fixed: the frame limit ran slightly slow (60 gave 58 frames a second); it is exact now.
- The help lines for VSync, Smooth motion and Frame limit explain how the three work together.

## [0.4.3] - 2026-10-25

- **Monitors are remembered by name**: if you plug monitors in or out, or Windows renumbers
  them, the game still opens on the monitor you chose. If that monitor isn't connected, the game
  uses your main monitor and goes back to the chosen one once it is there again.
- **Clearer Monitor row**: Options > Video > Monitor shows the monitor's number and resolution,
  and the line below it the full name and refresh rate. The log (`magebros_native.log`) lists
  every monitor at each start.
- Fixed: going back to a window could put it on another monitor than the one chosen (where it
  had been before), and F11 then went fullscreen there. A remembered position now only counts on
  the chosen monitor, and F11 goes fullscreen on the monitor the window has just been moved to.
- Fixed: in borderless fullscreen the picture was laid out one row taller than the screen
  (an extra row kept off the screen so graphics drivers don't switch to exclusive mode). It now
  fits the screen exactly.

## [0.4.2] - 2026-10-25

- **Refresh rate for exclusive fullscreen**: Options > Video has a new Refresh rate row. In
  exclusive fullscreen you can pick any rate your monitor offers at the chosen resolution (60,
  120, 144, 165 Hz...), or Desktop, your desktop's own rate (the default, as before). It is
  applied with Enter and the usual keep / revert question. The game itself still runs at the
  same speed; this only sets how often the screen refreshes.
- **A shorter resolution list** for exclusive fullscreen: modes smaller than the game's
  640 x 480 are gone, Desktop comes last, and the list stops at its ends instead of jumping from
  Desktop to the smallest mode.
- While a display change waits for Enter, the `*` now marks only the rows that will change.

## [0.4.1] - 2026-10-25

- **The window remembers itself**: its size (resizing it by dragging the edges too), where it
  was, whether it was maximized and which monitor it was on are saved and restored on the next
  start. If that spot is no longer on a monitor (one was unplugged, say) it opens centred.
- **F11 / Alt+Enter** now goes from the window to the fullscreen mode you used last, borderless
  or exclusive, and the choice is saved.
- **Window sizes**: Options > Video > Resolution lists the game's own 4:3 sizes first, from
  640 x 480 up, with whole multiples marked "2x", "3x" and so on (the 2D art scales evenly at
  those), then wider sizes. Only sizes that fit your monitor with the title bar are offered.
- **The first start** opens the largest 4:3 window that fits your monitor, instead of always
  1280 x 960 (2560 x 1920 on a 4K screen). "Reset all settings" does the same.
- A window on a monitor with a different Windows display scale no longer grows a little every
  time the game starts.

## [0.4.0] - 2026-10-25

Stage 7 begins: the options menu (F10 or gamepad Start), brought over from Hopmon Native, is
now made for Mage Bros.

- **Pause**: P pauses and resumes the game, as in the original (the manual's "P: Pause").
- **FPS counter**: Options > Video > Show FPS now shows the original's own frames-per-second
  counter in the bottom right of every screen.
- **Music pauses** while the game window is in the background, as in the original.
- **Replacement music works**: MP3 or FLAC files named `Music01` to `Music06` in a `music`
  folder next to `MageBros.exe` now play instead of the MIDI tracks (Options > Audio > Music
  source).
- **Controls**: the keyboard page now has player 1's keys, player 2's keys and the general keys
  (select, pause, back), each of which can be rebound. The gamepad page sets the fire and jump
  buttons. In the menu, Ctrl now selects as Enter does (Z and X, Hopmon's keys, no longer do).
- **The menu** plays the title menu's sounds. It offers **Main menu**, back to the title, and
  in a level also **Level select**, which leaves the level for the world map with your gold,
  life, magic and wand as they were when the level started (neither saves the level you leave). It opens only
  while you can play: not during the logo, the ending, the fades or a mage's death.
- Settings that aren't ready for Mage Bros. yet are greyed out: the HUD options, the field of
  view, cheats and mods. Hopmon's two cheats are gone. The ini's notes on keys, buttons, the HUD
  and cheats describe Mage Bros. now; an older ini is rewritten with them, keeping your settings.
- The whole game has been played from start to finish on 0.3.6: stages 5 and 6 are complete.

## [0.3.6] - 2026-10-24

- **The ending**: clearing World 4's last level now plays the original's ending instead of
  jumping back to the logo. The two mages walk side by side down a long corridor between
  scrolling film strips, with the menu music, while four credits slide past: the creator, the
  thanks, "The End" and the Saito Games logo. Press a button on the last one to return to the
  logo.

## [0.3.5] - 2026-10-24

- **The shop**: walking up to a shop's door opens the original's MAGIC SHOP and pauses the
  level. Spend gold on a heal potion (one more heart), a magic potion (magic refills faster) and
  two wands that change with each world. Items you can't afford or already have are greyed out.
  The board shows the shopper's magic level and wand. Each player shops with their own keys or
  gamepad and their own gold. As in the original, Enter doesn't buy (use your fire or jump key),
  and Escape leaves the shop but also gives up the level.
- **The wands**: each wand now fires its own shots, as in the original. The double wand fires
  two shots side by side. The triple wand fires one shot and two smaller ones spread wider. The
  X wands fire the same patterns as homing shots: after a moment each turns towards the enemy it
  picked as it was fired, or the boss if no enemy was near.

## [0.3.4] - 2026-10-24

- **Level select**: after the save slots comes the original's world map, with the level dots
  of each world lit up to the level reached, a tick on each cleared world, and your gold and
  life at the top. START plays the marked level, SELECT LEVEL moves the marker to any level
  reached, BACK saves and returns to the title.
- **The real level endings**: losing a level now saves and returns to level select (it no longer
  just restarts), and clearing a world's last level saves and returns to level select at the next
  world, as in the original.

## [0.3.3] - 2026-10-24

- **The save slots**: after the title comes the original's slot screen, with 8 slots for
  one-player games and 8 for two-player games. Each saved slot shows when it was saved and the
  level reached. Choose a slot to continue it (an empty one starts a new game), or press Delete
  to delete a saved game, answering OKAY or CANCEL. Until level select arrives, a slot starts
  straight at the level reached.
- The menu music carries on from the title into the save slots, and a level replayed after a
  death keeps its music playing, as in the original.
- Jumping between levels with the test keys (N / B, F2) no longer saves progress.

## [0.3.2] - 2026-10-24

- **Saving**: progress is saved after every level, as in the original: the level you have
  reached, the worlds you have cleared, and each player's gold, life, magic and wand. Saves go to
  `%APPDATA%\MageBrosNative\saves` (or the folder set by `save_path` in `magebros_native.ini`),
  written safely with a backup of the previous save. Saves from the original game are not
  carried over.
- Until the save slots arrive, 1 PLAYER and 2 PLAYERS continue the first slot of their kind (a
  new game the first time).

## [0.3.1] - 2026-10-24

- **The title screen**: after the logo comes the original's title, with its scrolling
  background, the pulsing Mage Bros. logo, the two mages fading in and the menu music. Choose
  1 PLAYER, 2 PLAYERS or EXIT with the arrow keys, W / S or a gamepad, and Enter, Shift, Ctrl or
  a gamepad button; Escape picks EXIT. Until the save slots and level select arrive, both games
  start at World 1-1.

## [0.3.0] - 2026-10-24

The front end begins (stage 6).

- **The logo**: the game opens on the original's Saito Games logo with its jingle, as the original
  does, before going into the game (the title screen comes next).
- **Fades**: every level fades in and out with the original's venetian blind of black strips.
  A cleared level waits 2 seconds before fading out to the next, a lost one 3 seconds.
- Escape only gives up a level once it has finished fading in, as in the original.
- Leaving world 3 goes back to world 1 until worlds 1 to 3 are all cleared, which opens world 4,
  as in the original. After the last world the game goes back to the logo for now (the ending
  comes later in stage 6).

## [0.2.24] - 2026-10-24

- **Player 2's shadow on platforms**: player 2 now casts its shadow on the platform under it, as
  player 1 does.
- Player 2's status panel was checked against the original's drawing code: it is drawn exactly as
  player 1's.

## [0.2.23] - 2026-10-24

- **The mages bump into each other** and no longer pass through each other.
- **Standing on each other**: a mage can land on the other's head and stand there, jump off it,
  and bump its head on the mage above when jumping underneath. The mage underneath carries the
  one on top as it walks, and on a lift both go up and down together.
- A mage floated by a floor fan doesn't land on the other one.

## [0.2.22] - 2026-10-24

- **Two players**: press F2 in the game (it restarts the level) or start with `--two-player`.
  Player 2 is the second mage, with D, A, W, S to move, Left Ctrl or `1` to fire, Left Shift or
  `2` to jump, or the second gamepad. Each player has their own life, gold and magic and their
  own colour of shot, and the status display shows both. The camera follows the point between
  the two mages. Enemies chase whichever mage is closer, and coins go to whoever picks them up.
- In a two-player game the level is cleared once one mage has warped and the other has warped or
  died, and lost only when both have died.
- Not yet: the mages can't stand on or carry each other (they pass through each other), and
  player 2 casts no shadow on platforms. Both come next.

## [0.2.21] - 2026-10-24

- **The wizard's wand**: it holds out its wand, glowing at the tip, and fires fireballs that grow
  as they fly and home in on the mage, faster and more often as the attack goes on.
- **Every object in all 40 levels is now in the game**: all four worlds can be played with
  player 1. (Two-player co-op comes next.)

## [0.2.20] - 2026-10-24

- **The wizard**, World 4's boss (4-10), with 10 hearts: it rises out of the pit when the mage
  reaches the far edge of the arena, glides in, circles the middle faster and closer, then stops on
  the other side and glides out to attack.
- **The wizard's blade**: a great spinning blade whose wind blows the mage along the arena towards
  the pit. (Its wand attack comes in the next version; until then it skips those turns.)

## [0.2.19] - 2026-10-24

- **The nest**, World 3's boss (also in 4-8): a great nest floating in the water, turning slowly,
  with 10 hearts. Every hit makes it bob and splash and sends one of its 40 birds after the mage;
  each bird chases the mage for a while, circles the nest and flies back in. When the nest falls,
  every bird out of it bursts.
- **All of World 3 is playable**, 3-1 to 3-10.

## [0.2.18] - 2026-10-23

- **Ghosts** (World 3, also 4-9): they drift about through walls, bobbing in the air. Get close
  and one turns to face the mage, then swoops down at it with a wail. Shots can't kill a ghost,
  but they knock it back.
- **World 3 is playable up to 3-9**: only the boss level, 3-10, is left.

## [0.2.17] - 2026-10-23

- **Floor fans** (World 3, also 4-9): fans set in the floor whose spinning blades blow upward.
  Jump over one and the mage floats up on the air, bobbing about two body heights above it; its
  blades still hurt if you walk into them.
- Level 3-4 now has nothing left to build.

## [0.2.16] - 2026-10-23

- **Rocket launchers** (World 3, also 4-1, 4-4 and 4-7): every 10 seconds a rocket rises slowly
  out of the launcher, turns to face the mage, then launches and homes in on it, trailing smoke.
- Level 3-3 now has nothing left to build.
- This changelog, and each GitHub release now shows its section of it with a link to the whole
  file. (The release workflow also moved to `actions/checkout@v7`.)

## [0.2.15] - 2026-10-23

- **Stalactites** (World 3): they hang from the ceiling, drop spinning when the mage comes near,
  and land with a thud, a screen shake and a cloud of dust, staying stuck in the floor.
- Levels 3-2, 3-6 and 3-9 now have nothing left to build.

## [0.2.14] - 2026-10-23

- **Jump needles** (World 3): spikes sunk in the floor that spring up when the mage comes near,
  stay up for 2 seconds and sink back.
- Level 3-1 now has nothing left to build.

## [0.2.13] - 2026-10-23

- **Pots** (World 3): large vases and small pots that block the way.
- **Kiwis** (World 3): they wander, and run away when the mage comes close or shoots them.

## [0.2.12] - 2026-10-23

- **The genie's attack**: it opens its mouth and fires spiked balls that roll after the mage.
- **All of World 2 is playable**, 2-1 to 2-10.

## [0.2.11] - 2026-10-23

- **The genie**, World 2's boss (also in 4-5): it hides in its lamp until the mage comes near,
  the lamp rocks and steams, and the genie rises out of the spout. It floats about, turns to face
  the mage, glides between three spots and has 10 hearts. (Its attack came in 0.2.12.)

## [0.2.10] - 2026-10-23

- **Turtles** (World 2): they walk slowly and can't be killed; a shot sends them into their
  shells for a few seconds.
- Levels 2-1 to 2-9 now have nothing left to build.

## [0.2.9] - 2026-10-23

- **Fans** (World 2): fans on posts that blow the mage along the level or across it.

## [0.2.8] - 2026-10-23

- **Hammers** (World 2): a spiked roller that rises when the mage comes near, turns to face it
  and slams down, front or back.
- **Screen shake**, as in the original, when a hammer lands.

## [0.2.7] - 2026-10-23

Also contains 0.2.6, which was built for testing but not released on its own.

- **Needle balls** (World 2): spiked balls that roll back and forth.
- **Squids** and the king squid (World 2, also World 4): they crouch, jump in place to turn and
  walk.

## [0.2.5] - 2026-10-23

- World 2 begins: **trees**, **potted plants** and hopping **mushrooms** (with the king mushroom).

## [0.2.4] - 2026-10-23

- The game view clears to black, as the original does, so the pits show black under the water.

## [0.2.3] - 2026-10-22

- **Wall candles** on the back wall, with their flames.

## [0.2.2] - 2026-10-22

- The original's lighting and lens, and its handling of scaled models' lighting.

## [0.2.1] - 2026-10-22

- **Fence doors** are now the wooden gates of the original (they had been built as metal fence
  pieces, which made 1-2 impossible to finish).
- **The shop** building stands in 1-3, 1-6 and 1-9 (its screen comes later).

## [0.2.0] - 2026-10-22

- **World 1 is playable from start to finish**, 1-1 to 1-10, with player 1.

## [0.1.4] - 2026-10-22

- Pits fixed: the cliff faces are built only under floor edges, as in the original.

## [0.1.3] - 2026-10-22

- **The dragon**, World 1's boss: it swims round its pools, crosses the floor breathing fire and
  shows its hearts on the status display; defeating it opens the goal.

## [0.1.2] - 2026-10-22

- **Water** in the pits (falling in costs a life, with a splash) and **fish** that leap out of it.

## [0.1.1] - 2026-10-22

- **Cannons** and their cannonballs, and all seven kinds of **moving platform**.
- `make_release.bat` raises the version itself and runs the game tests.

## [0.1.0] - 2026-10-21

The first test build:

- Reads every asset from your own `MageBros.exe`; sound, MIDI music, keyboard and gamepad, the
  options menu.
- A level viewer showing all 40 levels, and a model viewer.
- World 1 with player 1: movement, shots, magic, torches, coins, the goal and warp, the status
  display, the death sequence, shadows and particle effects, slimes, bees, wallabies, spikes,
  needle fans, barrels and fence doors.
