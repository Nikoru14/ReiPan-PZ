# PS5 title support files

ReiPan is licensed AGPL-3.0. The files here that come from other projects keep
their own licenses, which allow combining them with it.

## From ps5-native-app-boilerplate, via PS5_Vulkan (GPL-3.0-or-later)

`native/` comes from
[ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate)
at commit `470695e0c557f99ff2df0e36e4df713c5e636526`, by BlackBearReloaded.
`native/app_crt.cpp` is the copy in
[PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) at commit
`7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc` (`tooling/native/app_crt.cpp`),
which also sets the IEEE floating-point state before anything runs.

Modified: `native/ps5-pie.ld` additionally defines the `__eh_frame_start`,
`__eh_frame_end`, `__eh_frame_hdr_start` and `__eh_frame_hdr_end` markers
that the SDK's libunwind needs for C++ exceptions, and places `.got` and
`.got.plt` before `.data.rel.ro` at the start of the RELRO segment, without
which the console refuses to start the title (CE-107750-0).
`native/app-symbols.map` makes every symbol local, not only the allocation
operators, because the title converter accepts no exports.

- `native/app_crt.cpp`: title entry point (`_start`). Initializes the runtime,
  runs static constructors, calls `main`, then ReiPan's `catchReturnFromMain`.
- `native/ps5-pie.ld`: linker layout that keeps code, read-only data, RELRO and
  writable data in the separate ranges the console's loader expects.
- `native/app-symbols.map`: keeps the executable's symbols internal.

## From PS5_Dusklight (Zlib and MIT)

From [PS5_Dusklight](https://github.com/griches/PS5_Dusklight) at commit
`72d6885cc8b4ff4c5f6be23682ab11578ec926c4`, by griches and Mihawk:

- `sdl/SDL_ps5audio.c` and `sdl/SDL_ps5joystick.c` (Zlib): SDL3's audio
  driver (AudioOut, 48 kHz stereo) and controller driver (scePad, up to four
  players), unchanged.
- `sdl/SDL_ps5video.c` (Zlib): SDL3's video driver. Modified: ReiPan adds
  SDL's Vulkan hooks, so SDL_GPU renders through RADV to a VK_KHR_display
  surface on the console's VideoOut.
- `patches/sdl.patch`: registers those drivers in SDL 3.4.12 and builds them
  when the toolchain says PS5, unchanged.
- `platform/platform.c`, `platform/platform.h` (MIT, Mihawk): klog, the splash
  screen, the controllers and sound. Modified: its `catchReturnFromMain` is
  removed; ReiPan's own (`src/reipan/reipan_ps5.cpp`) closes the title.
- `platform/shims.c` (MIT): answers for libc functions no module a title loads
  exports, unchanged.
- `tools/cc-init-array.sh` (MIT): compiles with an init array, so static
  initializers run in source order, unchanged.
- `tools/link-title.sh` (MIT): adapted from its link script to link ReiPan
  with PS5_Vulkan's recipe.
- `tools/png-to-title-dds.py` (GPL-3.0-or-later, Mihawk-99, from PS5
  RetroArch's `tools/make-title-art.py`): converts a picture to the home
  screen art the console shows (3840x2160 BC7 DDS), unchanged. It is a build
  tool; nothing of it is in the title.

## Used at build time, not in this repository

- [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) (GPL-3.0-or-later):
  the RADV release archive built from
  [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) (Mesa, MIT), its link
  recipe (`tools/radv-link.sh`), the AGC import stubs, `ps5-native-tool` and
  the clean-room `libc.prx`.
- The [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) fork that
  PS5_Vulkan pins (GPL-3.0-or-later), with its platform layer
  `libps5platform.a`.

## ReiPan's own

`platform/libc_extras.c` defines the libc functions (`timegm`, `readlink`,
`mkstemp`, `link`, `symlink`) that the platform layer at PS5_Vulkan's SDK pin
lacks, licensed like the rest of ReiPan.

`sce_sys/icon0.png` is the ReiPan logo (resources/reipan.png) scaled to the 512x512 icon size.
`sce_sys/param.json.in` follows the layout of the boilerplate's and the
OpenLF2 PS5 port's `param.json`.
