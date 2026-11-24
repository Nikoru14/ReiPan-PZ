# Hazards — things that already cost time

Add an entry whenever something burns time and could again. Keep each one: symptom, cause, fix.

## Ghidra / GhidraMCP (HTTP on 127.0.0.1:8089)

- **mkfunc in a quick loop fails with "Unable to lock due to active transaction"** (2026-10-15): every
  ghidra.py mkfunc saves the program, and the next call can land while the save still holds the lock.
  Fix: retry after a few seconds (a loop with a 3-4 s pause got all 20 through); check with
  `ghidra.py func <addr>` afterwards.
- **`program` must be in the query string.** In a POST body it is ignored and the write lands on the
  active program — this once renamed 69 FF2 functions. `reipan.http()` does it right; raw curl must too.
  `switch_program` to FF3 before batches; `name_ctx.py` / `reconcile.py` snapshot before writing.
- **`dry_run` in a POST body is not honoured** — the write happens.
- **Naming policy rejects names** ("Exit", token-subset collisions): pass `strict_mode: "off"`.
- **Renaming to the default name resets it**: `new_name = "FUN_<addr>"` makes it default again;
  `delete_label` does not touch function names.
- **Bridge started before Ghidra**: `list_instances` is empty; `connect_instance("Ghidra")` still connects.
- **Scripts are disabled** unless Ghidra runs with `GHIDRA_MCP_ALLOW_SCRIPTS=1`; the tools decode the ELFs
  in Python instead.
- **C++ virtuals that are no Ghidra function** (2026-10-16, char_model.c): code reached only through a
  vtable was never disassembled into functions, so the neighbour's body swallowed it -- 18 of
  CharModel's slots (0x144500, 0x145498, 0x146390, ..) plus the linkonce 0x2ad4d0..; the staged
  decompilations then show half a function with the next one's callees in `- callees`, and rtti.py
  lists wrong ctors (0x147918, 0x2ad688 "CharPlayerShapeStatus"). Before reading a class TU, run
  `rtti.py --class X` and `ghidra.py func <slot>` for every slot address in the TU's range; `mkfunc` the
  ones that say "No function found", then `refresh.py` and re-brief.
- **Phase-table entries without functions**: code reached only through data tables was never disassembled;
  `create_function` with `disassemble_first: true` (done for the 120-phase tables).
  Dispatch tables too: SceneEffectFunc 0x362aa8 had 15 of 30 targets with no function at all (`ghidra.py func`
  answered "No function found"), so `ff3_functions.txt` sized each neighbour up to the next entry and the matcher
  shifted FF2 names one slot (ReqDither on CallBlur, SceneTestEffectFlgGet on ChangeDataPtr). Dump the table, mkfunc
  every target, then rename (scene_effect.c, 2026-10-20).
- **FF3 passes a pointer where its unpacker reads a byte.** SceneEffectCallBlur hands SetEffects(3, ..) &BlurAlpha
  (FF2's call shape) but FF3's SetEffects_BLUR takes the alpha with `lbu`: the blur alpha is the low byte of the
  static's EE address (sdata 0x3723bc -> 0xbc), whatever the script says. On the host a pointer there would give an
  arbitrary byte; scene_effect.c passes 0xbc (SCENE_BLUR_ALPHA_BYTE). Check the unpacker's loads (effect.c) before
  passing a variadic argument through.
- **`ghidra.py find` ignores the mask** (GhidraMCP `search_byte_patterns`): `find 08da808f ffffe0ff`
  finds nothing although `lw v0, -0x25f8(gp)` (`08da828f`) sits at 0x1f2224; a masked search only hits
  where the masked bits are already right (`sw zero, ..(gp)`). To find every access to a gp global, loop
  over the 32 rt registers with exact patterns (`(op<<24)|(28<<21)|(rt<<16)|imm`, lw 0x8c, sw 0xac;
  done in the story_door.c session) - 640 calls take about a minute.
- **Save**: `save_program` (FF3) after naming batches; nothing persists otherwise (`name_ctx.py` now does it).
- **`ghidra.py mkfunc` twice in a row: the second save fails** ("Unable to lock due to active
  transaction"); the function is created anyway. The next `name_ctx.py` batch saves it, so create the
  functions first and name them (and any new name passed to mkfunc) through a batch afterwards.
  2026-10-20 (menu_file_list.c): a mkfunc whose save failed right after a name_ctx.py run was gone after the
  next name_ctx.py (its refresh reloads Ghidra's saved state); `ghidra.py func` every created address after
  the batch, and re-create + re-batch the missing ones.
- **Names lost when name_ctx.py's save failed** (2026-10-16): name_ctx.py printed "Unable to lock due
  to active transaction" for its save; the names showed in Ghidra, but by `tus.py finish`'s refresh
  Ghidra had dropped every unsaved change -- this session's 21 names and 2 created functions, and the
  previous session's (map_rectangle.c) 11 names and 3 functions too, so ff3_functions.txt went back to
  wrong names. Fix: when a save prints that error, retry `reipan.http('POST', 'save_program', FF3)`
  until it says success; before finishing compare the ledger with Ghidra (context_names.json entries
  whose name differs from `reipan.function_names(FF3)` must be 0), re-create lost functions with
  mkfunc and re-apply the differing ledger entries as a name_ctx.py batch.
- **`name_ctx.py` refuses an overload's name** ("name already used"): FF3's two gra3dExecPrelight
  overloads need distinct Ghidra names (0x1dafd0 is `gra3dExecPrelightMatrix`), and a wrong low match
  elsewhere holding the name (0x1de0d8 had `_Vu0LoadMatrix_4_5_6_7`) must be renamed in the same or an
  earlier batch first.
- **Stale names after renaming**: stubgen, callcheck, stage and `tus.py` read cached `work/` data and
  `ff3_functions.txt`. Right after a batch, callcheck reported the old `FUN_` names as *missing* and
  stubgen wrote `REIPAN_STUB(..., 0x00000000)` for the new names. Cause: no `refresh.py` in between.
  `name_ctx.py` now saves and refreshes itself; after renaming by any other route, run `refresh.py`.
  **Don't pipe `name_ctx.py` through `head`**: the closed pipe kills it before its refresh, with the
  same stale-name result (stubs at 0x00000000). If it happened, run `refresh.py`.
- **`name_ctx.py` refuses duplicate names**: GCC emits many functions literally named
  `__static_initialization_and_destruction_0`; the second one (0x13e5d0) was refused. Give such
  functions a unique, file-specific name.
- **A batch that moves a name** (frees it on one function, gives it to another) used to fail with
  "name already used" when the taker came first; `name_ctx.py` now retries such entries last.

- **A staged function much larger than its decompile** (2026-10-15, plyr_mdl.c): the stage said
  FUN_0023a168 was 0xa9c bytes with 35 callees, Ghidra's body was 0xf0. Five look-row searches after it
  are reached only through a data table (0x330a00 + 0xc), so Ghidra never made functions of them and
  export.py sized FUN_0023a168 up to the next known entry. Fix: `ghidra.py asm` the gap, find the
  `addiu sp, sp, -N` prologues after each `jr ra`, `ghidra.py mkfunc` each (one at a time), name them
  in a batch. Check any staged size far above its decompile this way.
  The same hides leaf functions with no prologue (2026-10-19, photo_obj.c): FUN_00228a08's 0xb0 bytes held
  two unreferenced getters after its `jr ra` (0x228a58, 0x228aa0), and a `_GLOBAL__D_` thunk (0x2295e8) and
  a duplicate constructor copy (0x229608) had no function either. A size of 0xb0 against a decompile of
  ten lines, or modview's callee list naming a call the decompile lacks, is the sign.
- **A swallowed function shows up as a bogus callee of its host.** callcheck said g3dInitialize
  (0x1cbd90) misses a g3dSetGsRegister call: the call belonged to g3dSetRenderState, swallowed with
  two more unreferenced functions after g3dInitialize's `jr ra`. A FF3 callee the FF2 body does not
  have: check the asm for a `jr ra` mid-body before writing the call. Likewise kernels reached only
  through a function-pointer table (g3dCore's 0x1ccaf8..) hide inside the previous function.
  B2d item hooks are the common case: dump the ROM hook lists ({func, kind, id, name} records the
  Set*Hook functions copy to the stack) and check every func is a function entry -- 0x12cd28 sat inside
  0x12cc18 and 0x12dcd0 inside 0x12dcb8 (album_view / album_load_scr session, 2026-10-14).
  Again in album_zoom_view.c (2026-10-21): 0x12eaf0 inside 0x12e9f8 and 0x12f048 inside 0x12ef68, both
  only in the ROM hook lists.
  A quick tell: a function far bigger than its decompilation (FUN_0021af38: 0x1ac bytes, one `return`).
  The NPC act ops (ingame/npc/npc_act.c, 2026-10-15) had 17 of 35 table targets inside neighbours;
  `ghidra.py func` on every word of the dispatcher's tables finds them all.
- **Event handlers: trust the keyword tables, not FF2 matches.** The tiny `return Fn() == 0` wait
  predicates in the 0x199xxx run had grabbed FF2 handler names (EvPhotoLock, EvPadWait, EvGameDataSave,
  EvSoundPause, IsRegist_StreamID, ...) while the real handlers sat elsewhere, 81 of them hidden inside
  their neighbours (2026-10-14). Every handler is in the command table 0x3556b8 or the condition table
  0x3242e8 with its keyword string: `python evtbl.py cmd|cond` lists them; name from the keyword, and
  check a name you want is not already held by a wrong match (name_ctx.py reports "named N/M" even when
  an entry was skipped because the name was taken: grep ff3_functions.txt afterwards).
- **callcheck skips ALL-CAPS callees.** It ignores upper-case identifiers as macros (callcheck.py
  `not c.isupper()`), so a real call to EFFECT_MALLOC / EFFECT_FREE shows as "missing" although the
  code makes it. Explain it with `// callcheck: EFFECT_MALLOC (read as a macro)` (effect_obj.c).
- **A `callcheck:` note counts only the names before its first parenthesis or `;`** (callcheck.py
  `callcheck:\s*([\w, ]+)`). `callcheck: A, B (why), C (new-expressions)` explains A and B but not C,
  and `...; D (why)` never explains D. Put every name first: `callcheck: A, B, C, D (why ...)`
  (model_builder.c, 2026-10-16).
- **callcheck gives a function the last `FF3 0x...` comment above it.** A prototype in the unassigned@ block with `// FF3 0x...` followed directly by a static helper made callcheck check the helper against that callee's body (MISSING calls). Put PC-only helpers after a function that has its own annotation (MapFog.c, 2026-10-16). The file's header comment counts too: a `FF3 0x...` there ("0x00248f60 is compiler output") was given to the first unannotated helper; mention thunks at the end of the file, as puzzle.c does (kamon_pzl.c, 2026-10-21).
- **C++ vtable methods hidden in the previous function's body.** All four of MotionModelBuilder's and
  NormalModelBuilder's out-of-order vtable entries (0x20e368, 0x20e680, 0x20e9c8, 0x20ed58) and ShpMotion
  +0x18 (0x2172e8) had no Ghidra function; the staged decompilation of the host showed the merged body.
  Before reading a class, check every slot of `rtti.py --class` with `ghidra.py func <slot>`, mkfunc the
  missing ones, then re-decompile the hosts.
- **A function body can swallow the unreferenced function after it.** `FUN_001e6f88` (0x5d4 bytes) held
  its own 0x1c4 bytes plus an uncalled 0x3fc-byte function at 0x1e7150 and two empty `jr ra` stubs; the
  decompilation stopped at the first `jr ra` while the callee list showed calls it never made. Check:
  does the body end at the last `jr ra; delay`? If not, `python ghidra.py asm <lo> <hi>` the tail, look
  for a prologue (`addiu sp,sp,-N`), `python ghidra.py refs <addr>` (Ghidra xrefs, `jal`/`j` encodings,
  the address as data, `addiu` low halves), then `python ghidra.py mkfunc <addr> [name]`.
  Functions reached only through tables are the usual victims: CmenuCamStat had eight (vtable
  slots, its {state, func} step table 0x34daf0, its hook list 0x34d590) -- dump the page's tables
  and mkfunc every entry that `ghidra.py func` does not find at its entry.
  It is common: CheckPadNowPlaySe swallowed PadGetCount (0x2256a8), PadGetStickWord swallowed
  PadGetStickFloat (0x225840), IngameMain holds a GS callback (0x1d1c38), MapRec's `_GLOBAL__D_`
  0x10e630 held CMarkDat's constructor 0x10e650, LightCtl's `_GLOBAL__I_` 0x10d898 held CLoad's
  0x10d8b8 (C++ class files: the next file's ctor follows a tiny `_GLOBAL__I_` / `_GLOBAL__D_` thunk; rtti.py then lists the wrong ctor address). Quick test: a size in
  `ff3_functions.txt` much larger than the code up to the first `jr ra`. Functions referenced only
  through data (callbacks, vtable slots) are the usual victims. Without `ghidra.py`, `stage.py --asm`
  or MCP `read_memory` + hand decoding still recover the body (callcheck cannot see it).
  Tail-jump targets too: a `j <addr>` that leaves a function for a prologue (`addiu sp,sp,-N`) is a
  call to its own function -- BrightnessAdjustmentFilterDraw 0x17be50 ends in `j 0x17bf40`, and SubDeform's
  jump table ends in `j`s to two kernels Ghidra had folded into their neighbours (0x178c78, 0x178c90). An
  `ff3_functions.txt` size that runs past a `j` to an address inside the same body is the sign.
  Save-manifest callbacks are the extreme case: of the 65 entries of FF3's five save manifests, 54
  sat inside the previous function (mkfunc'd 2026-10-14, mc_set_data.c session). Each is 5
  instructions (`lui/addiu t7; li v0,size; jr ra; sw t7,0(a0)`).
  The B2d/Bp and layout code is full of them: 0x106cd8 inside 0x1069c8, 0x107568 inside 0x107410,
  0x108228 inside 0x1081f8, two-instruction returns at 0x106520/0x106e58/0x106e60/0x107408, code after a
  tail call (`j`) that no function covers (0x14d350 in MsgViewSetPics, 0x14e040 in MsgViewUpdatePic).
  Step chains kept as function pointers (`wrk.step = (code *)&LAB_...` in a decompile) hide every
  step: the event queue had five (0x24f670, 0x24f750, 0x24f868, 0x24f948, 0x24fa08; 2026-10-14).
  Callback iterators hide their callbacks: MapRcb.c (2026-10-16) passes each query's callback in t0
  (`lui t0,0x12; addiu t0,t0,-N` before `jal RcbForEachQuad`), and 5 of 9 callbacks plus an unreferenced
  query (0x11edd0, 0x11eee0, 0x11ef58, 0x11f340, 0x11f560) sat inside their neighbours. Decode every
  such constant and `ghidra.py func` it before reading the module.
  **The reverse happens too: a wrapper's body swallows the hidden function its `j` lands on, before
  its own entry.** `FUN_001c86f8` (a 0x34-byte wrapper) had Body 0x1c8560..0x1c872b, so `dec.py`
  printed 0x1c8560's code under 0x1c86f8's name and staged callee lists did not match the text;
  likewise `FUN_001c83b0`'s body began at 0x1c8388. Check `ghidra.py func <addr>`: a Body that starts
  before the Entry means a hidden function at the Body start; mkfunc it. The plain case also hides
  in staged slices: LensIsEquip 0x1c6230 ran on over LensSetEquip 0x1c62e8 (decompiled text without
  the call, callee list with LensIsHave); callcheck flagged the "missing" call.
  Tells: a callback address passed to a setter lands inside another function; callcheck reports calls
  "MISSING" that the host function plainly does not make (the cached call data credits the hidden
  function's calls to it). Reconstruct the hidden one as `FUN_<addr>` (or its name) with a
  `// callcheck:` line on the host, and list it under open-items "Ghidra fixes".
- **Tail calls are not calls to Ghidra.** `j <func>` at a function's end (InitSystemHeap ->
  InitCppAndSystemHeap, ScreenCaptureSave -> EffImageHalf32, GameSystemStart -> PadLockReset,
  GameSystemEnd -> InitCppAndSystemHeap, pre_super -> SetDefaultGsRegs, after_super -> BpCtlFrameReset)
  is missing from the callee list and from the callee's callers ("0 callers"); callcheck then reports
  the reconstructed call as *extra*. Read the last instructions before believing "unreferenced".
  Worse when the target has no function of its own: CameraMainInit 0x1faf98 ends in `j 0x226df0`
  (a two-instruction setter 0x2b000 bytes away), Ghidra grew its Body to 0x1faf98..0x226df7 and the
  decompilation showed the setter's store (`uGp000002a8 = 0`) inline. A Body far longer than the size
  in ff3_functions.txt, or a decompiled store to a global the function has no reason to touch, means
  mkfunc at the `j` target (2026-10-16: FurnPhotoFlgClear).
- **Switch tables on bytes decompile into goto soup** (player.c PlyrAnimeBlendFrames 0x237898: nested
  jump tables, shared tails, `beql`). Do not hand-trace them: get the ROM's answer for every input with
  `mipsemu.py --grid` (or the `Emu` class with hooks for loads / calls), compile the C rewrite with the
  MinGW gcc (reipan.MINGW_BIN) in a scratch harness and diff all pairs. 2026-10-14: three tables, 65536
  pairs each, matched on the first try only because of the check.
- **mipsemu limits** (2026-10-14, csr_ctrl.c): GhidraMCP's `disassemble_bytes` returns at most 1000
  instructions, so a long range came back cut short and the emulator stopped on a KeyError (`listing()`
  now fetches in chunks; since 2026-10-14 `ghidra.disasm()` does the same for `ghidra.py asm` and
  `sinit.py`, which had recovered only the first 0x40 of hud_sprt's 247 rows). Its memory is keyed by (address, size): code that stores a short and loads the
  word around it (FF3 reads `lw +8` over two shorts) sees stale values; and `run()` resets registers, so
  nested calls need their own stack. For a whole module, subclass Emu with byte-addressed stores, a
  nested `call` (sp - 0x400) and div/mflo/mfhi (the menu_csr session harness did this: 56018 commands
  compared against the C in under a minute).
- **Float arguments are not positional.** EE GCC passes the first float args in f12.. whatever their
  position, ints in a0..t3; Ghidra puts the float first or drops it (SetPlyrAnime 0x238020 shows
  `(0, 0x82, 1, 0xff, 0xff, 0)` = f12 0.0, a0 0x82, ...; some call sites show five args). Read `mov.S f12`
  / `lwc1 f12` before the jal. A `clear f12` right before a `c.le.S f0, f12` is the compiler's zero
  constant, not an argument (EffectManyCandleReq in PlyrDamageCtrl). The float args can also come out
  in the wrong order among themselves: MapDoorModeClose 0x1f3044 decompiles as
  `PlyrSet280Req(0, 50, 128, 0, 0)`, the asm is f12 0 (a `clear f20` copy), f13 50, f14 128, f15 0.
- **Ghidra drops arguments to callees it typed `void f(void)`** (or with too few parameters). The
  caller's decompilation then shows `ScnValueCtrlI()`, `FUN_001f58b8()`, `CheckPlyrAnimeEnd()`,
  `FUN_0021f7f8()` although a0 / a1 still hold the argument the caller loaded (ev_command.c, 2026-10-14
  session 3: SET_SLEEP_FLG_ passes (short)args[1], (short)args[2] to both scene-value controls;
  SET_EVENT_REC_HIT passes (args[2], kind) to both callees). Fix: for every call the decompiler shows
  with no or fewer arguments, read the asm before the `jal` (`ghidra.py asm`) and trust the registers.
- **gp-relative globals have no xrefs in Ghidra**: "no other writer found" of an sdata global (e.g.
  0x370abc) means nothing. Search the asm for the gp offset (gp = 0x3734f0, architecture.md).

- **Never test a GhidraMCP write with `dry_run`.** In a POST body it is ignored; in the query string
  `disassemble_bytes` honours it but `create_function` answered `"dry_run": true` and still created the
  function (removed again). Read endpoints are safe to probe; writes go through name_ctx.py /
  `ghidra.py mkfunc`, on purpose, and are saved by them.

- **Runs of tail-call wrappers decompile one call off** (menu_file.c 0x2078a0..0x2079e0, 2026-10-20):
  each 5-instruction wrapper ends in `j <func>`, and Ghidra showed every wrapper calling its *neighbour's*
  target (0x2078a0 "MenuNumberDisp()", 0x207950 "FUN_00207910(); MenuNumberDisp()", 0x2079c0
  "FUN_00205748(); MemoSetRead()") and the stage callee lists shifted with it. Ghidra's decompile of the
  hooks there also lost register arguments (in_t1_lo, in_stack). In a run of tiny wrappers or B2d hooks,
  read `ghidra.py asm` for the whole range first; it took less time than reconciling the decompiles.
- **A tail call into the next function hides it**: EnemyWrkOne 0x186ba8's switch case 3 is `j 0x186c58`,
  and Ghidra grew EnemyWrkOne's body over EnemyActOne (0x186c58..0x18705c): the decompilation showed one
  big function with the case-3 code inline. A `j` (not `jal`) to an address with its own prologue
  (`addiu sp, sp, -N`) inside a body is a separate function: `ghidra.py mkfunc` it.
- **VU0 distance without height**: FF3 inlines `vsub.xyz; vmul.xyz; vaddz.x; vsqrt` — `vaddz.x` adds only z
  to x, so it is the x/z distance (ENE_WRK.dist, EneAlphaCtrl, EneMoveCtrl). Ghidra prints `_vaddbc`;
  read the asm before writing a 3D length.
- **VU0 vsqrt is sqrt(|x|)**: a point-to-line distance computed as |p|^2 - dot^2 / |v|^2 can round
  slightly negative; vsqrt returns a small number, host sqrtf returns NaN and every later compare fails
  (HcBasePointRectangle). Write sqrtf(fabsf(x)) where the ROM's vsqrt input can be negative.
  CamCircleCrossLine (cam_rail.c) even accepts discriminants down to -100000 and takes the root of their
  magnitude.
- **GetTrgtRot(p0, p1, rot, 2) writes only rot[1]** (FF3 no longer clears rot): a caller that then feeds
  all three angles to RotX / RotY / RotZ (CamStancePitchCtrl) rotates by stack garbage in rot[2] on the
  EE; zero it on PC with a comment.
- **VU0 macros that read vf1, or lose the broadcast** (g3dGeom.c, 2026-10-16): vf1 = (1, 1, 1, -1) and
  vf2 = (1, 0, 0, 0) are constants (Vu0InitConstRegisters, data 0x35aa40). `vadday.x; vmaddz.x vd, vf1, vd`
  is a plain x+y+z sum, but g3dMatrixGetRoll's `vaddz.x vf13, vf1, vf13` is 1 + z^2 (FF2 too), and
  g3dCalcIntersectionEllipseAndLine's way back is `vmula/vmadda/vmadd.xyz` (component-wise), not
  `vmulax/vmadday/vmaddz` -- Ghidra prints both as `_vmula`/`_vmadd`. Decode the raw word (fs = bits
  15..11; special2 opcode = bits 10..6 << 2 | bits 1..0: 0x18..0x1b broadcast, 0x2a VMULA) before trusting
  MioPan's reading, and keep the ROM's arithmetic. Also: the inline normalise keeps the source w
  (`vmul.w vd, vs, vf0`) and the inline cross product keeps the first operand's w -- sceVu0Normalize /
  sceVu0OuterProduct on the host clear it.
- **Ghidra rejects template spellings in names** (2026-10-16): name_ctx.py printed FAILED for
  `fill<_LIGHTCOMPAREDATA*,_LIGHTCOMPAREDATA>` (the batch went on) and the old wrong name stayed. Use a
  plain spelling (`fill_LIGHTCOMPAREDATA`) and say the template in `why`; always grep name_ctx's output
  for FAILED -- a name already used elsewhere also fails (rename the wrong holder first).
- **`ghidra.py find` with a mask finds nothing**: `find 109a0000 ffff0000` returned none where the plain
  `find 109a` lists the five `addiu ..., -0x65f0` users (2026-10-15). To find every load/store of a
  static by its low half, search the two immediate bytes without a mask and keep the 4-aligned hits in
  .text. A member nobody addresses directly is reached through a getter's result (`MapRecGet()+0x18`
  was CMapRec's map window): check each caller of the getter before declaring a field unwritten.

- **An argument register passed through untouched.** Symptom: Ghidra shows FUN_0021d498(no, mot)
  with two parameters, but its callee FUN_0021f640 reads a third (a2) that nobody seems to set. Cause:
  FUN_0021d498 never writes a2 before the call, so its caller's third argument flows through (the
  event handler loads args[2] into a2; PlyrCameraUpdate sets 0). Fix: when a callee uses an argument
  register the function does not set, check the asm of the caller's caller and give the function that
  parameter.

- **Ghidra's parameter order for float arguments is unreliable.** Symptom: a decompile shows
  `FUN(float param_1, ptr param_2, ptr param_3)` while the asm moves a0 / a1 into the pointers and
  `mfc1 sN, f12` for the float (gra3dGenerateBlendedLight 0x1df968; callers decompile the same wrong
  order). Cause: EABI passes floats in f12.. and integers in a0.., so the position of a float among
  the arguments is not in the registers. Fix: read the prologue; pick an order, say so in the header,
  and check it against the first caller you write. Also VU0 macro code (vmulax / vclipw / cfc2 vc2)
  decompiles into `extraout_vfN` noise -- reconstruct it from `ghidra.py asm`, not the decompile.

- **Vtable slots without a Ghidra function** (2026-10-16, motion_model_manager.c). Symptom: `rtti.py
  --class` lists slot targets that modview, the brief and the TU's function count do not show (7 of
  MotionModelManager's 16 overrides; the TU looked like 13 functions); `dec.py` of the neighbour shows
  the slot's code swallowed into it (wrong callees). Cause: nothing calls the slots directly, so analysis
  never made functions there. Fix: `ghidra.py func` each slot address, `ghidra.py mkfunc` the missing ones
  (the first call may print "Unable to lock due to active transaction" yet still create it -- check with
  `func`), then name_ctx.py (its refresh brings them into the TU).
- **rtti.py's ctor/dtor list is a guess** (2026-10-16). It lists functions that store a vptr, plus
  neighbours; 0x212c40 was listed (and declared in motion.h) as BmdMotion's base-object constructor but is
  VertexMotion::Update, a per-frame step. Check a listed constructor's body (vptr store, base-ctor call,
  field init) before trusting the label.

- **A goto label in column 0 breaks callcheck** (2026-10-19, cat_ctrl.c): `loop:` at the start of a line
  followed by `if (...)` / `{` matches callcheck's definition regex, so it reports a function `if` with the
  previous annotation and "missing" calls. Indent labels (`  loop:`).
## Names inherited from FF2

- **A dropped FF2 debug module still attracts matches.** FF3 has no scene-test menu, yet 8 functions in
  FF2 scn_test.o's link slot matched FF2 scn_test.c (SceneTestMain, ScnValueCtrlI/C, ScnUDCtrl,
  SceneTestMenuCsrToFuncNo, ...) by size and shape alone: they were SceneModel methods and a sleep-flag
  table (2026-10-20). In a debug module's slot treat every match as unverified. The same run showed
  Ghidra bodies swallowing small functions: an RTTI vtable entry that points mid-function (0x257028,
  0x257040 inside "SceneTestMain", 0x257188 inside 0x257130) is a function start; split it with
  `ghidra.py mkfunc` before staging.
- FF2 names are hints. Wrong ones found so far sat in: phase code (`SpCmnTexMemLoad` on gameplay's one),
  sound (`SaAnimSet`, `SisNeckRegisterTarget`, `SaNeckPos`), pad (`PauseTitleDisp`, `GetPadStateStable`),
  file load (`SubTitleCheckDispAccept` = `LoadReq`), runtime (`*_type_info_function`, `do_dyncast`),
  2D (`Anim2D_CalcNowRGB` = `LayoutAnimStart`, `mimIsReqMimNum`/`mimStop` = message-page helpers,
  `dmaVif1GetPacketVIF` = `File0ad6Apply`), GPhase (`GetGameDataSaveDataMem` = `GPhaseCtrlFreeEndedNodes`),
  game work (`GalleryInit`/`GalPictureLoadWait` sat in ingame_wrk.c/ingame.c; `GetPlyrRoomID` returns the
  current player number), C++ (`~CTexture` = `MotScrHdlDelete`), options/pads (`one_UBI_Mode`,
  `GetPadDUALSHOCK2`, `DispPhotoName`), enemies (`EJobB02` = `GetEneSpeObjTbl`).
  maths (`GetTrgtRot` sat on 0x263a10, a C++ factory; the real one is 0x1efc28 in vu0math.c; libm
  0x294800 was `atan2f` but takes one argument and is `logf`).
  Check the body before trusting a name in a new module; fix with `name_ctx.py` (records an override).
- **FF3 has no sister mechanic** (the user's rule): every FF2 sister name in FF3 - `sis_*`, `Sa*`
  (sis_algo), `Sis*`/`Sister*`, `SetupSisMdl`, `ReqSisterMim`, `InitSisterTracePos`, `GetSisterRot`,
  `SisterGameOver`, `ChangeForceTraceMode` - is a wrong match, and the TU slots `ingame/plyr/sis_algo.c`,
  `sis_mdl.c`, `sister.c` hold other code (0x257ae0..0x258a68 is the sound API, now
  `ingame/sound/snd_ctrl.c`). Name such code from its body; never keep the sister file names.
- **`global.constructors.keyed.to.*` (ff2-match-low) on ordinary functions**: 0x13cb98, the callees
  renamed `EventLoadRestore` / `ScreenEffectParamInit`, `MorphGetCoord`. A real one has no callers and
  calls `__static_initialization_and_destruction_0(1, 0xffff)`; anything else is misnamed.
  The reverse too: a function shaped `if (a1 == 0xffff && a0 == 1) ctor(&bss_obj)` under an FF2 name (MapObjRegistPhf 0x11beb8) is a file's static init, so its file ends there and the functions before it belong to that file, not to the FF2 file the name implies (MapObjReg.c's slot was MapLoad.c's tail, 2026-10-21).
- **Shifted runs**: runs of similar small functions (copy/draw families, soft-float `dpadd`..) were matched
  one slot off even at score 0.9. Compare struct offsets / call shapes before trusting a run. libpad was
  one of them (fixed 2026-10-13: 0x284120 scePadGetDmaStr, 0x2842d0 scePadRead, 0x284348 scePadGetState,
  0x284540 scePadInfoAct, 0x284c60 scePadInfoPressMode, 0x284ce0 scePadEnterPressMode); check the rest of
  0x283e80..0x284f98 before relying on a name. More wrong FF2 names in anicode.c's slot:
  `motAniCodeClearBuf`, `motAniCodeReadMOT` (0x14f258), `motAniCodeIsEnd`, `motAniCodeReadTIMER` —
  the code there is FF3 layout code, not FF2's ANI_CODE reader.
- **FF2's debug build is full of assert/debug calls FF3 retail does not have** (`printf` in 450 functions,
  `SetAssertPreMessage`/`PrintAssertReal` ~440, `g3ddbgAssert` 369, `_fixed_array_verifyrange<T>` in
  hundreds). FF2 functions with them match FF3 at 19% vs 25% for clean ones. The matcher already drops
  these callees (`DEBUG_RE`) and compares instruction shape by containment; when porting, drop them
  (reconstruction.md "Port from FF2 first"). FF3's C++ objectification costs more matches than the
  asserts do: find the counterpart by module and class (`rtti.py`), not by shape.
- **Identical id wrappers** (`f(id) { Sub(GetX(id)); }`) cannot be told apart by shape: the matcher put
  FF2's SndBufStop on 0x26ab08, which calls the restart sub. Name such wrappers by the function they call.
- **Effect names on enemy code** (2026-10-14): in 0x186a88..0x189b90 every FF2 name was wrong — `HazeCtrlInit`
  (an M match!) is EneGetJointPos, `EffectSaeHazeReq` EneGetJointRot, `EffectKusabiHazeReq` GetEneWrkBuffer,
  `SetLeaf` EneDamageStart, `CallAmuletFire` EneSetStepReq, `EffectThunderLightGetLightningFlg` a getter of the
  gauge target, `EffectSprayParticleUpdate` EneBlinkCapture; the ff3 guesses `EffectOthWrkInit` / `EffectSlotMain`
  were EnemyMainInit / EnemyMain. Code that loops 0x2fa3a0 in 0x700 steps is enemy code whatever it is called.
- Small store-only functions matched by position are unreliable (`GameDataSaveCtrlInit` was GPhaseCtrlInit).
- GCC 2.96 runtime (RTTI functions, old EH) has no FF3 counterpart; never match it.

## Matcher

- The link-order chain is greedy: one correct anchor in a module FF3 linked elsewhere walls off alignment
  around it. Context-identified anchors stay off the chain. Small anchor changes move hundreds of order
  matches — always `reconcile.py --plan` and compare before applying a new run.
- `reconcile.py` never overwrites context names (ledger) and honours overrides; keep it that way.
- Matcher names at a module's head are often its neighbour's (2026-10-13): 0x26b768 "SndStreamStart-
  HeaderOnMemory" was SndStreamInit and 0x26bc38 "SndStreamPrintStatus" SndStreamMain (FF3 dropped the
  print-status function, so the chain slid). Check every `ff2` name a slice calls against its body and
  its caller's FF2 call order (SndInit / SndMain list them) before declaring it.
- Runs of same-shaped functions slide too (2026-10-19): ingame_effect.c's IgEffectSubFunc*Req are 0x70 / 0x3c
  bodies in FF2's order (Zero, Slow, Koku, Paralyze, View, Metsu, Ren, Tsui, Fuu) plus two FF3 ones, and the
  matcher put FF2's names five functions late (0x1e8fa8 "Zero" was Metsu, 0x1e9178 "Ren" an FF3-only one).
  What decided it: the EneDmgLargeHitReq preset indices each body passes, compared with FF2's preset table.
  When a slice calls into such a run, check each body's constants, not just its shape.

## File inference (which TU a function belongs to)

- **Inferred files can be badly wrong around FF3-only code.** `export.py` fills runs between two
  anchors of the same FF2 file, so one wrong small match spreads: `GameSystemStart/Frame/End`
  (0x1d2610..) landed in `outgame/gallery.c`; the boot phases (0x100388..) land in `crt0.s`; the
  `main/main.c` TU first pointed at a map-menu helper (0x1f81d0, matched as `one_SoftResetMain` by link
  position only) while FF3's `main` really is inside system.c. Fix: settle the extent in the TU's first
  session and pin it (`tus.py define`); pinned ranges override inference. Atexit thunks of singleton
  statics mark TU ends (architecture.md "TU ends"). More cases: the run inferred as `outgame/gallery.c`
  was ingame_wrk.c + ingame.c; pad code (CheckPadRptPlaySe) was inferred into photo.c; message.c code
  at 0x20daa0..0x20de38 is inferred as mime.c; GetOptionWrk is inferred into common/ol_load.c;
  `graphics/graph2d/tim2.c` was inferred as 0x25a850..0x25caa8 but only 0x25c368.. is TIM2 code — five
  low FF2 "matches" (Tim2IsClut2, Tim2LoadPicture/Image/Clut, Tim2CalcBufWidth: tiny bodies reading gp
  globals) dragged two FF3-only modules in. A `ff2?` mark on a tiny function is a hint, not evidence.
- **Inferred file *names* get taken literally.** A slot keeps FF2's file name even when FF3 put
  different code there, and sessions then pin, name and describe the code after the wrong module:
  `graphics/motion/anicode.c` holds FF3's menu windows / layout-type callbacks and the `CcmLoading`
  classes (6 of 95 functions match FF2 anicode); `ingame/puzzle/kai/kai_pzl.c` held the language select and the item menu (menu_item.c)
  and the `Story_Menu` phase besides 8 puzzle functions. Check the brief's identity section (`tuid.py`)
  before trusting a name: few or no FF2 matches from the named file, strings / classes / function names
  of another subsystem, and callers from elsewhere all say "rename".
- **Name an FF3-only file by its slot: the link order is alphabetical by file basename** (FF2:
  `fod.c`, `foot_se.c`, ... `m_plyr_camera.c`, `main.c`, `man_data.c`, `map_bgm.c`), so a new run's
  name must sort between its neighbours' basenames. 0x1c7c38 (font textures) sits between fod.o and
  foot_se.o -> `font.c`. A file's `__static_initialization_and_destruction_0` / `_GLOBAL__I_` pair
  (.ctors at 0x2e9100..) ends its text, so it belongs to the file *before* the next run.
  RTTI vtables follow the same order (`rtti.py` sorted by vtable address: CExtraMissionShelt,
  CGameClear, CLightCtl, ... CWindowMsg, CmemShelt, CmenuCam*), and each class's ctors/dtors
  (`rtti.py --class`) sit inside its file: a run holding several classes is several files, named by
  class (`common/ProcCtl.c`, `common/WindowMsg.c` out of the inferred FurnCtl.c, 2026-10-14).
  Check a queued name against this order before working: `ingame/event/prg/ev_macro.c` was inferred
  at 0x19a670..0x19ac18, but FF2 ev_exe.c's functions start at 0x19abe8 and FF2 ev_macro.c's
  (EventMacroInit, EventExeFuncCall) at 0x19b4a8, so the slot was ev_chapter.c + ev_exe.c and the
  real ev_macro.c sat four files later (2026-10-15).
- **`define` drops the whole inferred extent, also for a neighbour you only extend.** Pinning
  `ingame/camera/map_camera.c` to just its two leading getters (0x1fada8..0x1fadc8) turned the rest of
  the inferred file (0x1fadc8..0x1fd9c0, 43 functions) into `unassigned@001fadc8`; the same happened to
  man_data.c. Before defining a file that already has an inferred extent, read it from
  ff3_functions.txt (the FILE header to the next one) and pin all of it.
- **Renaming a wrong FF2 match moves inferred boundaries.** export.py anchors files on FF2 names, so
  replacing `MapSaveSetStat`, `PlayerDrawLock`, `MapDoorGetHdl` / `MapDoorAllPreRender` (2026-10-15)
  moved their functions and neighbours into new `unassigned@` runs, and stubgen put their stubs into
  `src/lib/unassigned_stubs.c` instead of MapSave.c / plyr_mdl.c / MapDoor.c (a header written for that
  TU was then useless). After a rename batch, re-run the TU lookup (`tus.Model().tu_of`) for the callees
  before writing their prototypes, and add the includes stubgen needs to unassigned_stubs.c.
- **A pinned TU is exactly its ranges.** An inferred file can span several runs (album_view.c:
  0x12bef8.. and 0x134510..0x13cbb8); `define` with one range drops the rest from it. List every range.
- **stubgen finds one declaration per line.** A prototype block written four to a line (`void EJobC00(ENEALG_WRK
  *alg); void EJobC01(...);`) got only the first of each line stubbed; the rest stayed undefined references
  ("no declaration found in src/"). Write file-local prototype blocks one per line (enemy_act.c, 2026-10-19).
- **stubgen places stubs by link-order inference**: callees at 0x100b80.. (B2d hook setters) went into a
  new `src/lib/crt0.c` because the inference put them after crt0. Move such stubs to
  `src/lib/unassigned_stubs.c` (or their real file) and delete the stray file.
- **A queued `unassigned@` key goes stale** once a session defines that run under a file name; `tus.py
  next` now skips queue keys with no functions and no ranges (2026-10-13). Queue file keys when you can.
- **Satellites** (getters/templates 0x2ab000+) are given to the TU that calls them most, which is not
  always the owner (`GetSysWrk` -> finder_camera.c). Pin them to the owning TU with `define`.

## Sessions

- **A session never launches other sessions or agents.** The auto-mode classifier blocks it ("Create
  Unsafe Agents"); the user starts each TU's session.
- **`tus.py finish` commits with `git add -A`**: any untracked file in the tree (a user's work in
  progress, scratch output) goes into the session commit. Look at `git status` before finishing; keep
  foreign files out (`.git/info/exclude` for the duration, or move them out of the tree). Locked files
  make it fail: Visual Studio's `.vs/` index did (now gitignored); if finish fails after recording the
  session, remove the duplicate session entry from `tus.json` before re-running it.
- **Two sessions in one checkout race (2026-10-15).** A scheduled run claimed item.c while an interactive
  session was committing a refactor in the same tree every few minutes. The run's `tus.py finish` (`git add -A`)
  swept the other session's half-done files into the TU commit (6af19a5), and the other session's next commit
  (b8e70dd, written from a tree older than 6af19a5) reverted the TU's source; it was restored in bee22c7. The
  other session's edits also turned the build red mid-run (the check failed on code the run never touched).
  Fix: before `finish`, list `git status --porcelain` and stop if a path is not the session's; wait for
  the other session's commit (poll until the foreign paths are clean), then re-check, then finish at once.
  After pushing, `git log -3 --stat` must still show the TU's files in their committed state. Better still:
  do not run the scheduled task while an interactive session edits this checkout.
- **Autopilot sessions run on a fixed allow-list** (`tools/reverse/autopilot.py` ALLOWED/DENIED, no
  prompts). Denied so far: writes under `.claude/` (these references — Claude Code protects that
  directory even with Edit/Write allowed), `ghidra.py` (so no `mkfunc`/`refs`), `rm`/`git rm`, `git -C`,
  `cd ... && git`, shell loops, `sed`, chained `head`, MCP `disassemble_function`. What works: the tools in
  the list (`dec.py`, `stage.py --asm`, `ctxcard.py`, `name_ctx.py`, ...), MCP reads (`read_memory`,
  `get_xrefs_to`, `decompile_function`, `disassemble_function`; `disassemble_bytes` *writes*). A session
  that cannot write a reference puts the text in its `tus.py finish --notes` under "For the references:",
  and the next interactive session copies it. Files it cannot delete are left as placeholders
  (`outgame/gallery.c/.h`, `src/lib/crt0.c`). The app's scheduled task (`reipan-next-tu`) runs as a
  normal app session instead: approvals granted on its first manual run are kept for later runs.
- **No backticks in `--notes` / `--summary`**: the shell runs them as commands inside the double-quoted
  argument. Use plain quotes.

## Reconstruction bookkeeping

- **A stub that tolerated a null object becomes a member that does not.** Symptom: after GameModel's
  methods became real, the --run 100 smoke run crashed (0xC0000005) on the first Story_Normal frame in
  `GameModel::Draw (this=0x0)` from MapDrawPlyrMdl. Cause: AllPlyrIsReady answers 1 when no player
  model was made, and none is while CharModel's load (0x145090) is a stub; the old free-function stub
  never dereferenced its argument. Fix: when stubs become members, check every caller the smoke run
  reaches for objects the host does not build yet and add a `// PC:` null guard (MapDrawPlyrMdl,
  PlyrGetJointRotY / Rot / Pos). Find the crash with CLion's gdb (`bin/gdb/win/x64/bin/gdb.exe -batch
  -ex run -ex bt --args ReiPan.exe` from cmake-build-relwithdebinfo/ReiPan with REIPAN_AUTOPAD set to
  settings.smoke_pad): the backtrace names the line at once.
  Pending case (2026-10-16): GameAnimeModel's accessors (GetAnimeFlag, IsAnimeEnd, SetAnimeSpeed,
  GetAnimeTime, GetAnimeLength) are members now and read `scr` through a null plyr_wrk->mdl; the
  --run 100 run does not reach PlayerMain yet, so no guards were added -- add them where a run faults
  (plyr_sub.c PlyrMoveUpdate / PlyrTurnStart, player.c PlyrAnimeEndCheck / PlyrGetAnimeFrame).

- **Twin constructors / destructors count only with the `FF3 ` prefix.** Symptom: a TU with every
  function written still showed its base-object twins and deleting destructors as todo (motion.c,
  14 of them). Cause: reipan.reconstructed() matches `FF3 0x<8 hex>` only; comments said
  "base-object twin 0x002125f8". Fix: write every twin address as `FF3 0x...` in the .c comment
  ("base-object twin FF3 0x002125f8, deleting FF3 0x002130e8").

- **A Ghidra name you pick may already exist at another address** (name_ctx reports "name already
  used at ..." and skips it). If the source already uses the new name, stubgen resolves it to the
  OTHER function's address (MapPutSetFunc -> 0x11c558 instead of 0x11c578), or writes an address-0 stub
  into unassigned_stubs.c. Read name_ctx's "named n/m" line; on a FAILED name pick another, delete the
  wrong stub and rerun check (and refresh.py before callcheck, its cache keeps the old names).

- **Callers' local prototypes of a stub disagree with the real signature.** Symptom: a TU makes
  FadeInReq real with u_int frames, and every caller that declared it locally with int frames fails
  to link (the sources build as C++, so int and u_int mangle differently). Fix: when a TU becomes real,
  grep src/ for its function names, delete the local prototype blocks and include the new header
  instead (fade.c 2026-10-15 did nine files that way), then rename callers if Ghidra names changed.

- **"No stub on that path was reached" can be wrong.** Symptom: title menu items missing, the handoff
  said no stub was hit, yet B2dDrawSpriteByName ended in the stub FUN_002ab488. Cause: the reached-stub
  list of a long run is hundreds of lines, and a helper behind a wrapper (a tail jump in FF3) does not
  look like part of the path. Fix: follow the draw call down to DispSprD / the renderer in the source
  and check each callee for `REIPAN_STUB` before hunting in data.

- **A file-scope C++ object whose constructor is a stub runs that stub before main.** FF3 files with a
  static init (`__static_initialization_and_destruction_0`, _GLOBAL__I_ / _D_ pairs) construct their
  objects at load time; on PC a `static T obj;` does the same, so a REIPAN_STUB constructor (or a
  destructor stub at exit) logs before / after the logger exists. Fix: write the small constructor and
  destructor for real, even in lib/unassigned_stubs.c (ev_main.c's camVci, 2026-10-15).

- **A stub getter returns null; its consumer, once real, crashes.** Symptom: the smoke run exits at once
  with 0xC0000005 (no screenshots) after a TU makes an init function real (MapSkyDataInit wrote through
  the stub GetMapSkyData). Cause: stubgen's singleton getters (lib/unassigned_stubs.c, 0x2ab000+) return
  0. Fix: write the getter with its class (a function-local static; the guard and atexit are the
  compiler's), as MapSky.c does for GetMapSky / GetMapSkyData. Likewise a stub returning 0 inside a
  `while (f() == 0)` retry loop (MyStrStart) hangs the game: port the callee or keep the caller a stub.

- **stubgen and array pointers** (fixed 2026-10-14): it could not read prototypes with `float (*mat)[4]`
  parameters ("no declaration found") and took `return g3dSetTransform(state, mat);` in a .c as the
  declaration of a function returning an array pointer, writing a stub that does not compile. Now it
  reads one level of parentheses and skips statements; functions *returning* `float (*)[4]` still need a
  hand-written stub.

- **`FF3 0x<8 hex>` anywhere in a .c file counts as a reconstructed function** (`reipan.reconstructed()`
  scans every line). Use it only in the annotation above a definition; refer to other addresses as
  `0x1e6b98` (prototype comments, notes). callcheck also takes such a prototype comment as the
  annotation of the next definition below it (a static helper then shows the callee's MISSING calls).
- **callcheck flags getters as MISSING `atexit` / constructor calls**: FF3 spells out the static's guard,
  constructor call and `atexit`; C++ function-local statics get them from the compiler. Expected;
  say so in the getter's comment. The same goes for `operator_new`/`operator_delete(_array)` (C++
  `new`/`delete` expressions), a `memset` that became `= {}`, and implicit base constructors: write
  `// callcheck: Name (why)` (tools README) so the check stays quiet.
- **A `// callcheck:` explanation is read from its first line only**: names wrapped onto a `//   ...`
  continuation line still show as MISSING. Put every name on the `// callcheck:` line and the reason
  on the next one.
  The names are the run of words and commas right after `callcheck:` (regex `[\w, ]+`): the first
  `(` or `;` ends it, so `rand (why); memset` explains only rand. Write `// callcheck: rand, memset (why)`.
  `::` ends it too: `callcheck: GameAnimeModel::GetAnimeFlag` explains only `GameAnimeModel`; name
  the member plainly (`GetAnimeFlag`, its source name) or by its flat Ghidra name.
  A note that names a `FUN_xxxxxxxx` goes stale when Ghidra renames the callee: six plyr_sub.c /
  n_plyr_camera.c notes said `FUN_00294560` after it became `fabs` (2026-10-17). Name libc and
  compiler helpers by their real names.
- **Shared static helpers inherit the previous annotation**: callcheck takes a function's comment block
  as everything since the last line starting with `}`, so a one-line helper (`{ return ...; }`) after a
  prototype commented `FF3 0x...` was checked as that function. Keep `FF3 0x` out of prototype comments
  (`// at 0x1ef758`), and when a reconstruction moves FF3's inlined calls into a shared helper
  (g2d_draw.c's G2dSendSprPacket), name them in a `// callcheck:` line on the caller.
- **callcheck's cache keeps old names** until `refresh.py` (a renamed callee shows as missing under
  its old name, e.g. MorphGetCoord's).
- **callcheck used to drop `FUN_xxxxxxxx` calls** (its macro filter treated all-uppercase names as
  macros, and `"FUN_00123456".isupper()` is True); fixed 2026-10-13, so older `// callcheck: FUN_...`
  explanations may now be redundant.
- **A local prototype with another signature is a C++ overload, not an error**: every .c compiles as
  C++, so `void SndSePlay(int, ..., int a4, int a5)` declared in six files linked to an old stub while
  snd_ctrl.c defined `int SndSePlay(unsigned, ..., void *s3s, int)` - the build stayed green and the real
  code never ran. When a TU defines a function, grep `src/` for local declarations of its name, replace
  them with the TU's header, and delete the stub (the brief only lists stubs by address).
- **A real function behind a polled stub can stall the boot**: the old SndSeBankLoad stub returned 0
  (ready); the real one asks the library stub SndBankGetFreeBankNum, gets 0 and returns -2 forever, and
  Boot_Load waited on a black screen. Check what the callers poll before replacing a stub; bridge the
  library calls the path needs (as `src/lib/snd_bridge.c` did for the SPU allocator until spu_mem.c landed).
- **Platform substitutions show up as missing/extra calls** (`srand` -> `ReiPan_SRand`, added
  `ReiPan_*` hooks). Expected when the code marks them `// PC:`.
- **Stubs that return a buffer pointer return null** (`return {};`), and that is invisible until the
  next consumer is real: `cmp_eeiopInit` handed FileLoadInit -> SndInit a null work buffer, harmless
  while SndBankInitAll was a stub too, an access violation the moment snd_bank.c landed (2026-10-13).
  When a crash follows a stub that returns `void *`, give that stub its pointer arithmetic first.
- **FF3 writes through a null layout-anim slot**: SaveLoadScrMsgUpdate (and its twins) call
  LayoutAnimSetFlag / LayoutAnimSetAlpha on the slot LayoutAnimStart returned before testing it for -1;
  on the PS2 LayoutAnimGetSlot(-1)'s null lands in low memory, on PC it is an access violation. Test the
  slot first (marked `// PC:`) wherever a reconstruction meets this order.
- **Unreferenced twins of constructors / destructors**: GCC emitted FF3's C++ constructors and
  destructors twice (complete- and base-object copies), so a run of identical bodies with no callers
  sits next to the used one (effect_obj.c lanterns: 0x165dd8 / 0x165ef0, 0x166438 / 0x166538, 0x166a30 /
  0x166ac8, 0x1672c0 / 0x167478, 0x167630 / 0x1676d8). Write one body, name the twin `<Name>2` in Ghidra
  and mention it as "(twin 0x...)" on the `FF3 0x` line of the used one. Constructors also carry the
  exception cleanup calls (operator_delete of members), which callcheck reports as missing: explain them.
  A twin can sit on the far side of a TU boundary: B2dBase.c held "B2dUnusedInit18" 0x109728, an
  unreferenced 0x18-byte init that was the C1 copy of CBgSe's channel constructor 0x109748 (BgSe.c,
  2026-10-19). An unreferenced init at a range edge whose body matches the next TU's first function
  belongs to that TU: move the boundary (`tus.py define` both files).
- **Twins and vtable slots hidden inside other functions** (motion.c, 2026-10-16): Ghidra had no
  function at many vtable slots (VertexMotion::Step 0x212b00, BmdMotion::GetFrameNum 0x2131f8 /
  BlendPose 0x213a40, ClothMotion::Stop 0x2156a0) and swallowed C2 twins into the function before
  them (BmdMotion base ctor 0x212cb8 inside VertexMotionUpdate, base dtor 0x212f18 inside
  BmdMotionCtor). Symptom: callcheck reports a member as missing calls that belong to the next
  class (heapCtrlMalloc in VertexMotion::Update). Check every `rtti.py --class` slot with
  `ghidra.py func`, `mkfunc` what is missing, and look at the asm past a short function's `jr ra`.
- **One `FF3 0x` per function**: callcheck takes the *last* address in the comment block above a
  definition, so a shared comment ("FF3 0x26d920 ... 0x26d9a8 is its twin") checks the wrong function.
  Give each definition its own `// FF3 0x...` line.
  The reverse bites too: a group line `// FF3 0xA / 0xB / 0xC` above several definitions counts only
  0xA as reconstructed, so the brief lists B and C as todo and the next session re-slices written code
  (ev_command.c, 2026-10-15: 86 handlers). A group comment may stay, but each definition still gets its
  own `// FF3 0x...` line right above it.

- **Making a stubbed constructor real changes event flow** (2026-10-16): while CharModel::Load was a
  stub returning 0, PlyrMdlCreate failed and GO_DREAM_WORLD / PLYR_MODEL_CHANGE (ev_command.c
  EvPlyrMdlChangeStart) took the refused branch and went on; with the real classes the change
  succeeds and EvPlyrMdlChangeWait waits on FUN_001f4668 (the door anime load, a stub returning 0) --
  the opening event stops before rs106 and the SCENE0010 movie. When a run loses ground after a TU,
  diff the `[load]` lines of the run log against a baseline run of the previous commit (git stash -u,
  build.py --run, stash pop) and trace EventExeCmdCall (print the command name and process) to find
  the command that never returns 1.

## Types and layouts

- **EE float literals are not always round-to-nearest** (2026-10-15, candle_ctrl.c): the ROM's 0.2 and
  0.1 are 0x3e4cccd0 / 0x3dccccd0 (host 0.2f / 0.1f are 0x3e4ccccd / 0x3dcccccd), its RAND_MAX
  0x4effffff. Read every literal's bits from the ELF and write the odd ones as hex floats
  (0x1.9999ap-3f), as fndr_effect.c does.
- **`param_1 != -0x240` in a decompilation is a null test of `&obj->member`** (enemy.c EneLightRelease /
  EneDeformRelease: `if (&ene->eff != NULL)` of an inlined helper on ENE_WRK +0x240). It names a
  sub-struct at that offset: lay it out as its own struct (ENE_EFF), and keep the test with a local
  pointer (`ENE_EFF *eff = &ene->eff; if (eff != nullptr)`) so the compiler does not warn.
- **A mid-struct pointer moves every later host offset.** One EE pointer (ENE_WRK._128) shifts all fields
  after it on x64; the struct's `== 0x...` asserts after it become `>=` lower bounds and the layout note
  says where host and EE part (enemy.c 2026-10-14).

- **FF2's layout is a hint, not FF3's.** GRA3DCAMERA is 0x1e0 in FF2 and 0x220 in FF3 (matWorldView
  moved from scratchpad into the object at 0xe0): an FF2-derived comment called FF3's +0x120
  "matWorldClipPolygon" when it is matWorldScreen. Check offsets against FF3 accessors before naming.
- **MinGW is not "GCC layout".** MinGW-w64 defaults to `-mms-bitfields`: a bitfield run that changes
  declared type (`u_int a:4; u_short b:4;`) opens a new unit on MinGW *and* MSVC (8 bytes), while EE GCC
  packs it into one word. layoutcheck's self-test showed both host compilers failing the EE size.
- **`long` is 32-bit on both Windows compilers** (LLP64); FF2's `types.txt` says `long int` for 64-bit
  fields. Translate to `u_long`/`int64_t`.
- **Tail padding differs between the host compilers.** GCC (MinGW) places a derived class's first field
  in the base class's tail padding (AnimeCodeManagerGame.time), MSVC does not. For host-layout classes assert
  `offsetof >= ...` / `sizeof >= ...`, not `==`.
- **A plain `float[4]` is not a VU0 vector.** The `sceVu0*` typedefs are 16-aligned (as on the EE) but
  FF2's `types.txt` often spells members `float v[4]`, which is 4-aligned: a vector after an `int` then
  sits at +4 instead of +0x10. Use the typedef; the offset asserts catch a miss.
- **FF3 sizes in operator_new under-allocate host classes** (char_model.c, 2026-10-16). Symptom: a
  SIGSEGV inside my_mallocFree / heapCtrlFree when a model is released (CharPlayerModel::ReleaseShapes under
  EvGoDreamWorld, frame ~1700 of a --run 160), only once some code writes the object's last fields. Cause:
  `new (operator_new(0x28)) CharWatchControl(this)` copies the EE size from the decompilation, but a class
  with a vptr and pointers is bigger on x64 (0x30), so the tail overwrites the next heap block's header.
  Fix: `operator_new(sizeof(Class))` with the FF3 size in a comment; run gdb on the smoke run to see it
  (the run otherwise just stops producing screenshots: Windows keeps the crashed process until the timeout).
- **A SingleLinkList sized with the EE record size truncates host records** (effect_oth.c, 2026-10-16).
  Symptom: none until the record's tail is read -- EffMapPartReq's list was `SingleLinkListInit(&list, 0x70)`,
  but EFF_MAP_PART holds three pointers, so the host record is 0x80 and SingleLinkListAddEnd copied only
  0x70 bytes: Type, Num, Area and No never reached the cell (the per-type draw would have skipped every
  record). Fix: `SingleLinkListInit(&list, sizeof(RECORD))` with the EE size in a comment, like operator_new.
- **callcheck reads `callcheck: A, B (why)`, names first, one parenthesis last.** `A (why), B (why)` explains only
  A: the regex stops at the first parenthesis, so B is still reported MISSING.
- **The build never runs MSVC.** Only `layoutcheck.py` does (syntax only, via vswhere's vcvars64.bat);
  an MSVC-only error shows up there first.
- **EE float division by zero is not IEEE**: `div.s` returns +-MAX and the VU computes 0 * MAX = 0, so
  FF3 normalises zero vectors to zero (snd_3d.c `GetLenDirection`, `Snd3DCalc` with a source on the
  listener). On the host 1/0 is inf and 0 * inf is NaN, which then turns into INT_MIN pitches and
  volumes. Guard such divisions with a `// PC:` helper that scales by 0 (snd_3d.c `snd3DInvLen`).

- **MioPan's g3dSprite.cpp swaps the GIFTAG words.** Symptom: a ported CSprite would emit a tag whose first
  word is the REGS list. Cause: MioPan writes the PRIM / NREG expression to `Gt.lRegs` and the REGS nibbles to
  `Gt.lTag`; FF2 and FF3 store the tag at header +0x10 and REGS at +0x18. Also MioPan's CRenderTarget::Clear
  sets the sprite colour, which FF2 / FF3 never do. Fix: check every MioPan GIF-packet builder against the
  stores' offsets, not its field names.

## Shell and Python on Windows

- **A NUL byte makes git treat a reference file as binary**, so `core.autocrlf` stops normalizing it and
  every session that rewrites it on Windows flips its line endings (architecture.md held a literal
  `"phf\0"` NUL until 2026-10-14; its blob was CRLF while the commit log said LF). Never write raw control
  bytes into a .md; write escapes (`\0`). Check with `git diff --stat` before finishing: a whole-file
  diff for a small edit means line endings changed. A backslash-zero typed into a Bash heredoc can
  arrive as a real NUL; edit such text with the Edit/Write tools. In C source (2026-10-20, a `'\0'`
  appended to utility2.c through a quoted-heredoc Python string) GCC built it but layoutcheck's MSVC
  pass failed with C2137 "empty character constant".

- **An apostrophe in a quoted heredoc still breaks the Bash tool** (`unexpected EOF while looking for
  matching '`, 2026-10-16: a naming batch with "the cat's" in a `why`, C comments with "don't"). The
  whole call fails before running. Write JSON batches and code with the Write tool.
- **Heredoc backslashes**: bash heredocs mangle `\` (regexes, `\n` in C strings). Use the Write tool for
  files with escapes; quoted-heredoc Python with `chr(92)` if scripting. Seen again 2026-10-16: a doubled
  backslash-n in a quoted heredoc's Python string arrived as a real newline inside a C printf (red build).
- **Python text mode vs the working tree's CRLF** (2026-10-16): `core.autocrlf` leaves CRLF in the working
  tree. `open(p).read()` turns it into LF, so patterns written with a plain newline match; a binary or
  `newline=''` read finds nothing. Normalise CRLF yourself when reading bytes, and look at
  `git diff --stat` for whole-file diffs afterwards (text=auto normalises the commit either way).
- **A long Bash command fails with `ENAMETOOLONG: name too long, uv_spawn`** (seen with a ~600-line
  heredoc appending reconstructed C, 2026-10-14). Nothing runs, including earlier commands chained in the
  same call. Write long code with the Write tool (a scratchpad file, then `cat >>`), keep one Bash call
  per step.
- **A Ghidra function body that ends just before unreferenced code hides a function**: 0x2353a0's
  decompile looked complete but the function list showed callers it did not have; the asm after its
  `jr ra` was PlyrViewDistCheck 0x235458 (reached by `jal` from 0x228a88 only). When `- callers` in a
  staged bundle lists a function that does not call it, `ghidra.py asm` past the body end and
  `ghidra.py refs` the next address.
- `python -I` drops the script directory from `sys.path`; the tools import `reipan`/`elf32` locally, so
  run them as `python tool.py` from `tools/reverse`.
- The console is cp1252: tools printing FF3 strings call `sys.stdout.reconfigure(encoding='utf-8')`.
- **Never pipe `name_ctx.py` (or `refresh.py`) into `head`**: the pipe closes early and kills the
  refresh it runs after naming, so feat_ff3.json / ff3_functions.txt keep the old names (callcheck
  then reports renamed callees as MISSING). Run `python refresh.py` again if it happened.

## Platform layer (MioPan's, written for FF2)

- **An EE wait on an IOP status never ends when nothing answers the RPC.** Until 2026-10-14
  src/system/iop/iop.c registered no service: sceSifCallRpc returned, iop_ret stayed zero, and every
  "wait for the IOP" (SndStreamIsPreload, CheckEndPointThrough) hung or lied. The IOP module is now
  MioPan's; when adding an EE command, check that iopCommand steps over exactly the payload the EE sends
  (iop_snd_def.h asserts the sizes): one wrong size turns the rest of the frame's queue into garbage.
- **SDK shims only answer what FF2 asked.** FF3 calls things FF2 never did, and the shim may return a
  placeholder: `scePadInfoPressMode` returned `scePadReqStateComplete` (0) instead of "pressure mode
  supported" (1), so the boot pad check showed its controller-error message and stopped every smoke run
  in phase 1 (fixed 2026-10-13). When FF3 code behaves as if hardware is missing, read the shim in
  `src/sdk/` against Sony's semantics before blaming the reconstruction.
- **A missing card directory must answer -4.** libmc.cpp listed a "DIR/*" that does not exist as 0
  entries; FF3's checks (AutoLoadMcCheckWait, the load / save screens) tell "no save yet" (-4,
  sceMcResNoEntry) from "save damaged" (a listing whose file count does not match), so a fresh host
  card showed "Game data damaged" at boot. Fixed 2026-10-14 (outgame/autoload.c session). The host card
  is `%APPDATA%/Mikompilation/ReiPan/memcard/mc0-0/`; delete its folders to test the fresh-card path.
- **Textured vertex colours above 0x80 brighten (GS MODULATE).** The GS computes (Ct * Cf) >> 7 and
  clamps at 0xff, so 0x81..0xff brighten the texel up to 2x. `Ps2ColorToFloat` (reipan_renderer.cpp)
  used to cap the colour at 1.0: FF3's font texels are 0x80 grey and its colour labels full-range RGB
  (label 0 = 0xe6), so every message printed 128 grey and the memory-card yes/no's selected choice
  vanished into its gold bar. Fixed 2026-10-17: the colour runs to 255/128 and sprite.frag saturates
  `texel * colour` before fog. Symptom of a regression: text flat grey whatever its label.
- **sprite.frag discards `col.a <= 0`; the GS does not.** A GS fragment with alpha 0 is still
  written unless the alpha test rejects it, and a FIX-weighted blend never reads it. FF3's title movie
  (title_obj.c) is a DispSprD at vertex alpha 0 (TITLE_OBJ.alpha is never faded in on that path) with
  ALPHA 0x68 FIX 0x80 (Cs + Cd): on PC every pixel was discarded and the title backdrop stayed static
  although the movie decoded and the video texture was served (2026-10-15, ldimage.c session; the user
  asked about depth: 2D sprites are ZTST ALWAYS + ZMSK, so depth is never involved). Fixed narrowly in
  DrawTexturedQuadImpl: a quad with vertex alpha 0 under GS_BLEND_ADD_ONE and no effective alpha test gets
  0x80. Still wrong: FIX below 0x80 falls back to GS_BLEND_ALPHA (no blend constant in SDL_GPU), which
  weights by that 0 vertex alpha, so the movie's fade-in (FIX 6 -> 0x80 over the first frames) is
  invisible and it pops in. Symptom to recognise: a draw that runs (texture found, registers set) but
  leaves no pixel -- try the vertex alpha first.
- **FF3's 2D code uses two GS origins; a PC quad must match its packet's.** XYOFFSET_1 is
  (0x6c0, 0x720) = (1728, 1824) (gs_reg.c). Code that adds **0x6c0 / 0x720** (message.c SetFontPat,
  BpCtl.c `BP_GS_X/Y`) takes top-left screen pixels: PC quad at (x, y). Code that adds **2048**
  (g2d_draw.c DispSprD / DispSqrD, `G2D_GS_ORG`) takes centre-relative coordinates: PC quad at
  (x + 320, y + 224). Symptom of mixing them: every message printed (+320, +224) off, at the bottom right
  and cut off (SetFontPat's PC quad added 320 / 224 to pixel coordinates; fixed 2026-10-14). When writing
  a PC bridge for a GS packet, derive the host position from the packet's own constant, not from another
  file's bridge.
- **Decode the primitive type from the GIF tag before writing its PC quad.** The renderer's quads are
  strips (TL, TR, BL, BR; indices 0-1-2, 2-1-3). BpCtl's quad hooks were commented TRISTRIP but their tags
  say PRIM 0x155 / 0x145 = **TRIFAN**, with corners round the perimeter (TL, BL, BR, TR); fed as a strip
  that is a bow-tie. PRIM is bits 47..57 of the tag (`(tag >> 47) & 0x7ff`, type = low 3 bits: 4 strip,
  5 fan, 6 sprite); a fan p0..p3 is strip {p1, p2, p0, p3}. A SPRITE gives two corners: expand to four.
- **SleepThread on the EE main thread used to return after 1 ms.** The main thread is not a HostThread,
  so it had no wakeup counter: FileLoadCancelAll / FileLoadCancelWait* / FileLoadReqEEWait would have
  returned before the loader finished. eekernel.cpp now gives thread id 1 its own counter (2026-10-13).
- **SuspendThread is a mutex, not a freeze.** Any thread that reads state the game guards with
  SuspendThread(loader) must hold `ReiPan_LoaderCriticalEnter/Leave` while it does (thFileLoad does).
- **A PSS movie froze for good after any hitch** (fixed 2026-10-15, story_movie.c session). Symptom: the picture
  stops while the audio plays on and the game keeps running (the user saw it after dragging the window, but
  it also happened by itself ~8-18 s into SCENE0010). Cause: the catch-up loop in src/sdk/libmpeg_video.cpp
  unreffed the picture in hand before decoding the next; when the next was not demuxed yet (the audio ring
  meters the demuxer), it was left with an empty 0x0 frame, dropped it without advancing next_due, stayed
  behind, and threw every later picture away the same way (playpss frameCount stuck, `starved` flipping).
  Fix: decode into a spare AVFrame and swap only on success. MioPan's copy has the same bug. How it was
  found: a once-a-second printf in playPssSetPacket of audioDec (state, count, totalBytesSent), the demux
  ring cursors, ReiPanMpegVideoPending / Starved and playpss_mp.frameCount, then one in the decoder.
- **Two host threads since 2026-10-15** (src/reipan/os/reipan_host.h). Windows runs a modal move/size
  loop inside the event pump while a window is dragged, which used to stop the whole game. Now the OS
  main thread only inits SDL (events, video, gamepad, audio), creates the window and the GPU device,
  claims the window, and pumps events into a queue; the game (system.c GameMain, the EE main thread)
  runs on its own 8 MB-stack thread, renders and acquires the swapchain there (D3D12 / Vulkan allow it;
  SDL's docs ask for the window's thread, see the header), and reads events with ReiPan_HostPollEvent.
  Rules: SDL window-state calls, OS dialogs and anything SDL marks "main thread only" go through
  ReiPan_HostRunOnMainThread (it waits; never call it from the main thread's own pump path while the game
  waits on the main thread); never call SDL_PollEvent / SDL_PumpEvents from game code (use
  ReiPan_HostPollEvent / ReiPan_HostPumpEvents); text in queued events is copied, other event pointers
  (drop, IME candidates, clipboard mime types) are cleared. The ImGui SDL3 backend still calls a few
  window-thread functions (cursor, mouse capture, IME) from the game thread; they fail quietly at worst.
  REIPAN_SINGLE_THREAD=1 restores the old single-thread run (debugging). Tested with WM_SYSCOMMAND SC_MOVE
  (the drag's modal loop) for 6 s: frames kept coming; single-threaded they stopped 7.2 s.
- **A stub with FF3 address 0 lands in lib/unassigned_stubs.c.** Symptom: `[stub] Name (FF3 0x00000000)` and the
  stub sits in unassigned_stubs.c under "address unknown (0 candidates)" instead of in its TU's file. Cause: the
  header declared a new name (EffLanternApplyLight) before Ghidra had it, so stubgen could not find the address.
  Fix: name it first (name_ctx.py), delete the address-0 stub, run `tus.py check` again -- it is regenerated in the
  owner's file with the address. (MapDoorAnimGetAddr and FurnPhotoFlgClear are older cases still there.)
- **Debugging a crash in a smoke run**: `exited early with code 3221225477` is an access violation. Run
  the exe under CLion's MinGW gdb for a backtrace (RelWithDebInfo has symbols):
  `"<CLion>/bin/gdb/win/x64/bin/gdb.exe" -batch -ex run -ex "bt 15"
  ./ReiPan.exe` from `cmake-build-relwithdebinfo/ReiPan` (the game runs on the thread named ReiPanGame;
  `thread apply all bt`, or REIPAN_SINGLE_THREAD=1 to keep it on the main thread).
  To reach the same phase as `tus.py check`, export first: PATH with the MinGW bin folder (DLLs),
  REIPAN_AUTOPAD=<settings.smoke_pad>, REIPAN_SHOT_DIR / REIPAN_SHOT_EVERY (optional). Stubs that
  return a pointer return null: the first real caller that dereferences one crashes (ItemGetCnt on the
  first in-game frame, 2026-10-15); the backtrace names the stub's TU, which then goes first.

### A MioPan convention on top of a ROM-faithful port (the flashlight lit behind Rei)

- Symptom: Rei's flashlight lit the room behind her, as if flipped; the user saw it from the first day it
  drew (2026-10-19).
- Cause: two negations of one vector. The VU1 spot kernel opens the cone along -vDirection, so FF3's
  installs hand it the anti-beam vector (MapDrawSetRoomLight 0x1183e0 negates the player's beam,
  FodSetSpotLights stores light - interest). MioPan drops those negations at its install sites and negates
  in its host kernels instead ("vDirection is the beam"); ReiPan reconstructed the installs from the ROM
  but took MioPan's kernels, so the host flipped the ROM's already-flipped vector.
- Fix (2026-10-20): the host kernels follow the microcode (gra3d.c gra3dCalcVu1VertexColor,
  reipan_renderer.cpp, reipan_vu1_lighting.hlsli, mikupan_common.hlsli). The rule: a host transcription of
  hardware behaviour takes the hardware's sense; when MioPan's comment says "the port keeps X engine-wide
  and reconciles it here", check whether ReiPan's callers keep X too, or follow the ROM.

### A host shader that replaces microcode drops the microcode's culling (Rei's shadow on the ceiling)

- Symptom: Rei's projected shadow showed twice, on the floor and again on the ceiling / beams above her
  (the user, 2026-10-20; rs101's overhead beam in the shadow debug view, frame 3300).
- Cause: the host receiver shader (shadow_receiver.frag, from MioPan) only projected the silhouette
  through the light camera and clipped to its depth range (near plane 1000 along the light direction from
  the target), so every receiver between Rei and the light sampled the map too. The ROM's projection
  microcode SgSuShadow_dma_main (VU1 0x2e6ac0, byte-identical to FF2's vu1/ff2_03.vsm) culls per receiver
  triangle: all three vertices with projector z < 0 (s_matIP's z row; 0 at the caster box's light-most
  corner), the camera's back face (screen cross), and a camera-facing side that faces away from the light
  (face normal against matLIP's w column = -light direction in local space).
- Fix (2026-10-20): _DrawShadow hands the host s_matIP's z row, s_vDirection and the camera position
  (ReiPan_RendererShadowSetProjector); the receiver fragment shader rejects pz < 0 and faces whose camera
  side looks away from the light. The rule: when a host shader stands in for VU1 microcode, read the
  microcode for what it rejects (ADC bits, CLEAR_MESH), not only what it computes.

### A one-shot copy into a new capture slot is lost

- Symptom: a picture made from a LocalCopyLtoL copy that runs once (the photo's frozen frame, page
  0x3480 at the shot) comes out black, while effects that copy every frame look right.
- Cause: ReiPan_RendererCaptureGsBlock declares the slot, but a new slot only got its texture in
  the next frame's UpdateViewExtend, and a capture into a slot without a texture was dropped. An
  effect that copies every frame loses one frame; a copy made once loses everything.
- Fix (2026-10-19, photo_make.c session): a slot declared mid-frame gets its texture at once, at the
  size the frame's other slots were given (g_capture_alloc_w / _h). If a one-shot capture still shows
  black, check that the source draws ran before the capture point (IngameDrawSub before the step).

## Build

- **`build.py --run N` alone runs without the scripted pad** and sits in the language select, so a trace in title code prints nothing and looks like dead code (cost a few runs, 2026-10-14). Use `tus.py check <TU> --run 45` (it passes `settings.smoke_pad`), or `build.py --run 45 --pad "<the smoke_pad string>"`. Temporary printf traces: prefix them and grep the run log; remove them before finish.

- **A smoke run can write the user's reipan.ini** (2026-10-27, title_room.c): runs use the real
  `%APPDATA%\Mikompilation\ReiPan\reipan.ini`, and the overlay saves half a second after any setting
  changes -- including ones the game makes itself (the title backdrop framing a room by itself sets its
  scene's camera). A test build then leaves its defaults in the user's file. Set
  `REIPAN_WINDOW_MODE=windowed` (any REIPAN_* renderer override) for the run: saving is off for that
  session.
- **A smoke run with no game data sits on the game-files screen** (2026-11-04,
  `reipan_rml_gamefiles.cpp`): GameMain ticks that screen before InitMainSystem until the player
  answers, so no game code runs, the log stops at `no game files found; asking the player for them`, and
  every shot is black (the shot reads the game's frame back, not the RmlUi layer over it; look at the
  window itself). Point `[paths] data_folder` at a folder or a disc image, or set `$REIPAN_DATA` to a
  folder. A folder only counts with ZERO3.FHD in it: ee_iopInit gives up without that table.
- **A BGM swapped in over one just started from a preload can wedge the next title** (2026-10-27,
  title_room.c): SndBgmPlay into slot 0 the frame init_Title_Mode's SndBgmStartPreloaded(0) ran left a
  stream that StreamAutoIsAllStop never saw stop, so after the album the title's init waited on
  SndBgmIsAllIdle() for good (screen black, title.phf loaded, no BGM preload). Cause in the stream layer
  not traced; the title backdrop avoids it by having init preload the chosen track itself.

- **MinGW must be on PATH** or `cc1plus` fails with no diagnostics — use `build.py`.
- **shadercross file lock** ("being used by another process") on parallel shader builds — rerun (build.py
  retries once).
- `ld: cannot open output file ReiPan.exe: Permission denied` — the game is still running.
- `-fno-strict-aliasing` is required (MioPan's release-only crash); it is in CMakeLists.txt.
- Relative includes that only resolve through a deep `-I` in MioPan break here (`sce_gs.h` had one).

- **layoutcheck compiles every header on its own.** A data header that a .c includes after defining
  the element types (message_data.h) fails standalone ("undeclared identifier"): give it its own
  includes, types and constants.
- **Seeing a draw path the smoke run does not reach yet**: the run sits at the language select without
  pad input. A temporary call in `one_LangSel_Main` (message.c's probe: MsgPrintPage / PrintNumber at
  fixed positions) shows it in the next shot; revert it and re-run `tus.py check` so the committed
  `last.png` is the real frame.

## callcheck

- **Helper functions of your own hide FF3's calls.** A `static inline` helper (or template) that wraps
  an FF3 callee (`PlyrGetCurNo`, `FUN_001ea2b0`, `CWaitVariable_short_Work`) makes callcheck report the
  callee MISSING on every caller, because it reads the source, not the inlined code. Write the inlined
  code out in each function as FF3 has it (fndr_parts.c's 0xff -> PlyrGetCurNo() lines); a helper that
  calls no FF3 function (BlinkSwitchWork) only shows as extra-only, which is fine.

- **Ghidra functions that run on into a hidden one** (a ctor copy, a hook after a tail call) report the hidden one's calls as MISSING on the visible function: confirm with `mipsdis.py` and explain them there (`// callcheck: CcmLoadingCtor (...)` on CcmLoading::Main).
- `// callcheck:` takes the names up to the first `(`: write `callcheck: A, B (why A; why B)`, not `A (why), B`.
- It also stops at the end of the line: a list that wraps needs a second `// callcheck:` line (effect_sub.c's CheckPointDepth), or the names on the next line stay MISSING.
- A constructor with an initializer list is parsed as a function named after the base class (the `Base(x)` before `{`): its own explanation still has to name the base ctor.
- **A trailing `// FF3 0x...` on a one-line definition belongs to the next function** (2026-10-16, mmanage.c).
  Symptom: one-liners like `void M::Draw() { obj->Draw(); }  // FF3 0x2106c0` checked against the address one
  row up (Draw against 0x2bd838, DrawAlpha against 0x2106c0, ...). callcheck takes the last annotation before a
  definition. Fix: put `// FF3 0x...` on its own line above each one-liner.

- **One comment for two functions** (2026-10-20, story_menu_photo.c). Symptom: `// FF3 0xA / 0xB` above
  a pair leaves 0xB "not reconstructed" (`reipan.reconstructed()` counts `FF3 0x` only), and writing
  `FF3 0xA / FF3 0xB` makes callcheck pin the *next* function to 0xB and report its calls as MISSING.
  Fix: a shared description is fine, but give every function its own `// FF3 0x...` line right above
  its definition; ctor/dtor base copies get "FF3 0x<base>: the base-object copy, the same body as" on
  the line before the complete one's `FF3 0x...`.
- **callcheck reports libvu0 calls "missing" in a correct port.** Symptom: CheckEneView listed
  sceGifPkAddGsData / FUN_00282ce0 / FUN_00282f60 as missing and sceVu0* as extra. Cause: FF3's
  libvu0 functions at 0x282ce0.. were unnamed or wrongly matched (sceGifPkAddGsData was
  sceVu0UnitMatrix). Fix: read the 10-instruction body (`ghidra.py asm`), name it with its Sony name
  (`name_ctx.py`, tag ff2-context), re-run the check.

- **Dispatch-table handlers inside "files"** (2026-10-14, enemy.c). Symptom: a pinned TU starts with short
  functions that all read 1..3 operand bytes from a work +0xa0 pointer and write +0xc2 / +0xa8; their FF2
  names do not fit, Ghidra shows no callers. Cause: they are opcode handlers reached only through a pointer
  table (no xrefs, the table has no reference to its own entries), and Ghidra merged handler pairs into one
  body. Fix: dump the .data pointer run around the table (`refs` the run's first word to find the
  dispatcher), map every entry to its opcode, and give the handlers to the interpreter's file; mkfunc each
  entry before naming.
- **A ROM table reader that stops at a null field misses rows** (2026-10-14, ev_command.c). Symptom:
  evtbl.py listed 48 event conditions; SELECT_CHOICE (MSG_CHOICE's reader) and 22 more were missing, three
  without a Ghidra function. Cause: row 48 of the condition table 0x3242e8 is EV_OR, an operator with handler
  0, and the loop ended there. Fix: end ROM tables when the row id stops matching its index (or the keyword
  is empty), not on a null field; a lone function whose `ghidra.py refs` shows a "data word" hit just past a
  table's known end is such a row.

- **`ghidra.py mkfunc` answers "Unable to lock due to active transaction"** (2026-10-15, several mkfunc
  calls in a row). Cause: the save after the create collides with the previous call's transaction; the
  function is usually created anyway. Fix: check with `python ghidra.py func <addr>` before retrying; the
  next successful mkfunc (or name_ctx.py) saves the program.

- **sinit.py cannot read a table built on the stack** (2026-10-15, mission_list.c). Symptom: a 0x1454-byte
  static initializer (MissionListStaticInit 0x1ab7a0) whose decompilation is hundreds of uStack_ stores,
  memset and GetPALMode calls, then a copy loop to bss. Cause: it builds the whole table on the stack and
  copies it with ld/sd; sinit.py skips sp-relative stores and calls. Fix: run it in mipsemu.Emu with a
  byte-level memory (subclass: stores and loads per byte, unset stack padding reads 0) and a call hook
  (GetPALMode -> 0 or 1, memset writes bytes); read the target range after each run and diff the two runs
  to find the PAL-dependent fields. Two runs of 1300 instructions take a second.

- **Variadic unpackers: floats are doubles, ints are slot words** (2026-10-15, effect.c). Symptom: an FF3
  va_list reader shows dptofp(*(undefined8 *)(ap + 8 * n)) for some slots and byte / word reads for
  others. Cause: GCC 3 on the EE spills every variadic argument to an 8-byte slot; floats are promoted to
  double (soft 64-bit, converted back with dptofp), ints and bytes read the slot's low word / byte. Fix:
  va_arg(ap, double) for the dptofp slots, va_arg(ap, int) (then the cast) for byte / word slots,
  va_arg(ap, void *) where the stored word is dereferenced or stored into a pointer field; host callers
  pass (double) for float slots and nullptr, not 0, for pointer slots. Ghidra's FF2 names on the readers
  can be off by a renumbered id: match the stores, not the name (FF3's SetEffects_DITHER was id 3, BLUR).
- **Batch file numbers collide** (2026-10-15, effect_obj.c). Symptom: a new `work/batches/<tu>_007.json`
  silently replaced an earlier session's batch of the same name (work/ is gitignored, so git shows
  nothing). Cause: the numbers are not contiguous (006 / 007 / 008 .. 011 existed, 005 missing), so
  guessing the next number from memory picks a taken one. Fix: `ls work/batches | grep <tu>` and take
  the highest number + 1. The applied names survive in Ghidra and `data/context_names.json`.
- **ELF data addresses are not in the emulated EE RAM** (2026-10-16, g3dDma.c). Symptom: code that
  hands an FF3 .data/.rodata address (a VU1 micro-program chain such as 0x2df690, a static table) to
  ReiPan_GetHostPointer gets a pointer into calloc'd zeros, and a walker over it (_AppendVUProgTag waits
  for an END tag) never stops. Cause: the port does not load the ELF image; FF3's statics are host
  statics in src/, and only loaded files, the packet buffer and the scratchpad live in emulated RAM.
  Conversely ReiPan_GetPs2Address(host static) is 0. Fix: rebuild the data as a host static and pass
  the host pointer, or pass nullptr where nothing on PC consumes it (the VU1 microcode).

- **Python edits of src/ and reference files fail on non-ASCII or CRLF text** (2026-10-16, memo.c).
  Symptom: `assert old in s` / `str.index` fails although the text is visibly there. Causes: Python's
  default `open()` encoding on Windows is cp1252 (the UTF-8 `// ────` rules decode to mojibake), and
  some files (open-items.md) have CRLF line ends, so multi-line anchors with plain newlines miss. Fix:
  `open(p, encoding='utf-8', newline='')` and match the file's own line end.

- **A stub with an out-parameter leaves the caller's vector as stack garbage** (2026-10-16, camVci.c).
  Symptom: after the opening event the player stood at x 3.4e-39, z -1.0e12 and the map camera showed
  black. Cause: EvPlyrPosSet calls MapAreaGetHeight(ground, pos, ...) and copies pos into ground only
  when it returns < 0; MhGetMapHeight's stub returns 0, so ground was never written and PlyrSetPos took
  stack garbage. Fix: write the stubbed callee (map_height.c); when a garbage position or matrix shows
  up, look first for a stubbed callee that should have filled an out-parameter.

### SingleLinkList cells sized in EE bytes truncate host records

- Symptom: a record byte-copied into a SINGLE_LINK_LIST comes back with its last fields garbage
  (a Type, an alpha phase), only on the host.
- Cause: `SingleLinkListInit(&list, 0x18)` copies the EE record size, but a record that holds a
  pointer (pSgdTop, pHandle) is larger on the 64-bit host (LIGHT_COME_IN_DATA 0x18 -> 0x20,
  WATER_FLOW_DATA 0x20 -> 0x28); SingleLinkListAddEnd memcpys ElemSize bytes.
- Fix: init with `sizeof(RECORD)` and the EE size in a comment (effect_obj.c's five model lists,
  2026-10-16), or pad the record to its EE size with static_asserts (EFF_FALL_CTRL / EFF_PARTICLE_CTRL).

### A per-frame LocalCopyLtoBAdrs into an EE stash costs a GPU read-back (2026-10-19)
- Symptom: none yet -- caught while writing EffectOthBufCopy, which stores GS 0x38e0 at EE 0x117b000 every
  in-game frame.
- Cause: on the host, LocalCopyScreenReadback skipped the read-back only for a frame buffer stored into a
  stash; any other registered page (0x38e0 after LocalCopyLtoL) went through ReiPan_RendererReadbackScreen,
  a full download of the screen mirror.
- Fix: g2d_draw.c now copies capture slot to capture slot when either stash (0x10ef000 / 0x117b000) is the
  EE side: LocalCopyLtoBAdrs(page -> stash) captures the page's slot under the stash address and
  LocalCopyBtoLAdrs(stash -> page) hands it back (LocalCopyBtoL no longer repeats it). Other EE addresses
  (the haze's 0x11c1000) still read back. The EE bytes of a stash are not filled on the host.

### A work-frame mark that never lapses drops a real picture (2026-10-19)
- Symptom: the new-game movie SCENE0010 played its sound over a black screen (the title backdrop movie,
  same picture path, still showed). Probes: the decoder published every frame and MovieDispMain called
  DispSprD with TEX0 TBP 0x2bc0 every frame, but the renderer's video texture was never sampled.
- Cause: effect_sub.c's host gate (EffHostTexIsWorkFrame, asked by g2d_draw.c's G2dHostSprite) drops a
  textured quad whose TBP was ever an EffSetDrawFrame target, and the list never forgot a page. GS pages are
  shared: 0x2bc0 is the room glow's work frame (room_glow_buf {0x2bc0, 0x3020}), the enemy aura's frame
  and movie.c's movie page. A new game runs Story_Normal in rs101 before the movie phase; once the room
  glow had run there, 0x2bc0 stayed a "work frame" and the movie sprite was dropped every frame.
  `git bisect` (worktree under out/, e623248 good) named 5efa7a7 as the first bad commit; how that commit
  first lets the glow mark 0x2bc0 before the movie was not traced -- the sticky mark is the cause.
- Fix: a mark lapses EFF_HOST_WORK_KEEP (2) renderer frames after its pass last wrote the page
  (ReiPan_RendererGetFrameIndex). General rule: host bookkeeping keyed on a GS address must expire or be
  cleared when the page is reused; never assume an address belongs to one owner for the whole run.
- Bisecting a picture: build each step in a separate `git worktree` (configure with the main build's
  `_deps/*-src` as FETCHCONTENT_SOURCE_DIR_* and -DREIPAN_SIBLING_EXTERN=../MioPan/extern), run
  `build.py --run 82 --pad <smoke_pad without 2000:START>` and judge the shots' mean brightness after
  frame 1740 with PIL; `git bisect run` does the rest (about 4 minutes a step).

### The work-frame gate also dropped EE-uploaded pictures at 0x2bc0 (2026-10-20)
- Symptom: the screen saver (effect_scrsaver.c) ran -- sheet loaded, the face's envelope stepping, the draw
  reached -- but no face appeared; the diff of a face frame against a hidden one was only grain.
- Cause: in rs101 the room glow writes work frame 0x2bc0 every frame, so EffHostTexIsWorkFrame (effect_sub.c,
  asked by g2d_draw.c's G2dHostSprite) kept 0x2bc0 marked, and every sprite naming TBP 0x2bc0 was dropped --
  also the PSMT8 saver face (PK4SendVram) and the PSMT4 film-noise sheet (LangTexSend 0).
- Fix: an indexed TEX0 (PSMT8 / PSMT4 / PSMT8H / PSMT4HL / PSMT4HH) is never a work frame: a GS-side pass
  renders colour formats only, so such a picture was uploaded from the EE. The renderer separates the
  0x2bc0 capture slot from real textures the same way (reipan_renderer.cpp IsCaptureSlotAddr: CT32 / CT24
  only). If a later picture at a work page goes missing and it is CT32 / CT24, the gate is the first suspect.
- Testing an idle-triggered feature: patch the threshold down in a scratch edit (both NTSC and PAL
  branches -- the EU build takes the PAL one), log with SDL_Log (the run log captures it; stderr is not),
  and revert before the real-threshold run.

### `tus.py next` picks an old partial TU although the queue is not empty (2026-10-16)
- Symptom: `next --claim` says "continuing a partial TU" (effect_obj.c) while tus.json's queue
  still lists four entries.
- Cause: the entries were `unassigned@` keys that later sessions defined under file names; `pick`
  skips such stale keys silently, so the queue is empty in effect.
- Fix: treat it as an empty queue (SKILL.md: re-assess). Read the last in-game run's `[stub] ...
  reached` lines (work/run_relwithdebinfo.log) for the milestone's path, then `tus.py claim` the TU
  that holds the blocking stub and `tus.py queue <file> --top` the next ones.

### A 3D effect draws nothing although its quads reach the renderer (2026-10-16)
- Symptom: the candle flames' host quads (ReiPan_Graph3dDrawEffectQuad) pass the guard band, reach
  DrawClipTrianglesImpl inside the frame, and the screenshot is pixel-identical to the run without them.
- Cause: their texture page (effdat, TBP 0x3daa / CBP 0x3e67) is all zeros in host GS memory: the
  resident effect textures come from VRAM_TEX_PK4, which ScreenEffectDataLoadReq / Setup
  (unassigned@00158ed0) load and send, and those were stubs. An additive (ALPHA 0x48) or alpha-tested
  quad with a zero texture adds nothing, so nothing shows, not even a white box.
- Fix: check the texture before the geometry. ReiPan_GsDownloadTexture(tex0, &hash) on the quad's TEX0
  returns the decoded RGBA; all zeros means nothing uploaded that page. Find the uploader (PK4SendVram /
  LoadTim2 callers) among the reached stubs.

### A world-space debug line draws nothing (2026-10-18)
- Symptom: ReiPan_RendererDrawWorldLine from game code (effect_sub.c's 3D line body) is called every frame
  with sane points and nothing shows.
- Cause: it projects through the renderer's own view-projection (ReiPan_RendererSet3DViewProjection),
  which the graph3d bridge never sets; g_3d_camera_valid stays false and every call returns early.
  The 3D effect path projects through the game camera instead (reipan_graph3d.cpp ProjectWorldToHostClip:
  matWorldClipObject).
- Fix: apply GRA3DCAMERA.matWorldClipObject to (pos, 1) yourself and use ReiPan_RendererDrawClipLine /
  DrawClipPoint (EffDrawLine3DSub). Also: a debug draw hooked into a function only some frames call
  (EffDispSprD2Direct runs during events only) shows only in those frames' shots.

### A brief's range starts one function early (2026-10-18)
- unassigned@00180a78's neighbour run ended with 0x180680, a `_GLOBAL__I_` in `.ctors` (architecture.md
  "TU ends"), so effect_sub.c starts at the next function, 0x1806a8. Before pinning a range, look the
  first and last addresses up in `.ctors` (`python ghidra.py refs <addr>` shows the data word).

### A crash far from its cause: a zero placeholder table (2026-10-19)
- Symptom: the game dies right after a door in `MotionModelBuilder::BuildTexture` (`GetSgd(0)` is null),
  called from `EnemyMain -> enemyIsReadyData -> CharModel::Build`; the door code is not on the stack.
- Cause: the enemy data tables were zero-filled placeholders in lib/unassigned_stubs.c ("not extracted
  yet"). The first enemy that loaded got an all-zero ENE_CMN row, so its model was file 0, a pack
  without the `mpk` member. Any placeholder table becomes a crash as soon as code that reads it runs.
- Fix: extract the table from the ELF (enedat.py, enemotdat.py, randghostdat.py are the pattern) and
  check the real size: here the route table had 25 entries, not the 16 declared. To find such a crash:
  `python gdbrun.py` (play with host input), then `gdbrun.py --cmd "frame N" "p *ene->cmn"` on the held
  game; grep lib/unassigned_stubs.c for zero tables the faulting code reads.

### A caller's buffer sized from what it copies, not what the callee writes (2026-10-20)
- Symptom: none while the callee is a stub; host stack corruption once it is written.
- Cause: AlbumCurInfoStamp passed int date[3] to CdClockReadInt 0x25ccd0, which writes six ints; the
  caller copies only three, so the prototype comment guessed three. The EE frame (0x20 bytes of locals
  before the saved registers) had room for all six.
- Fix: size a caller's out-buffer from the callee's stores (or the EE stack frame), never from what the
  caller reads back; when you write a callee, grep its callers' buffers.

### A frozen camera after an event: the event script waits on a stub (2026-10-19)
- Symptom: after an event the view stays on the event's shot while the player still walks (the user's
  "camera stuck in the second room"); nothing crashes, no phase is missing.
- Cause: the event program stalls on a command whose wait never ends, so it never reaches its
  `EV_CAM 0` / DELETE_EV_CAM_VCI and MapCamGetEventCameraFlg stays 1. Room 2's event 164 waited in
  GHOST_APPEAR on EneGetState == 2; the ghost never became ready because enemyIsReadyData ANDs in
  EneFlyModelInitWait, a stub returning 0 (fly_ctrl.c).
- Fix: trace the commands. A temporary printf in EventExeCmdCall (ev_macro.c: event id, command name,
  args, result, deduplicated) shows the command that returns 0 for ever; then follow its wait. A
  temporary probe in one_Story_Normal printing MapCamGetEventCameraFlg / MapCamGetCurType and the
  player position (every 15 frames) tells an event camera from a map camera. A stub on a readiness
  path (IsReady, InitWait, IsLoadEnd) returning 0 is a classic hang: return what FF3 returns for the
  common case (here type-2 enemies pass at once) or write the function.

### A phase with no handlers freezes the game silently (2026-10-19)
- Symptom: after an action (a photo) the screen goes black or stops, the log shows
  `[stub] phase33_one (FF3 0x...) reached` once.
- Cause: gphase.c's tables keep nullptr for unwritten phases; GPhaseCallMissing returns 0 from a
  missing one, so the phase never ends and nothing requests the way back.
- Fix: grep the run log for `phase.*_one` stubs whenever a run freezes after an action; write the phase.

### A warp's puzzle opens at a different frame each run (2026-10-21)
- Symptom: a scripted puzzle solve (`--warp ...,puzzle=T`) works once, then presses do nothing in the
  next run; one direction looks broken (RIGHT "never moved" the rousoku cursor in three runs).
- Cause: reipan_warp.cpp opens the puzzle after 6 s of *wall-clock* play, while `--pad` counts pad
  reads (frames). Load times move the puzzle's first frame by a few hundred reads, so the presses meant
  for the board land while the rules message is still up (its CROSS pages are eaten, the moves vanish).
- Fix: pad the script: several extra CROSS presses for the message (on a fixed place they only buzz),
  then the moves from about read 2400, and a long enough `--run` (170). A temporary printf probe in the
  cursor code showed every move once the timing was right. Confirm with a shot before blaming the code.

### Smoke run exits at once with code 541541187 (0x20474343) and no log
- Symptom: `build.py --run` prints "exited early with code 541541187", no log lines, no screenshots.
- Cause: 0x20474343 is GCC's uncaught C++ exception; spdlog's rotating log sink throws on rotate
  (errno 13) because another ReiPan.exe still holds the log file -- here a second session's
  `build.py --run 170` in the same checkout (2026-10-18). Found with CLion's gdb:
  `gdb -batch -ex "catch throw" -ex run -ex bt ReiPan.exe`.
- Fix: wait until no ReiPan.exe runs (tasklist), then run again. Never kill another session's
  process. A tree with changes you did not make means someone works in the checkout: finish with
  `tus.py finish --no-commit` and commit only your own files.

- **ROM loops that reuse their counter** (2026-10-19, random_ghost.c): RandGhostAreaRelease 0x24de48's
  inner `for (i = 0; i < 5; i++)` over a row's areas reuses the outer slot counter (asm: `li s1, 4` in the
  inner loop, `li s1, 5` in the branch delay slot after it), so the outer loop resumes at 6 while its
  pointer keeps stepping and can run past ene_wrk[15]. Ghidra shows it as a stray `iVar6 = 5`. Read the
  asm when a decompile assigns a constant to a loop counter; keep the jump, guard the pointer.
- **B2d item hooks: the decompile drops the stack arguments** (2026-10-21, kusi_draw.c): Ghidra shows a
  hook's B2dDrawSpriteDirect call with eight register words only, so a hook that rewrites a stack slot
  before passing it on looks like a plain pass-through. KusiDrawCursor stores 0x80808080 / 0x804646fa
  into the rgb slot (0x10(sp), args->stack[2]) and KusiDrawArrow / KusiDrawArrowQuad xor the flags
  (0x18(sp) = stack[3], or t1 for a quad) with 0x2000 to mirror the step-back arrows. Read the asm for
  every `sw ..., 0x0..0x18(sp)` before the jal 0x1066e0 / quad call in a hook.
- **A stub inside a TU marked done is never picked** (2026-10-21, sleep_movie.c): `tus.py next` skips
  TUs whose state is done / verify / blocked, also in the pinned queue, so `tus.py queue <done TU>` does
  nothing and a stub left in a done TU's range (IngameWrkSaveGet in ingame_wrk.c, FUN_002367f0 in
  player.c) stays stubbed while the queue reports empty. Find them with `grep -rn "stub of FF3" src`
  and the run log's `[stub] ... reached` lines; claim the TU by name to work on it.

- **Mouse in a menu: a hook that does nothing** (2026-11-05): `ReiPan_MenuMouseIn` answers 0 until something
  claimed the mouse this frame. The MenuMouse* target helpers claim for themselves; a hook of plain
  `ReiPan_MenuMouseButton` / `Click` / `Hover` calls has to call `ReiPan_MenuMouseBegin` first, or it works
  only on screens that happen to show capCtl hints (which claim in the poll).
- **A layout item with no size** (2026-11-05): group items report 0 x 0 and their box is what they hold
  (`ReiPan_MenuMouseItemBox` unions the children); rotated sprites report the box around their corners. An
  item drawn moved by its hook (Kusi's cursor, the album's marks, the film sort cursor) reports its own cell
  0: use `MenuMouseCsrOrigin`, not `MenuMouseCsrFrom`, whose box is the cursor's cell.
- **Item names against the art** (2026-11-05): caption.phf's icon names are the Japanese release's;
  this disc draws the cross at `R_bottun_maru`, the triangle at `_batu`, the circle at `_sankaku`. Window type
  8 swaps its `R YES` / `R NO` plates to light the answer, so anchor on the words (`yes_on` / `no_on`).
- **A plain message over a menu is not modal** (2026-11-05): the puzzles keep a guide line open while the
  board takes input, so "a message takes a click anywhere" also pressed OK on the board. Check
  `ReiPan_MenuMouseHasTargets` before a click-anywhere.
- **Testing the mouse by posted messages** (2026-11-05): a posted WM_MOUSEMOVE does not hold: the real
  cursor is elsewhere, so SDL sees WM_MOUSELEAVE and re-reads GetCursorPos. Exact positions take
  `build.py --mouse` (game coordinates); posted messages prove right click, keys and the wheel. A scripted
  run ignores the host's mouse and keys (`script_active`); use `--user-dir` so it leaves the player's saves alone.
