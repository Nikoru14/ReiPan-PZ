# Writing FF3 code into ReiPan

How a function goes from Ghidra into `src/`. Accuracy to FF3 beats elegance: this is a
reconstruction, not a rewrite. When something here proves wrong or incomplete, fix this file.

## Where code goes

- `stage.py` prints each function's **inferred file** (from `ff3_functions.txt`, built from FF3's link
  order against FF2) and its ReiPan path: `/home/zero_rom/zero2np/src/outgame/title_menu.c` ->
  `src/outgame/title_menu.c`. Keep FF2's directory layout (`main/`, `outgame/`, `ingame/...`,
  `graphics/...`, `system/...`, `common/`) so FF2 knowledge and MioPan code line up.
- **That path is a hypothesis, not a fact.** It is whatever FF2 file sat in the same link-order slot.
  Decide a file's name from several signals together — FF2 matches (from that file, or none), strings
  (source-file names in error reporters are decisive), RTTI classes built or implemented there, the
  names of its functions, which TUs call it — which the brief's identity section (`tuid.py`) lists.
  When they disagree with the slot's name, use a descriptive name in the subsystem the code serves
  (an FF3 menu-window module under `ingame/menu/`, not `graphics/motion/anicode.c`), and record the
  deciding signals in `tus.py define --why`.
- FF3-only modules: B2d/Bp layout system -> `src/system/b2d/` (FF3's own file names: `B2dCtl.c`, `B2dSpr.c`,
  `B2dFile.c`, `B2dInp.c`, `BpCtl.c`, `BpTex.c`). Others: pick a descriptive directory, record it in
  `architecture.md`.
- The TU key is the file: `tus.py` maps every game function to one (pinned ranges in `data/tus.json`,
  else the link-order inference). Unassigned runs (`unassigned@<addr>`) get a real file with
  `tus.py define <file> <lo>-<hi> --why "..."` before code is written; stubgen and stage.py follow it.
- Singleton getters / template instances (0x2ab000+) have no file of their own: write them in the TU
  that owns the class (pin them with `define` if the brief's guess is wrong).
- Never put game code under `src/reipan/` or `src/sdk/` (platform layer copied from MioPan). Platform
  changes are allowed when needed; keep them minimal and call them out in the commit message.

## Every function carries its address

```c
// ──────────────────────────────────────────────────────────────────────
// One line on what it does and why, in FF3 terms.
// FF3 0x0025a310; FF2 CheckSoftReset 0x001d83b0 (same rule, counter moved into sys_wrk).

int CheckSoftReset(SYS_WRK *sys)
{
```

- `FF3 0x<8 hex digits>` is load-bearing: `status.py` counts reconstructed functions by it, `callcheck.py`
  finds the FF3 callees to compare with it. One annotation per function, in the comment block directly
  above (blank lines allowed).
- Name functions exactly as Ghidra has them. Renaming in code means renaming in Ghidra too
  (`name_ctx.py`), so both stay one source of truth.
- Statics and globals: note the FF3 address (`/* sdata 0x3726b4 */`) when known.

## Port from FF2 first (modules FF2 has)

FF3 reused FF2's engine: the 2D/3D renderer, file formats (models, motions, textures, the CMP
container), file loading, sound. Much of it was "objectified" — FF2's C structs became C++ classes
(names in `rtti.py`), free functions became methods taking `this` first — but the logic and data
layouts mostly survived. MioPan already holds FF2's reconstruction of these modules
(`../MioPan/zero_rom/zero2np/src/`: `graphics/graph2d`, `graphics/graph3d`, `graphics/motion`,
`system/eeiop` (cddat, fileload, ee_iop, cmp_eeiop), `system/encodes`, ...: ~75k lines). For a TU whose
module FF2 has:

1. **Start from MioPan's file of the same path** (the TU key is FF2's path), not from an empty file:
   read it whole, next to the FF3 bundle.
2. **Convert it to FF3's shape**: the class names, bases and virtual slots from `python rtti.py --class
   <Name>` (and `--vtable <addr>`); FF3's struct offsets where they moved; FF3's own helpers.
3. **Drop FF2's debug code**: the debug build calls `printf`, `SetAssertPreMessage`, `PrintAssertReal`,
   `g3ddbgAssert/Warning/Printf`, `_fixed_array_verifyrange<T>`, `MemoryCardAssert`, `PrintMsg*` in
   asserts and range checks. FF3 retail has none of them; never reconstruct them (callcheck will not
   miss them). This is also why FF2/FF3 function shapes differ even where the logic is the same.
4. **Verify every function against FF3's decompilation** and change what FF3 does differently; FF3
   stays the authority. Keep MioPan's names, types and comments where they still fit, and say
   `from MioPan <file>` plus what changed.

FF3-only modules (B2d/Bp, layout animations, the HUD, anything without an FF2 file) are reconstructed
from FF3 alone, as below.

## Semantics

- Follow the FF3 decompilation, not FF2's or MioPan's code. FF2/MioPan are references: when `stage.py`
  shows MioPan source for the FF2 counterpart and the FF3 decompilation does the same thing, port MioPan's
  version (types, names, comments) and say `from MioPan <file>:<line>` in the comment; otherwise write
  from the decompilation and note what changed from FF2.
- **Class names come from RTTI.** Before inventing a name for a type with a vtable, run `rtti.py --vtable
  <addr>`: 113 FF3 classes have their real names in the binary (`GameModel`, `CTexture`, `capCtl`,
  `CWindowMsg`, ...). Use the real name for the C++ class even while its functions keep Ghidra names.
- **File numbers are `CD_FILE_DAT` names** (`system/eeiop/cd_file_dat.h`, generated from ZERO3.FHD):
  `BpCtlLoadReq(CAPTION_PHF + GetLanguage(), ...)`, not `0x11bd`. `python fhd.py --find 0x11bd`.
- Ghidra pitfalls: variadic calls lose arguments (recover them from the disassembly, `stage.py --asm`);
  comma-and-`&&` chains are ladders of `if`s; `(int)param` casts are often 64-bit register noise.
- Keep the game's constants as constants with a comment (SE ids, frame counts, phase ids). Name them as
  macros/enums once their meaning is certain. File numbers already have names (`CD_FILE_DAT`, above).
- C++ is fine and expected (the game is C++): references, classes, `nullptr`. Singletons follow FF3's
  shape — a getter returning a pointer to a function-local static object — so call sites read like the
  decompilation (`if (!inst) inst = GetSysWrk();` collapses to `GetSysWrk()->...` only when nothing
  observable depends on the cached pointer).
- Singleton getters: a function-local static whose constructor is FF3's init function
  (`struct SYS_WRK { SYS_WRK() { InitSysWrk(this); } }`; `GetSysWrk() { static SYS_WRK w; return &w; }`).
- **Methods, not free functions** (the user's rule, 2026-10-15). A class's non-virtual methods compile
  to plain functions with `this` in a0, so Ghidra shows them as C functions beside the virtual ones
  `rtti.py` lists. Once a type is a C++ class (RTTI / a vtable, or ctor / dtor / `operator new` code),
  every function whose first argument is that object is a member: write
  `void CandleControl::SetState(CandleAnimeStatus *s)`, called `ctrl->SetState(s)` (or `SetState(s)`
  from another member), not `CandleControlSetState(CandleControl *ctrl, CandleAnimeStatus *s)`.
  Signals: every caller passes the object (or its own `this`) in a0; the body uses a0 only as that
  type; it sits among the class's ctors and virtuals. A function that takes the object later or not at
  all stays free (or is a `static` member when it only touches the class's statics); FF2 C modules
  that FF3 kept as C (plain structs, no ctor) keep FF2's free functions.
  - Names: Ghidra keeps its flat name (`CandleControlSetState`); the source writes `Class::Method`
    with the class prefix dropped, and the `FF3 0x` annotation links the two (status, `callcheck.py`
    and `stubgen.py` go by address). Declare every member in the class with its address
    (`void SetState(CandleAnimeStatus *s);  // FF3 0x0013c1d0`): stubgen stubs an unwritten member from
    that line; constructors and destructors are stubbed by hand.
  - The C-style methods written before this rule were converted on 2026-10-15 (about 140: candle_ctrl.c,
    cmn_fade_layer.c, WindowMsg.c, B2dBase.c, the map, menu and common classes); GameModel's and
    AcsModel's followed on 2026-10-16 with `graphics/game_model.c`. Still free functions on a model:
    CharModel's (FUN_00145xxx, char_model.c) -- its TU converts them the same way (GameAnimeModel's
    followed on 2026-10-16 with `graphics/game_anime_model.c`: callers write
    `static_cast<GameAnimeModel *>(plyr->mdl)->GetAnimeFlag(0)` while the work fields stay GameModel *).
  - A stand-in class whose constructor is still a stub can still be created with a real
    new-expression: give it its declared constructor as a hand stub (`T::T(...) { REIPAN_STUB(...) }`)
    and the compiler sets a valid host vptr, so the virtual calls reach stubbed members instead of a
    garbage vtable (the model builders in game_model.h). A class used only through its vtable needs
    its overrides hand-stubbed too: the linker then reports "vtable for T", which stubgen skips. A function FF3 calls through a plain function-pointer table stays
    a file function even when it takes the object first (CmenuCamStat's steps: the table at 0x34daf0
    has 8-byte {state, function} entries, where a pointer to member would add an adjustment word).
  - Types without RTTI count too when FF3 has constructor code for them, and were converted the same
    day: C1/C2 constructor twins (B2D_SEL, B2D_PLACE, STEP_SEQ, CSR_GRID, LERP_F, MENU_PIC, TITLE_OBJ,
    B2D_GRID_SPR, the lanterns), `operator new` + an init on the result (BP_OBJ, GPHASE_NODE,
    HUD_LAYOUT, PLYR_MDL_FX), a function-local static whose guard runs an init or registers a
    destructor with atexit (SYS_WRK, PACKET_BUF_MGR, GPHASE_CTRL / TREE, MAP_SKY / _DATA, the FINDER_*
    works), a constructor that runs one on a member (CmenuCamStat's sel / grid / lerp, PHOTO_MODE_WRK's
    lens and charge work), a stack object built by one (CandleFurn), and FF2's class templates
    (CWrkVariable and co., one explicit specialization per FF3 instance so each keeps its address).
    An init that other code also calls (GameSystemStart's GPhaseCtrlInit, MapSkyInit) stays a member
    `Init()` that an inline constructor calls; one only a constructor or new-expression runs becomes
    the constructor (`new BP_OBJ(file)`; a destructor likewise). Objects FF3 keeps on its C++ heap stay
    there: `new (operator_new(sizeof(T))) T(...)`, `p->~T(); operator_delete(p);`. Not converted: types
    with no constructor code of their own (the photo sub-works, MENU_CSR_WRK, csr_ctrl's CSR_ITEM /
    CSR_VIEW) and FF2 C modules (SingleLinkList).
  - The EE passes float arguments in f12.., so a function whose list starts with floats still gets
    the object in a0 when it comes first among the integer / pointer arguments (`LerpFSet(from, to,
    frames, lerp)`, `PhotoWrk1e8Add(4 floats, w, speed)`): a member, keeping the floats first
    (`LERP_F::Set(from, to, frames)`).
  - Pitfalls: a member hides a free function or a base member of the same name inside the class
    (`::IsLoadEnd(load_id)` in CLightCtl::IsLoadEnd, `AnimeCodeManager::Start(no, skip)` from
    AnimeCodeManagerGame::Start, `using CB2dBase::Load;` beside CmenuCamBase's virtual `Load()`); a
    parameter or local that shadows a field needs `this->field`; a helper the class's callbacks or
    other classes call cannot be private.
    callcheck cannot see constructor / destructor calls (implicit member construction, new / delete
    expressions, a static's guard): name them in a `callcheck:` note with their Ghidra names.
    Destructors (`T::~T`) are checked like any function, so a derived one needs its base's
    `...DtorBase`, a member's destructor (`LoadDtor`) or a delete's `operator_delete` in the note.
- Structs, enums, typedefs: see "Types and layouts" below.
- EE addresses in game code (load targets like `0x10ef000`, packet buffers) go through
  `ReiPan_GetHostPointer()` (`reipan/reipan_memory.h`) at the point of use. Hardware goes through the
  SDK replacements (`sceGs*`, `sceDma*`, `sceSif*`, libvu0 intrinsics) — never talk to SDL from game code.
  Where a PS2 behaviour has to be emulated in game code, mark it `// PC:` with the reason.
- `printf` goes to the log (pc_prefix redirect); keep the game's messages.

## Types and layouts

Types are rediscovered piece by piece; write each one down as code the moment reconstructed code
touches it, and make the compilers prove its layout.

- **Recreate the type, never an offset.** `(u_char *)p + 0x120` or `*(int *)(obj + 0x14)` is not
  finished code: define (or extend) the struct and use the field. Before (gs_reg.c's first version):
  `m = (float *)((u_char *)gra3dGetCamera() + 0x120);` after: `gra3dGetCamera()->matWorldScreen`, with
  GRA3DCAMERA laid out in `gra3d.h`. If a layout truly cannot be settled in the session, name the
  known fields and pad the rest (`u_char _008[0x38];`, PAD_WRK), never a bare offset.
- **Where:** the header of the TU that owns the object (its getter, its global, its init), even when
  that TU is not reconstructed yet (create the header minimal). File formats: the loader's header.
- **Start from FF2, verify in FF3.** MioPan's headers / FF2's `types.txt` give names and a first
  layout; FF3 changes them (GRA3DCAMERA grew 0x1e0 -> 0x220 by keeping matWorldView at 0xe0, every
  later field moved 0x40). Check each field reconstructed code uses against FF3 accesses (getters,
  init, copy loops); unverified fields keep FF2's name with `// FF2, unverified`.
- **Size evidence:** memcpy/copy-loop lengths (`_InitCamera` copies 0x220), array strides
  (`base + i * 0x3c`), distance to the next global in bss/data, allocation sizes, constructors that
  clear N bytes. Write the size in the header comment (`/* 0x220 */`).
- **Every struct carries asserts** right after it: `static_assert(sizeof(T) == 0x..)` and
  `static_assert(offsetof(T, field) == 0x..)` for each field reconstructed code touches (all of them
  for file formats and DMA data). Structs holding pointers cannot keep the PS2 size: assert the
  offsets before the first pointer and say `layout: host -- <why>` in the comment above, or keep EE
  addresses as `u_int` when the bytes come from a file or go to the hardware (PACKET_BUF_MGR, CMP_HEADER).
  `layoutcheck.py` (run by `tus.py check`, required by `finish`) compiles every game header on its own
  with MinGW GCC and MSVC so both evaluate the asserts, and fails on structs without them.
- **Host types (the EE is GCC 3.x, MIPS; the hosts are MinGW GCC and MSVC on x64, both LLP64):**
  - `long` is 64-bit on the EE and 32-bit on both Windows compilers: EE `long`/`u_long` ->
    `u_long` (uint64_t) or `int64_t`; never plain `long` (layoutcheck flags it).
  - Pointers: 4 bytes on the EE, 8 on the host (see the asserts rule above).
  - 16-byte alignment: on the EE `sceVu0FVECTOR`/`FMATRIX`/`IVECTOR`, `qword` and `u_long128` are
    16-aligned, which pads whatever precedes them. ReiPan's typedefs are aligned the same way
    (`vu0_intrin.h`, `scetypes.h`), so declare such members with them (GRA3DCAMERA) rather than the
    `float x[4]` / `float m[4][4]` / `int q[4]` that FF2's `types.txt` often shows: a plain array is
    4-aligned and shifts every later field. Any other 16-aligned member gets `alignas(16)`.
  - Bitfields: MSVC *and* MinGW (MS layout by default) open a new storage unit whenever the declared
    type changes; EE GCC packs by the declared type's alignment. Use one type per bitfield run, pick
    the type whose unit reproduces the ROM's bytes (MioPan's CCenterCircle: `u_char`), and assert the
    size. For GS/VIF/DMA registers prefer the SDK's shift/mask macros (`SCE_GS_SET_*`, `SCE_GIF_SET_TAG`).
  - Enums: 4 bytes on all three, but when the ROM stores one in a byte/short/word use that integer type
    and name the enum in a comment (`int type; // G3DCAMPROJECTIONTYPE`, ENCODE_DIV_SECTION's `short`).
  - `wchar_t` (EE 4, Windows 2), `long double` (MinGW x64 16), `__attribute__((packed))` (GCC-only; use
    `#pragma pack(push, 1)`): do not use in game types. `char` is signed on the EE and x86 but unsigned
    on ARM hosts: `s_char`/`u_char` when the sign matters.
  - C++ classes: no virtual functions unless FF3 has a vtable for them (the vptr is 4 bytes on the EE,
    8 here, and moves every field after it). When FF3 has one, write a real class with virtuals in the
    EE slot order and note each slot's EE offset (`// EE vtable +0x0c`); the complete and deleting
    dtors take two slots where the destructor is declared (usually first, but `GameModel`'s are at
    +8/+0xc: check `rtti.py --class`). Name the class from RTTI. A class not identified yet gets a
    minimal polymorphic stand-in holding only the slots called so far, named `VtblCallXX` by EE offset; rename
    them in every caller once the class's bodies say what they do (ModelManager's, 2026-10-16). Derived classes: MinGW reuses
    the base's tail padding, MSVC does not, so assert `>=` there.

## Stubs and the frontier

- Called but not yet written -> `python stubgen.py --from-build work/build_relwithdebinfo.log`. It writes
  `REIPAN_STUB("Name", 0xADDR)` bodies (log once, return `{}`) into the inferred file, so the build links and
  a run shows which missing pieces were reached.
- **Bridges** (`REIPAN_BRIDGE("Name", 0xADDR)`, `common/reipan_stub.h`): temporary stand-ins built from
  FF2/MioPan code that *work* (none left: the file loader's bridge was retired with ee_iop.c, the SPU allocator's `snd_bridge.c` with spu_mem.c), so later TUs run on real
  data. The tools count them as stubs and the brief lists them for deletion; when the TU owning one is
  reconstructed, delete the bridge function (and its file with the last one). Never write `FF3 0x...`
  in a bridge file.
- **A stub leaves out-parameters unset**: callers that read them get garbage (BpCtlCreateWork passed
  FUN_00105090's item/key counts to B2dCtlSetup and crashed). Initialise such locals at the call site
  with a `// PC:` comment when FF3's callee always writes them.
- Declarations first: stubgen needs a prototype in a header. Write the module header (types + prototypes)
  before calling into it.
- Replace the whole stub definition when reconstructing; the `// stub of FF3 0x...` comment goes with it.
- The **frontier** (callees of reconstructed code not yet written) plus the stubs the last smoke run
  reached decide the next TU (`tus.py next`, `status.md`).
- Stubs land in the callee's TU file. When a TU's real file is settled later, stubs of its functions
  left in other files (e.g. `src/lib/unassigned_stubs.c`) must go; the brief lists them.
- Callees in `unassigned@` runs have no header yet: declare them in a commented block at the top of the
  calling .c; move the prototypes to the real header when that run is defined.
- stubgen skips data. Globals of other TUs that reconstructed code reads are defined by hand in that
  TU's file, or in `src/lib/unassigned_stubs.c` for unassigned runs (e.g. `pad_wrk`).
- Platform hooks used so far: `ReiPan_LogInit` (top of main), `ReiPan_SRand` for newlib `srand`,
  `ReiPan_ResolveVBlankWait` (WaitVBlank), `ReiPan_RendererEndFrame` (end of SendDMAMain);
  `ReiPan_RendererBeginFrame` belongs at the start of the frame (pre_super, as in MioPan).

## Verify and commit

`python tus.py check <TU> --run 15` does steps 1-4, `python tus.py finish ...` steps 5-6:

1. Build green (errors and undefined references summarised; undefined references become stubs).
2. `callcheck.py` on the TU's file — no *missing* FF3 calls, or a comment in the code saying why
   (inlined helper, call moved, FF3 change). *Extra* calls are informational.
3. `layoutcheck.py` — every game header compiles on its own under MinGW GCC and MSVC with its
   `static_assert`s holding, game .c files compile under MSVC, every struct has asserts (or a
   `layout: host` note), no host-dependent types. `finish` refuses to commit when it fails.
4. Smoke run (`build.py --run 15`, output in `work/run_relwithdebinfo.log`): reached stubs are listed and
   rank first for the next pick. **Look at the screen**: the run saves the ROM's 640x448 frame every 60
   presented frames (~2.4 s; the game runs at 25 fps) as `work/shots/shot_<frame>.png` plus `last.png`;
   open them with the Read tool. Once a TU draws something, check it against what FF3 shows: layout
   and texture contents are in the unpacked tree `$REIPAN_DATA/bin/data` (`.tm2`, `.b2d`
   per container): `python tm2png.py --file CAPTION_PHF` (or a number, or a name fragment) converts a
   file's textures to PNGs in `work/ref/<FILE>/` to compare with; the logos are "T2", "TECMO", "ZERO"
   (`PRE_TITLE_PHF`). Say in the report what the shots show; a wrong picture is a bug to fix or record,
   never a pass, and the visual test outranks logs and callcheck. `finish` copies `last.png` to
   `progress/<time>_<tu>.png` and names it in the commit (`Screen:`). The log also names the data root
   and the archive (`ReiPan paths: data = ...`, `archive = ...`).
   Save and load code (a `*SaveGet` block, a `*LoadRestore`) shows on screen through a snapshot round
   trip: `build.py --run 75 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS --warp chapter=2,tp=0,snap=1`
   saves one (the log names the `.rps`), then `build.py --run 60 --pad <the same> --snapshot <that name>`
   must show the saved scene again.
5. `refresh.py` — regenerates `ff3_functions.txt` and `status.md`.
6. One commit per session, made by `finish` (refuses a red build, a failed layoutcheck and the main
   branch); its message lists
   the reconstructed and stubbed functions.
