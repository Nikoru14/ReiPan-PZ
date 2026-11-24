## ReiPan PS5 v0.1.2-PZ

Fixes for Rei's hair, head and leg vanishing, and a mirror fix. The same
fixes are in ReiPan v0.1.4-PZ for Windows.

## Fixes

- **The back of Rei's hair went missing**, on the rooftop walkway among
  other places, and stayed missing for the rest of the night until a save
  was loaded.
- **Rei's head vanished for a split second**, and that was what took the
  hair with it.
- **Rei's leg vanished for a moment while petting the cat.**
- **Mirrors facing one particular way reflected the room wrongly** (from
  ReiPan, karas84).

The first three had one cause. On the PS2, the square root of a number a hair
under zero gives a tiny number, and dividing by zero gives the largest
number. On the PS5 both give "not a number" (NaN). When two animation keys
were almost identical, rounding made one bone's blend take that square root.
The bone went NaN for a few frames, and with it everything hanging from it:
her head, or her leg. The spring that sways her hair then kept that NaN for
good. The game now calculates these the way the PS2 does. The same kind of
division by zero is fixed wherever it could happen: cloth, hair chains,
ghosts walking their routes, lights, effects and cameras.

## For troubleshooting

- **Draw inspector** (overlay, L3 + R3: **Debug → Diagnostics / A-B → Draw
  inspector**): hides one draw at a time, a character's pieces or the effect
  sprites, says what each is and why any of its triangles were dropped, and
  writes the frame's draws to `reipan.log`.

## Installing

Only the European version of the game works: **Project Zero 3, SLES-53825**.

1. Copy the `PPSA39393` folder to `/data/homebrew/` on the console (or
   `homebrew/` on a USB drive) for ShadowMount+ to mount, replacing the old
   one. It appears on the home screen as **ReiPan PS5**.
2. Copy your disc image (`.iso`) to `/data/ReiPan/`. A copy of the disc's
   files works too: `IMG_BD.BIN` with `ZERO3.FHD` beside it.
3. Start it. If it does not find the game, the first screen says where to put
   it; copy it there and choose **Look Again**.

Settings, saves and the shader cache in `/data/ReiPan/` carry over from
earlier versions. Please include `reipan.log`, and `crash.log` if there is one, when
reporting a problem.

## Known issues

- There is no on-screen keyboard and no file browser, so PS2 save transfer is
  not available on the console.
- HD font replacements cannot be added: the title's `resources/` folder is
  read-only on the console.
