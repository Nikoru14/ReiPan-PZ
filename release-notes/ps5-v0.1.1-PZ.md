## ReiPan PS5 v0.1.1-PZ

Clean 60 fps on the console: Rei, the curtains and everything else that
moves no longer flicker, tear or double in the extra frames. The same fixes
are in ReiPan v0.1.3-PZ for Windows.

## Fixes

With **Options → Graphics → Extra frames → 1 extra**, ReiPan draws a frame
between each two of the game's own, with everything halfway between where it
was and where it is. The game rebuilds Rei, the cat and the other characters
from scratch every frame, so for each piece of them ReiPan has to find the
same piece in the frame before. When it took the wrong one, the frame in
between drew it halfway to something else. All of these are fixed:

- **A curtain flashing a bright band** across it, or the light through a
  window jumping, as she ran towards it at certain angles.
- **One of her legs vanishing for a frame**, after petting the cat among
  other times.
- **Her face, eyelids and elbow tearing** for a frame while the camera
  follows her, and while she turns on the spot.
- **A pale double around her head, arm and choker**, and her hair and
  shoulder strap flickering: pieces the game draws differently every frame,
  like the outline around her, now move with her body.
- **Her face breaking into shards with the camera close to it**, and the
  frame rate dropping there.

How it works now: each piece of a character is matched by the part of the
model it is drawn from, which is the same every frame, so it always blends
with itself. A piece that cannot be matched moves in one piece with what
is around it. With the camera close, the work stays at a few milliseconds a
frame, so 60 fps holds.

- **Over-the-shoulder camera:** Rei is hidden while the camera is inside her
  -- backed against a wall, or pitched steeply up or down -- instead of
  showing the inside of her model as dark shards.

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
v0.1.0-PZ. Please include `reipan.log`, and `crash.log` if there is one, when
reporting a problem.

## Known issues

- There is no on-screen keyboard and no file browser, so PS2 save transfer is
  not available on the console.
- HD font replacements cannot be added: the title's `resources/` folder is
  read-only on the console.
