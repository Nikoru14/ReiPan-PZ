---
name: reipan-port
description: The workflow for reverse-engineering Fatal Frame 3 / Project Zero 3 (PS2, SLES_538.25) and porting it into ReiPan one translation unit per session, as automatically as possible — the TU queue (tus.py) and session protocol, Ghidra via GhidraMCP, porting names from the FF2 debug build, naming from context with provenance tags, the 120-phase game map, staging decompilation, reconstructing C++ into src/, stubbing the frontier, building/running, and keeping status, docs and this skill current. Use it for any work in the ReiPan repo or on FF3 code - "continue the port", "next TU", "tu main/main.c", "decomp X", "reconstruct", a FF3 address or function name, Ghidra naming, phases, stubs, build or run problems - even when the user does not name the skill.
effort: xhigh
---

# ReiPan: Fatal Frame 3 decomp + PC port

ReiPan reconstructs FF3 (EU retail `SLES_538.25`, no symbols) function by function into C++ that runs
on MioPan's platform layer. MioPan (`../MioPan`) is the same author's FF2 port; FF2's Feb 2004 debug
build (`SLES_523.84`) has full symbols and is the main source of names. This skill is the playbook and
the memory of the project: follow it, and keep it true (see "Keeping this skill alive").

**Work is cut into translation units (TUs), one per session.** A TU is one FF3 object file, rebuilt as
one source file under `src/` (key = its path, `main/main.c`). Each session takes one TU (or one slice
of a big one) from claim to commit; nothing carries over in the conversation, everything carries over
in the repo: `tools/reverse/data/tus.json` (state, handoff notes, history), this skill's references,
and git.

Read before working:
- `references/status.md` — generated: naming coverage, TU summary (next pick, active/partial/blocked,
  recent sessions), reconstruction progress, the frontier.
- `references/open-items.md` — cross-TU questions, decisions, done log.
- Then as needed: `architecture.md` (what FF3 is made of), `reconstruction.md` (how code is written),
  `hazards.md` (traps). FF2/PS2/renderer knowledge lives in MioPan's skill:
  `../MioPan/.claude/skills/miopan-reversing/SKILL.md` — read the relevant section, don't copy it here.

## Environment

- Ghidra 12.1.2 with the GhidraMCP plugin (HTTP `127.0.0.1:8089`), a project with FF3
  (`SLES_538.25.bak`) and FF2 (`SLES_523.84`) open. Scripts use HTTP through `tools/reverse/reipan.py`,
  which always puts `program` in the query string (see hazards). Offline (Ghidra down): status, export,
  tus list/check/finish, callcheck and build still work from cached `work/` data.
- Tools: `tools/reverse/` (run from that directory: `python <tool>.py`). Full table in its README.
- Build: `python build.py` (MinGW + ninja, configures on first use). Game ELF at repo root
  (`SLES_538.25`, gitignored); MioPan checkout beside this repo.

## One TU per session (the protocol)

Invocations: `/reipan-port tu <TU>` (a named TU), `/reipan-port next` (pick one). Any other request is regular work: use the tools below, still finish with a commit.

1. **Claim and read.** `python tus.py claim <TU>` (or `python tus.py next --claim`), then
   `python tus.py brief <TU>` and read `work/tu/<slug>/brief.md` — handoff notes from the last session
   first — and the staged bundle `work/stage/tu_<slug>/` (FF3 decompilation, call order, strings,
   FF2 counterpart, MioPan source per function). The brief's **slice** is this session's work (reached
   stubs, then frontier, then their same-TU callees; ~10 KB of FF3 code).
2. **Boundaries.** If the brief says membership is *inferred* or *undecided*, settle the TU's extent
   first: neighbours, FF2 file order, callers, shared statics. Pin it:
   `python tus.py define <file> <lo>-<hi> [...] --why "..."` (hi exclusive; re-exports). If the slice
   belongs to another file, define that file, `python tus.py claim` it (releases the first claim), and
   re-brief. A **host task** (the brief says "Host task": a src/ file with no FF3 functions, queued by the
   user with `tus.py queue <file> --top --task TEXT`, e.g. the graph3d bridge) skips steps 2-3: do what its
   text says, then check (with the screenshot) and finish like a TU. Inference is often wrong around FF3-only code (hazards.md "File inference"). A run that
   holds two small modules: define both, do both, `tus.py finish <other> --no-commit` first, then
   finish the claimed one (that commit carries both).
   **The file name is a guess too.** TU names come from FF2's link order, so a slot can hold code that
   has nothing to do with its name (`graphics/motion/anicode.c` is FF3 menu-window code; FF3's anime-code
   player sits in `album/prg/scr_play.c`). The brief's "Is it really ...?" section (`tuid.py`) weighs
   the FF2 matches, strings, RTTI classes, function names and caller/callee TUs; when they point
   elsewhere, name the file for what it is and say in `--why` which signals decided. For now this must
   not stall the work: decompiling the code matters more than the perfect name.
3. **Names.** Every slice function and direct callee you need gets a real name: `ctxcard.py <addrs>`
   for evidence, a batch JSON in `work/batches/`, `python name_ctx.py <batch>` (tag + one-line `why`;
   it saves the program and runs `refresh.py`, so later steps see the names). Inherited FF2 names are
   hints — check bodies; library callees get their Sony/libc names. Re-run `tus.py brief` if names changed.
4. **Header first.** `src/<dir>/<file>.h`: types, globals, prototypes of what other TUs call. Every
   struct/enum the slice touches is recreated as it is rediscovered — in its owning TU's header, at FF3
   offsets, with `static_assert` on size and used offsets so MinGW GCC and MSVC both prove it; no
   `ptr + 0x120` casts in finished code (reconstruction.md "Types and layouts": `long`, pointers,
   16-byte vectors and bitfields differ between the EE and the hosts). Outgoing calls get prototypes in *their* TU's header (create it minimal if missing) so stubgen
   can stub them; callees in `unassigned@` runs get a commented prototype block in the .c until their
   file is defined. Static helpers stay in the .c. **A class gets its methods as members**: a function
   whose first argument is the object (`this` in a0) is a member even when it is not in the vtable --
   `Class::Method`, declared in the class with `// FF3 0x...` (reconstruction.md "Methods, not free
   functions").
5. **Code.** Reconstruct the slice following `reconstruction.md` (FF3 semantics, `FF3 0x<addr>` on
   every function). **If FF2 has the module, port it**: start from MioPan's file of the same path, turn it
   into FF3's classes (`rtti.py`), drop FF2's assert/debug calls, verify each function against FF3
   (reconstruction.md "Port from FF2 first"). Class names come from `rtti.py`, file numbers from
   `CD_FILE_DAT` (`fhd.py`). Delete this TU's stubs that live in other files (the brief lists them).
   Slice done and context still light: take the next functions (`tus.py brief` again).
6. **Check.** `python tus.py check <TU> --run 15` — build, stub undefined references, rebuild, callcheck,
   layoutcheck (GCC + MSVC), smoke run (reached stubs steer the next pick; a scripted pad drives the
   boot to the logos and title, `--run 45` to get there, `--run 100` into a new game: Normal at read 1600,
   "Now Loading" about frame 1680, START at read 2000 skips the opening movie after ~6 s, title menu and difficulty select in the shots before; from read 3000 the pad walks Rei through rs101's door 106140 into room 2 (area 11, ~read 3840), where event 164 shows the ghost behind the lattice (~3990..4060) and the map camera follows her down the corridor; a photo at 4900 runs phase 33, so `--run 170` ends in rs101, `--run 200` in room 2, `--run 250` after the photo). Code that path never reaches (the real-world house, later chapters): `python build.py --run 75 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS --warp chapter=2,tp=0` starts a game in chapter 2 (Rei wakes in her bedroom) and teleports her beside the cat in the living room; `--warp` takes any chapter / area / world (build.py's docstring, open-items.md "Starting elsewhere"). Repeat until green; missing
   calls fixed or explained in a comment. **Look at `tools/reverse/work/shots/last.png`** (and the
   `shot_*.png` sequence) with the Read tool. **The picture is the test** (the user's rule): logs, stub
   hits and callcheck show the code ran, not that it is right. A TU on the drawing path is `done` only
   when the screenshot shows what the reference shows (`python tm2png.py --file <FILE>` -> the game's
   own textures in work/ref/); a wrong picture means `partial` with the difference in the notes. When
   the code is complete but nothing on screen can show it yet (its caller or the draw code is still a
   stub), finish with `verify`: not picked again, and the first session whose screenshot shows its
   output marks it `done` (status.md lists them). Also check the `verify` TUs whose output your screen
   now shows. `finish` commits the last shot to `progress/` (reconstruction.md "Verify and commit").
7. **Record.** Facts -> `architecture.md`; traps -> `hazards.md`; cross-TU questions/decisions ->
   `open-items.md`; better ways of working -> this file / `reconstruction.md` / tools README. If writes
   under `.claude/` are denied (autopilot), put the text in `--notes` after "For the references:",
   one item per target file, ready to paste; an interactive session copies it in.
8. **Finish.** `python tus.py finish <TU> --status done|verify|partial|blocked --summary "<what was done>"
   --notes "<handoff: what is left, decisions, traps, where to start>" --coauthor "<the Co-Authored-By
   line from the session instructions>"`. It builds (refuses red or a failed layoutcheck), records
   the session, refreshes status, and commits everything on the current branch (`git add -A`: check
   `git status` for files that are not yours first). Then report in 3-5 lines and **stop** — the next
   TU belongs to a new session.

`partial` is normal for big TUs (the next session continues it first, after the user's pinned queue) — use it only when code is
left to write or a picture is wrong; complete code that cannot be seen yet is `verify`. `blocked` means
a decision is needed — say which in the notes and in `open-items.md`. Never leave the tree dirty or the
build red.

## Starting sessions

The user starts each session: `/reipan-port next` in a new conversation, the app's scheduled task
`reipan-next-tu` (every 20 minutes: preflight, one TU), or
`tools/reverse/autopilot.py` in a terminal. A session never starts other sessions, agents or scheduled
tasks. Within a session, run the protocol without asking at each step; pause only for decisions that are
genuinely the user's (restructuring the tree, non-trivial platform-layer changes, destructive git) — and
when no answer can come, decide, record the doubt in `open-items.md`, and finish. Commit locally; push
only when the user of this session asks (a push publishes the commit). Current priority (user's call, 2026-10-15): reach
the game, then 3D -- leave the loading screen, movies, the event core, a room in 3D, Rei on screen
(open-items.md "Next up"; the pinned queue in tus.json encodes it). An empty queue means re-assess,
not "follow the frontier": the frontier score counts call sites from written code that may never run
(it led seven sessions into in-game effects nobody could see). Refill the queue from "Next up", in the
order a smoke run reaches the stubs.
Porting runs at effort `xhigh` (this skill's frontmatter; the user's rule: automated runs at xhigh
or above, since lower effort reconstructs worse). Keep that line when editing the frontmatter.

## Tools by step

| step | tool |
|---|---|
| queue, claim, brief, boundaries, check, finish | `tus.py list / next / claim / brief / define / check / finish` |
| regenerate derived files | `refresh.py` (fetch + features when Ghidra is up, export, status) |
| map code | `modview.py range|tree`, `phases.py`, `phasegraph.py`, `ff3_functions.txt` |
| name | `ctxcard.py`, `name_ctx.py`, `name_phases.py`; bulk: `matcher.py` -> `reconcile.py --plan` |
| read code | `stage.py` (bundles), `dec.py` (one-off decompile, `--ff2`), `ghidra.py` (asm, refs, mkfunc), `mipsemu.py` (run a leaf function; check switch-table rewrites over every input), `sinit.py` (tables a static initializer fills) |
| classes | `rtti.py` (`--class NAME`, `--vtable ADDR`): FF3's 113 RTTI classes, bases, vtables, ctors/dtors |
| TU identity | `tuid.py <TU>` (also in the brief): FF2 matches, strings, classes, names, neighbours vs. the file name |
| file numbers | `fhd.py --find NAME_OR_ID`; `fhd.py --header` regenerates `src/system/eeiop/cd_file_dat.h` |
| event handlers | `evtbl.py [cmd\|cond] [FILTER]`: the command / condition tables, keyword + argument format per handler (name handlers after their keyword) |
| event scripts | `evdump.py show / tree / outline / find / chapters`: the event file decoded (conditions, programs, the event tree, the chapters); what a script does with a handler |
| start elsewhere | `build.py --warp SPEC` / `--snapshot NAME`; in game the ImGui Debug -> "Start point / snapshots" window (`reipan/debug/reipan_warp.cpp`): chapter warps, teleports, snapshots, event open / start, the game clear (`clear=E` plays it; `cleared=D` treats the game as cleared so New Game opens the start menu), the cat's work, the map-sky effect (`sky=1` at Rei, `sky=2` on the camera: the tattoo event's sky dome) |
| title backdrop | the port's live room behind the title (`outgame/title_room.c`, after MioPan's; Title_Mode's init / pre / end wrapped in gphase.c, three hooks in title_menu.c): IngameInit + MapPlayStartReset without the player, the map half of IngameDrawSub, a two-point camera path, a chosen BGM / KAN stream in BGM slot 0; ImGui Settings -> "Title screen..." (`reipan_ui/reipan_ui_title.cpp`) edits `[title]` / `[title.sceneN]` in reipan.ini |
| event browser | in game the ImGui Debug -> "Event browser" window (`reipan/debug/reipan_events.cpp`, after MioPan's): every event by Hour / room, its conditions judged live (yes / no) and its programs with arguments, Jump (one load straight to where the event starts, everything it waits on settled) and Go (teleport there), forced start / end / re-arm; `build.py --event jump:ID` (or `go:ID`, `chapter:N`, comma-separated) for smoke runs, the `[events] evN now:` log line says which condition holds |
| game data | `rawfile.py FILE` (a file as loaded: CMP-decoded, PHF members listed), `tm2png.py --file FILE` (reference PNGs), `enemotdat.py` (ELF pointer tables -> host C++, the pattern for nested .data tables), `chardat.py` (the character model tables), `randghostdat.py` (the random ghost sets / zones), `enedat.py` (the enemy rows and route lists) |
| frontier | `build.py`, `stubgen.py --from-build`, `callcheck.py` (all wrapped by `tus.py check`) |
| type layouts | `layoutcheck.py` (GCC + MSVC asserts, missing asserts, host-type lint; in `check`/`finish`) |
| crashes | `gdbrun.py`: the game under gdb (play, or `--pad`); a fault holds it, `--cmd` inspects it, `--break` stops a hang (hazards.md "A crash far from its cause") |

Naming exists to make reconstruction possible; it is not the goal. Name what the slice needs (its
functions and their direct callees), then write code; callees you do not need yet become stubs.

## Naming conventions (Ghidra)

- Provenance tags on every name: `ff2-match` / `ff2-match-low` (matcher), `ff2-context` (FF2 counterpart by
  reading code), `ff3-inferred` (new name). `data/context_names.json` is the ledger with evidence;
  `data/overrides.json` lists wrong FF2 matches that were replaced (reconcile never restores them).
- Style: FF2's (PascalCase with module prefix: `MapPutSetObj`, `SndSePlay`, `B2dCtlFindItem`), phase
  functions `init_/one_/end_/pre_/after_<Phase>`, library functions their real ABI names.
- A name states what the code does, not a guess about game meaning; put uncertainty in `why`.
- The matcher is for bulk re-runs after the naming state changes a lot; it is greedy and sensitive
  (hazards.md), so always look at the plan first.

## Keeping this skill alive

This file and its references are the project's memory across sessions. Updating them is part of
finishing any TU, not optional cleanup:

- **Generated facts** (`status.md`, `ff3_functions.txt`): never hand-edit; `refresh.py` (run by
  `tus.py finish`) regenerates them.
- **Per-TU state and handoff** -> `tus.json` via `tus.py finish --notes` (and `define --why`). Write notes
  for a reader with no context: what is left, what was decided, where to start.
- **Established facts** about FF3 (entry points, structs, globals, a phase's role, address ranges) ->
  `architecture.md`, with the FF3 address. Replace what it supersedes; don't append contradictions.
- **Unconfirmed guesses and cross-TU questions** -> `open-items.md`, and resolve them there.
- **Anything that burned time** -> `hazards.md` as symptom / cause / fix.
- **A better way of working** -> this file or `reconstruction.md`, and `tools/reverse/README.md` for
  tools. If an instruction here turns out wrong, fix it in the same commit that discovered it.
- **New automation**: when the same multi-step manual work happens twice, write a script in
  `tools/reverse/` (use `reipan.py` for paths and Ghidra access), document it in the README and here.
  Each TU should take less hand work than the last.
- Keep this file under ~250 lines; move detail into references and point to it.

## References

| file | what | when |
|---|---|---|
| `references/status.md` | generated progress, TU summary, frontier | start of every session |
| `tools/reverse/data/tus.json` | TU state, ranges, handoff notes, history, queue | via `tus.py` |
| `references/open-items.md` | cross-TU questions, decisions, done log | choosing work; after finishing |
| `references/architecture.md` | FF3 program flow, phase map, idioms, systems, globals, address map | understanding a TU |
| `references/reconstruction.md` | file placement, annotations, semantics, stubs, verification | writing code |
| `references/hazards.md` | Ghidra/matcher/inference/shell/build traps | when something behaves oddly |
| `tools/reverse/README.md` | every tool, pipeline, provenance tags | running tools |
