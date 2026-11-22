## ReiPan PS5 v0.1.0-PZ

The first release of ReiPan for a jailbroken PlayStation 5: the Fatal Frame 3
(Project Zero 3) PC port as a native title that ShadowMount+ mounts, with
everything in ReiPan v0.1.2-PZ for Windows -- the over-the-shoulder camera,
the Scare camera and 60 fps presentation among them.

## The PlayStation 5 title

- **Native PS5 title.** Rendering is SDL_GPU on Vulkan, through
  [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s port of Mesa's RADV
  driver, linked into the title: 1920x1080, and 60 fps with **Extra frames**
  on. Built on the same platform layer as MikuPan's PS5 build.
- **Console drivers for SDL3:** video on the console's display, sound through
  AudioOut, and up to four DualSense controllers.
- **Plays straight from your disc image.** Put the `.iso` -- or `IMG_BD.BIN`
  with `ZERO3.FHD` -- in `/data/ReiPan/` and it is found at start; upper or
  lower case names both work. The movies play from it too, with their sound;
  nothing else needs copying.
- **Pauses at the home screen:** while the home screen, the control centre or
  a system dialog has the controller, the game and its sound wait until you
  are back.
- **Overlay:** L3 + R3 shows and hides the overlay's menu bar, as F1 does on a
  PC (L3 + Options opens the console's own debug menu on a jailbroken PS5).
  Desktop-only options (window mode, HDR, mouse, keyboard bindings) are hidden.
- **Uses the console's cores.** The game's texture streaming is written in
  parallel by four worker threads beside the game's own, and the game is built
  fully optimised: a room's frame takes about 6 ms of the game thread's time,
  so the 60 fps mode holds through the ghost fights.
- **Notifications and logs:** starting and stopping show as console
  notifications. `reipan.log` is written to `/data/ReiPan/` once a second, with
  the frame's timings every five seconds; if the title crashes, `crash.log`
  there says where.

## The game

- **Over-the-shoulder camera**, off by default: **Options → Camera → Camera
  view → Over the shoulder**.
  - The right stick turns it and R3 puts it back behind her; it drifts back
    behind her while she walks. The left stick walks the way it is pushed, as
    the camera sees it.
  - It collides with the rooms and furniture as they are drawn: it looks over
    a low table, stops short of a wall or a closed door, and looks past ropes,
    poles and candles.
  - Raising the Camera Obscura opens it where the camera was looking, and its
    raise and lower animations stay over the shoulder.
  - **Scare camera** decides what happens when the game cuts to an event
    camera while you can still walk (a ghost appearing, something to look at,
    the dream's graveyard path): **Guided** (the default) turns once towards
    what the shot frames, **Original** plays the game's own shot, **Off** stays
    put.
  - Events that take control away, the Camera Obscura, crawling, ladders and
    the damage and death shots keep the game's own cameras.
  - Distance, height, shoulder, field of view, look speed and smoothing are on
    the same page.
- **60 fps presentation:** **Options → Graphics → Extra frames → 1 extra**
  draws a frame between each of the game's own; the game still runs at its own
  pace.
- **Analog stick** (Options → Controls) swaps which stick aims and which walks
  with the Camera Obscura raised.
- Fixes from the Windows builds: the black screen before the first ghost
  fight's cutscene, the black main menu after quitting a game, and striped
  save thumbnails.

## Installing

Only the European version of the game works: **Project Zero 3, SLES-53825**.

1. Copy the `PPSA39393` folder to `/data/homebrew/` on the console (or
   `homebrew/` on a USB drive) for ShadowMount+ to mount. It appears on the
   home screen as **ReiPan PS5**. It is a different title from MikuPan PS5, so
   both can be installed.
2. Copy your disc image (`.iso`) to `/data/ReiPan/`. A copy of the disc's
   files works too: `IMG_BD.BIN` with `ZERO3.FHD` beside it.
3. Start it. If it does not find the game, the first screen says where to put
   it; copy it there and choose **Look Again**.

Settings, saves, the shader cache and `reipan.log` are kept in
`/data/ReiPan/` (or the title's own `/download0/ReiPan/` if `/data` cannot be
written). Please include `reipan.log`, and `crash.log` if there is one, when
reporting a problem.

## Known issues

- There is no on-screen keyboard and no file browser, so PS2 save transfer is
  not available on the console.
- HD font replacements cannot be added: the title's `resources/` folder is
  read-only on the console.
