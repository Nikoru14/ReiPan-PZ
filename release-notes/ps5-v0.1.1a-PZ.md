## ReiPan PS5 v0.1.1a-PZ

A mirror fix from ReiPan. The same fix is in ReiPan v0.1.3a-PZ for Windows.

## Fixes

- **Mirrors facing one particular way reflected the room wrongly.** The
  reflection's rotation had no axis to turn about for a mirror facing that
  way, so its image was broken. From ReiPan (karas84).

Everything from v0.1.1-PZ is unchanged: the over-the-shoulder camera, the Scare
camera and clean 60 fps extra frames.

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
v0.1.1-PZ. Please include `reipan.log`, and `crash.log` if there is one, when
reporting a problem.

## Known issues

- There is no on-screen keyboard and no file browser, so PS2 save transfer is
  not available on the console.
- HD font replacements cannot be added: the title's `resources/` folder is
  read-only on the console.
