# ReiPan
Fatal Frame 3 PC Port

## Build

Requires a C/C++ compiler, CMake, Make, Git and SDL3's system development
packages. Dependencies are fetched automatically. Linux builds a minimal static
FFmpeg (MPEG-2 decoder, parser and colour conversion); movie demuxing and PCM
audio are handled by the game. Windows uses prebuilt FFmpeg from `extern/ffmpeg`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --config Release
```

Build Release: a Debug build runs the game at half speed or less. Visual
Studio's generator ignores `CMAKE_BUILD_TYPE` and builds Debug unless told
otherwise, which `--config Release` does; when opening the folder in Visual
Studio, pick the **x64 Release** preset instead of x64 Debug.

A Windows build is also made on GitHub, only when asked for: **Actions →
Windows build → Run workflow** on any branch, then download the
`ReiPan-windows-…` artifact from the run. **Actions → Release** with a version
(say `0.1.0-PZ`) builds the same and publishes it as release `v<version>`, with
`release-notes/v<version>.md` as its description; write and merge that file
first.

The executable is `build/ReiPan/ReiPan`. To reuse checked-in shader bytecode,
configure with `-DREIPAN_COMPILE_SHADERS=OFF`.

## Game files

ReiPan does not include the game, and only the European version works: Project
Zero 3, SLES-53825. The first time it starts without finding it, it asks for your
own copy:

- **Disc image (.iso):** ReiPan plays straight from it.
- **IMG_BD.BIN:** the game's data copied from the disc, with `ZERO3.FHD` from the
  disc in the same folder.

Your choice is checked, then saved as `data_folder` under `[paths]` in
`reipan.ini`. Change it later in **Settings → Game data folder…**. A folder holding those
files beside the executable or above it is found without asking.

## Over-the-shoulder camera

An optional modern camera in place of the game's fixed angles: **Options →
Camera → Camera view → Over the shoulder** (or the overlay's **Settings →
Camera**). It follows the player from behind and beside her:

- The right stick turns it, and R3 puts it back behind her. The left stick walks
  the way it is pushed, as the camera sees it, whatever the Player controls
  option says.
- It collides with the rooms and furniture as they are drawn. It looks over a
  low table, stops short of a wall or a closed door, and looks past ropes,
  poles and candles.
- Raising the Camera Obscura opens the viewfinder where the camera was looking.
- Event cameras, the viewfinder, crawling, ladders and the damage and death
  shots still play as the game made them. The camera picks up again afterwards.

Distance, height, shoulder, field of view, look speed and smoothing are on the
same page and saved under `[over_shoulder_camera]` in `reipan.ini`. The
overlay's menu also shows what the collision is doing.

## Mouse

In play the mouse works as in MikuPan:

- **Looking:** it turns the over-the-shoulder camera and aims the Camera
  Obscura. The pointer is captured while it does, and free again in every menu
  and while the overlay's bar is up (F1 shows and hides it).
- **Right button:** raises the Camera Obscura. **Options → Controls → Camera
  Obscura button** picks **Hold** (up while held) or **Toggle** (click to raise,
  click to lower).
- **With the Camera Obscura up:** left button is the shutter (held charges),
  middle button the lens shot, and the wheel changes film.

Mouse look and its sensitivity are on the same Options page. The overlay's
**Settings → Controls → Mouse** also moves each action to another button.
Everything is saved under `[mouse]` in `reipan.ini`.

## PS2 save transfer

Open **Settings → PS2 save transfer…** to transfer European, US or Japanese
gameplay saves. Choose the target region when exporting. For European PS2 saves,
select the original game's PAL/60 Hz timing so mission time records remain correct.

Import from an extracted save folder, an 8 MB memory card, or a PSU/MAX/XPS/SPS/CBS
archive. Select saves and assign their destination slots. Export formats are:

- **ECC (.ps2):** a memory card with spare/ECC bytes, for PCSX2.
- **Raw (.bin):** an 8 MB card without ECC bytes, for OPL.
- **PSU (.psu):** an EMS archive, for uLaunchELF and PS2 save-transfer tools.
- **MAX (.max):** an Action Replay MAX archive.
- **XPS/SPS (.xps/.sps):** an X-Port V2 / SharkPort archive.
- **CBS (.cbs):** a CodeBreaker archive.
- **Extracted folder:** the game's save files, for tools that accept folders.

Archives contain this game's selected saves, not an entire memory card. Card
exports create a blank card populated with the selected saves. Choosing a format
opens its picker; confirming the destination exports immediately. Missing
extensions are added automatically; existing output files/folders are refused.
Imports replace only the selected destination slots. Load imported saves normally
afterward, and close the game's save/load screen before transferring.

Unavailable saves remain visible with an explanation; other valid slots can still
be transferred. Regional script differences can prevent transferring an active
scene or conversation. The extra US/Japanese Trailer unlock has no European
equivalent and is left unobtained when exporting from ReiPan.

## Font texture replacements

Redraw or upscale the PNGs uniformly up to 4×, preserving glyph layout, colours
and transparency. Copy the hash-named directory into `resources/fonts/` beside
the executable and restart. Enable **HD fonts (if available)** in Renderer or
**Use HD Fonts when available** in Game options. Missing or unmatched files
fall back to the original fonts.
