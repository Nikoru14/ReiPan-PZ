## ReiPan PS5 v0.1.3-PZ

Gyro aiming on the DualSense, smoother walking through camera changes, and
old saves that load again. The same changes are in ReiPan v0.1.5-PZ for
Windows.

## New

- **Gyro aiming** (from ReiPan, with the console's motion sensor now read):
  while the Camera Obscura is raised, turning the DualSense aims it, alongside
  the stick. **Options → Controls → Gyro** sets the sensitivity, yaw or roll
  to turn, and the inversions; the gyro's drift is learned whenever the
  controller lies still with the Camera Obscura down.
- **Gyro recenter behaviour** (same page): the right half of the touch pad
  with the Camera Obscura raised holds the gyro still while pressed, so the
  controller can be brought back to a comfortable angle, and then:
  - **Zero** (the default): the view stays where it is.
  - **Level**: the view tips back to level.
  - **Level and back to start**: it also turns back to where Rei faced when
    she raised the camera.

  The touch pad's left half stays Select.

## Fixes

- **Walking through a camera change no longer fights the over-the-shoulder
  camera.** Holding the stick through a door, a search or an event shot keeps
  Rei walking the way she was, so the cut never turns her round, but coming
  back to the over-the-shoulder camera that old direction used to stick until
  the stick was let go. Now it eases onto the camera's direction within a few
  frames, and steering the stick a new way hands it to the camera at once.
- **Old saves no longer crash when loaded** (from ReiPan), including saves
  made by another build or on another PC: a save kept a pointer that only
  meant something to the build and session that wrote it.

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
