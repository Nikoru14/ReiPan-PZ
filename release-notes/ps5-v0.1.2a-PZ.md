## ReiPan PS5 v0.1.2a-PZ

A fix for the paper streamers on the Stained Corridor's rope. The same fix is
in ReiPan v0.1.4a-PZ for Windows.

## Fixes

- **The paper streamers (shide) hanging from the rope in the Stained
  Corridor burst into long spikes across the screen**, most of all through
  the Camera Obscura. The rope's links carried a stray value that the PS2's
  graphics hardware ignores but the PS5's does not; the streamers now hang
  and sway as on the PS2.

Everything from v0.1.2-PZ is unchanged.

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
