# ReiPan
Fatal Frame 3 PC Port

![logo](resources/reipan.png)

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
- **Scare camera** decides what happens when the game cuts to an event camera
  while you can still walk (a ghost appearing, something to look at, the
  dream's graveyard path):
  - **Guided** (the default) stays over the shoulder and turns once towards
    what the event's shot frames. The right stick or mouse takes over at once.
  - **Original** plays the game's own shot.
  - **Off** stays over the shoulder and turns nowhere.
- Events that take control away, the viewfinder, crawling, ladders and the
  damage and death shots still play as the game made them. The camera picks up
  again afterwards.

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

## PlayStation 5 (experimental)

ReiPan cross-builds for a jailbroken PS5 as a native title folder that
[ShadowMount+](https://github.com/drakmor/ShadowMountPlus) can mount, the way
MikuPan's PS5 build does. It renders with SDL_GPU's Vulkan backend on
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan)'s RADV, Mesa's AMD
Vulkan driver ported to the console, which is linked into the title. SDL3 gets
the console's own video, audio and controller drivers from `ps5/sdl`. This is
early work.

What you need, on Linux or WSL2:

- `cmake`, `ninja-build`, `meson`, `git`, `curl`, `unzip`, `python3`, and
  [LLVM 22](https://github.com/llvm/llvm-project/releases) (RADV built with an
  older Clang does not link into a title).
- PS5_Vulkan with its RADV release archive built, by its own instructions:
  it fetches its payload SDK fork and builds RADV from
  [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa). Then build its
  converter and `libc.prx`:

  ```sh
  bash PS5_Vulkan/tools/rebuild-libc.sh
  ```

  That gives `.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`,
  `build/runtime-shim/ps5-native-tool` and `runtime/libc.prx` inside
  PS5_Vulkan. Nothing from your console is needed.

Then configure and build with PS5_Vulkan's SDK and LLVM 22. The checked-in
SPIR-V shaders are used:

```sh
export LLVM_CONFIG=<LLVM 22>/bin/llvm-config
cmake -B build-ps5 -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=<PS5_Vulkan>/.deps/native/ps5-payload-sdk/toolchain/prospero.cmake \
      -DREIPAN_PS5_VULKAN_DIR=<PS5_Vulkan> \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo -DREIPAN_COMPILE_SHADERS=OFF
cmake --build build-ps5 --target ReiPan
```

The title folder is written to `build-ps5/app/PPSA39393`. Copy it to
`/data/homebrew/` (or `homebrew/` on a USB drive) for ShadowMount+ to mount.
Put your game in `/data/ReiPan/`: the disc image (any `.iso`), or `IMG_BD.BIN`
with `ZERO3.FHD` beside it. It is found when the title starts. Names are
matched ignoring case.

`reipan.ini`, the memory card, `reipan.log` and, after a crash, `crash.log`
also go to `/data/ReiPan/`. If the title cannot write there, they go to its own
`/download0/ReiPan/` instead. The title reports starting and stopping as PS5
notifications, and the game pauses, sound included, while the home screen is
up. L3 with R3 shows and hides the overlay's bar, as F1 does on a PC. The
window, HDR, mouse and key-binding options are left out on the console.

| Option | Default | Meaning |
|---|---|---|
| `REIPAN_PS5_TITLE_ID` | `PPSA39393` | `PPSA` plus five digits; must be unique on the console |
| `REIPAN_PS5_CONCEPT_ID` | `39393` | Five digits |
| `REIPAN_PS5_CONTENT_SUFFIX` | `REIPANPROSPERO01` | 16 characters `A-Z` or `0-9` |
| `REIPAN_PS5_TITLE_NAME` | `ReiPan PS5` | Name on the home screen |
| `REIPAN_PS5_BACKGROUND` | (none) | A 16:9 picture (PNG or JPEG) for the home screen and launch screen; converted to `sce_sys/pic0.dds` and `pic1.dds` (3840x2160 BC7). Needs Python with numpy and Pillow. Use art you may redistribute if you share the title folder |
| `REIPAN_PS5_LAUNCH_BACKGROUND` | the background | A different picture for the launch screen |
| `REIPAN_PS5_BUILD_LABEL` | `ReiPanPS5-dev` | Build name in the start-up notification and the log |

A PS5 release is made with **Actions → PS5 Release**, which creates a draft
release `ps5-v<version>` from `release-notes/ps5-v<version>.md`; attach the
zipped title folder to it, then publish it.

See `ps5/NOTICE.md` for where the PS5 support files come from.

## Graphics API

ReiPan picks a graphics API by itself: Direct3D 12 on Windows, Vulkan on Linux,
Metal on macOS. Choose another in **Options → Graphics → Graphics API** or
**Settings → Graphics**, then restart. One that cannot be started falls back to
the automatic choice. If one starts but misbehaves, set `backend = auto` under
`[render]` in `reipan.ini`, or start ReiPan once with `SDL_GPU_DRIVER` set to
`direct3d12`, `vulkan` or `metal`.

## Gyro aiming

While the Camera Obscura is raised, a controller with a gyro (DualShock 4,
DualSense, Switch Pro, Steam Controller, ...) aims it by being turned, alongside
the stick and the mouse. It is on by default; **Options → Controls → Gyro** sets
the sensitivity, yaw or roll to turn, and the inversions. The gyro's drift is
learned whenever the controller lies still with the Camera Obscura down.
The right half of the touch pad recenters, as **Gyro recenter behaviour**
(Options → Controls → Gyro) says. Each holds the gyro still while pressed, so
the controller can be brought back to a comfortable angle: **Zero** leaves the
view where it is, **Level** tips it back to level, **Level and back to start**
also turns it back to where Rei faced when she raised the Camera Obscura. On the PS5, where a touch pad click is Select, its left half
stays Select while aiming. The PS5 build reads the DualSense's gyro too.

## Trigger rumble

A controller with motors behind its triggers (Xbox One and later) also plays there
what the rumble is for: heartbeats, blows, a ghost's grip, thunder and the
cutscenes. The grips shake as the game asks, and any other controller behaves as
before. It is on by default; **Options → Controls → Rumble** sets the strengths,
tests each motor, and can add the Camera Obscura (the shutter, the charge and
shutter chances), which the original does not shake for. A controller that Steam
Input stands in for only has its grips.

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
