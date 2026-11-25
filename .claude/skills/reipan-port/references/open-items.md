# Open items — cross-TU questions, decisions, done log

Edit this as work happens: move items to Done with the commit, add questions when something is unclear,
delete what is obsolete. Per-TU state and handoff notes live in `tools/reverse/data/tus.json`
(`tus.py`); `status.md` (generated) has the numbers and the next pick; this file has the judgement.

## Next up (in order; the pinned queue in tus.json encodes it, `tus.py next` follows it)

**First, two bug fixes (the user's call, 2026-10-19), pinned as host tasks at the top of the queue:**
~~`fix/new_game_movie.c`~~ (done 2026-10-19: a sticky work-frame mark on GS 0x2bc0 dropped the movie
sprite; see Done and hazards.md "A work-frame mark that never lapses"), then ~~`fix/second_room_camera.c`~~
(done 2026-10-19: two causes, both fixed -- room 2's event 164 waited for ever in GHOST_APPEAR because
EneFlyModelInitWait was a stub returning 0, so it never released its camera; and a photo froze the game in
phase 33, whose handlers were nullptr. See Done and "The first door taken in game"). The milestone queue
below resumes after them. **settings.smoke_pad now walks Rei through rs101's door into room 2** (event 164
at ~read 3990, a photo at 4900): `--run 200` ends in room 2, `--run 250` after the photo.

**Re-assessed 2026-10-15 (user's call): reach the game, then 3D.** The boot, logos, title, title menu,
difficulty select and Load screen draw on PC; the 2D plan of 2026-10-13/14 is finished except the
movie picture (its text is in git history before this commit). With the pinned queue empty,
`tus.py next` then followed the frontier score into in-game code nothing can reach: 62 in-game TUs
are `verify` (about 400 KB of reconstructed FF3 code nobody has seen run) and effect_obj.c took seven
sessions. The user's own New Game run ended on "Now Loading..." forever. Measured this session (two
temporary patches, reverted):
- `Story_NowLoading` (phase 30, `IngameNowLoadingMain` state 2) waits on `Wrk38e6d8IsReady`, a stub
  returning 0. Three of its other conditions only pass because model creation is still stubbed
  (`AllPlyrIsReady` counts no models; CharPlayerModel's ctor 0x145c50 is a stub), so the model classes
  must land before or with whatever makes the player model real, or the hang comes back.
- With that returning 1, phase 32 Story_Normal starts and its first frame crashes (0xC0000005):
  `PhotoModeWrkFinderDraw` (finder_photo.c) reads `*ItemGetCnt(8)` and the stub returns null.
- With both patched, Story_Normal runs (the in-game BGM KAN_21_YUKI_STR starts) on a black screen:
  EventMain, CameraMain, MapDrawAll and the effect / model code are stubs.

The queue follows the path a new game takes, milestone by milestone. Each milestone names the picture
that proves it; a TU that cannot show it yet finishes `verify` and the queue moves on. In-game runs:
`tus.py check <TU> --run 100` (settings.smoke_pad now also presses CROSS at read 1600: Normal on the
difficulty select, "Now Loading" around frame 1680, Story_Normal about 70 s after launch; the title
menu and difficulty select are in the shots before that; since 2026-10-15 START at read 2000 skips the
154 s opening movie SCENE0010 after ~6 s so the event after it is reached inside the run -- remove
"2000:START" from settings.smoke_pad to watch the movie and its captions).

0. ~~**The graph3d host bridge first (the user's call, 2026-10-16: "put as top priority the bridge or what is
   needed to see the 3D")**~~ (done 2026-10-16 as verify: MioPan's miopan_graph3d.cpp ported into
   `src/reipan/rendering/reipan_graph3d.cpp` with the camera / fog / TRI2 hooks and the VU1 light snapshot
   in gra3d.c; with a temporary debug camera the room rs101 draws textured and lit,
   progress/20261016-1420_reipan_graph3d_debugcam_rs101.png). **Next blocker: the game camera is
   degenerate.** At --run 100 the 7 preset models of rs101 reach _gra3dDrawSGD every frame, but s_Camera
   sits at the room origin (-132, 0, 490) with zero direction and up rows (matCoord[1], [2]), so
   matWorldClipObject is zero and CheckBoundingBox culls every block: the screen stays black. The map
   camera latches no record because the player stands at (0, 0, 0) on floor 0 (RegDat.c notes) and the
   opening event's camera (EventCameraInitCtrlReq 0x19cd28 / camVci::SetEvCam 0x139f20, event_camera.c)
   is a stub: take the event camera or the player init / position next, whichever the opening event
   needs first. The first shot that shows rs101 marks reipan_graph3d.cpp done.
   **The user's call (2026-10-16): the event camera next, as top priority.** ~~`ingame/camera/event_camera.c`~~
   (done 2026-10-16 as verify: all 42 functions, MioPan port; EventCameraReq / EventCameraVCIReqAddr / EventCameraMain
   -> gra3dApplyCamera are real now, but the --run 100 opening event only reaches camVci::SetEvCam 0x139f20 (stub)
   after SCENE0010, so the screen stays black and nothing calls the event camera yet).
   ~~`unassigned@001374d8` (camVci)~~ (done 2026-10-16 as its own file `ingame/camera/camVci.c` 0x139e10..0x13a010:
   SetEvCam resolves the VCI pack member and starts EventCameraVCIReqAddr. **The room shows in 3D**: at --run 160
   the opening event's camera paths fly through rs101 / rs106 textured and lit from frame ~1800 to ~2820
   (progress/*_ingame_camera_camVci.c.png); event_camera.c and reipan_graph3d.cpp marked done. The game runs
   at about 25 fps while the room draws (60 frames per 2.4 s; the movie before it ran at ~46) -- look at the
   renderer's cost later.)
   **TOP PRIORITY NOW (the user's call, 2026-10-16): the player-controlled camera.** When the event ends
   (DELETE_EV_CAM_VCI -> EventCameraCut) and the map camera takes over, the screen goes black again. Measured
   with a temporary probe in MapCamNormalCameraCtrl (reverted): after the event the player stands at a garbage
   position (x bits 0x0024dd97, y 0, z bits 0xd36bae35 = -1.0e12), floor 0, flags 0xa000, mode 0, so no
   RegDat camera record latches. Cause: EvPlyrPosSet (PLYR_POS_SET, ev_command.c) gets the ground point from
   MapAreaGetHeight -> MhGetMapHeight 0x1fda98, a stub returning 0 (not < 0), so its out vector is never
   written and PlyrSetPos takes stack garbage; the floor comes from FUN_0011f578 (11 + the floor index of
   MhCtlGetMapHeight's hit; the RegDat files sit on floors 11 / 13 / 15), a stub returning 0. Queue, pinned:
   ~~**`ingame/map/map_height.c`**~~ (done 2026-10-16: MhGetMapHeight and all 12 functions; the user, playing the build,
   reports that once Rei is under control the screen is black but first-person mode shows her correct spot, so
   PLYR_POS_SET now places her and the map camera is what is wrong -- the smoke run at --run 160 shows the same black
   after frame ~2880), then
   ~~**`unassigned@0011ec38`**~~ (done 2026-10-16 as one FF3-only file `ingame/map/MapRcb.c` 0x11e820..0x11f668 +
   0x2ac870: the room's rcb object file and its queries, MhCtl.c's two wrong FF2 matches included; with it
   `ingame/map/hit_check_base.c` 0x1e7b08..0x1e8738, whose HcBaseIsInTriXZ was a stub returning 0 under RegDat's
   camera-record test, MhIsInTriXZ and every rcb query). **The map camera works**: at --run 160 the opening event
   ends on "Where am I...?" (frame ~2820) and from frame 2880 to the end of the run (3900) the map camera holds a
   fixed view of the room (progress/*_ingame_map_MapRcb.c.png); Rei's spot is empty because the player model is
   still stubbed. map_camera.c, RegDat.c and map_rectangle.c marked done by that picture. Not seen yet: the camera
   following Rei between rectangles (the smoke pad does not walk; add pad moves to settings.smoke_pad to test).
   ~~The rest of unassigned@001374d8~~ (done 2026-10-16 as two FF3-only files, both verify: `ingame/camera/cam_rail.c`
   0x137cb8..0x139e10 -- the rail camera of cam_rail.ecb (rectangle types >= 100), the mode-10 stance camera and the
   XZ helpers -- and `ingame/camera/dmg_cam_shot.c` 0x1374d8..0x137cb8, the damage camera shots. The --run 160 picture
   is unchanged (the map camera's fixed view of rs101): the scripted pad never walks Rei into a rail rectangle, no
   enemy attacks, mode 10 is not entered. Mark cam_rail.c done when a run shows a rail camera follow Rei (area 1 has
   rails 100 / 101 / 109; add held stick moves to settings.smoke_pad), dmg_cam_shot.c when a ghost's hit cuts to the
   shots. CamRailFrame reads WorldToScreenPos 0x180710, still a stub (unassigned@0017f2b0): the call site seeds the
   screen centre, so until it is written the rail camera never re-aims for an off-centre look point.)
   ~~Next on the queue: M5 (Rei on screen)~~ **Rei is on screen (2026-10-16, `ingame/char_model_sub.c`)**: the pinned
   queue had run dry (its four unassigned@ entries were all defined and verify), so `tus.py next` fell back to
   effect_obj.c; re-assessed from the --run 100 log instead: the player's draw registration CharDrawSetPlayer 0x148c00
   (PlyrMdlIsReady) was a reached stub, so the player never entered MapPut's draw list. With the character draw table
   written (0x1479b0..0x149c98 defined as char_model_sub.c, the old alg_manage.c guess), --run 160 shows Rei standing in
   rs101 under the map camera from frame ~2880 to the end (progress/*_ingame_char_model_sub.c.png; her textures match
   CH000_00_PK4's reference). Marked done by that picture: char_model.c, game_model.c, model_builder.c, mmanage.c,
   model_object.c. Still verify: game_anime_model.c / motion_model_manager.c (two shots 1000 frames apart look
   identical: no visible idle motion yet -- check whether her bone motion plays), plyr_sub.c / player.c / unit_ctl.c
   (need walking: add held stick moves to settings.smoke_pad), gra3dShadow.c (no shadow seen under her).
   **Queue refilled (2026-10-16):** ~~`ingame/char_model_sub.c`~~ (done 2026-10-16: the look, CharWatchControl::Update
   0x148f30 / CalcAngle 0x1498e8 / TurnJoint 0x1495a8, CharCatWatchControl::Update 0x149ad0 and CharClampf; at --run 160
   Rei stands intact in rs101 from frame ~2880 and her head eases slightly between shots. It also fixed a heap overrun
   that crashed the dream-world model change at frame ~1700: char_model.c allocated every helper object with its FF3
   size, operator_new(0x28) for the 0x30-byte host CharWatchControl, hazards.md "FF3 sizes in operator_new"), then `graphics/effect/effect_obj.c` (2026-10-16:
   SubCandleFlameDraw / Step and the blue flare EffBlueFireFreaDraw written, plus the host path for 3D effect quads,
   `ReiPan_Graph3dDrawEffectQuad` in reipan_graph3d.cpp (MioPan's RendererPacket3DUV); measured at --run 160: dozens of
   rs101 candle flames (flow 4, fw 0.7 / 0.5, around (-50, -11, 600)) pass the guard band and reach the renderer every
   frame, on screen near the wax candles left and right of Rei, **but the picture is unchanged**: the effect texture
   page (effdat TEX0 TBP 0x3daa / CBP 0x3e67, PSMT4) is all zeros in host GS memory, so the additive quads add
   nothing. That page is VRAM_TEX_PK4 (file 0xacf), loaded by ScreenEffectDataLoadReq 0x158ed0 and sent by
   ScreenEffectDataSetup 0x158f38 (PK4SendVram), both reached stubs called from outgame/boot.c), so
   ~~`unassigned@00158ed0`~~ (done 2026-10-16 as one FF3-only file `graphics/effect/effect_data.c` 0x158ed0..0x159188:
   the VRAM_TEX_PK4 page, the saved effect state block 0x398960 and the butterfly model holder). **The candle flames
   show**: at --run 160 every wax candle left and right of Rei in rs101 carries a pale flame tip with a soft halo
   (progress/*_graphics_effect_effect_data.c.png), the shapes of VRAM_TEX_PK4's flame frames (work/ref/VRAM_TEX_PK4);
   the candle part of effect_obj.c is seen. Open: the file name is invented (could be the head of effect_butterfly.c
   for the model trio -- sbss order 0x372f14 effect.c / 0x372f1c here / 0x372f28 the 0x15b500 run fits either; kept
   as one file). **Next: make Rei walk** (held stick moves in settings.smoke_pad) and look at what the walking run
   reaches (cam_rail.c, plyr_sub.c / player.c / unit_ctl.c are verify waiting on it).
   (2026-10-16: with the queue still empty, `tus.py next` took `graphics/effect/effect_oth.c` from the frontier, partial:
   the opening event's monochrome and the brightness filter now show in rs101 (progress/*_graphics_effect_effect_oth.c.png). Refill
   the queue from a walking run before more frontier picks.)
   **Rei walks (2026-10-16, `album/prg/scr_play.c`).** The scheduled run claimed effect_oth.c from the frontier, then
   re-assessed with a walking pad instead (sdk/libpad.cpp's REIPAN_AUTOPAD gained `<read>:<BUTTON>*<reads>` holds and
   LSUP / LSDOWN / LSLEFT / LSRIGHT stick pushes; settings.smoke_pad now ends `3000:LSUP*240`). Rei turned to the stick
   but never moved: a probe in PlyrMotMoveGet showed anime 6 (walk) and flags2 2 requested while the bone motion sat on
   motion 0, frame 61 -- the anime-code player (ScrPlayStart / ScrPlayStep / GetLength) was a stub, so no motion ever
   started and the root move (CharModel::GetRootMove) stayed 0. With it written, --run 170 shows Rei walking forward and
   the map camera cutting to the next rectangle as she reaches the gate (frames 3060 / 3180 / 3240,
   progress/*_album_prg_scr_play.c.png). Marked done by that picture: game_anime_model.c, motion_model_manager.c,
   motion.c, plyr_sub.c, player.c (map_camera.c's rectangle following seen too). ~~**Next on the walking path: `unassigned@001fe5f8`**~~ (done 2026-10-16 as two FF3 files: `ingame/map/map_hit_check.c` 0x1fe3d8..0x1fea48, done -- MrecWallPushOut, MapHitPushOutQuad, MapHitLineCheck, MapHitTourouPushOut and FF2's two collision helpers; at --run 170 Rei slides along the left wall (about 100 push-outs of 0.2..0.4 units, measured with a reverted probe) and stops by the gate post at frame ~3240 where she used to walk on into the gate rectangle; and `ingame/story_menu_map.c` 0x1fea48..0x1ff6e8, partial: IsMapEnable and the object-free helpers written, the map menu phase waits on the map screen object's class, see its tus.json notes). **Next (queued first): `unassigned@001c8260`** -- FootSePlayFloor 0x1c8278 (footsteps, reached on the first step; parked in unassigned_stubs.c) with FootSePlayId 0x1c8260. Not seen yet:
   cam_rail.c (the pad does not walk into a rail rectangle), gra3dShadow.c (no shadow under her). effect_oth.c stays
   partial (its handoff is unchanged; this session also split the map-particle callbacks 0x17f140 / 0x17f188 / 0x17f4a0
   / 0x17f4b8 / 0x17f638 / 0x17f8a0 into functions: tables 0x2f9a40 (type init) / 0x2f9a50 / 0x2f9a60 (particle reset /
   init) / 0x2f9a70 rows per type 0 / 1 / 2, so the map-particle module runs to 0x17f8b8, past effect_oth.c's 0x17f2b0
   bound -- extend the range when that slice is written).
   (2026-10-18: effect_oth.c's deform kernels written -- SubDeform types 0..8 = FF2 SetDeform0 / 3 / 4 / 5 / 6 plus
   FF3's type-8 fan jitter 0x17a1c8 and MakeScrDeformPacket 0x2b5998, each with a host triangle-list path over the
   LocalCopyLtoL capture at 0x2bc0. Not seen: the only CallDeform2 callers are PlyrDamageCtrl 0x2334bc / 0x2339fc, so
   the warp shows when a ghost hits Rei; the first run with a hit should check the picture warps rather than blanks.
   Doubt: the strip kernels draw with CLAMP_1 0 (REPEAT) as the ROM does; the host bridge always clamps.)
   (2026-10-18, second run: effect_oth.c's dither and menu backdrop written -- SubDither3 / SubDither4 (types >= 8,
   FF3-only: cy starts at -9908638 and the (u1, u2) / (v1, v2) pairs are packed as hi << 16 | lo words, kept),
   DitherPatternUpdate / MakeRDither3 (PSMT8H sheet at 0x2200, clut 0x3fbc, host upload), SubDither4Disp, the flash
   wrappers 0x17c2a8 / 0x17c2d8, the unreferenced SetDeformScrRipple 0x17c308 and the menu_file.c backdrop EffMenuBg*
   0x17d978..0x17db58 + EffFogTileDraw 0x2b67d0 (the 0x17d978 group was not a camera shake). Unseen: every dither
   pass and SubFadeFrame / SetOverRap draw through **EffDispSprD2Direct 0x182930, a stub reached every in-game frame**
   (unassigned@00180a78), so that run is queued first; the dither also needs WorldToScreenPos 0x180710
   (unassigned@0017f8b8). Doubts: EffFogTileDraw's host quad takes one alpha per cell (the GS flat strip shades its
   two triangles with vertices 2 and 3); the dither's REPEAT needs ReiPan_RendererDrawTexturedQuadClamp in
   EffDispSprD2Direct's host path. Left in effect_oth.c: the sound-bank list rest (dead) and the particle objects
   0x185908..0x186888, called from unassigned@00186888 / ene code.)
   (2026-10-18, third run: ~~`unassigned@00180a78`~~ split into **`graphics/effect/effect_sub.c`** 0x1806a8..0x183d58 +
   0x2b8220..0x2b8ce0 (all 44 functions, verify) and **`graphics/effect/effect_torch.c`** 0x183d58..0x185908 (defined; all 33
   written 2026-10-20 as verify, see "Open questions from effect_torch.c"). EffDispSprD2Direct
   is real (DispSprD2 sent as 64-pixel column strips; the host draws the quad once with the sprite's CLAMP, so the dither's
   REPEAT is honoured). Measured with a reverted probe: during the opening event it is called once a frame for a
   full-screen copy of the display page (TEX0 TBP 0 / 0x1180, alpha 0x80, ALPHA 0x44) -- a frame copy, the identity on the
   host -- so the --run 170 picture is unchanged (no dither / vignette / room glow active in rs101 yet). WorldToScreenPos,
   Get2PLength (XZ only in FF3), the light cone test, CheckPointDepth (ENE_WRK +0x400 is now a PP_JUDGE `blink`; on the PC
   the Z store reads zeros, so "on screen" = "visible", MioPan's degradation) and the 3D sprites are written. **The user
   asked (2026-10-18) to see the unused "light shaft" code**: it is not a shaft effect but a spotlight falloff pair
   (EffSpotLightSet / EffSpotLightRate) and debug wireframe helpers (EffDrawLine3D / Z, EffDrawBox3D, EffDrawArrow3D,
   EffDrawCircleXZ) plus a one-point Z probe, none called anywhere in the ELF (ghidra.py refs, ctors included). Their host
   path is ReiPan_RendererDrawClipLine; a reverted demo hook drew a box, ground circle, facing arrow and the 40-degree light
   cone around Rei during the opening event (progress/20261018-1129_graphics_effect_effect_sub.c_debugprims_demo.png).
   **Decided (the user, 2026-10-18):** an ImGui toggle, Debug > Player light cone (reipan_ui.cpp; flag in
   src/reipan/reipan_debug.h), drives ReiPan_EffDebugOverlay at the end of EffectDrawMain: the cone EffPlyrLightConeCheck
   tests (blue), its 1200-unit reach (yellow), a box round Rei (green) and her facing (red), drawn with FF3's own
   primitives (progress/*_debug_menu_player_light_cone.png). Off by default. Doubts: Set3DPosTexure forwards its caller's t3 as
   the mono flag in the ROM (garbage); the port passes eff_wrk.monochro_mode. ~~The work-frame gate (effect_sub.c's
   EffHostTexIsWorkFrame) skips any TEX0 whose TBP was once an EffSetDrawFrame target~~ -- it did drop a real picture
   (the opening movie on 0x2bc0, 2026-10-19); marks now lapse 2 frames after their pass stops.)

   (2026-10-19: with the pinned queue's entries all defined, `tus.py next` took unassigned@0024cec0 from the
   frontier: it was two FF3-only modules, now `ingame/radio.c` and `ingame/enemy/random_ghost.c`, both verify
   (nothing in the opening switches them on). The queue is still empty: refill it from a run that walks past
   the gate (the radios / random ghosts need RADIO_TURN_ON / RANDOM_GHOST_ON from a later event).)

   (2026-10-19, later: `tus.py next` took unassigned@001766c8 from the frontier: it was the head of FF2 effect_scr.c,
   so effect_oth.c now starts at 0x1766c8 -- the room glow pairs, InitEffectScr / EF, the blur (SubBlur, FF3's fan blur
   SubBlurFan, SetBlur / RunBlur / CallBlur2), focus, contrast and nega; all 25 written, verify. RunBlur now refreshes the
   half copy 0x38e0 every in-game frame and EffectOthBufCopy stores it at EE 0x117b000; on the host that round trip goes
   capture slot to capture slot (g2d_draw.c LocalCopyLtoBAdrs / BtoLAdrs with a stash address; before, a stash store of a
   page was a GPU read-back). --run 170 is unchanged (Rei by the gate, ~24 fps as before). Unseen: nothing in the opening
   asks for kinds 3 / 5 / 10..13 (blur, focus, nega, contrast); CallNega2 (PlyrDamageCtrl, a ghost's hit) is the first
   likely one -- check that the nega flash inverts rather than blanks. Doubt: SubBlurFan's host triangles use the
   renderer's default blend for ALPHA 0x44 and ignore the masked ZBUF, as the deform kernels do. The queue is still empty.)

   (2026-10-19, third: the queue still empty, `tus.py next` took `graphics/effect/effect_ene.c` from the frontier (7 reached
   stubs, most of any TU), pinned 0x15b658..0x1622d0 (the CEffectFinder singleton wrappers 0x1621d8.. included), partial:
   init / release, the damage overlay and the spirit strands with their ribbon written from MioPan, checked against FF3
   (16 overlay records, the file's own sprite table camdat rdata 0x352858, GetTrgtRotType2Neg, FF3's camera-view test).
   New host bridge ReiPan_Graph3dDrawGouraudTriangles (reipan_graph3d.cpp; MioPan RendererGouraudTriangles3D) for the
   ribbon. --run 170 unchanged (Rei by the gate). Unseen: everything here needs a photographed ghost (EneHitEffectReq from
   the finder's hit); the first run that damages one should show the spinning blue / red overlay over the ghost and 4
   strands flying to the player. Doubts: the overlay sends the TIM2 at EE 0x1fa8000 (FF2 SetSprFile2's effect page) but
   nothing known loads one there in FF3 (0x8000 past the system heap's end) -- the port sends only a real "TIM2" there,
   so camdat's page (TBP 0x2710..) is whatever VRAM_TEX_PK4 left; DrawNewPerticleSub's ROM work arrays hold 30 points
   while the strands pass 36 (a stack overlap in the ROM; the port sizes 36). **Re-assess before more frontier picks:**
   every effect TU taken from the frontier since 2026-10-16 (effect_oth / effect_sub / effect_scr / effect_ene) is
   waiting on a ghost encounter; a smoke pad that walks to the first ghost and raises the camera would show them all.)

   (2026-10-19, fourth: the queue still empty, `tus.py next` took `ingame/ingame_effect.c` from the frontier (5 reached stubs:
   IgEffectInit / Main / StoryMainScreenEffectReq and the player put), all 60 functions written from MioPan, verify. **The
   screen look of the frame now applies**: at --run 170 rs101 is lighter with a soft glow and film grain over the whole
   picture (the canned SCREEN_EFFECT_PARAMETER set: dither 3, contrast 0xc at 75 / 75, the room glow kind 0x10), where
   the stub left it dark and clean. Doubts: no PS2 capture to compare the strength of grain / contrast against -- the
   first session with a reference screenshot of the Manor of Sleep should judge it (too bright or too noisy points at
   effect_oth.c's dither / contrast kernels, not at this file). Unseen: the flashlight lens flare (needs player flag
   0x8000, the light motion; not set in the opening walk), the lanterns (no caller in FF3), the water-flow and
   light-come-in furniture (none in rs101). The lantern flame's y offset is rdata 0x370da0 = -9599.0 (FF2 +25):
   kept as the ROM has it; the lantern draws themselves are unreferenced. Also new: IsActiveEneDmgParticle 0x15e7f0
   written in effect_ene.c; EffectButterflyNumGet / EffectButterflyReq declared (stubs in effect_butterfly.c).)

   (2026-10-19, fifth: the queue still empty, `tus.py next` took unassigned@0014ac58 from the frontier (CineInit / Main /
   Draw were reached stubs every in-game frame). It was two FF3-only modules: `ingame/cine_film.c` (the projector films,
   all 44 written) and `ingame/clear/ClearMenu.c` (phase 43 after the game clear, hooked into gphase.c), both verify.
   --run 100 unchanged (the opening event in rs101). Unseen: the projector film plays only on CINE_FILM_PLAY in a
   projector room (areas 0x13 / 0x30 / 0x1c); the first run that reaches one should show the film on the wall screen at
   half alpha with the reel turning. **Re-assess before more frontier picks** (as noted after effect_ene.c): a smoke
   pad that walks past the gate is what the verify TUs wait for.)

   (2026-10-19, sixth: the queue still empty, `tus.py next` took `graphics/effect/effect_butterfly.c` from the frontier (4 reached
   stubs: EffectButterflyInit / Main / ParticleMain / the particle init, every in-game frame), pinned 0x159188..0x15b348, all 33
   written from MioPan and FF3, verify. --run 170 unchanged (Rei by the gate): no butterfly is ever requested in the opening --
   ambient ones come only from IgEffectButterflyReq (no caller in FF3), guides from SET_BUTTERFLY events. The first run whose event
   sets a butterfly should show a red butterfly model with a soft additive glow (effdat 0x54) and sinking motes, fading out when
   Rei is within 10 units. Doubts: the model matrix starts from the y / z flip 0x329400 and SetWorldMatrix flips again (the ROM;
   check it is not upside down); Position.w drifts for guides (the ROM adds xyzw). The tail 0x15b348..0x15b500 (MapSky data wrappers)
   joins the 0x15b500 run as unassigned@0015b348. **Re-assess before more frontier picks.**)

   (2026-10-19, seventh: the queue still empty, `tus.py next` took graphics/effect/effect_scr.c from the frontier: it was
   enemy code -- the tail of ene_main.c (0x189b90..0x18a338, the photo-hit reaction, EneAnimeSet, the time step, written)
   and the head of FF3's enemy_act.c, the ghost script interpreter, now defined 0x18a338..0x1931e0 (partial: EneActSet,
   EneAlgCtrl / EneBlinkCtrl, the blink ops and control ops 0x00..0x0f written; 128 handlers stubbed). Unseen: no enemy
   acts in the opening. The interpreter is what makes a ghost move and attack, so the rest of enemy_act.c is on the path
   to the first encounter that every effect TU since 2026-10-16 waits for. **Re-assess before more frontier picks.**)

   (2026-10-19, eighth: the queue empty, `tus.py next` took unassigned@0022ada0 (6 reached stubs, all called by photo_dat.c's
   photo sequence); its head was FF2's photo_make.c, now **`ingame/photo/photo_make.c`** 0x22ada0..0x22ca90, all 19 written,
   done. **The developed photo shows**: at --run 250 (shots every 8 frames) the shot at read 4900 flashes white (frame ~4912),
   then until ~4968 the frozen frame sits inside the E_CMN_TEX_PK4 #6 border at (128, 96)..(512, 352) with the world dimmed
   round it, and the viewfinder comes back at ~4976. photo_dat.c's developed-frame half is seen by that (still verify: the
   hint picture needs a subject). It needed a renderer fix, decided without the user (veto welcome): a capture slot declared
   mid-frame now gets its texture at once (hazards.md "A one-shot copy into a new capture slot is lost"); before, 0x3480 was
   black. Also new: G2dHostPageReadback (g2d_draw.c), so CopyScreenToBuffer2's repack reads real pixels. Unseen: the hint
   picture (DrawPhotoHinttex: a subject whose PHOTO_OBJ_DAT names a hint texture), DrawSpecialFurnPhoto (a scenery shot),
   the album draws (DrawPhotoBuffer, the thumbnails), and the encoded photo itself (FUN_00228968, stub). Next on the photo
   path: the storage run 0x228718..0x2289a8 (UncompressData 0x228878 and the encode; its callees carry wrong FF2 matches:
   SetPlayerHitRadius is GetPhotoExpandNo, PlyrNAnimeCtrl is ExpandFile) and unassigned@00229990's decoder state
   (CheckPhotoExpandEnd / GetPhotoExpand). The rest of unassigned@0022ada0 (0x22ca90..0x231628: FF2 photo.c's PFILE_WRK
   code with CopyPFileWrk / the SortPhotoData_* sorts, the StoryMenu_Film phase, PlyrFlagInit 0x22f628 reached every new
   game) is still unassigned.)

   (2026-10-19, ninth: the queue empty, `tus.py next` took unassigned@001d7a58 (GameDataSaveWrkInit / SoulListSetHave reached every
   new game). It was two FF3 files: **`ingame/gameover/prg/gameover.c`** 0x1d7a58..0x1d8208 (GameOverReq, phase 10 the game over
   movie, phase 40 Story_LoadGame = the game over menu; done) and **`ingame/item/prg/soul_list.c`** 0x1d8208..0x1d84f8 (the ghost
   list, 217 x {have, read} + four bonus flags; verify: nothing on screen reads it before the ghost list menu). A temporary probe
   (GameOverReq after 300 Story_Normal frames, reverted) showed the whole path: the GAME OVER movie (GAMEOVER_PSS), then the
   Game Over menu (Album / Return to title, help line) over the outgame backdrop movie, its title matching GAMEOVER_PHF's
   gameover.png (progress/20261019-2307_ingame_gameover_prg_gameover.c_probe_movie.png / _probe_menu.png); gameover_menu.c
   marked done by it. The scripted pad then chose Album: **phase 8 (the album, 0x123480.., unassigned) is all stubs**, so the
   screen keeps only the backdrop. Doubts: GameOverMenuGetPhase never answers 0x44 with the two rows seen, so one_Story_LoadGame's
   load branch (68 linked, then IngameLoadRestore -> 30) looks dead in the EU build; the old Ghidra name GameDataSaveWrkInit
   (now SoulListInit) and the FF2 matches GameDataSaveTexMemFree / LiberateGameDataSaveMem were wrong (0x1d84f8 / 0x1d8540
   are the head of game_result.c, which reads sdata 0x373344; renamed GameResultPicGetNo / GetAlpha). **Re-assess before more frontier picks.**)

   (2026-10-19, tenth: the queue empty (its six entries are all defined), `tus.py next` took unassigned@0013a010 (TattooEvCameraMain
   0x13a178 reached from CameraMain every in-game frame, TattooEvInit from EnemyMainInit). It is one FF3-only file,
   **`ingame/tattoo_event.c`** 0x13a010..0x13b320, all 19 written, verify: the tattoo grab event's camera (ease in on Rei's
   joint 2, the dream look, a wall-avoiding follow camera, the way back via MapCamNormalCameraCtrl, now public in
   map_camera.h), its sky-effect handle and the screen-effect save. --run 170 unchanged (the map camera follows Rei through
   rs101 into room 2; idle state returns 1). Unseen: everything else needs a ghost whose script runs EJobE04 (a tattoo ghost,
   kinds 0x1f / 0x20); the first such run should show the camera easing in on Rei, a white flash, then the monochrome dream
   look with the follow camera circling her 70..90 units out. Doubts: the way back ends on fov 1.5, not on the fov saved at the
   start (ROM, kept); TattooScrEffectAfter (screen effect 9) is also EneEffInit's choice for kinds 0x1f / 0x20, so 9 may be
   the tattoo ghost's look rather than "after"; the sky effect (kind 0x12, EffMapSky* 0x15b4e0..0x15b5c0, MapSkyData*) is
   stubbed in unassigned@0015b348. **Re-assess before more frontier picks.**)

   (2026-10-20: the queue empty, `tus.py next` took unassigned@0017f8b8 (ScreenSaverInit / Main / Draw reached every
   in-game frame). It is one FF3-only file, **`graphics/effect/effect_scrsaver.c`** 0x17f8b8..0x1806a8 + 0x2b6ee0..0x2b8220,
   all 17 written, done: FF2's screen saver (MioPan effect_scr.c) plus FF3's film noise, which effect kind 0x11 reuses
   (FUN_0017b970 now calls EffFilmNoiseDraw / EffFilmShakeDraw). **Seen**: idle in rs101 (pad stops after 2000:START), the
   saver loads SCREENSAVER2_PK4 and the blue ghost face of work/ref/SCREENSAVER2_PK4 dissolves in over the walls, large and
   faint, with heavier grain. It needed a host fix: effect_sub.c's work-frame gate dropped every sprite at TBP 0x2bc0 while the
   room glow runs, PSMT8 / PSMT4 pictures included; indexed formats are exempt now (hazards.md "The work-frame gate also
   dropped EE-uploaded pictures"). The EU build is PAL: the wait is 7500 frames, about 5 minutes of game time at rs101's
   ~24 fps. Doubts: the dust specks / scratch lines (LangTexSend 0 sheet) were not singled out in the shots -- check them in
   the first run with kind 0x11 (no caller seen yet); EffFilmLineDraw's CLAMP REGION_REPEAT on v and the dust's u2 = w quirk
   are ROM-faithful but the host sprite path may not honour region repeat; SubBlur's arguments (scw = blur byte 0..40,
   cx 180, cy 320, alpha 1) look odd but are what the ROM passes; the noise alpha wraps ((int)(f * 64) & 0xff, ROM).)

   (2026-10-20, later: the queue empty, `tus.py next` took unassigned@001b3078 (DevelopFilmInit / DevelopFilmSaveGet reached every
   new game). It is one FF3-only file, **`ingame/story_develop.c`** 0x1b3078..0x1b57d8 + 0x2b91a8..0x2b94b0: the develop table
   (tbl325260 is now DEVELOP_FILM develop_film[20]; ev_change.c / ev_open.c / ev_command.c / photo_dat.c / save_data.c use the
   header) and phases 38 Story_Develop / 65 Story_DevelopFilm, hooked into gphase.c; all 70 written, verify. --run 100 unchanged
   (the opening event in rs101, no new stubs). Unseen: phase 38 needs DEVELOP_REQUEST from a later event with a film shot
   (SHOOT_DEVELOP_FILM / a photo whose PHOTO_OBJ_DAT._30 names a film); the first run that opens it should show the darkroom
   backdrop of work/ref/GENZOU_PHF (genzou1_bg, red safelight) with a film strip and the film picture, and after "yes" the
   developed picture dissolving in over ~150 frames. Doubts: DevelopPicHook reads a1 / a2 as the item's w / h (box-kind hook args,
   B2dCtl.c kind 5) for the SubFocus pulse; the strip thumbnails are culled against the frame column's y (frame_y) but drawn at
   the thumbnail column's y (ROM, kept); develop_dat.soul is -1 in every EU row, so the ghost-list branch never runs; the PC
   guards in phase 65 skip the thumbnail when no film is listed (the ROM writes through address 0). **Re-assess before more
   frontier picks.**)

   (2026-10-20, third: the queue empty (its six entries are all defined), `tus.py next` took unassigned@001106d8 (frontier
   score only, no reached stubs). It is **`ingame/menu/menuCamBasic.c`** 0x1106d8..0x112150 + 0x2ac048..0x2ac128, CmenuCamBasic,
   the camera menu's Capabilities page (camera levels bought for points), all 33 written, done. **Seen through a reverted
   probe** (one_Story_Normal requested phase 64 after 400 frames, CmenuCamMain::IsReady skipped hud->IsReady, the page was
   switched to 1 after 150 menu frames): rows Max Value / Range / Sensitivity / the special ability with the dark and blue
   pips, LV numbers, 10000pts costs (bright on the selected row), 0 points and the row's help text, all from
   work/ref/EDIT_PHF (progress/*_ingame_menu_menuCamBasic.c.png, the probe shot; the Status page in
   progress/20261020-1803_ingame_menu_menuCamBasic.c_probe_status.png, so menuCamMain.c / menuCamStat.c / menuCamBase.c were marked done). **The camera menu is not
   reachable without a probe**: phase 57 StoryMenu_Top (0x20bd28..) is nullptr in gphase.c, and CmenuCamMain::IsReady
   waits for ever on HudLayoutIsReady 0x1505a0 (anim_2d.c stub; HudLayoutInit / Main / Play are stubs too). Not seen:
   buying a level (0 points; the yes/no, the shine at the next pip, CamLvUp on frame 0x22), the special-row name
   lit/plain choice for each player. Doubt: FF3 keys the level cost by the row (CamLvGetCost(plyr, row, lv)) while the
   level comes from CamLvGetKind(row); they differ only when slot 3 is empty and row 3 shows slot 4 (kept).)

   (2026-10-20, fourth: the queue empty (its six entries are all defined), `tus.py next` offered unassigned@0012a068
   (frontier score 13, no reached stubs); the scheduled run re-assessed and took unassigned@0026fd50 instead, the TU
   with the most stubs a smoke run reaches (SndPCMStreamInit / Main, every frame from SndInit / SndMain). It is FF2's
   **`system/eeiop/snd_pcmstream.c`** 0x26fd50..0x270240, all 12 written, done (sound, not on the drawing path; --run 100
   unchanged, the two stubs gone). Only Init / Main have callers in FF3: the PCM streamer never plays. Its
   PCM_STREAM_START is 0x10 bytes (no file name), so system/iop/iop_snd_def.h's IOP-side struct shrank to 0x10 and
   iop.c's command walk steps over 0x10, as for STREAM_START. The remaining reached stubs on the boot path are
   Vu0InitConstRegisters (unassigned@001e6ae8) and the save-work getters FUN_001cfeb0 / OptionWrkSaveGet /
   FinderBankWrkInit / IngameWrkSaveGet / FUN_002367f0 / FUN_00237858, then PlyrFlagInit / MenuRefCtrlInit /
   FurnPhotoFlgClear (FF3 0x226df0; its stub in unassigned_stubs.c logs address 0) / FUN_001ecd40 at new game -- refill the queue from those.)

   (2026-10-20, fifth: the queue still empty, `tus.py next` took `album/prg/album_mem.c` (frontier score 6); written, verify
   -- FF3 takes the album staging buffer from the system heap, not FF2's DMA-ring tail; nothing reaches the album screens
   (the phase 27 / 28 glue 0x128a58..0x128e30 is unassigned). **The queue was not refilled because the reached stubs sit in
   TUs marked done**: FUN_001cfeb0 / IngameWrkSaveGet (0x1cfd40) are inside `ingame/ingame_wrk.c` 0x1cfc80..0x1d0710 and
   FUN_002367f0 / FUN_00237858 inside `ingame/plyr/player.c` 0x231628..0x2394a8, both status done, so `tus.py next`
   never picks them; OptionWrkSaveGet 0x224618 and FinderBankWrkInit 0x25fd38 are in no defined TU. Next session: claim
   ingame/ingame_wrk.c directly (`tus.py claim`) and write its two stubs, then player.c's two, then define the files
   around 0x224618 / 0x25fd38. Possibly tus.py should keep a TU partial while its range still holds REIPAN_STUBs.)

   (2026-10-21: the queue empty, `tus.py next` took unassigned@001ec978 (ItemTblReset 0x1ecd40, ITEM_RESET, reached every
   new game). It was the tail of the item menu plus a small table file: **`ingame/menu/menu_item.c`** 0x1eac20..0x1ecd30 +
   0x2bccd8 (phase 59 StoryMenu_Item, all 56 written, done) and **`ingame/item/prg/item_tbl.c`** 0x1ecd30..0x1ecdd8 (done).
   **Seen through a reverted probe** (one_Story_Normal requested phase 59 after 400 frames, init skipped HUD_LAYOUT::IsReady,
   a stub returning 0): "Tools" with the Limited Items / Key Items tabs of work/ref/ITEM_PHF, the rows with counts, the
   item's picture with its blue glow and the description window; with the opening's own item table (0, all zero) only Key
   Items (Professional Camera) is listed, with ItemTblReset(2, 1) in the probe Limited Items shows Type-7 Film 50 /
   Type-14 Film 30 / Herbal Medicine 3 (progress/20261021-0804_ingame_menu_menu_item.c_probe_tabA.png). **Not reachable
   without a probe**: phase 57 StoryMenu_Top is nullptr and HudLayoutInit / IsReady / Main / Play / Close are anim_2d.c
   stubs (IsReady 0 would hold init step 1 for ever, as for the camera menu) -- the HUD layout and StoryMenu_Top
   (0x20b530..0x20c400) are what open every story-menu page. Not seen: "use it?" and the reasons (LayoutMsg 0x61), using
   one, the page keys. Doubts: end_StoryMenu_Item answers 1 without releasing anything when the layout has stopped
   but the HUD is not HUD_LAYOUT_DONE (ROM, kept); the tab keys step +1 on TAB_PREV and -1 on TAB_NEXT (the same with two
   tabs). Rei starts the game with no film because the opening's ITEM_RESET uses table 0 -- check against the PS2 when a
   later ITEM_TABLE_CHANGE_ is seen. Next: refill the queue from the reached stubs (the save-work getters in ingame_wrk.c /
   player.c, PlyrFlagInit, FurnPhotoFlgClear) or the story-menu top + HUD layout.)

   (2026-10-21, later: the queue still empty -- its six unassigned@ keys are all defined under other names, so tus.py next
   skips them -- `tus.py next` took unassigned@0015b348 from the frontier (score 4): **`graphics/effect/effect_mapsky.c`**
   0x15b348..0x15b658, the FF3-only map-sky effect (kind 0x12, the sky dome of MapSky.c at a position), all 10 written, verify:
   only the tattoo event (TattooEvCameraMain state 4) requests it and no tattoo ghost is on the smoke path; the reference is
   work/ref/DOME_MDL_PK4 (160x128 + 256x128) with the SCENE91xxM_PSS movies on it. --run 100 unchanged (rs101 in 3D at frame
   2520). Doubt: SetMapSky on an EFFECT_MALLOC failure runs Exec on the cleared pnt[1] in the ROM (a read of EE address 0);
   the host returns instead. The reached stubs are still the save-work getters in done TUs (see the 2026-10-20 fifth note):
   claim ingame/ingame_wrk.c directly, or drop the stale entries from tus.json's queue and refill it.)

   (2026-10-21, third: the queue still empty, `tus.py next` took unassigned@00226138 (frontier score 4; FurnPhotoFlgClear
   0x226df0 was a reached stub at every new game). It is **`ingame/photo/photo.c`** 0x226138..0x2274e0 with the old
   unassigned@00226e38: phase 45 Story_Peeping, the peeping view, all 29 written, verify (Ghidra had merged two
   unreferenced getters 0x226e08 / 0x226e18 into 0x226e00; split). FurnPhotoFlg* are renamed PeepState* (FF3's word is
   the peeping state 0..5, not FF2's photo bit), FUN_002265f0 = PeepLookCtrl, FUN_00226e38 = PeepingRequest; callers in
   player.c / map_camera.c / ev_change.c / ev_command.c / finder_view.c updated. **Seen through a reverted probe**
   (one_Story_Normal called PeepingRequest(0) at its 400th frame, `--warp area=5,world=0,stop=1`): the screen fades,
   the camera moves to record 0's eye and shows area 5's room beyond it with the fade layer's "Change Finder / Return"
   help, the left stick pitches then yaws the view up to the record's limits, CROSS fades back to the game camera
   (progress/20261021-1112_ingame_photo_photo.c_probe_peep_area5*.png). Not compared with the PS2 (no reference capture);
   the slide (second stick, max 1 unit) is too small to see. Mark it done when a run reaches event 8 (area 5, map-hit
   rect 20004, facing 90 +- 40, OK) or 9..11 (area 17) naturally. The pulse VibPulseClear / Main / ReqDist
   (0x1e75f0..0x1e7878, SE 0x1a9b0009 + vibration per beat; then a hermite path helper 0x1e7878..0x1e7b08; name the
   file when it is defined) are reached stubs while peeping. Doubts: PeepPulseStart requests the pulse with the two data
   points and at once again without them (the second wins: a fixed beat) -- FF3's code, kept; PeepLookCtrl's vertical
   branch pushes opposite to the horizontal one for the same stick sign (ROM, kept).)

   (2026-10-21, fifth: the queue still empty, `tus.py next` took unassigned@001d97a8 (frontier score 2, no reached
   stubs). The whole run 0x1d84f8..0x1da418 after soul_list.c is **`ingame/story_menu_ghost_list.c`**: phases 63
   StoryMenu_GhostList and 88 StoryMenu_GhostListBonus, all 30 functions written (4 hooks mkfunc'd), done. The inferred
   game_result.c / game_result_top.c / gameover_menu_top.c / unassigned@001d8da0 slots there were FF2 look-alike matches.
   **Seen through a reverted probe** (one_Story_Normal marked ghosts photographed / read and SetNext(63) at its 400th
   frame): the Ghost List screen with 11/33, numbers, names and ?????, new marks, the cursor row, the picture
   (GLIST_JG_010 matches work/ref), the room title between its ornaments and the description; the panel slides out on an
   unread entry and in after OK reads it, R1 pages to 10..18 with the scroll bar (progress/*_ingame_story_menu_ghost_list.c*.png).
   Not seen: phase 88 (a bonus needs 50 photographed ghosts, and only 33 count before a clear: SoulListIsFull), cancel
   back to 57 (StoryMenu_Top is still nullptr). Doubt: the ROM leaves the scroll bar / picture pointers dangling after
   end_StoryMenu_GhostList; the port clears them.)

   (2026-10-21, fourth: the queue still empty, `tus.py next` took unassigned@00112db0 (frontier score 2, no reached
   stubs). It is **`ingame/menu/menuCamStLens.c`** 0x112db0..0x1146a8 + 0x2ac128..0x2ac1b8, CmenuCamStLens, the camera
   menu's "Upgraded Lens" page (owned lenses as a 6-row scrolled list, levels bought for points), all 28 written, done;
   FurnCtl.c is now empty. Ghidra had swallowed 8 entries (steps 0x112fa8 / 0x113240 / 0x113358, hooks 0x113508 /
   0x113af0 / 0x113c70, Setup 0x1143f8, Draw 0x114698); created. No fourth class: the 0x112db0..0x113db0 run is this
   page's helpers, steps and hooks. **Seen through a reverted probe** (one_Story_Normal gave Rei lenses 0..6 with levels
   3 / 1 / 2, 25000 points and requested phase 64 at its 400th frame; CmenuCamMain::IsReady skipped hud->IsReady; Run
   switched to page 2 after 150 frames): the Upgraded Lens tab, six lens icons Bs / Zr / Cr / Sr / Sl / St with blue and
   dark pips, Lv3 / Lv1 / Lv2 / Lv0 digits, costs (0 at Lv3, bright on the cursor row), 25000 pts, the scroll bar for
   the seventh lens, the cursor row frame and the lens's help text, all parts of work/ref/LENS_PHF
   (progress/*_ingame_menu_menuCamStLens.c.png). Not seen: buying a level (yes/no, shine, LensLevelUp on frame 0x22),
   scrolling, the empty page (unreachable: the other pages do not switch here without a lens; on its first entry FF3
   calls SetDisp on the still-null cursor sprites, CmenuCamMain runs SetHooks before Setup -- PC guard). Doubt: the
   constructor picks the lens column as ingame_wrk.b00 != 0, not MenuCamLensGetColumn, so Miku (1) reads Kei's column
   (ROM, kept).)

   (2026-10-21, sixth: the queue empty, `tus.py next` found nothing (no frontier, no queued TU); re-assessed and
   claimed `ingame/menu/anim_2d.c` again for its unwritten head 0x1502a0..0x150b40, the HUD_LAYOUT object every story
   menu page waits on (HudLayoutInit / Release / IsReady / Close / Main / Play were stubs, IsReady 0 held the camera and
   item menus' init for ever). All written, plus LensGetSlotNum 0x1c6668 (fndr_parts.c); 0x1504e0 was swallowed into
   HudLayoutInit in Ghidra (created: HudLayoutReleaseBase). **Seen through a reverted probe** (one_Story_Normal ran
   ItemTblReset(2, 1) and SetNext(59) at its 400th frame, **no IsReady skip**): the item menu opens by itself now, with
   the status panel bottom right -- Rei's face and the full blue HP strip in the status_waku_H frame, the parts of
   work/ref/STATUS_H_PHF (progress/*_ingame_menu_anim_2d.c.png, the probe shot). The layout was status_H (no camera
   items: the finder is not ready yet in the opening). Not seen: the camera-item layouts (film icon / count, flash,
   lens slots, Miku's slow gauge: a later point with the camera), the candle fire wobble (the candle is off in the
   opening), the out / cancel labels on closing (phase 57 StoryMenu_Top 0x20b530..0x20c400 is still nullptr in gphase.c,
   so nothing opens or closes a story menu without a probe). **Next: StoryMenu_Top** (unassigned@0020b590, it also runs a
   HUD_LAYOUT) makes every story-menu page reachable from the pad (START in game); then refill the queue from the
   reached stubs (ingame_wrk.c FUN_001cfeb0 / IngameWrkSaveGet, player.c FUN_002367f0, FinderBankWrkInit 0x25fd38).)

1. **Leave the loading screen** (M1): ~~`unassigned@001e9ef8`~~ (done 2026-10-15 as `ingame/item/prg/item.c`, verify:
   ItemGetCnt has a real table now, so PhotoModeWrkFinderDraw's crash is gone; the map-put callbacks 0x1e9ef8..0x1ea2b0
   went to `ingame/ingame_effect.c`), ~~the map control objects 0x11cea8..0x11d648~~ (done 2026-10-15 as
   one FF3-only file `ingame/map/MapCtrl.c`: "Now Loading..." goes away, Story_Normal runs 30 s+ on a black
   screen with the in-game BGM; the room load now really starts, which exposed IngameDrawRoomMorph reading
   room->mdl 0 -- PC guard added), ~~the ECB table readers `unassigned@00154cf0`~~ (done 2026-10-15 as `graphics/ecb.c`, verify: the
   in-game run loads area_data / map_menu / furn_save / meimetu.ecb and room rs101, then runs Story_Normal
   to the end of a 100 s smoke run on a black screen, no crash), ~~`unassigned@00154870`~~ (done 2026-10-15 as `ingame/map/DoorStat.c`, verify: door_stat.ecb loads, Story_Normal runs to the end of a 100 s run on a black screen; graphics/dmaVif1.c held only its wrong matches and is gone) and `common/SingleLinkList.c` (FF2's;
   MioPan port). Picture: "Now Loading..." (LOADING_PHF) goes away and phase 32 runs without a crash;
   put the next blocking stub in the handoff.
2. **Movies on screen** (M2): ~~`system/playpss/ldimage.c`~~ (done 2026-10-15: the title backdrop movie
   shows behind the 60% TITLEBG artwork and animates; needed a renderer fix, hazards.md "sprite.frag
   discards"; MapSky.c, title_obj.c, my_strfile.c, playpss.c, pss_loop.c marked done). Old text: playpss.c already decodes OUTGAME_BG_PSS at
   320x224 (about 25 pictures a second), but `setLoadTexTags` 0x273d08 (FF3-only, forwards to 0x273a40
   with a 0x400 stride) returns 0, so MapSkyMovieProc never gets a packet size and MapSkyMovieSendFrame
   sends nothing. Port MioPan's ldimage.c plus 0x273a40 / 0x273d08 and give it the PC direct picture
   path (reipan_video.h: MioPan calls ReiPan_VideoBegin(tbp) from setLoadImageTags; the title's TBP is
   0x2bc0). MapSkyMovieSendFrame copies the chain into the VIF1 packet buffer, so either the renderer
   takes an image-load chain there or the PC path skips the chain. Then ~~`audiodec.c`~~ (done 2026-10-15 as
   verify: MioPan port; no movie with audio runs yet -- the title backdrop passes iopBuff 0, so the
   first story movie (M3) is its test: it must play to the end, not stall in audioCallback). Picture: the
   title backdrop movie behind the title menu at `--run 45`; it marks MapSky.c, pss_loop.c,
   my_strfile.c, title_obj.c and playpss.c done.
3. **The story starts** (M3): the event core that makes ev_command.c's 324 written handlers reachable:
   ~~`ev_main.c`~~ (done 2026-10-15 as verify, 0x19b768..0x19baf8: EventMain now runs every
   Story_Normal frame and event.obj + the VCI pack load; what it calls is still stubs: EventCtrlCenterMain /
   EvCtrlCenterInit / EvPhotoObjInit (ev_open.c), EventExe / EventExeInit (the exe-slot run 0x19ac78..0x19b058,
   inferred unassigned), EvTimerMain / EvTimerCtrlInit (ev_timer.c), ev_seInit (ev_se.c), EvChapterDisp*
   and the ladder list (now `ev_ladder.c` 0x199150..0x1992d0, not queued; LadderListLoad is reached every new game) -- take ev_open.c and the exe-slot run first), ~~`unassigned@0019b158`~~
   (done 2026-10-15 as `ingame/event/prg/ev_get.c`, verify: the argument decoder EvGetCmdArgs 0x19b158, EvGetTblAddr / EvGetExeAddr,
   parent / sub-id lists, EvGetRot360; first reached when ev_open.c's EventSetOpenCondition / EventCtrlCenterMain or ev_macro.c's
   dispatcher 0x19b5b8 are written: the opening event running is its test),
   ~~`ev_macro.c`~~ (done 2026-10-15 as verify, 0x19b4a8..0x19b768: the interpreter EventExeFuncCall, EventExeCmdCall,
   Event_EvEnd, EventLoadRestore and the 235-row command table 0x3556b8 in C++; first run when ev_open.c's
   EventCtrlCenterMain / EventSetOpenCondition start an event -- the opening event running is its test; it
   added the stub EventSetCloseCondition 0x19bd08 to ev_open.c), ~~`ev_exe.c`~~ (done 2026-10-15 as verify, 0x19abe8..0x19b158, with
   ~~`ev_chapter.c`~~ 0x19a0a0..0x19abe8: GetObjectPos, the chapter title, the ghost tables; the code once
   inferred as ev_macro.c), ~~`ev_open.c`~~ (done 2026-10-15 as verify, 0x19baf8..0x19c658: the event core runs now.
   A temporary trace (printf in EventExeCmdCall / SetEventInitStatus, reverted) showed at Story_Normal's start
   the unconditional events 0, 1, 5, 7, 16, 19, 34, 76, 88, 89, 1060, 1602.. 1780 auto-start and run (event 0
   PLYR_PAD + FADE_OUT, event 5 ITEM_GET .. SET_WAKEUP_MOVIE / SET_SLEEP_MOVIE, 89 CHANGE_TATTOO_LEVEL ..
   SET_DREAM_PLAYER), the next frame judged open conditions start 17..32 and 148..151 (148 CHANGE_CHAPTER,
   MAP_STREAM_STOP: the in-game BGM stops; 151 PLYR_LOCK, EV_CHANGE_WORLD, GO_DREAM_WORLD, PLYR_MODEL_CHANGE,
   SET_REAL_CAMERA), then event 151 waits on AREA_CHANGE every frame: MapAreaChange waits on ev_seIsReady
   0x19c988, a stub returning 0), **`ingame/event/prg/ev_se.c` next** (0x19c658..: its save getter, ev_seInit,
   ev_seRelease, ev_seChangeRoom, ev_seIsReady; queued at the top); the movie phases ~~`unassigned@00218300`~~ (done 2026-10-15 as all of
   `graphics/movie/movie.c` 0x217c90..0x218678, verify: FF2's movie.c ported -- StartMovie / MovieDispMain / EndMovie and phase 39
   Story_SceneMovie; at --run 100 MovieReqPlay (stub) is reached right after Story_Normal starts, so movie_projecter.c's MovieReqPlay
   is what requests phase 39 next; its getters MovieReqGetSceneNo 0x2186b0 / MovieReqGetRetPhase 0x2186c0 / MovieReqDispCapture
   0x218898 and scene.c's GetFileNoFromSceneNo / SceneDecisionMovie are stubs now: write them with it),
   ~~`movie_projecter.c`~~ (done 2026-10-15 as verify, with scene.c's table readers: the opening event asks MovieReqPlay for scene 0 (SCENE0010M_PSS 0x97b, a movie) from phase 32, phase 39 picks 67 Story_Movie and the run reaches its stubs phase67_init / one; the screen stays black after "Now Loading" until Story_Movie draws), ~~`unassigned@002188f8`~~ (done 2026-10-15 as three files: `graphics/movie/story_movie.c` (phases 67 / 90), `movie_title.c` (captions, FF2 port) and `movie_title_dat.c`: at --run 140 the opening movie SCENE0010 plays with its captions ("Yuu..." at timer 1133 = 1360 x 5/6, PAL); movie.c and movie_projecter.c marked done; audiodec.c stays verify until a movie is seen to play to its end -- the 154 s opening is longer than a smoke run; it needed the libmpeg_video.cpp catch-up fix, hazards.md "A PSS movie froze"),
   ~~`unassigned@00219688`~~ (done 2026-10-15 as `system/eeiop/file_cache.c`, verify: not movie code at all but the shared ref-counted file cache 0x32d700 that effects / Menu_DrawTexMulti / album edit / the game-model loader use); ~~`ingame/subtitle/subtitle.c`~~ (done 2026-10-15, verify: all 17 functions; no subtitle is requested before the event stalls, see below). ~~`ingame/plyr/plyr_mdl.c`~~ (done 2026-10-15 as verify, 0x239ac0..0x23c028: the look and the viewfinder; PlyrIsFinderModeOff is real, so the opening event goes on after the movie: a temporary trace in EventExeCmdCall (reverted) showed MOVIE_PLAY, then EV_CAM (passes the finder-exit wait, EventCameraInitCtrlReq), SET_EV_CAM_VCI (camVci::SetEvCam), MAP_STREAM_PLAY (the in-game BGM), FADE_IN, EV_STOP for ~13 s, SET_EV_CAM_VCI again, EV_STOP to the end of a 100 s run, on a black screen). **Next blocker (2026-10-15):** nothing draws: the event camera (EventCameraInitCtrlReq 0x19cd28, ingame/camera/event_camera.c; camVci::SetEvCam 0x139f20, the VCI camera) and the room draw (MapDrawRooms 0x118650, CameraMain 0x1fadc8) are stubs -- M4. Picture: the new game's opening event plays its
   movie (probably SCENE0010M_PSS 0x97b; confirm from the event data).
4. **A room in 3D** (M4, about 120 KB of FF3 code):
   - camera: ~~`ingame/camera/map_camera.c`~~ (done 2026-10-16 as verify: all 47 functions; CameraMain now runs every
     Story_Normal frame and reaches MapCamNormalCameraCtrl -> MapCamRectangleCameraCtrl, which stop at the Mrec stubs:
     ~~the camera half of `ingame/map/map_rectangle.c`~~ (done 2026-10-16 as verify: the whole file, 0x1ff6e8..0x1ffe18,
     24 functions; the camera latch, event labels, footstep record, MrecReset), ~~`ingame/map/RegDat.c`~~ (done 2026-10-16 as verify:
     all 42 functions, 0x11f668..0x120800; at --run 100 the room's 4 registration files register with sane type counts,
     but every camera lookup asks for position (0, 0, 0) on floor 0 and misses (the files sit on floors 11 / 13 / 15):
     **the camera's next blocker is the player's position and floor**, which the stubbed player init / model leave at 0
     -- the map camera only picks a record once the player stands in the room); the event camera EventCameraInitCtrlReq /
     EventCameraMain (event_camera.c) gives the opening event its shots. Doubt: FUN_0013a178 (unassigned@001374d8, the
     camera effect) is a hand-written stub returning 1 (its idle answer) so the map camera runs; the real one must keep that),
     ~~`graphics/graph3d/g3dCamera.c`~~ (done 2026-10-16 as verify: all 6, FF2's code constant for constant, MioPan's
     clip-space Y flip kept -- see Decisions made), `g3dGeom.c` (small FF2 ports);
   - the graph3d engine from MioPan (FF2's modules, checked against FF3): ~~`gra3dSGD.c`~~ (done 2026-10-16
     as verify: all 69 functions; nothing calls _gra3dDrawSGD yet -- MapDrawInitRoom / MapDrawRooms are stubs),
     ~~**the host bridge next to it**~~ (done 2026-10-16 as verify, see item 0): port MioPan's `src/miopan/rendering/miopan_graph3d.cpp` into
     `src/reipan/rendering/reipan_graph3d.cpp`. gra3dSGD.c already calls its five hooks
     (reipan_graph3d.h: DrawPresetMesh / DrawRuntimeMesh / DrawRuntimeMeshPost / UploadGsImage /
     NotifyUVChange) where MioPan calls MioPan_Graph3d*, with the same arguments; they are empty. The port
     must rewrite MioPan's SGDSELF32 pointer walks to EE addresses (SgdPtr), and bring back what MioPan adds
     around the walker: _HostPublishLight (directional slots + g3dApplyLight before realtime draws and in
     BoundingBoxCalcP), g3dSetRealtimeLighting(0) in SetPreRenderMeshData, MioPan_Graph3dApplyCamera / the
     fog push (left out of gra3dApplyCamera / gra3dApplyFog), the TRI2 / TIM2 texture hooks in gra3dTRI2.c /
     mdlwork.c (reipan_renderer.cpp is MioPan's renderer already). Take it with the first TU whose draw
     reaches _gra3dDrawSGD (MapDraw.c), or as its own session. ~~`gra3dSGDData.c`~~ (done 2026-10-16 as
     verify: all 24 functions, sgdRemap stores absolute EE addresses; first run when the model managers in
     `unassigned@00210958` (FUN_002113d0 / FUN_00211450 call sgdRemap) and MapDrawInitRoom (sgdVerifyLightData)
     are written; the bridge port gets a sixth hook, ReiPan_Graph3dInvalidateSkinCache, already called from
     sgdRemap / sgdRemapInverse as MioPan does), ~~`g3dVif1.c`~~ (done 2026-10-16 as verify, 0x1cf870..0x1cfc80, FF2's five
     functions without asserts; gra3dInit runs g3dVif1Init / g3dVif1SetRegister at boot), ~~`g3dDma.c`~~ (done 2026-10-16
     as verify, 0x1cced0..0x1cd17c: FF2's packet builder over system.c's PACKET_BUF_MGR; g3dVif1.c's null-packet PC guards
     dropped; the boot's VIF1 register packets now really go into the chain and the --run 100 shots are unchanged:
     logos, title, difficulty select, opening movie, black in-game; its output shows only through the host bridge's
     model draw), ~~`gra3dTRI2.c`~~ (done 2026-10-16 as verify, 0x1e6698..0x1e6ae8: FF2's five functions without
     asserts; SGDTRI2FILEHEADER.gsli is a real sceGsLoadImage now. PC: every descriptor of a header sent down the
     VIF1 chain is also uploaded to the emulated GS (_HostExecLoadImageChain), as MioPan does. The bridge port
     adds MioPan's texture-binding hooks here: MioPan_Graph3dTextureUploadBound -- skip the host upload, then
     _SetPREVIOUSTRI2PRIM(NULL) --, MioPan_Graph3dNoteTextureUpload after the loop, and gra3dHostForEachTRI2Image /
     gra3dHostLoadTRI2 for the binding to walk a unit's images), `gra3dDma.c` (careful: see "VU1 program / subroutine addresses" below),
     `gra3dTRI2.c`, ~~`gra3dMisc.c`~~ (done 2026-10-16 as verify, 0x1dfbc8..0x1e0330: FF2's module trimmed to FF3's 11 functions; first run when char_model.c (_SetLightToShadow, the light set callers 0x144450 / 0x144dd8 / 0x1460b0), the model managers in `unassigned@00210958` (the shadow draws), candle_ctrl / PlyrDeathCtrl (gra3dPrelight) or the cine projector `unassigned@0014ac58` (gra3dSetSpecialLight / gra3dUpdateSpecialLight) run; its callees MapDrawPrelightRoom, gra3dshadowSetLight / DrawSGD / SetBoundingBox, gra3dSetLightIntens and CineGetProjector* are stubs), ~~`g3dLight.c`~~ (done 2026-10-16 as verify, 0x1cde18..0x1ce0f8: FF2's five functions; first run when effect_obj.c's EffLanternInit, g3dLightEx.c or gra3dShadow.c's _CalcColor run on screen), ~~`g3dLightEx.c`~~ (done 2026-10-16 as verify, 0x1ce0f8..0x1cec20: FF2's 13 functions; first run when gra3d.c's light emulation (gra3dEmulateLightDataSub -> g3dEmulateDirectionalLight / g3dGenerateDirectionalLightByEmulatedData), gra3dSGD.c's slot binding (g3dIsBBLightingup), 0x1df968 (g3dBlendLight / g3dCalcLightPower) or gra3dSetLightIntens's callers draw; 0x1cec20.. is CRenderTarget, the next file), ~~`gra3dBoundingBox.c`~~ (done 2026-10-16
     as verify with `gra3dConst.c`: 0x1dd8b0..0x1ddec0 / 0x1ddec0..0x1ddf48, FF2 port; first run when gra3dSGD.c's
     draw, gra3dShadow.c or char_model.c's bounding boxes run on screen), and ~~`unassigned@001de0d8`~~
     (done 2026-10-16 as `graphics/graph3d/gra3dMirror.c`, verify, 0x1de0d8..0x1dfb90: FF3's planar mirror --
     gra3dDrawModelCb draws the room's "morph" model as a mirror: VU0 clip of its mesh to a screen rect, the
     reflection through its plane, the room redrawn through the reflected camera scissored to the rect; plus
     gra3dGenerateBlendedLight 0x1df968 for char_model.c. First run when IngameDrawRoomMorph gets a room
     model (room->mdl is 0 on PC until MapDrawInitRoom / the model managers exist) in a room with a mirror;
     motQuaternion2Matrix 0x2bb108 is written since 2026-10-16 (motion.c));
   - FF3's model classes (C++, no FF2 counterpart; architecture.md "Classes", `rtti.py --class`):
     ~~`unassigned@001d2a40`~~ (done 2026-10-16 as `graphics/game_model.c`, verify: GameModel /
     GameMotionModel, 40 functions; the lantern model now loads and builds -- its builder is the
     stand-in stub, so mot stays nullptr and nothing draws. Picture that marks it done: any model
     drawn through GameModel::DrawMdl once the builders and the motion objects exist),
     ~~`unassigned@001d3760`~~ / ~~`unassigned@001d3a90`~~ (done 2026-10-16 as
     `graphics/game_anime_model.c`, verify: GameAnimeModel, 16 functions 0x1d36f0..0x1d3b40; the
     callers' free stubs FUN_001d38e0.. are members now. Runs when a CharModel is built (CharModel's
     constructors / Load 0x144ed0 / 0x145090 in char_model.c are stubs, so plyr_wrk->mdl stays
     nullptr); picture that marks it done: Rei or an NPC animating), ~~`unassigned@0020df28`~~ (done 2026-10-16 as `graphics/model_builder.c`, verify: Normal- /
     MotionModelBuilder, the director, ModelCollision (the cld shapes, push-out tests); the new-game run now builds the
     lantern's NormalModelBuilder and reaches the ModelObject constructor stub; picture that marks it done: any model
     drawn through GameModel::DrawMdl), ~~`graphics/mmanage.c`~~ (done 2026-10-16 as verify, 0x20f388..0x210818: ModelManager is a host class now
     (MDL_MOT a typedef), NormalModelBuilder::BuildMotion makes it, so the lantern's GameModel gets its mot; the
     collision registry ModelCollisionSet (the old MissionCtrl* names) and ModelCcs (chain physics) are written;
     the --run 100 shots are unchanged, black in-game: what ModelManager forwards to is ModelObject's members,
     stubs in lib/unassigned_stubs.c), ~~`unassigned@00210958` / `graphics/motion/morph.c`~~ (done 2026-10-16 as
     `graphics/motion_model_manager.c`, verify, 0x210818..0x2113d0: MotionModelManager, 20 functions; BuildMotion
     now makes it; ModelManager's slots have real names (PlayMotion .. SetShapeLoop); MOT_CTRL is a typedef of
     BmdMotion and MotCtrlSetFrame is Motion::SetFrame. Picture that marks it done: a model with a bone motion
     (MotionModelBuilder: Rei, NPCs, cloth furniture) drawn and moving), ~~ModelObject's run~~ (done 2026-10-16 as
     `graphics/model_object.c`, verify, 0x2113d0..0x2125f8: ModelObject / TextureModelObject /
     MultiTextureModelObject, every function; the lantern's ModelObject is now real (sgdRemap of its SGDs) and
     the --run 100 shots are unchanged, black in-game; its draws reach _gra3dDrawSGD only once something calls
     GameModel::DrawMdl / ModelManager::Draw on screen and the graph3d bridge is ported. morph.c / morph.h are
     gone: MorphGetHandle / MorphGetCoord were ModelObject::GetSgd / GetCoord), `graphics/motion/motion.c`
     (partial 2026-10-16, 0x2125f8..0x217c90: Motion / BoneMotion / BmdMotion (keyframe slerp, blend, root
     yaw) / VertexMotion / ShpMotion / ShpPhysicsMotion and the three data classes written, plus
     motQuaternion2Matrix 0x2bb108 (gra3dMirror.c's reflection is no longer zeros) and motQuatSlerp 0x2bd9a0;
     **left: the cloth simulation** -- ClothMotion::Init 0x214ce8 with 0x214e28 / 0x214fd0 / 0x215208 /
     0x215278 / 0x2156c8 / 0x2161d8, the step ClothMotion::Pose 0x2162b8, Reset 0x216410, the wind helpers
     0x2bdba0 / 0x2bdd90, and the bone-turn helpers 0x2165d8 / 0x216c40 that char_model.c calls; nothing in
     the --run 100 path builds a bone, shape or cloth motion yet, so the picture that proves it is Rei or an
     NPC animating);
   - the room draw: ~~`ingame/map/MapDraw.c`~~ (done 2026-10-16 as verify, 0x117720..0x118900 with the old
     `unassigned@00118768` run, plus three small modules it needs: `ingame/map/FurnLoad.c` 0x116fa0..0x117720 (the
     key table and the 48 furniture records), `ingame/map/RoomLoadAddr.c` 0x1f73b8..0x1f74f8 (the furniture load
     cursors) and `ingame/map/PosFile.c` 0x243898..0x2439d8 (the area_pos reader). A temporary trace (reverted) at
     --run 100 showed rs101 (buffer 0, origin 0) record its 9 obj_pack models and register 23 area_pos objects
     (4 doors, 13 effect marks, f016 / f189 / f190 / f501), rs106 (buffer 1, origin -132, 0, 490) 40; the shots are
     unchanged, black in-game). ~~the model handle builder~~ (done 2026-10-16 as
     `ingame/map/MapObjMdl.c`, verify, 0x203310..0x203828: MAP_OBJ_MDL is a class now; a temporary trace (reverted)
     at --run 100 counted 26 builds, 7 plain / 1 cloth / 18 motion, every one with an SGD; the "builder is a stub"
     PC guards are gone and the run still reaches frame 2520 without a crash; the shots are unchanged, black
     in-game). **Next blocker for the room picture: the graph3d host bridge** (reipan_graph3d.cpp, see gra3dSGD.c
     above; queued first, item 0). Then ~~`MapLight.c`~~ (done 2026-10-16 as verify, 0x1197c8..0x11afa0 with the old
     `unassigned@0011acb0` run: the hit vec table's doors and the door light blend; MapLBuffLoadDoor now runs at --run 100 and stops at
     the RegDat walk stubs FUN_00120718 / FUN_00120760 (unassigned@00120718, next to RegDat.c), so no door vec is made yet), `MapFog.c`, `MapLBuff.c`, `RegDat.c`,
     `map_height.c` (MhInitMapHeight is reached).
   Picture: area 1's room around the start position (compare with its textures, `tm2png.py`). Marks
   gra3d.c, g3dCore.c, MapLoad.c, MapRoomFile.c, MapPut.c, MapObjWrk.c and FurnSave.c done as their
   parts show.
5. **Rei on screen** (M5): ~~`ingame/char_model.c`~~ (done 2026-10-16 as verify: all 95 functions, the five model
   classes and AnimeCodeInterpretorChar; the stand-ins in ingame.h are gone, plyr_wrk->mdl / ene->mdl / NPC and
   cat models are real objects now and the player model loads ch000 at --run 100. **It moved the next blocker:**
   the opening event's GO_DREAM_WORLD (PLYR_MODEL_CHANGE) now succeeds in recreating the player and waits in
   EvPlyrMdlChangeWait on the door motion pack load; ~~that is `ingame/map/MapDoor.c`~~ (done 2026-10-16 as verify,
   0x1f4448..0x1f4788: MapDoorLoadReq / MapDoorCheckLoad / MapDoorAnimInit on DCH000..002_DEFAULT_PK4 and the door
   table; the 'kaza2_pzl.c' guess was wrong and kaza2_pzl.c/.h are gone). With it the --run 100 shots reach the
   SCENE0010 opening movie again (frames 1800..1980) with the player model really recreated; after the START skip
   the event runs on black (event camera, BGM) -- the room picture needs the graph3d bridge (item 0). Picture that marks char_model.c done: Rei drawn
   (needs the graph3d bridge), ~~`ingame/plyr/plyr_mdl.c`~~ (done 2026-10-15, verify: the look needs the model's watch control),
   ~~`unassigned@00203310`~~ (done 2026-10-16 as MapObjMdl.c + mdlwork.c's head) + ~~`graphics/motion/mdlwork.c`~~ (2026-10-16: FF3's memos, renamed `ingame/memo/memo.c`, verify), `ingame/map/MhCtl.c`, ~~`gra3dShadow.c`~~ (done 2026-10-16 as verify, with `graphics/graph3d/g3dRenderTarget.cpp` partial: see "Open questions from gra3dShadow.c").
   Picture: Rei standing in the room, then walking with the scripted pad (marks player.c, plyr_sub.c,
   unit_ctl.c done).
6. After that, by what the in-game screenshots reach: the HUD and camera (finder, photo mode: many
   verify TUs), NPCs, enemies, the in-engine scenes (scene.c; debug/scn_test.c holds phases 66 / 89),
   menus, save points, and the effects (effect_obj.c is partial with about 90 functions left: resume it
   when a room with candles or lamps draws; its draws also need the host 3D effect packet path, Open
   questions "No host draw path").

Title-side leftovers (not queued; take them when one blocks a picture): the black screen about 120
frames after coming back to the title from the Load screen (title_menu.c notes; the EE stops the BGM);
the item-hook printers B2dHookPrintMsg / B2dHookPrintNumber / B2dDrawSpriteDirectLinear
(0x2093c8..0x209610, stubs; inferred menu_item.c) used by the Video Mode screen (phases 6 / 18); the
option screens (phase 19's file `unassigned@00223e80`, 20-23 before it); the PCM stream run
0x26fd50..0x270240 (FF2 snd_pcmstream.c; SndPCMStreamInit / Main stubs); `ee_iopQueryInit`
(ee_iop_q.c); mc_make_file.c (mc_icon.c calls it). The renderer colour clamp is under "Decisions
waiting for the user".

## Queue empty: the dead g3d tool files (2026-10-22, eeFile.cpp)

`tus.py next` found nothing (queue empty, frontier empty, 0 stubs). The todo TUs left with code are FF2's g3d
tool classes, all unreferenced in FF3 (ghidra.py refs: no jal / j / data word to their ctors): eeFile.cpp,
eeFindFile.cpp, CDebugEdit.cpp, g3dBoundingVolume.cpp, plus the libstdc++ runs misnamed common/utility.c /
outgame/title_movie.c (not port targets). Taken: ~~`graphics/graph3d/eeFile.cpp`~~ (done 2026-10-22: CFile /
CStdioFile / CFileName, FileSplitPath, OnError; architecture.md). It cannot show on screen; a reverted host test
(MinGW, stub sceOpen) checked Open's flags (write + create -> 0x202, read -> 1), the strerror message, and
GetFileName / GetFileTitle / GetFilePath of "host0:x/y.bin" -> "y.bin" / "y" / the path.
- Decided: the libstdc++ basic_string instances FF3 emitted (named string_* in Ghidra) are std::string on the
  host; they are listed with their addresses in a comment block at the end of eeFile.cpp, not reconstructed.
  The same rule fits the vector<string> / vector<sce_dirent> instances eeFindFile.cpp brings.
- ~~`graphics/graph3d/eeFindFile.cpp`~~ (done 2026-10-22: CFindFile, 12 members + the vector / sort instances
  listed in a comment block; architecture.md). A throwaway host test (MinGW, stub sceDopen / sceDread) checked
  IsMatchSpec ("*.bin" no case, "zz.*", "room*.pk2" vs "hall.pk2", "." never), the '|' spec split, the sorted
  match list and the sub-directory lists. Its PC guards: bounded copies in the inlined stricmp, 256-byte dot / OR
  buffers (2 on the EE, "a..b" overflows them), sscanf of the extension from a copy (the EE scans it in place).
- ~~CDebugEdit.cpp with unassigned@0010afa8~~ (done 2026-10-22: IEditObject / CEditRoot, 0x10afa8..0x10b788 +
  linkonce 0x2ab6c8..0x2abdf8; architecture.md). Ghidra's FF2 matches 'replace' (really string _M_mutate) and
  'CEditRoot::operator new' (really vector<IEditObject *>::_M_deallocate) were wrong; renamed, in overrides.json.
  Eleven inline virtual bodies 0x2abb68..0x2abc50 had no Ghidra function; created. Doubt: which ctor / dtor of
  each twin pair is the complete-object one is guessed (no callers); the vtable's slot 0 (0x10b380) is taken as
  the complete dtor.
- Next: g3dBoundingVolume.cpp. All dead code: finish them `done` after a build and callcheck, or better spend a run
  turning `verify` TUs into `done` with pad scripts (30 verify TUs; status.md lists them).
- Doubt: which of the CFile ctor twins 0x154df0 / 0x154e30 is the complete one is not decidable (identical
  bodies, no callers); the Ghidra names CFileDtor (0x154e70, the base-object dtor CStdioFile's dtors call) and
  CFileDtorBase (0x154ec8, the vtable's complete dtor) read the wrong way round; kept, the source says which is which.

## The sky dome's model classes, and the last stubs (2026-10-21, MapSky.c)

`tus.py next` found nothing (queue empty, frontier empty). The three stubs left in src/ were taken:
- FootSePlayId 0x1c8260 was already written in foot_se.c; plyr_sub.c (PlyrCrawlSeTimer) still called it by its
  old name FUN_001c8260, which kept a stub alive. Now FootSePlayId(FOOT_42_0_DOMA_4BAI_BD_BD, ...).
- PssModel::Build / DrawMdlPre and the rest of the dome's inline code 0x2ac590..0x2ac870 (PssModelBuilder,
  DummyTextureModelObject) joined `ingame/map/MapSky.c` (architecture.md). **src/ has no stubs now.**
- **Seen, with a wrong picture (partial).** Nothing on a scripted path runs the map-sky effect (only the
  tattoo event's dream look, TattooEvCameraMain state 4), so the warp tool has a new key: `sky=1` requests
  EffMapSkyReq at Rei after 6 s of play, `sky=2` keeps the dome on the camera (it is a hemisphere of radius 84
  seen from inside only, so from the map camera sky=1 shows nothing). With
  `python build.py --run 75 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS --warp chapter=2,sky=2` the dome draws
  over the top of the bedroom (a band with its jagged rim, from about frame 1150), the sky movie loads, plays
  and MAP_SKY::MovieSendFrame answers a frame every draw (probed: playing 1, load 4, play 1), but the band is
  black (average 3/255). A reverted probe that sent the dome's own cloud TIM2 (tpk texture 1) to texture 0's
  TBP instead showed the clouds faintly in the band: geometry, UVs, light and TIM2 texturing work. **Next: the
  movie frame does not reach the 3D bridge's texture.** MovieSendFrame writes the decoded frame through the
  path3 chain at the TBP of the dome's texture 0 (160x128); the title backdrop reads the same kind of upload
  through DispSprD and shows, but reipan_graph3d.cpp's model textures come from TIM2 / TRI2 sends
  (MakeTim2Direct, Tim2PackSend, gra3dLoadTRI2FileToVRAM) and do not see it. A host-side fix (let the bridge
  sample the GS memory at that TBP, or have MovieSendFrame tell the bridge the texture changed) is platform
  work; the movie content itself was not checked (SCENE9100M..9108M_PSS).
- **Corrected 2026-10-22: the movie frame does reach the dome; the black is the light.** Probed with the
  sky=2 warp: the renderer's direct movie path is armed at the dome's TBP 0x2dc0 (texture 0 is 160x128
  PSMCT32, TW 8 TH 7), every dome draw's TEX0 goes through IsVideoTex0 -> GetVideoTexture, and the
  decoder publishes 160x128 frames (grey clouds, average 6..56 of 255, alpha 255). No host fix is needed.
  The band is black because PssModel::DrawMdlPre lights the dome with the player's area light, and in the
  bedroom (area 47) that set has ambient 0.04 and no directional lights (0 / 23 point / 7 spot): a ~50/255
  picture times 0.04 is ~1/255. A reverted probe forcing ambient 1 showed the movie's clouds in the band.
  So the picture can only be judged in the tattoo event's own area and light (TattooEvCameraMain state 4),
  which no scripted path reaches yet; MapSky.c is `verify` until a session sees that event. Still open:
  which area that event runs in, and whether its light set is bright enough to show the sky as FF3 does.
- Doubt: MAP_SKY_DATA::Draw builds the matrix with w = pos[3] + 1; the warp passes w 0. What the tattoo event's
  vector holds in w (TATTOO_EV_WRK +0x40) was not checked.

## Queue empty, the last partial TU closed (2026-10-21, common/utility2.c)

`tus.py next` found nothing; the only partial TU, common/utility2.c, was finished (done; architecture.md
address map). Ghidra had swallowed eight helpers (RingBufAdd .. SetPrintAssert into GetExtension,
RotLimitChk2 into GetNameWithoutPath); created and named. Doubt: FF3 keeps two empty variadics where FF2
had three reporters (PrintWarningReal, SetAssertPreMessage, PrintAssertReal); 0x268cf8 / 0x268d20 are
named PrintWarningReal / PrintAssertReal by order only, nothing references them. The IOP copy's rename
block (utility2i.h) now covers every helper, so the EE and IOP copies link side by side.
**Next, from here**: no todo TU with functions is on any game path. What is left: graphics/graph3d/eeFile.cpp
(0x154df0..0x155d58, CFile / CFindFile / CFileName, called from 2 unknown sites) and eeFindFile.cpp,
CDebugEdit.cpp, g3dBoundingVolume.cpp (fuzzy, small); the libstdc++ runs misnamed common/utility.c /
outgame/title_movie.c (not port targets); 3 stubs (FootSePlayId, PssModel::Build / DrawMdlPre). Better use
of a run: turn verify TUs into done with pad scripts (the story menu rows, open-items "The story menu top").

## Queue empty again: the stubs every boot reaches (2026-10-21, sleep_movie.c)

`tus.py next` found nothing (pinned queue done, frontier 0, one partial: common/utility2.c). Re-assessed
from the stubs a `--run 100` reaches; all of them fire at boot from save_data.c's getter table (data
0x332b1c), so they come first:
- ~~FinderBankWrkInit 0x25fd38 / FinderBankMoviePlay 0x25fd68~~ (done 2026-10-21 as verify: their own
  FF3-only file `ingame/sleep_movie.c` 0x25fce0..0x25fdb8, FinderBankSaveGet moved there from
  unit_ctl.c; architecture.md). Seen when a run reaches WAKEUP_MOVIE_PLAY with a movie set (event 2,
  the dream's exit choice, after event 5's SET_WAKEUP_MOVIE 27): the chapter-select starts of
  `--warp chapter=N` (events 91 / 438) skip it, so no scripted run shows it yet.
- ~~Three save-block getters still stubbed inside TUs marked done~~ (done 2026-10-21: IngameWrkSaveGet,
  TitleMenuItemOpenSaveGet 0x1cfeb0, PlyrWrkSaveGet 0x2367f0; host size like EvExeCtrlSaveGet, FF3's own
  PlyrLoadRestore repairs the saved pointers; seen by a snapshot round trip: `--warp chapter=2,tp=0,snap=1`
  saved Rei in the living room, `--snapshot <that .rps>` in a new run put her back on the same spot with the
  cat. No save callback is a stub any more, so a card save is no longer cut short at a stubbed block.
  reipan_warp.cpp's SnapExtra tail now duplicates blocks 27 / 28; it stays until a load is seen to place her
  from block 28 alone (drop it then; old .rps files stopped loading anyway, the block hash changed).
  Next, from here: the queue is empty; stubs left are FootSePlayId 0x1c8260 and PssModel::Build / DrawMdlPre
  0x2ac5f0 / 0x2ac6a0 (none fires in `--run 100`), partial common/utility2.c; re-assess from "Next up".)
  Old note: they were stubbed inside TUs marked done, so `tus.py next` never picked
  them (pick() skips done; queueing a done TU does nothing). They return size 0 and leave *out unset,
  so a memory-card save drops those blocks and a load restores nothing into them:
  IngameWrkSaveGet 0x1cfd40 (block 27: ingame_wrk 0x327560, 0x4c) and FUN_001cfeb0 (block 0: the
  title-menu item-open flags 0x3eb368, 0x10), both in ingame/ingame_wrk.c; FUN_002367f0 (block 28:
  0x32f4a0, 0x9f0, the player work) in ingame/plyr/player.c. Each is two lines on the EE, but
  INGAME_WRK holds a pointer (plyr_wrk at 0x2c) and is 0x4c only on the EE, so the host needs a
  decision on how the save image is laid out (copy EE-sized fields, or fix the pointer up on load):
  look at how save_data.c treats the other blocks with pointers before writing them. To work on them,
  claim the TU by name (`tus.py claim ingame/ingame_wrk.c`), or ask the user to set its state back.
  Other stubs left: FootSePlayId 0x1c8260 (foot_se.c), PssModel::Build / DrawMdlPre 0x2ac5f0 / 0x2ac6a0
  (map sky, unassigned_stubs.c); none fires in `--run 100`.

## The story menu top, phase 57 (2026-10-21, story_menu_top.c)

With the queue empty the unwritten phases were re-assessed (gphase.c nullptr: 31 EV_MISSION_START,
42, 57). 57 came first: every story_menu_*.c page returns to it and it was the only way into them.
Done: `ingame/story_menu_top.c` 0x20b590..0x20c400 (architecture.md). Seen in play with
`python build.py --run 320 --shot-every 20 --pad <settings.smoke_pad>,5400:START,5650:DOWN,5750:CROSS,5950:TRIANGLE,6150:TRIANGLE`:
START in room 2 opens the menu (~frame 5420) over the darkened capture and the MENUBG backdrop: "Menu", rows Map /
Tools / Photos / File (no camera item 8, no memo yet: the rows close up), the cursor bar, X Confirm / Triangle
Return, the play time 0:02:05 ticking, 0 pts., the HUD (Rei, HP), the help line ("You cannot use this menu
right now." on Map: lock 0x1b is on in the opening). DOWN -> Tools and its help line; CROSS opens phase 59
(Tools, Key Items: Professional Camera); TRIANGLE back plays menu4-back; TRIANGLE leaves to the game (room 2).
Marked done from the same shots: ingame/story_menu.c (phase 37), ingame/menu/menu_bg.c (the backdrop),
common/cmn_time.c (the play clock).
- **Next for the menu (verify TUs now reachable in play)**: the Photos row (60, story_menu_film.c), File (62,
  menu_file.c / menu_file_list.c), Map (58: buzzes while lock 0x1b is on; open it later in the game), the camera
  (64, needs item 8) and the ghost list (63) rows. Each needs only a pad script; mark them done from the shots.
- Not seen: the map row's buzzer (OK on Map while locked), START as the way out (cancel was seen), a menu with
  more than four rows, hours >= 10 (two digits).
- ROM quirks kept: init sets MenuWdwSetKeepOpen(1) but the end clears LayoutMsgSetKeepOpen(0), not the MenuWdw
  one; the end always plays menu<n>_cancel, also on the way into a row. MENU_YESNO (0x20b658..0x20b878) and
  MenuCsrObjSetListPos (0x20b590) have no caller in the ROM (no jal, j or data word); written, never run. MENU_YESNO's
  OK plays SE 0x1a9b0001 and its cancel 0x1a9b0003 -- the other way round from the menus.
- **Left of the unwritten phases**: 31 EV_MISSION_START was written 2026-10-21 (`ingame/mission/mission_start.c`,
  verify) and ~~42~~ (done 2026-10-21: `ingame/story_game_clear.c` 0x258a68..0x258b20; every phase in gphase.c is
  written now). Seen with `python build.py --run 215 --shot-every 30 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1500:START,1600:START,2400:CROSS,2900:CROSS,3300:CROSS --warp chapter=2,clear=0`
  (`clear=E` is new: GAME_CLEAR_ with ending E after 6 s of play): the ending movie, START skips it, the results
  page (Normal, rank S, Night 0, 00:00:07, 0pts., 0/ 33), the reward list (Extra Mode, Ghost List expanded, Mission
  Mode, Gallery Mode, Costume Option, Hard), the second list (Ending 1 is now accessible), then phase 43's Clear
  Menu (Save / Album / Return to title). Marked done from those shots: GameClear.c, ClearMenu.c, outgame/sel_menu.c,
  common/cmn_util.c. Not seen: the other ending (clear=1, movie 0x16), Clear Menu rows (save / album / title confirm),
  a mission clear / failed (modes 4 / 5 skip the sequence). The other todo TUs are mostly not port targets: the
  "common/utility.c" / "outgame/title_movie.c" inferences over 0x260000..0x268000 are libstdc++ (locale,
  exceptions, basic_string) with wrong FF2 matches (TitleMovieInit, TitleMenuDispMain, GetDistV ...).

## The queue ran dry: the puzzles are what is left (2026-10-21, kamon_pzl.c)

`tus.py next` found nothing (pinned queue done, frontier empty); 21 stubs remain in src/. The largest
unported in-game block is 0x244000..0x24c000, whose link-order names are wrong: the "savepoint*",
"save_load_disp.c" and "roku_pzl.c" TUs hold FF3's puzzles (puzzle.c's table 0x361600 points into them).
Done: the kamon puzzle, three files ingame/puzzle/kamon/kamon_pzl.c / kamon_draw.c / kamon_board.c
(0x248658..0x24a6c8, architecture.md). Left, by the same pattern (open / close / has-item, a phase with
children, a board class after the phase code, each file ended by a GPhaseCtrl atexit thunk):
- ~~KUSI (type 2, phase 72 children 91..94)~~ (done 2026-10-21: ingame/puzzle/kusi/kusi_pzl.c / kusi_draw.c /
  kusi_board.c, 0x245d38..0x248658, architecture.md; board 0 solved on screen with
  `--warp chapter=2,puzzle=2,pidx=0,item=43`: two rules pages (CROSS at reads 1350 / 1600), then DOWN RIGHT RIGHT
  DOWN CROSS LEFT CROSS (stone on 8 -> 7), RIGHT UP UP CROSS DOWN DOWN CROSS (2 -> 5 -> 8), UP LEFT LEFT UP CROSS
  DOWN RIGHT RIGHT (0 -> 3 -> 4 -> 5), "It seems the door has unlocked....", back to the bedroom). Not seen: boards 1 / 2
  (pidx=1 / 2, items 44 / 45), the retry (circle, phase 94), undo by cancel (phase 93 backwards), the quit prompt
  0x75/2. The cursor wraps: LEFT from cell 0 lands on cell 2 (cell 1 is not on board 0). **All five puzzles
  are now ported**; the queue is empty again -- re-assess from "Next up" (11 stubs left in src/, partial
  common/ol_load.c and common/utility2.c).
- ~~KAZA (type 0, phase 73 children 95 / 96)~~ (done 2026-10-21: ingame/puzzle/kaza/kaza_pzl.c / kaza_draw.c /
  kaza_board.c, 0x24a6c8..0x24b490, architecture.md; both boards seen: solve, quit prompt, out of moves).
  Seen in passing (puzzle.c, not KAZA): the first frame of phase 49's cross-fade (shot 1158 of the
  warp run) shows the screen capture with its lower half smeared into vertical stripes, for one frame
  only; look at it if a puzzle open ever looks wrong on a real play.
- ~~SIZUMEDAMA (type 3, phase 75 children 100..103 + 116 / 117)~~ (done 2026-10-21: ingame/puzzle/sizumedama/sizumedama_pzl.c /
  sizumedama_draw.c / sizumedama_board.c, 0x24b490..0x24cf18, architecture.md; solved on screen: placing, take-back prompt,
  single-clip playback with its subtitle, the full song, the clear message, back to the bedroom). Not seen: the quit prompt
  (TRIANGLE in phase 100), SQUARE playback of a placed stone (the 'Listen to a song' guide shows). KUSI is the last puzzle.
- ~~ROUSOKU (type 4, phase 76 children 104 / 105)~~ (done 2026-10-21: ingame/puzzle/rousoku/rousoku_pzl.c /
  rousoku_draw.c / rousoku_board.c, 0x2447b0..0x245d38 + 0x2be128..0x2be570, architecture.md; board 0 solved on
  screen with the clear message, quit back to the bedroom). Not seen: boards 1..3 (pidx=1..3), the quit
  prompt (TRIANGLE), the flare's height jitter (GetRandValF(0.95) + 0.1 on top of the table: looks large,
  kept as the ROM has it; judge it on a real play).
`build.py --warp ...,puzzle=T[,pidx=I][,item=N]` (new, reipan_warp.cpp) opens any of them after 6 s
of play, as PUZZLE_START does; script the pad after about read 1150 (the rules message is up first).
The 6 s are wall-clock time and the pad counts reads, so the frame the puzzle opens at drifts by a few
hundred between runs (hazards.md "A warp's puzzle opens at a different frame each run").
Doubts: the kamon fail path (out of moves: label "out", message 0x76/3) was not seen on screen; the
pieces not on the board (piece[2] on the ROM's board) stay unset, as in FF3.

## The album from the title menu (2026-10-21, album_edit.c)

With the queue empty, the unwritten phases were re-assessed: phase 8 (the title menu's third item, Album) and its children were nullptr in gphase.c.
Done: `album/prg/album_edit.c` 0x1229a0..0x125318 (phases 8, 24, 25, 26, 52, 54, 78; architecture.md). Seen on screen with
`python build.py --run 175 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1450:CROSS,1750:CROSS,1900:RIGHT,2000:CROSS,2150:CROSS,2300:CROSS,2650:SQUARE,2800:DOWN,2950:CROSS,3250:CROSS,3450:TRIANGLE`:
New / Load for the top row, the cover choice (cover 2 on the top album), the bottom row, the edit page (Copy / Delete / Menu / Return),
the menu with only Load / Help lit (no photos), the Help viewer (two pages), the load confirm (No), the leave confirm, back to the title menu on Album.
Not seen (no photos in an album opened from the title until the card load works): copy / overwrite / protected messages, erase,
Album_Sort, the Enlarge row. **Next for the album** (queued): the phase 27 / 28 glue is
`album/prg/album_phase.c` since 2026-10-21 (verify): Album_Open's Load (bottom row, RIGHT, CROSS) now opens phase 28 and the load page
(ALBM_SENTAKU frame, cursor, five empty rows, info strip) comes up, then sits there: AlbumLoadCtrlInit / AlbumLoadMain / AlbumLoadEnd
are stubs in album/prg/album_load.c (AlbumLoadMain returns 0, so the page never closes: the album's Load is a dead end until
album_load.c is written; it is queued). Pad to see it: 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,
1450:CROSS,1750:CROSS,1900:RIGHT,2000:CROSS,2300:RIGHT,2450:CROSS with --run 170. Then the zoom page phase 53 + 79
~~`unassigned@00126738`~~ (done 2026-10-21 as `album/prg/album_zoom.c`, verify: phases 53 / 79 / 106; its view run
0x12e798..0x12f470 became `album/prg/album_zoom_view.c` on 2026-10-21 (verify: the kakudai page with the kind's mount papers DAISI1 / DAISI2),
and both are unreachable until an album holds a photo: Album_Menu row 0 needs AlbumMenuCondHasPhoto), then `album/prg/album_load.c` (FF2's machine;
its inferred range 0x125318..0x126738 is split with a 4-function fuzzy `system/b2d/album_load.c` run that holds AlbumLoadMain
0x1253b0: define album_load.c over the whole range first). The verify TUs album.c / album_disp.c / album_view.c now show their
page, cursors, menu and help lines; mark them done when a run shows photos (decoder, info lines, page turn).
**album_load.c done 2026-10-21 (verify)**: 0x125318..0x126738 defined whole (the fuzzy system/b2d/album_load.c run is absorbed). Seen with the pad above plus 2800:CROSS,3200:CROSS,3500:TRIANGLE and --run 210: the slot window with message 0xc (which slot to load from), CROSS on slot 1 reads the host card and reports 0x2d (cannot find album data in slot 1), CROSS back to the slot select, TRIANGLE closes the page and phase 28 returns to Album_Open. Not seen: the album list (steps 4..7), Load now? (10/11), the load (12/13) and Load successful (16) -- they need an album on the card, i.e. phase 27 (Album_Menu Save, needs a photo) or a card image with album data. An unformatted card is not offered a format on load: it reports 0x2d like an empty card (FF3 as decompiled).
Doubt: AlbumKeyCsrInit writes five rows into FF3's three-byte list, clobbering album_conf_default / album_conf_pos with -1 (unobservable;
the host list has five rows). Phase 0x73 (StartMenu_MissionSave) as the album's return phase skips LayoutAnimMain in Album_Help, so the
album presumably opens from the mission save too; not traced.

## Open questions from start_menu_top.c (2026-10-21)

- **Seen only through a probe.** The start menu opens from Story_Start only once a game has been cleared
  (FinderStateHasAny), which no smoke run or warp reaches. A reverted probe (FinderStateHasAny returning 1,
  the boot pad to 1600:CROSS then 2000:UP, 2150:CROSS, 2400:TRIANGLE, 2600/2660/2720:DOWN, 2850:CROSS,
  3000:DOWN, 3100:CROSS, 3250:RIGHT, 3400:CROSS, --run 170) showed phases 80, 107, 108 and 118 right against
  SETUP_PHF's textures: the seven "Setup" entries with the cursor bar and each entry's description, the
  difficulty list (Easy / Normal open), the costume grid (Rei / Miku / Kei / Ruri, Normal / Nothing, Ruri's
  costume cell empty), the choosing frame with arrows in 118, and back to 108
  (progress/ shot of this commit: the costume page). Status done on that probe.
- **Not exercised**: OK on Start game / Continue (Story_Start with step 2: IngameLoadRestore; a new game
  from the start menu runs IngameNewGameReset, MapAreaDataLoad, FurnSaveLoad, EventDataLoad(0) first), the
  Return prompt (phase 12, message (0x6a, 8), yes -> Title_Menu 17), and costume choices with bought slots
  (a fresh game owns none, so 118 lists only entry 0) -- StartMenuCostumeApply then calls PlyrSetCostume /
  PlyrSetAcs / CatSetAcsNum with them.
- ~~**Idea**: a warp option that marks a clear~~ (done 2026-10-21, the user's request: `--warp cleared=D` alone, or
  the ImGui "Game clear" section's "Treat the game as cleared"; reipan_warp.cpp MarkCleared / KeepCleared put one
  clear at difficulty D into FINDER_STATE_WRK at the title and again in Story_Start after IngameInit resets it).
  With the boot pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1300:LEFT,1450:CROSS,1600:CROSS and
  --run 80, Hard shows on the difficulty select and New Game opens Setup (SETUP_PHF: Start game, Continue greyed,
  Mission, Costumes, Extras, Gallery, Return); start_menu.c marked done. The pages (shop 109, gallery 82, missions 83)
  now need only pad scripts.

## Open questions from story_menu_film.c (2026-10-21)

- **Seen only through a probe.** Phase 60 is reached from the story menu's top list (57 StoryMenu_Top, not
  written: `gphase.c` has nullptr there, start_menu_top.c's neighbour). A reverted probe (one_Story_Menu
  requesting 0x3c instead of 0x39, the smoke pad plus 5250:START after the room-2 photo, --run 270) showed the
  menu right: title, button captions, the large photo, room "Hall with Tatami", the date, the 16 slots with the
  small picture in slot 1, the sort menu on square (progress/20261021-0925_ingame_story_menu_film.c_probe_*.png).
  Mark done when 57 is written and a run opens the film entry.
  (2026-10-21: 57 is written, story_menu_top.c. In a fresh game the rows are Map, Tools, Photos, File, so the
  pad below with 5650:DOWN,5700:DOWN,5750:CROSS instead of 5650:DOWN,5750:CROSS opens Photos = phase 60 in play.)
- **Score missing**: item 0x472 prints through B2dHookPrintNumber 0x209470, a stub (unassigned@002093c8); the
  "Pts" caption shows with no digits. (2026-10-21: written in menu_bg.c and seen printing the video-mode
  countdown; the score digits still need the probe above to be seen.)
- Not seen: the delete question / "protected" / "no photos" windows (LayoutAnimStart 5 / 3), the subjects list
  (the probe photo had none), a re-sort's effect on the slots.
- Kept ROM oddities: FilmCsrSetPos wraps by w + h (10 / 4), not w * h -- only the sort cursor is set that way,
  from rows 0..2, so it never matters. The static initializer stores PHOTO_PHF + GetLanguage() at start-up
  (0 then: SYS_WRK's ctor sets language id 2) and FilmGridCreate adds GetLanguage() again; the port's table
  holds the base files, the same result.
- Pad bits as named in the code: 0x20 circle (delete), 0x80 square (sort) -- the button captions in the
  picture confirm both (cross Protect, circle Delete, square Menu, triangle Return).

## Open questions from photo_comp.c (2026-10-21)

- ~~The photo storage needs the unassigned run 0x229990~~ (done 2026-10-21: defined as
  `system/compress/compress.c` 0x229990..0x22a928 and ported from MioPan, verify). The --run 250 photo
  (read 4900) takes the DCT path, so CompressFile now runs on the real capture; a host round trip of a
  game frame through CompressFile / ExpandFile decodes it with a mean error of 0.9 (quality 1) to 1.6
  (quality 4) per channel. Still unseen: a decoded photo on screen (the album / save point viewer,
  UncompressData type 1 -> ExpandFile x 30). The first session whose screenshot shows a stored photo
  marks compress.c done.
- photo_comp.c is a made-up name: FF3 split FF2's photo_make.c in two objects (the drawing half kept the
  name at 0x22ada0). Any string or symbol naming the real file would settle it.

## Option screens 20 / 22 / 23 (2026-10-21, option_system.c / option_sousa.c / option_button.c)

Done by picture: the inferred common/ol_load.c range 0x222640..0x223e60 was three option screens (the FF2
ol_load matches were their atexit thunks and help getters). Seen with python build.py --run 150 --shot-every 30
--pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1350:RIGHT,1450:CROSS,1650:CROSS,
1850:RIGHT,1950:DOWN,2000:DOWN,2050:DOWN,2100:RIGHT,2200:CROSS,2350:DOWN,2450:CROSS,2600:RIGHT,2700:CROSS,
2850:DOWN,2900:DOWN,3000:CROSS,3150:SQUARE,3300:RIGHT,3400:CROSS (Settings with vibration toggled and the
effects knob moved, Game play 3rd -> 1st person, Controller Type A field / finder view, Type B).
~~Phase 21 Brightness~~ (done 2026-10-21 as outgame/option_akarusa.c 0x222170..0x222640, OPTION_AKARUSA_PHF:
the slider over option byte 0, 12 levels 0..11, the sample picture under BrightnessAdjustmentFilterDraw at the
chosen level, five help lines 0x65/0x32..0x36). Seen with python build.py --run 170 --shot-every 30 --pad
100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1350:RIGHT,1450:CROSS,1800:DOWN,1900:DOWN,
2000:CROSS,2300:LEFT*150,2550:RIGHT*400,3050:CROSS,3300:CROSS,3600:LEFT,3700:TRIANGLE (default, darkest with the
picture nearly black, brightest, OK, reopened at 11, cancel). The filter strength at the ends has no PS2
capture to compare with. All four option screens are written; phase 12 (Default Settings prompt) was already.
Not seen: the cancel path's restore (option_cancel), the Mono choice, BG music volume, Type C, Game play rows
1..3 changed.

## Open questions from option_top.c (2026-10-21)

- The option screens 20..23 sit before option_top.c, one file each, every one ending with a 7-instruction
  GPhaseCtrl atexit thunk the FF2 matcher misnamed after ol_load (0x223e40 ol_loadQueryTotalFreeSize ends
  phase 20's file 0x223558..0x223e60; 0x223558 ol_loadQueryMaxFreeSize, 0x222d30 ol_loadGetHeap and
  0x222620 SetSdwSrcDrawFLG look like the thunks / getters of the files before). Each file also has a
  "MenuCsrGetPos(id) + n" help-line getter (0x223578, 0x222640 ol_loadHeapInit = wrong match). The inferred
  common/ol_load.c over that range is wrong: define the four screens as outgame/option_*.c (phases 20
  System Settings 0x223558.., 21 Brightness 0x222170?.., 22 Game Play 0x222620?.., 23 Controller
  0x222d30?..) after checking each thunk. Phase 21 has its screen draw in InitDrawFLG / 0x222208
  (BrightnessAdjustment...), a wrong FF2 name too.
- To see the option screens: python build.py --run 110 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1350:RIGHT,1450:CROSS,1800:DOWN
  (title menu -> Option at read ~1450, the top menu from shot 1500 on; add CROSS on an entry to open
  its screen; phases 20..23 are still nullptr in gphase.c, so opening one shows nothing yet).
- OptionTopGetMsgNo is static in option_top.c (both callers are phase 19); FF3 has it global.

## Starting elsewhere: the warp tool and what it showed (2026-10-19, reipan/debug/reipan_warp.cpp)

- The ImGui Debug -> "Start point / snapshots" window (and `build.py --warp` / `--snapshot` for smoke
  runs) starts a new game in any chapter / area / world, teleports within play, saves and loads
  snapshots, opens or starts events and shows the cat's work. A chapter warp is FF3's own chapter
  select: the chapter event's first sub-event runs (architecture.md "Story structure"). Use it to check
  code the --run 100 path never reaches (the house, later chapters).
- **The house and the cat are on screen**: `python build.py --run 75 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS
  --warp chapter=2,tp=0` wakes Rei in her bedroom (area 47, event 438), then teleports her beside cat
  label 0 in the living room (area 48): Miku on the sofa, the cat (ch015 + ch019) on the sofa back or at
  labels 1 / 4, its `[warp] cat` log line active / loaded / visible. cat_ctrl.c's "first shot" is met;
  its motions, the pet act, the door follow and the event spot are not checked yet.
- **Puzzles**: `--warp chapter=2,puzzle=1,item=46` opens the kamon puzzle after 6 s of play (any
  type / board / item: build.py's docstring); the run that solved it is in kamon_pzl.c's notes.
- **Card saves do not load back yet**: SetMemoryCardSaveDataToBuff stops at the first block without an
  address (FF3 does too), and 12 of the 61 save callbacks are still stubs -- block 3 FUN_001b0780 first,
  also 4 FUN_001b3398, 10 FUN_0019ca60, 17 OptionWrkSaveGet, 21 / 22 FUN_001d8208 / 8220, 26 FUN_00257ac8,
  27 IngameWrkSaveGet, 28 FUN_002367f0 (plyr_wrk), 41 FUN_0025b738, 44 FUN_0021a640, 56 FUN_00237858.
  A card save holds blocks 0..2 with the checksum after them; the load screen calls it damaged.
  Snapshots skip empty blocks instead and carry blocks 27 / 28's essentials themselves until written.
- **A game that starts in place draws nothing until an area change**: MapAreaChange's MapAreaMhSet
  (the player's map hit object and floor) and MrecSetCameraSet (the area's camera set) are what make the
  room draw; a new game gets them from its start event's AREA_CHANGE, a card load from the player block
  (28, a stub). A card load or a warp with events stopped stays black (film grain over black, fade 0,
  phase 32) without them; the warp tool calls both (EnterArea) after Story_NowLoading.

## ~~The flashlight lights behind Rei~~ (the user, 2026-10-19; fixed 2026-10-20)

- Fixed 2026-10-20: the host spot kernels follow the microcode now (cone along -vDirection). See Done
  and hazards.md "A MioPan convention on top of a ROM-faithful port". Left open: the flashlight shadow
  (gra3dShadow.c "No shadow is cast yet") was not seen, so its direction is unchecked; _CalcDirection
  takes a spot's direction from its position, not vDirection, so the fix should not touch it.

## The first door taken in game (2026-10-19, the user's play under gdb)

- The user played the fixed build with host input (`tools/reverse/gdbrun.py`) and took a door: no crash.
  Before the fix the game died right after the door in `MotionModelBuilder::BuildTexture` (null SGD):
  the first enemy to load there (slot 0, type 2 auto enemy id 5) had an all-zero ENE_CMN row because
  the enemy tables were zero placeholders, so CharModel::Load asked for file 0. Fixed by extracting
  them (enedat.py); the route table 0x323ff0 also grew from 16 to 25 entries.
- **Fixed 2026-10-19 (`fix/second_room_camera.c`), two separate hangs:**
  - The frozen camera: rs101's door 106140 (floor 11, x -55..-25, z ~735, facing +z) leads to area 11
    (rs205's corridor, Rei at (147, 0, 750)). Walking -x down it starts event 164: EV_CAM 1, a fixed shot
    through the lattice, GHOST_APPEAR 2 3 (the white-kimono ghost ch029 behind the lattice), EV_STOP 60,
    EV_CAM 0. GHOST_APPEAR waited on EneGetState == 2 for ever: enemyIsReadyData ANDs in
    EneFlyModelInitWait (fly_ctrl.c), a stub returning 0, so the event never reached EV_CAM 0 and the
    view stayed on the event's shot while Rei walked on. Written: GetFlyWork, CheckEffectFly, FlyAct,
    EneFlyModelInitWait (both pass type-2 enemies at once, as FF3). Now the map camera takes over and
    follows Rei along the corridor (progress/*_fix_second_room_camera.c.png).
  - The photo: PlyrFinderShot requests phase 33 (Story_Photo); its init / one / end were nullptr, so a
    shot froze the game (black screen). Written as ingame/photo/photo_dat.c (0x2274e0..0x228718, the
    whole photo sequence): a shot now flashes and returns to Story_Normal. The developed frame, hint
    picture and album encode are not drawn yet (their helpers are unassigned stubs), and FUN_0023c9b8
    (the shot's record, a stub) never calls PhotoSeqStart, so the sequence runs on a zeroed work:
    the first photo takes the no-subject path (flash, hold 0 frames, capture, 10 end frames); later
    photos start at step 14 and end at once.
- Follow-ups (not queued; the user's call): (1) ~~the shot TU 0x23c028..0x23e260~~ (done 2026-10-19 as
  `ingame/plyr/plyr_shot.c`, verify: PlyrShotRecord now calls PhotoSeqStart with a real definition, so a
  photo of nothing runs flash -> developed frame -> hold -> viewfinder, frames 4910..4980 at --run 250); (2) the photo frame
  drawing 0x22b1a0..0x22cxxx and the encode 0x228770..0x2289a8 (unassigned); (3) the rest of
  fly_ctrl.c (FlyModelDataInit 0x1bf480 and the model object 0x1bf148.., FlyRule, EneFlyAct...: ordinary
  enemies with fly types still never get ready); (4) ~~phase 45, the peeping view~~ (written 2026-10-21 as
  `ingame/photo/photo.c` 0x226138..0x2274e0, verify).
- The door TUs (story_door.c, MapDoor.c, MapDBuff.c, DoorStat.c) ran in game without a crash, but no
  screenshot of the door was checked, so they stay `verify`. Checked 2026-10-19 with `--shot-every 10`:
  the smoke pad takes door 106140 at read ~3800, the screen fades out (3810..3840) and in on room 2 (3860);
  no door model or door camera is drawn in between (MapDBuff.c's model handle FUN_002035d0 is a stub), so
  they stay `verify`; MapLoad.c (room 2 loads behind the door and draws) was marked done.

## Open questions from man_data.c (2026-10-20)

- **CMapRec.view: x1/y1 or w/h?** CMapScreen::Draw (0x1f87b0) stores the window item's x, y, x + part 3,
  y + part 4 at view[0..3] and scissors to them as x0, x1, y0, y1; MarkDoor.c (CMarkDoor::Draw) treats
  view[2] / view[3] as width / height (view[0] + view[2]). Either FF3's MarkDoorDraw 0x10f3e8 really does
  that (a ROM quirk: the clip window is then too big, harmless) or MarkDoor.c misread it. Check the
  MapRecGet() loads in 0x10f3e8 (s5 there is the mark record, not the rect).
- **SetHelp (0x1f83d0) has no callers**: "help_on" / "help_off" on the MAPMENU layout; written as a member.
- **Room picture**: Draw sets MENU_PIC.pic = row->snap_no + RS101_R01_TM2 for a visited (flag 0x10) row,
  else -2 (blank); confirmed on screen (rre02_r1.tm2, the living room).

## Open questions from ev_timer.c (2026-10-20)

- Seen nowhere yet: the only EV_SET_TIMER users are events 291 (chain 288 / 246 / 244), 318..320 and 495 (chapter parts under 88), not on the smoke path. evdump prints them as [0, 0, 117] and [0, 0, -1]; EvEvSetTimer passes (args[2] timer, args[1] req_event_id, (u_char)args[0] state), so 117 is frames and -1 is a deadline one tick behind the counter: it fires only after the 400,000,000-frame wrap, i.e. a timer parked for ever (ROM arithmetic, kept). req_event_id 0 / state 0 look odd for a real target; check the argument order against the script when the first of these events runs (the --warp tool can start the chapter).
- 2026-10-20 re-assessment note: the pinned queue was empty again (its six unassigned@ keys are all defined), and tus.py next took unassigned@001106d8 (menu camera, 0 reached stubs) by frontier score; this run took ev_timer.c instead because its stubs were the reached ones on the in-game path. Remaining reached stubs in a --run 200: SndPCMStreamInit / Main (unassigned@0026fd50), Vu0InitConstRegisters (unassigned@001e6ae8), FUN_001cfeb0 / IngameWrkSaveGet (ingame_wrk.c), OptionWrkSaveGet, FinderBankWrkInit, FUN_002367f0 / FUN_00237858, PlyrFlagInit, MenuRefCtrlInit, FurnPhotoFlgClear, AlgManageFlagClear, FUN_001ecd40.

## Open questions from mission_mode.c (2026-10-20)

- Nothing reaches mission mode in a smoke run (title start mode 2 needs the Mission entry of the start menu, opened by
  a clear): mission_mode.c, mission_list.c, mission_end.c, mission_start.c (phase 31, the "Mission Start!" banner;
  reference work/ref/MISSION_START_PHF) and start_menu.c's mission phases stay verify. A warp to
  an extra mission (FinderModeWrkStart + mode 2 + phase 29, as one_StartMenu_MissionMain does) would show the HUD
  (value top right at 563,24; time left at 563,47 when the mission has a limit).
- FINDER_MODE_WRK +0x20 (time_348) counts frames while PLYR_WRK +0x348 is 2; what _348 == 2 means (a battle
  state? PlyrBattleCheck clears it) is unknown, so mission type 6 is named after the field.
- FF3 0x2b90f8 (the shared int clamp) has static copies in puzzle.c and mission_mode.c; give it one home when its
  inline-instances file is defined.
- ingame/photo/finder.c / finder.h are gone (their two getters became FINDER_MODE_WRK::GetDef / GetRecord); the
  tus.json entry ingame/photo/finder.c has no ranges left.

## Open questions from story_night.c (2026-10-20)

- Phase 35 Story_NightChange is unseen: the scripted run never changes the world side (SET_NIGHT_FLG
  with args[1] != 0, or ScnTestIngameMain in IngameScene.c). A session whose run reaches a sleep scene
  or SET_NIGHT_FLG should check the fade, the room reload and where Rei stands (NIGHT_START_POS x / z are
  room-relative, y from MapAreaGetHeight), then mark story_night.c done.
- "Night" = side 1 is read from the keywords (SET_NIGHT_FLG maps args[0] 1 to side 1; PlyrMdlChange kind
  = side == 1); not confirmed which side is the dream world.
- NightStartPosSet's second argument (SET_NIGHT_START_POS args[4], in a1) is never stored by FF3; kept
  in the prototype. NIGHT_START_POS.floor is only zeroed, so MapAreaFloorChange always gets 0 unless a
  save block 44 carries something else.

## Open questions from scene.c (2026-10-20)

- ~~An engine scene would now hang (fod.c stubbed)~~ (done 2026-10-20: `graphics/scene/fod.c` written, all 20
  functions, verify: FodNextFrame advances / ends the scene, FodInit resolves the lights, the camera and light
  records are walked). No scripted run reaches an engine scene yet; the user playing past room 2 might. Mark
  fod.c done with scene.c when an engine scene (2 / 3 have cut lists) shows its camera moves and lit models.
  Doubts kept from the ROM, unseen: a directional light's direction gets the room offset added like a
  position (FodGetFirstLight / FodGetToSgLight); a selected point light's resident vDirection is zeroed;
  FodChangeFodLightToGra3dLight's lights land in FodLight whatever pGra3dLight it is given (FF2 the same);
  installing a light sets GRA3DLIGHTSTATUS word +0x08 (bEnableToShadow, an FF2 name) as well as bEnable.
- Unseen, so unchecked: SceneDrawManMdl's accessory order (PlyrAcsIsNot2), the man models' far-to-near sort
  (ascending sceVu0RotTransPers z, as the ROM), the aura alpha (amp * sin + alpha, cast to a byte as the ROM),
  the hand spot light's direction sense (ROM: 0 - normalize(pos - target), kept). Mark scene.c done when an
  engine scene (2 / 3 have cut lists) shows its rooms, models and camera moves and returns to the room.
- The ROM leaves deleted model pointers in the SCN_MDL rows (SceneManModelClear / SceneObjModelClear); the port
  nulls them (SceneCtrlRelease clears the slot right after either way).
- SceneGetHeaderData and SceneObjModelLoad / SceneManModelLoad copy names into dead stack buffers (FF2's debug
  prints) and SceneMdlAddSet drops its GetAlignUp: not reproduced.
- SCENE_HDR +0x10 (FF2 mirror_flg) and +0x00 are not read in FF3; SCENE_CTRL.count_flg (+0x27bc) is set, no
  reader seen; hdr_flg[2..3] not read.

## Open questions from scene_effect.c (2026-10-20)

- The scene effect TU is `verify`: the first scene0xxx.eff with screen records (an engine scene through scene.c's
  draw, or a movie with fades) shows it. scene0010.eff (the opening movie) has only three vibration records (movie
  frames 3225.. x 1.2 on PAL), so the smoke run cannot see it; a pad log of vib_small_time would.
- SceneEffectEneAuraRegist never writes pRet (+0x24) in FF3, so the ROM's AllDelete would ResetEffects heap
  garbage; the port zeroes it (PC: comment). Check whether heapCtrlMalloc zero-fills on the EE.
- SceneEffectFadeModelCtrlExec loops for ever on an unknown ModelType in the ROM (it never steps to pNext); the port
  steps on. Harmless unless a script has a type > 3.
- SCN_MDL +0x3c (SceneManModelAuraSet's second float) has no reader yet; +0x40 is a float the aura sine reads and
  +0x44 looks like a SCENE_CTRL pointer (0x252630 reads sc + 0x1200 through it) -- a host-layout change for scene.c.
- SceneGetModelPDeformPos's offset direction (towards the camera, GetTrgtRot(pos, camera)) is read from the
  decompilation only; scene.c's TU confirms it.

## Open questions from scene_dat.c (2026-10-20)

- The file name `graphics/scene/scene_dat.c` is a link-slot guess (FF2's scene_dat.o, tables only, sat
  between scene.c and scene_effect.c; SceneCutTimingGet reads its cut-timing table). The play control
  in it is FF2 IngameScene.c / scene.c work; rename if a better signal turns up.
- scene_ctrl (bss 0x3f7d70) is defined here, not in scene.c: ScenePlayEnd addresses 0x3fcdd0 /
  0x3fcde0 as scene_ctrl + 0x5060 / + 0x5070, which GCC does only inside one file's bss.
- (Resolved 2026-10-20 by scene.c: SCENE_CTRL +0x27e0 is SCENE_FILE, SCN_MDL and FOD_LIGHT are typed.)
- Nothing plays an engine scene yet: phases 66 / 89 are written (graphics/scene/IngameScene.c,
  2026-10-20) but no scripted run requests an engine scene, and scene.c's SceneCtrlFind /
  SceneCtrlLoadReq / SceneRoomLoadReq / SceneStreamLoadReq are stubs. Stubs this code calls: SceneDataLoadMain, SceneRoomLoadMain,
  SceneInitAfterModelLoad, SceneDraw, SceneObjModelClear, SceneManModelClear (scene.c), FodNextFrame
  (fod.c), gra3dPrelightScene (gra3dMisc.c 0x1dfb90, written 2026-10-21). First engine scenes with cut lists: 2 (0020), 3 (0030).

## Open questions from IngameScene.c (2026-10-20)

- The file name `graphics/scene/IngameScene.c` is descriptive (FF2's file of the Story_Scene phases);
  the code sits in FF2 scn_test.o's link slot, and the four ScnTestIngame / sleep-flag names keep an
  earlier session's guesses. The debug/scn_test.c TU is gone (src/debug/ deleted); its last inferred
  function, SceneTestEffectFlgGet 0x2546d0 (returns type + 0x20; FF2 scn_test.c), sits inside
  scene_effect.c's run and belongs to that TU.
- (Done 2026-10-20: scene.c wrote SceneCtrlLoadReq together with SceneDataLoadMain / SceneRoomLoadMain; but see
  "Open questions from scene.c": with fod.c still stubbed an engine scene would never end.)
- SceneModel::GetJointCoord: the ROM indexes char_joint_tbl row -1 for a man model the table lacks;
  the port returns -1 instead.
- The sleep flags' kinds: 2 = a film to develop (DevelopFilmDevelop); the others come from event
  scripts (SET_SLEEP_FLG_). Which side 0 is (dream or real) is unsettled here; npc_act.h calls side 1
  the dream side, ev_command.c's GO_REAL_WORLD sets side 1 -- check against the event scripts.

## Open questions from effect_torch.c (2026-10-20)

- Unseen: only the flies light torches (FlyEffectDataInit: EffectSetTorch2(fly->dpos, 5 or 2) for fly_dat mdl_no
  0x8000 | 0 / 1), and no smoke run launches a fly yet. The first run where a ghost sends effect flies should show
  small orange flames (torch types 5 / 2: FF2 colours 57 63 66 / 181 89 67, Scale 2.02 / 0.84) with a flare under
  each and a spark burst every 150..300 frames; mark effect_torch.c and fly_ctrl.c done then.
- FF3 dropped FF2's 25x flame scale, so a torch's particles live in units of Scale / 100: type 5 is a 2-unit flame
  where FF2 drew 50 FF2 units (= 2 FF3 units at the 1/25 world scale fly_ctrl.c noted). Consistent, not a bug.
- The light shaft (kind 0x1a) is dead in FF3: EffectTorch2BigFreaReq 0x185718 has no caller (no jal, no pointer),
  so EffectTorch2BigFreaMain never posts it. Kept and written.
- SetEffects(0x18, ...) has no caller but EffectSetTorch2Sub; the SE flag reaches TORCH_CTRL.SeReqFlg and nothing reads it.

## Open questions from menu_file_list.c (2026-10-20)

- **Verify on screen**: phase 62 (the file menu) and 87 (its first-visit message) are written and wired in
  gphase.c, but 62 opens only from the story menu top (37 -> 57, not written) or the memo button
  (MemoNoticeMenuCheck -> MemoMenuReq, which needs memo_flg.open_flg from SET_MEMO_OPEN_FALG and a found memo).
  The smoke pad reaches neither. Whoever first sees it: the tab column (CATEGORY_TM2 cells), the memo titles
  (KOUMOKU1_TM2 + language * 10 + i strips), FUSEN stickers beside new memos, the category picture
  (CATEGORY_PIC_01..04) and grid bars on MEMO_PHF; references `tm2png.py --file MEMO_PHF`, `--file CATEGORY`,
  `--file KOUMOKU1`, `--file FUSEN`. The first visit shows message 99 over the darkened menu first. Then
  menu_file.c (86) can be checked from the same screen (OK on a found memo).
- **The extent** (decided, open to veto): one object 0x204810..0x207490, no atexit thunk between memo.c's
  0x203e98 and 0x207470; the request record 0x3f6048 is read only by phase 62 code. It absorbed the inferred
  menu_cam_edit.c / menu_cmn.c (their FF2 names were all wrong matches) and unassigned@00204810 / 00205498 /
  002070b0. Name invented (menu_file_list.c) since FF2's menu_file.c slot holds the detail page.
- **Hidden functions created in Ghidra**: 0x206830 MenuFileTitleHook (the hook table at rodata 0x35c518 points
  into 0x2065a0's old body) and 0x207200 MemoDetailReq (inside MemoMenuReq's body; no callers, requests phase 86
  directly with mode 3). The mkfunc save step failed with "Unable to lock due to active transaction" twice;
  name_ctx.py's save made them stick (check with ghidra.py func after a mkfunc).
- MenuFileNewHook picks the sticker cell as (tab * 3 + tab / 4 * 2 + row) % 4 -- read from the asm, looks like
  a fixed scatter so neighbouring rows get different notes. The texture arrays at gp+0x110 / gp+0x118 are
  indexed cell / 10 and cell / 4 (always 0): written as one-element arrays; gp+0x114 is never touched.
- FF3 reads memo_wrk[-1] in MenuFileListPad (OK on an empty row list) and MenuFileListGetPageNum (no memo);
  kept, unreachable in practice (a tab always has memos). MenuFileTabFree deletes the lists without clearing
  the pointers (MenuFileTabClear does it on the next visit).
- end_StoryMenu_File stops the menu background unless the menu returns to phase 57 or MemoMenuReq gave a link
  (the GPhaseCtrlRequest link; memo.c's notice passes its ret, the event command 0).
- Phase 87's name StoryMenu_FileFirst is invented. MemoMenuIsEnable's force (MemoMenuReq's 4th argument) skips
  the finder lock 0x12 and IsMenuEnable; the event command passes 1.

## Open questions from menu_file.c / menu_bg.c / story_menu_camera.c (2026-10-20)

- **Verify on screen**: phases 86 (file detail) and 64 (camera menu) are wired in gphase.c but open only
  from the story menu (37 -> 57 top -> 62 file list / 64), and 57 / 62 are not written; MenuBg runs from
  the story menu and the game-clear screens (OutgameBg). The smoke pad reaches none of them. Reference
  for the backdrop: `tm2png.py --file MENUBG_PHF` (and MNBG1_SITAJI_MONO_CL2 / _BLUE_CL2); for the detail
  page MEMO_PHF. BpTexSetClut must take the host TIM2 buffers MenuBgClutHook passes.
- **The run split**: 0x208120 (GPhaseCtrlTerm atexit thunk) ends an object, so 0x207490..0x208140 is
  menu_file.c and phase 64 starts the next. No thunk marks where phase 64's file ends; 0x208370 (an
  unreferenced MenuBgSetHandle right before MenuBgGetHandle) was taken as menu_bg.c's start. Whether
  MenuGetTopPhase / MENU_LIST / the layout helpers 0x208df8..0x2093c8 are a separate object, and whether
  0x209240 / 0x2092f0 belong with the B2dHookPrint* family at 0x2093c8 (unassigned@002093c8, up to
  menu_draw_tex.c 0x209610), is not settled.
- ~~**Left unassigned**~~ (done 2026-10-20: the whole 0x204810..0x207490 is ingame/menu/menu_file_list.c, see
  "Open questions from menu_file_list.c"): 0x2070b0..0x207490 before menu_file.c (IsFinderEnable 0x2070b0, MemoMenuReq
  0x207148, phase 87's init/end/one 0x207290 / 0x207298 / 0x2072a0, 0x207470) -- probably the same file
  menu object as 0x204810.. (phase 62); the file list 0x204810..0x207010 (MenuFileListMain 0x204c58,
  MenuFileGetTab / Row 0x2055c0 / 0x2055d8, MenuFileStepMemo 0x205610, MenuFileGetMemo 0x205748,
  MenuFileGetPageNum 0x205780, MenuFileListGetMemo 0x205b30, GetMenuLangFileNo 0x205f70) spans inferred
  files (menu.c, menu_cam.c, menu_cam_edit.c, menu_cam_main.c, menu_cam_top.c) that are FF3's phase-62
  code, not FF2's camera menus. The next session on it should define it as one FF3 file (story menu file
  list) and move the prototypes out of src/ingame/menu/menu_cam_edit.h and menu_file.c's local block.
- MenuFileDetailPad's page prev / next are pad_key[16] / [17] (repeat); the file list itself uses
  PAD_KEY_TAB_PREV / NEXT for its tabs.

## Open questions from story_menu_photo.c / story_menu_photo_view.c (2026-10-20)

- **Verify on screen**: phase 61 (the document menu) opens only from the story menu (phase 37 -> 57 ->
  61); the scripted smoke pad never presses the menu key. Whoever first opens it should compare against
  work/ref/FILE_TOP_PHF (tm2png.py --file FILE_TOP_PHF; the menu loads FILE_TOP_PHF + language) and
  FILE_ETURAN_PHOTO_PHF (the map view), check the tabs, titles, new markers, thumbnail (item 0x435) and
  scroll bar, then mark both TUs done. Phase 57 (StoryMenu_Top, 0x20bd28..) is not written yet, so the
  menu cannot be reached even by hand today.
- **Names**: the phase table calls 61 "StoryMenu_Photo", but the content is the collected documents
  (tabs tab_siryou / tab_kosyo / tab_tegami / tab_map, layout FILE_TOP_PHF; the EU menu may call them
  files). Functions use the prefix Doc*, the files follow the phase name. Phases 84 / 85 have no names
  in the table: StoryMenu_PhotoDetail (by analogy with 86 StoryMenu_FileDetail) and StoryMenu_PhotoMap
  are inferred. The flags were FinderCamFlag* (wrong: they are the FILE_GET documents) and are DocFlag*.
- pad_key[14] / [15] are named PAD_KEY_TAB_PREV / NEXT from this menu only (with d-pad left / right);
  pad_key[16] / [17] (PAD_KEY_PAGE_*) page the list here and step the document in 84 / 85. Which
  physical buttons they are is not checked.
- Kept as FF3 has it: DocListGetPageNum, PhotoDetailSetDoc and DocPicHook index msg_pic_key_dat with
  the current key without a -1 check (the menu never offers OK on an empty tab: DocMenuNextTab falls to
  the "no documents" step); DocPicHook draws doc_pic even when null (it never is while the hook is set).
- 0x1b07a8 (DocFlagDummy) is a counted loop over the flags with an empty body (four nops a pass), no
  caller; written as an empty loop.

## Open questions from menu_cassette.c (2026-10-20)

- **Verify on screen**: the cassette player opens only from an event's TAPE_START (or the unreferenced
  CassetteMenuReq); no scripted smoke run reaches one. Whoever first gets a tape (items 0x35..0x3b) and a
  TAPE_START event should compare the list with work/ref/CASSETTE_PHF (tm2png.py) and mark the TU done.
- CassetteMenuCtrl's cancel in the list rewrites pad_wrk[0].trg (cancel bit off, OK bit on) and calls
  LayoutAnimMain before closing: kept as FF3 has it; presumably so the kept-open message window sees an
  OK and closes. If the window does not close (or a second press leaks), look here.
- cassette_tbl row 0 plays subtitle 1 and row 1 subtitle 0 (ROM data, kept). end_msg is -1 in every
  row, so a tape counts as heard only when it plays to the end (a key press that stops it does not).
- CassetteDrawRows bounds the rows by top + row < count but indexes from top: FF3's code, kept; with
  top -1 (cursor off) it would read list[-1]. Never seen; MenuCsr keeps top >= 0 while the list exists.

## Open questions from photo_file.c (2026-10-20)

- The file name ingame/photo/photo_file.c is invented: the code is FF2 photo.c's album half, but FF3 keeps it
  apart from FF2 photo.c's other parts (0x226e00 FurnPhotoFlg*, the decoder state 0x229ba8), so it got its own
  name. Rename freely if a later find settles FF3's photo.c.
- PICTURE_WRK +3 was FF2's pad; FF3's AddPhotoData stores the photo mode there (GetPhotoModeWrk()->GetMode()),
  renamed `mode`. album_view.c shows its "flag 3" item when it is non-zero (AlbumPhotoIsFlag3): which photo
  modes are non-zero, and what the album marks with it, is unconfirmed.
- The rest of the old run, unassigned@0022ec88 (0x22ec88..0x231628), is FF3's film menu (phase StoryMenu_Film)
  plus two unreferenced functions at the end (0x2309f8, 0x231438) and PlyrFlagInit 0x22f628 in the middle
  (reached at every new game, a stub in unassigned_stubs.c). Settle the end before defining it.

## Open questions from plyr_shot.c (2026-10-19)

- Unseen: everything that needs a ghost in the viewfinder -- the charge (ENE_WRK flag 0x10, the charge
  gauge filling), the damage number (DamageDispReq), the score panel (PHOTO_WRK_288 on[1..12] icons and
  the number), the particles flying home, the hit effects, the lens effects, the auto turn (lens function
  3) and EJobC41's FinderViewTurnToEne. The first run that photographs a ghost should show the panel's
  bonus icons and a score, and the ghost react. Mark plyr_shot.c done then.
- PLYR_WRK flag 4 (PLYR_FLG_000004: time step 0.6, a shot only slows the ghosts and scores nothing) has no
  setter traced; PLYR flag 0x80 (PLYR_FLG_SHOT_KILL) no reader outside this file.
- ingame_wrk._28 is what AddPhotoData files as the room (FF2 passes GetPlyrRoomID()); SHOOT_DEVELOP_FILM
  passes it on too. Rename it room_no when the reader side confirms.
- PlyrShotParticle: FF3 leaves ENE_DMG_PARTICLE_REQ.EffectType as stack garbage; the port zeroes it (the
  suction burst should not read it).
- SUBJECT_WRK.sp_no (+0) is never written by FF3's shot code (FF2's second soul entry); AddPhotoData keeps
  +2 / +4 only. PHOTO_WRK_DEF::GetSpecial always returns -1, so PHOTO_TYPE_SPECIAL never comes from a shot.
- sceScfGetLocalTimefromRTC (new in sdk/libscf.cpp) is the identity: the host's sceCdReadClock already
  returns local time.
- ~~FINDER_MODE_WRK::AddScore / SetMaxLevel (mission_mode.c, 0x1aae20 / 0x1aaef0) are stubs~~ (written 2026-10-20). ~~GetSavePhotoNo /
  AddPhotoData are stubs~~ (written 2026-10-20 in ingame/photo/photo_file.c: the album now records each shot).

## Open questions from photo_dat.c / fly_ctrl.c (2026-10-19)

- photo_dat.c's name: the content is FF2 photo.c's phase half (Story_Photo, PhotoMain); FF3's photo.c
  slot (0x226e00..) holds FurnPhotoFlg*. Kept the link-order name; rename when the photo files settle.
- PHOTO_WRK bits 0x01 (PhotoSetFrameBit), 0x02 and 0x04 (PHOTO_BIT_END) have no reader in the ROM that
  ghidra.py refs finds (0x3f6678 is only addressed from this file); maybe read through the photo-frame
  helpers' own pointer arithmetic -- check when 0x22b888 is written.
- PhotoStepHintOut frees the hint texture but FF3 leaves the pointer, so PhotoHintTexLoad would free it
  again on the next subject with a hint (a double free in the ROM unless heapCtrlFree tolerates it); the
  PC clears the pointer after the free (`// PC:`).
- PhotoStepEnd reads the current subject's record (+0x30/+0x38/+0x3c) for every subject in the list, as
  FF3 does.
- FLY_DAT (0x327318, 4 rows): all fields now named from their readers (2026-10-19 second fly_ctrl.c session).
- fly_ctrl.c doubts (2026-10-19): EneFlyActByKind kind 3 measures the distance from the enemy to its target's
  first 16 bytes (FF2's PLCMN_WRK began with mbox.pos; FF3 PLYR_WRK +0x00 holds turn timers), so its spread is
  noise -- kept as FF3 does. SetFlyOne type 3 adds GetTrgtRot's radians as if degrees (* pi / 180): a tiny
  spread, kept. Nothing in FF3 sets FLY_STA_HOLD. No smoke-run event launches a fly yet (EJobC18 has not been
  seen in the opening), so the fly drawing is unverified: the first ghost that sends flies should show small
  FLY000 / FLY001 models wobbling toward Rei's head, fading on a photo or a flash.

## Open questions from enemy_act.c (2026-10-19, control ops 0x10..0x47)

- EJobC27 reads ENE_WRK.adat->act_len (+0x58) without a type test; the ROM reads EE address 0x58 for an
  ordinary enemy (adat 0). PC skips when adat is nullptr (loop[0] left). Check against an auto enemy's script.
- EJobC2A / EJobC2B index ENEALG_WRK.cnt (+0xbe) by an operand byte; 2 or more would write idx (+0xc0).
  Assumed 0..1; dump the ALG pak's uses when the branch handlers that read cnt are written.
- EJobC3E steps both deform rates by deform_b1[0] (the ROM reads [0] twice); kept, EJobC3F uses [0] and [1].
- New names (ff3-inferred): ENEALG_WRK.var[8] (+0xc8, per-enemy script variables), cnt[2] (+0xbe),
  ENE_WRK.rot_keep (+0x178, EJobC31 / EJobM08), AENE_DAT.act_len (+0x58). FUN_0013b2a8 (restore the kept screen
  effect) is still unnamed and unassigned (enemy.c and enemy_act.c declare it locally).
- (2026-10-19, move ops EJobM00..M0D, branch ops EJobB00..B12 / B14) FF3 measures enemy distances with the inline
  VU0 length over x and z only (vaddz.x); EaDistXZ stands in. EJobM06 divides the root move by that distance with
  no zero test (an enemy exactly over its node: the EE saturates, the host gives inf); kept. EJobB0B indexes
  ENEALG_WRK.loop[] by an operand byte unchecked like EJobC06 (3 would hit bwait_time on the EE). EJobM0C's ease
  counter is one sbss int (0x372fdc) shared by every enemy; kept as FF3 has it. EJobB0C reads five u16 jumps and
  uses four (dmg_react 0/1, 2, 5, 3/4 -> operands 0, 1, 2, 3); the fifth is unused in the ROM. New bss:
  ene_warp_alpha 0x3df540 (EJobM03), ene_goal_pos 0x3df550 (EJobM0B). Unseen on screen: no ghost acts in the
  --run 100 opening (no EJob stub reached), so none of this is verified by a picture yet.
- (2026-10-19, last slice: branch ops EJobB0D / B0E / B13 / B15..B34, effect ops E00..E07; the TU is complete, verify)
  Mark enemy_act.c done when a screenshot shows a ghost moving / attacking (the first encounter). Doubts:
  EJobB0D (the attack hit check on flags 0x100000000 / 0x200000000, the motion script ops 0xd / 0x10 windows) reads a
  second u16 operand it never uses, and leaves its points uninitialised for modes > 2 (PC zeroes them); it also takes
  the player's joint 2 into a local nobody reads (kept). EJobB24 / EJobB25 measure in 3D (vadday.x / vmaddz.x), unlike
  EJobM06's XZ: checked in the asm. EJobB16..B19 / E04 read dat->link with no type test besides type != 2 (E04 none:
  a type-2 enemy would read adat-less dat 0 on the EE). New bss 0x3df650..0x3df6d0+0x100 (EJobB0E's hold: two jump
  tables, a frame counter, the facing it turns the player to). New field AENE_DAT.near_dist (+0x54, EJobB1D).
  ENE_WRK.act_req's reader is EJobB1E (1 / 0xf0 -> first jump, 2 -> second, then cleared).
  **The tattoo grab event** (unassigned@0013a010, state bss 0x391000, enemy 0x391024) now has names:
  TattooEvStart 0x13a080 (EJobE04), TattooEvResume 0x13a158 (EJobE05; was the wrong ff2-match-low
  __static_initialization_and_destruction_0), TattooEvIsLate 0x13ac20 / TattooEvIsIdle 0x13ac38 (EJobB33 / B34); its
  main FUN_0013a178 (682 instructions, reached as a stub in the --run 100 opening) is the next piece of that run to
  define as a file. (Done 2026-10-19: the run is `ingame/tattoo_event.c`, all 19 written, verify; see architecture.md.)

## Open questions from photo_obj.c / photo_obj_dat.c (2026-10-19)

- **Not seen yet (verify).** No SET_PHOTOABLE_ runs in the --run 100 / 170 smoke paths, so every area change
  (areas 1 and 6 were measured with a temporary probe) finds its objects inactive and no glow slot fills.
  Mark both files done when a run shows the glow (kind 0x1e sparkles plus parts deforms) at a photo object, or the
  filament hint in the viewfinder; the first SET_PHOTOABLE_ in the story is the place to look (evtbl.py cmd 112).
- **File names invented.** photo_obj.c sits in FF2 play_data.c's slot (photo_make < photo_obj < player holds);
  photo_obj_dat.c follows the 0x229990.. run (PlyrHPdownCtrl, PlyrNAnimeCtrl: player HP / anime control), so its
  name may break the alphabetical link order. Rename both freely when the run between them is identified.
- **PhotoDatInit (photo_dat.c 0x2284a0) was written by this session** (three calls; FUN_0022ca90, the 0x32f290 table
  clear, stays a stub in an unassigned run). The rest of photo_dat.c is still todo.
- **PC guards:** PhotoObjSlotSet returns when no slot is free (FF3 writes through null: a fifth active object in one
  area); PhotoObjMain zeroes the near position FF3 leaves unset when only a target exists.
- **Save blocks 29 / 30**: block 30 holds the slots with their effect pointer (FF3 saves the raw 0x140 bytes; the host
  saves its larger table). The room load's PhotoObjAreaOn restores the slots after a load: it cuts each object of the
  area (PhotoObjSlotCut releases the saved effect) and takes a slot again. The saved pointer is stale on the host
  (crash in EffectRelease, 2026-11-24), so IngameLoadRestore calls the PC-only PhotoObjLoadRestore, which drops it.
- Unknown record fields +0x24..+0x3c (ids 0x6d3.. / 0x947.., indices 0..0x20 of the 33 type-3 objects, score 1000 / 2000
  in the low half of +0x1c): read by photo_dat.c / the album code, not here.

## Open questions from cine_film.c / ClearMenu.c (2026-10-19)

- **The film's host draw**: each visible screen cell goes to ReiPan_Graph3dDrawEffectQuad with TEX0 TBP 0x2bc0
  (256 x 128, PSMCT32, MODULATE, TCC 1) at colour 0x7f, alpha 0x40, GS-Z bias 10. Assumed: the PSS frame reaches host
  GS memory at 0x2bc0 the way the title backdrop's does (same MAP_SKY path, MovieSendFrame); the bridge draws 320
  quads a frame (one call per cell) -- batch them if the film costs frames. The dark projector (2) draws black at
  alpha 0x20 and passes its flag to MapSkyMovieSet as alpha_th, as the ROM does; what that threshold does to the
  picture is not checked.
- The reel turn (map object f033 of area 0x13) keeps the reel's area / name in sdata between films: projectors 1 / 2
  have no reel, but CineReelTurn still turns f033 if an earlier projector-0 film set it (ROM behaviour, kept).
- CineFilmPlayReq's first argument is the projector's area (EvCineFilmPlay args[0]); ev_command.c called it film
  before -- renamed. (done 2026-10-20: the cine film menu and the film table 0x3508c8 are written as ingame/cine_menu.c,
  phase 47; FUN_0014a820 / alg_manageIsReadyAlg are CineFilmTblGetPhotoWin / CineFilmTblFindFile. Kept as two files:
  the ROM has no shared statics between them, only calls.)
- ClearMenu.c calls the select-menu module 0x1ed658..0x1edd90 (done 2026-10-20: outgame/sel_menu.c, verify; the
  game over menu's twin over CLEARMENU_PHF, rows SEL_MENU_ROW {item, kind, text, help, result}; file name invented
  after the SelMenu prefix and the outgame neighbours lang_sel.c / csr_ctrl.c). Unchecked guess: the "kind" of the
  help line is the row's own kind (+4), as the code reads, though the clear menu's help ids 4 / 4 / 5 look like kind
  0x6c ids -- the first row (kind 0x69, help 4) may show another message than meant; compare on screen. Phase 42 (GameClear's own phase functions 0x258a68..) is not hooked in
  gphase.c yet, so phase 43 is reachable only after those are written.

## Open questions from BgSe.c (2026-10-19)

- **Labelled ambient SEs never start yet**: CBgSe::Main gates rows with a region label (areas 1, 6, 7, 8,
  10, 11: labels 10061, 22038, 32033, 30061, 38077, 41075) on RegSecListGet4Pos / RegSecListHasLabel
  (FF3 0x198640 / 0x1985c0, the unassigned run 0x1985c0.. beside the enemy code, lib/unassigned_stubs.c),
  both stubs returning 0. Those rows load their bank and stay in step 2. Writing the run's three list
  helpers (0x1985c0, 0x1985f8, 0x198640: about 0x180 bytes, RegDat calls already written) starts them;
  define the run first (it also holds EneSpeObjTbl's object code, architecture.md 0x1985c0 row).
- Audio-only TU: no screenshot can show it. Finished done on the log evidence (area 1 on a new game:
  KAN_SE_01_KOUSHI_BD loaded by CBgSe::SetArea). Listen for the ambient loops once the region helpers land.

## Open questions from telephone.c (2026-10-19)

- ~~TelTalkScript is stubbed~~ written 2026-10-21 (tel_talk.c, verify): all 15 methods. The null
  script after a mid-call Stop is guarded on PC (Work returns 1, the call ends). Kept FF3 quirk: Work
  also returns 1 when the voice stream ends, without Stop, so the map BGM is not faded back in and a
  message window can stay open -- look at that when the first call runs. Where the call is: no
  TEL_TALK_START in the main event file; evdump.py --file EV_MISSION_OBJ shows it in event 379's end
  program, but that file's decode looks wrong (garbage sub-event ids), so the call's event is not found
  yet. Work(2, 0x80) from TelTalkMain: 2 is the SubTitleReq kind, 0x80 the YesNoSetMsg first message.
- The ring SEs play with loop 0; whether SYS_REAL_1_BD samples 0 / 1 loop by themselves (an endless ring,
  TEL_START 0, starts them once) is untested -- listen when the first phone event runs.
- Who loads banks SYS_REAL_1/2/3_BD (TelRingBankIsReady only waits): ingame.c 0x1b7d load and ev_command.c;
  if a ring never starts, check 0x1bf5 / 0x1bf7 get loaded.

## Open questions from effect_ene.c (2026-10-19, hit effect / large hit)

- Unseen: nothing in the opening damages a ghost, so the large-hit flash, the hit blur / contrast and the
  rumble have never been on screen. The first run with a camera hit on a ghost should check: a lens-like
  fan over the ghost re-showing a still of the frame, grown and fading (presets 0..3 for plain shots), plus
  the screen blur from the ghost's point.
- Host path doubt: the flash's still goes through EE 0x11c1000, which is not one of g2d_draw.c's two capture
  stashes (0x10ef000 / 0x117b000), so LocalCopyLtoBAdrs reads the screen back (once per capture) and
  LocalCopyBtoLAdrs uploads the EE bytes to GS 0x2bc0 every flash frame. If 0x2bc0 is a declared capture
  slot at that moment (the deform / blur copies use it), ReiPan_RendererDrawTexturedTriangles2D samples
  the slot instead of the upload. If the flash shows the wrong picture, make 0x11c1000 a third stash
  (the haze uses it too).
- Decided: the flash's draw blends however the renderer's textured-triangle path blends (it takes no ALPHA
  register); presets use ALPHA (Cs - 0) * As + Cd (A 0, B 2, C 0, D 1) or B 1 -- additive either way.
- FF3 has no EneDmgScreenEffectReq: ene_dmg_eff.flow is only written by EneDmgMain, so the damage sprite
  ramp idles at 0 for ever (kept as the ROM has it).
- Names: FUN_001e91e8 (every IgEffectSubFunc*Req's tail: (ene, n, frame)) is probably FF2's
  IgEffectSubFuncPDeformReqSet (0x48 bytes against FF2's 0x11c); not checked. IgEffectSubFuncEx1Req /
  Ex2Req are placeholders for FF3's two new camera functions (labels 0x14..0x19).

## Open questions from effect_ene.c (2026-10-19, particle bursts / enemy aura)

- Unseen: a camera hit's burst (EneDmgParticleEffectReq) is requested from roku_pzl.c and the damage path
  (stubs / unreached), the end burst from IgEffectEffectEndParticleReq (ingame_effect.c, stub). The first
  ghost photographed should show a blue bloom of up to 48 sparkles that fly to the lens point with pale blue
  comet tails and charge the lens (LensAddCharge 9 per particle); the end burst a coloured bloom that fades.
- Decided: the ROM leaves ENEDMG_P_WRK.cnt (the arming clock) and EffectType uninitialised in both
  requests (stack garbage); the port zeroes the local. A failed tail allocation leaks the particle array
  in the ROM (EFFECT_FREE(0)); kept.
- The enemy aura on PC: **done 2026-10-26** with renderer work frames (ReiPan_RendererBeginWorkFrame,
  reipan_renderer.h; effect_sub.c's ReiPan_EffHostWorkBegin / End): EneAuraFrameBegin opens a 640 x 448
  window-space work frame at TBP 0x2bc0, the model draws into it depth-tested against the room, the screen
  mix goes in under TEST DATE (reproduced as a blend that keeps the work frame's alpha, the ghost mask), and
  EneAuraDrawLayers draws each layer as a quad sampling it (new blend GS_BLEND_REV_SUB for the 2/0/0/1
  layers). The first aura frame of a session still takes the old path (model straight to the screen). Seen
  with a reverted probe forcing aura row 3 on rs101's lattice ghost, whose own row is 0 (aura off).
- Done 2026-10-19 (fourth session): 0x161af0 (EffEneTimerRearm) and the finder sparks (CEffectFinderMgr,
  CEffectFinder / CEffectFinderSpark, 0x161f38..0x1622d0 and 0x2b0b98..0x2b12a0) written; the TU is complete
  (verify). All of it is unreferenced in FF3, so no screen will ever show the sparks. Decisions: the classes
  use host new / delete (not FF3's operator_new heap: a virtual delete must match its new); EffectFinderMain
  keeps the ROM's missing null check and DeleteAll leaves the slots dangling as the ROM does. Doubt: the
  spark's third random number lands in scw (-0.5..0.5) rather than vrot -- kept as in the ROM; the owner of
  EffEneTimerRearm's +0x240 block is unknown (no FF3 reads of +0x290..+0x2a4 found).

## Open questions from radio.c / random_ghost.c (2026-10-19)

- **Both verify, unseen.** The opening (--run 170) switches no radio on and no random-ghost area on, so
  neither runs past its early returns. A radio shows as a caption (MovieTitleDispMain bank 0x7c, y 380)
  plus its broadcast / static loops once an event runs RADIO_TURN_ON; a random ghost as a type-1 enemy
  after RANDOM_GHOST_ON and a 45..75 s wait (NTSC). Mark them done from the first run that shows either.
- unassigned@0024cec0 now holds only 0x24cec0 / 0x24cef0 (0x54 bytes): helpers of the stone pedestal
  module before it (init_Story_StonePedestal 0x24b5e0.., the "daiza" layout and record playback with
  subtitles, 0x24cc38..0x24cec0, still under the savepoint_main.c / savepoint_fade_in.c inference).
  Define that module as one file (name it after the pedestal, not the save point) and take these two.
- radio.c's range ends at 0x24da20 so it keeps 0x24da08, an empty function RadioInit calls first, which
  sits after the file's atexit thunk 0x24d9e8 (thunks usually end a file). It could be random_ghost.c's
  first function instead (an empty init the radio code calls); no caller decides it.
- FF3's RandGhostAreaRelease reuses the slot counter for its inner area scan (hazards.md "ROM loops that
  reuse their counter"); the port keeps the jump and stops at ene_wrk[16] (the ROM reads on into .data).
  RandGhostFollowCheck releases slot -1 when the next ghost has no work and RandGhostBgmStart reads the
  chance through a null row: both guarded (// PC:).
- PLYR_WRK.flags2 0x2000000 (blocks the candle-out enemy 0x10a and releases it) is not traced; the
  enemy 0x10a itself (type 0, loaded by EneLoadStart while the candle is out) is not identified.

## Open questions from cat_ctrl.c (2026-10-19)

- **Seen on screen 2026-10-19** through the warp tool (`--warp chapter=2,tp=0`, see "Starting elsewhere"
  above): the cat lives in the real-world house, which the --run 100 path never reaches (GO_DREAM_WORLD
  deletes the cat models). The shot shows the cat (ch015 + ch019) in the living room; check its motions
  and the pet act (OK pressed within 30 of it) before marking cat_ctrl.c done.
- CatSightCheck 0x13f658 measures with `vadday.x / vmaddz.x vf14, vf1, vf14`: x2 + y2 + vf1.x * z2. vf1 is
  whatever the callee left; taken as 1 (3D distance). If the cat's door look or the pet act triggers oddly, check.
- CatLabelGetMotSet 0x140580 with idx < 0 draws 0..5 over the label row's bytes +0x20..+0x25 (motion sets, move,
  sub, spot): an FF3 quirk kept; every caller passes 0 or 1.
- CatActFreeCheck with every label locked gets -1 from CatLabelSelect and puts the cat at label -1 (reads
  before the table, as FF3 does). Never seen; the NPCs lock at most labels 2 and 4.
- Save block 55 is saved in the host layout (sizeof(cat_wrk), host pointers; CatCtrlLoadRestore clears them).

## The event browser (2026-10-22, reipan/debug/reipan_events.cpp)

- The ImGui Debug -> "Event browser" window lists the event file's 1781 events (read in play, kept at the
  title): the Hours under ev88, the room groups under ev1060, the rest (ev1, ev1602, ev1746 ...). Clicking a
  row shows its open / close conditions judged now and its programs with their arguments (Copy puts it all
  on the clipboard). Jump loads a new game in the event's Hour straight where it starts (area, world, side,
  on its rectangle facing the way PLYR_ROT / PLYR_INTO_SIGHT ask): the Hour's start event is marked ended in
  Story_NowLoading and its state commands replayed once the Hour's event opens (the camera, items,
  PHOTO_UNLOCK, doors), and every prerequisite is forced ended. Go teleports in the game as it stands (into
  the other world: a reload from memory, after that world's EVENT_REBOOT).
- Smoke runs: `python build.py --run 100 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1300:CROSS,1450:CROSS
  --event chapter:2,jump:451` (a jump needs the event file, which the chapter start loads) ends with
  "[events] ev451 has started." (the Four-Point Well Key in area 5). The `[events] evN now:` line gives each
  open condition's live result, area, floor and facing.
- PLYR_AREAIN never holds at the exact middle of a rectangle: HcBaseIsInTriXZ splits it along the 0-2
  diagonal and a point on an edge counts as outside (as in FF3). The warp stands her between the middle and
  triangle 0-1-2's middle.
- Siblings of the target that open on the place still fire (ev465 plays its event camera scene when a jump
  lands in area 6, before ev455); after it the picture stayed on the event camera (open bug, same family as
  the second-room camera fix).

## Open questions from story_menu_map.c (2026-10-18)

- ~~**The map button did not open the map in the smoke run.**~~ Answered 2026-10-20 (man_data.c): lock 0x1b
  (SET_MAP_IN_LOCK_FLG) is set by event 148's init (the chapter 0 opening) and cleared only at its end, so
  the map cannot open anywhere on the scripted --run 100..250 path. It opens in the house:
  build.py --run 95 --warp chapter=2,tp=0 --pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1450:L2 (L2 is
  PAD_KEY_MAP in pad config 0) shows the full map menu from frame ~1500: frame, title and help line, area and
  room names, the room picture, floor 1F with the living room in the current-room CLUT, Rei's marker and a
  door mark (man_data.c's progress shot). Not exercised yet: floor change (L1/R1), zoom (CROSS), close
  (TRIANGLE: IsClosed), the scroll arrows (ArrowUpdate while moving). Original entry: a run with the scripted pad plus
  3320:L2 (Rei walking in the courtyard, --run 185) never reached init_StoryMenu_Map: IngameMain
  requests phase 0x3a only when IsMapEnable() (IsMenuEnable, PlyrIsFinderModeOff, !IsLockTblOn(0x1b))
  and pad_key[PAD_KEY_MAP] | pad_key[PAD_KEY_MAP2] triggers. Unchecked which fails (the opening's lock
  table rows, or a stub on the way); a session that wants the map on screen starts there.
- **The map screen's files are misnamed.** CMapScreen's methods sit in m_plyr_camera.c (0x1f74f8..
  0x1f8230, pinned 2026-10-18) and man_data.c (0x1f8230..0x1f8af8); both names are FF2 link-order
  guesses (wrong low matches m_plyr_cameraSetSave*, MAN_DATA). Better: one ingame/map/MapScreen.c
  (or two). Not renamed, the code came first. 2026-10-19 (m_plyr_camera.c written): the two files
  share sdata 0x370fd0..0x371000 (queue count, MENU_PIC *, "door", 1/128, the SCISSOR word, 0.4) and
  sbss gp+0x1c..+0x2c (bg item, row, floor, layer, drawn screen), rodata 0x35b800 (the sprite hook's
  switch) / 0x35b828 (the MAPMENU hook list): almost certainly ONE FF3 object. The shared state is
  defined in m_plyr_camera.c and declared extern in m_plyr_camera.h (map_screen_*); man_data.c's
  CMapScreen::Draw must reset map_screen_spr_num, set map_screen_bg_item / row / drawn (null at the
  end), install MapScreenSpriteHook / MapScreenQuadHook, call MapScreenDrawCurRoom after
  CMapLayer::Draw, and pass map_screen_menu_hooks to B2dSetHookList for the menu handle.
- **CMapScreen::IsReady / Release walk 5 words from +0x00** (frame, menu, then layer[0].handle[0..2]),
  IsClosed 3: in the ROM the head looks like an int[5] handle array overlapping layer 0. The host
  class has frame / menu as fields; whoever writes those members must keep the ROM's walk
  (layer[0]'s handles are -1 or already loaded, so the overlap is harmless there).
- **CMapLayer is written (MapLayer.c, 2026-10-20, verify).** Unseen until man_data.c's CMapScreen
  methods (Draw, IsReady, IsClosed, Release, LoadFloor) are written and the map opens (item above).
  CMapScreen::Draw calls CMapLayer::SetScroll(x, y) (0x1f3d58) then Draw. Port decision: SetScroll
  skips FF3's unchecked handle[MapSnapGetFirstLayer(floor)] read when it returns -1 (FF3 reads the
  word before the object; no layout is then placed either way). Both SetScroll and SetPlayer measure
  through floor 2 layout 0's transform (MapSnapGetStat(2, 0)) as the common reference -- if the
  marker or a floor sits off-centre on screen, check that reference first.
- pad_key[10] (0x32dd30, square in every layout) pressed together with cancel keeps the map open; its
  other users are unknown (PAD_KEY_10 in pad_ctrl.h).

## Decisions and doubts from the graph3d host bridge (reipan_graph3d.cpp, 2026-10-16)

- Left out of MioPan's plan on purpose: `_HostPublishLight` (it feeds the g3d core's slots and
  g3dApplyLight, which only the prelight law g3dCalcVertexColor reads; MioPan's realtime and preset colours
  come from gra3dSnapshotVu1Lighting since 2026-08-20, so it changes nothing the bridge draws) and
  `g3dSetRealtimeLighting(0)` (MioPan's s_bRealtimeLighting is permanently 0; ReiPan's g3dCore is FF3's
  prelight model only). Add them back only if a lit object looks wrong and MioPan's code is the reason.
- ~~The spot cone in gra3dCalcVu1VertexColor keeps MioPan's sense~~ (settled 2026-10-20: every host spot
  kernel takes the microcode's sense, -vDirection, as FF3's installs and _IsBBLightingupSpot expect; the
  flashlight lights the meshes in front of Rei).
- The TIM2 half (ReiPan_Graph3dBeginTim2Upload / End / NotifyTex0Rebase) has no caller yet: whoever writes
  the character / item texture senders (FF2 mdlwork.c SendEneVram / SendEneVramMono / SendItemVram and the
  MpkAddTexOffset counterpart; not found in FF3 yet -- the old guesses MdlManmdlTexSend 0x203d88 /
  MdlManmdlTm2MonoSet 0x203de8 are memo notice code, ingame/memo/memo.c, 2026-10-16) brackets each
  send with them as MioPan's mdlwork.c does. Without the bracket the draws simply resolve their TEX0s from
  GS memory every frame (correct, slower).
- The realtime path (characters, ghosts: DrawRuntimeMesh / Post) compiled but has not drawn anything yet:
  every model at --run 100 is preset. g_dbgBlockId (the flat-colour debug view's block index) is not set by
  ReiPan's walker.
- gra3dApplyCamera now widens the clip volumes by ReiPan_RendererGetViewExtend (MioPan's widescreen): 1 at
  4:3, so the PS2 culling is unchanged there.

## Open questions from map_rectangle.c (2026-10-16)

- ~~MDAT_SE's type~~ (settled 2026-10-16, MapRcb.c): `MrecSetSEInfo` 0x1ffd80 latches RcbGetSeObj's se_ object
  (RCB_OBJ, MapRcb.h) and `MrecGetSeNo` returns its prm[0], the sound number. Their only caller "FrameRateSelPad" 0x1c8278 compares the number with 99 / 51: it is FF3's
  footstep code (foot_se.c?), a wrong FF2 match -- rename it when its TU is taken.
- `MREC_HIT` (map_rectangle.h) is the generic stat record MrecFindRectStat hands its callback; the door callback reads
  +8 as the door id, MrecCameraHitCallback reinterprets it as MDAT_CAM. Merge with RegDat.h's records when RegDat.c
  settles what RegDatGetStPtr returns.
- MrecCheckOnSameCamRect gets map_camera's pRectStat, which for types 1 / 8 is MrecGetCameraInfo's latch *copy*
  (0x3f5214), not the RegDat record: if RegDatCheckSameRectStat compares pointers it never matches for those types.
  That is FF3's behaviour; check it when RegDat.c is written (map_camera only calls it with CamDataType 9 records?).
- Camera kinds 1 / 8 have alternate rectangle sets 20 / 21 and 22 / 23 (MrecSetCameraSet, from CAM_CHANGE_TYPE and the
  area's cam_type): the room data must hold rectangles of those types; confirm when a room's RegDat file is dumped.

## Decisions and doubts from motion.c (2026-10-16)

- **VertexMotion::Play's arguments are (loop, stop_end, reverse, frame, speed)**, not (mot_no, loop,
  reverse): +0x14 picks the end behaviour (frame = count - 1 when reverse == loop, else 0), +0x20 = Stop
  at the end, +0x1c is a hold (frame frozen, IsEnd 0). MotionModelManager::PlayShape's parameters were
  renamed to match; SetShapeLoop (Ghidra name kept) writes the hold, so MotScrHdlPlay modes 2/3 freeze
  the shape at frame 0 with speed 0. Doubt: whether the script's "mode 4" (first argument 1) is meant
  as loop -- it is by the code.
- ~~The BmdMotion yaw callback (+0x48 / +0x4c) is never set~~ (resolved 2026-10-16: CharModel::SetYawFunc
  0x145410 sets it -- PlyrMdlSetup FUN_0023e7b0, EnemyStart CallNega2, PlyrCameraUpdate PlyrCameraMotTurnCB).
- ROM quirks kept: GetKeyRatio's even-length tail test reads this->frame and the PK4's motion count
  parity; ShpPhysicsMotion's z delta pair is the other way round from x / y; ShpMotion::Pose reads
  weight[k + 1] on the last frame.
- PC: BmdMotion::FindBmd treats a null PK4 member as "not a BMD" (the EE reads address 0); the
  per-bone stack buffers (FF3 VLAs) are host vectors; the BMD key table is relocated with EE addresses
  (SgdAddr), read back with SgdPtr.
- Cloth (2026-10-16, second session): ROM quirks kept -- CopyParam's preset pointer is a function-local
  static (gp byte 0x198 / gp 0x1a0), so the first cloth with a preset fixes it for every later one; Calc
  stores the rotations, not the inverse rotations, into the CLOTH_MAT_RINV pair at the end of a step
  (the first step stores the inverses; nothing reads the pair); the turn check uses the root position's
  direction about the world origin. PC: the inline normalise of a zero vector gives zero (the VU's
  0 * rsqrt(0)); Calc's stack matrices are zeroed (FF3 leaves the bone-1 halves as garbage with one bone).
  Unseen: no cloth model is built on the --run 100 path (kind 2 map objects, MapObjWrk.c, and the
  characters build them); the first cloth on screen verifies this code. Doubt: the turn threshold reads
  the root's world position as a direction, which only makes sense for models placed near the origin
  -- check against a moving NPC's skirt when one draws.
- (2026-10-16, char_model.c) Left in alg_manage.c as hand-written stubs: CharRmdData::GetMove 0x148760 (so
  every root move is 0 -- the player and enemies do not walk by their motions until it is written),
  CharPlayerShape::Update 0x147a48 / CharPlayerShapeMepati::Update 0x147b00 (no blink, face or hand
  shapes) and the states' slots (0x147c78.., 0x148060..: the host states keep the base's empty Enter /
  Leave). The class layouts in alg_manage.h are minimal: CharWatchControl's 7 words after `off` and
  CharPlayerShape +0x10 have no meaning yet. Doubts: CharPlayerModel::StopShapes reads a 3-row stack
  table by GetShapeChr (0..4 -> 0..2); a PC guard skips the loop for other values (FF3 would read
  garbage). GetLightCreationData captures light_scale on its first call (FF3's function-local static).
  The anime script tables (char_model_dat.inc, chardat.py) are copied up to each script's 0 opcode;
  AnimeCodeManager::Step / Start are still stubs (album/prg/scr_play.c), so no script runs yet.
- The IK helpers 0x2165d8 / 0x216c40 are ModelObject members (IkLimb / IkChain) declared in
  graphics/model_object.h but defined in motion.c, where FF3 links them; their callers 0x1466e8 /
  0x146ac8 (CharPlayerModel::FootIk / ArmIk) are written since 2026-10-16. IkChain's axis outside 0..2 multiplies by a unit matrix on PC
  (FF3: stack garbage).

## Open questions from game_model.c (2026-10-16)

- ~~**Model builder stand-ins.**~~ Resolved 2026-10-16: the builders live in graphics/model_builder.h/.c; the
  director is `ModelDirector` (Construct 0x20ee10). GameModel::Build / GameMotionModel::Build's `mot != nullptr`
  PC guards are gone (2026-10-16: BuildMotion makes the MotionModelManager). Doubts: (1) `MDL_OBJ` is now
  a typedef of `ModelObject` (motion.h), whose VtblCall14 / 18 are concrete stubs in lib/unassigned_stubs.c; (2) ~~the
  collision registry 0x32d6c0 still carries FF2's MissionCtrl* names~~ (done 2026-10-16: ModelCollisionSet, mmanage.h);
  (3) draw_en_37126c (sdata 0x37126c, defined in ingame.c) sits between model_builder.c's sdata constants
  0x371268 / 0x371270 and is read by ModelCollision::HitCheck: probably this file's variable, which ingame.c
  writes; left where it is; (4) ~~MOT_CTRL may be RTTI Motion~~ (resolved 2026-10-16: MotionModelManager's
  GetBoneMotion returns its BmdMotion, so MOT_CTRL is a typedef of BmdMotion; MotCtrlSetFrame 0x212658 runs on
  shape and cloth motions too and is Motion::SetFrame).
- ~~**MorphGetCoord returns an int EE address**~~ Resolved 2026-10-16 (model_object.c): it is
  ModelObject::GetCoord and returns a host SGDCOORDINATE *; callers that keep an EE address (ModelCollision,
  ModelCcs, gra3dDrawModelCb) pass SgdAddr(obj->GetCoord(0)).
- **ROM quirks kept**: GameModel::Build reads the PK4 holder after FF3 deleted it (the host deletes it
  last); GameMotionModel::Load leaves the file numbers set after a refused request, so the destructor
  releases the model file a second time. FileCache::Release of a file it no longer holds is assumed
  harmless -- check when a model load fails on purpose (full cache).
- **PC null guards** (no player model while CharModel's load 0x145090 is a stub): MapDrawPlyrMdl,
  PlyrGetJointRotY / PlyrGetJointRot / PlyrGetJointPos. Drop them when CharPlayerModel is real.
- PssModel's overrides Build 0x2ac5f0 / DrawMdlPre 0x2ac6a0 are hand stubs (lib/unassigned_stubs.c);
  they belong with PssModelBuilder (0x2ac6d0..) -- the sky dome's TU or the builder TU.

## Open questions from motion_model_manager.c (2026-10-16)

- **rtti.py's ctor/dtor lists are guesses by vptr stores.** 0x212c40 is listed as BmdMotion's base-object
  constructor twin (and motion.h said so); it is VertexMotion::Update, called every frame on shape and cloth
  motions. BmdMotion's real base-object twin is unknown -- check when motion.c is written.
- **Motion fields are partly guessed** (motion.h, layout: host): +0x10 (_10) is "the data playing" for vertex
  motions (VertexMotion::Stop clears it, Update tests it) but BmdMotion::GetNum reads word +8 of it as the motion
  count (the motion PK4?). BoneMotion and VertexMotion use +0x18..+0x28 differently (BoneMotion: no, frame_end,
  _20, blend, _28; VertexMotion: reverse, loop, _20, no), so those fields sit in the subclasses.
- **BoneMotion.no is an int now** (PlayMotion stores a word at +0x18; MOT_CTRL had short no + _1a).
  GameMotionModel::GetMotNo returns it as short, as before.
- The other MotCtrl* functions (GetEntryId 0x213920, MakeBlendWork 0x213f10, FreeBlendWork 0x2140e8,
  MakeWork44 0x214120) sit in BmdMotion's code: make them BmdMotion members when motion.c is written
  (callers: ingame_wrk.c only).
- MotionModelManager's destructor deletes nothing: MotionModelBuilder's destructor owns the motions and ccs.
  If a model's motions leak or are freed twice once Rei loads, look at who deletes the builder.
- The stubs it calls (motion.c): BoneMotion::Update / IsEnd, BmdMotion::GetNum / ResetPose / VtblCall18 /
  GetFrameNum, VertexMotion::Update / IsEnd / Stop, ShpMotion::Stop / WriteVertex, ShpPhysicsMotion::StopVirtual,
  ClothMotion::Reset / SetWind, Motion::SetFrame. The motion constructors are stubs that set no fields, so
  BuildMotion's shape / cloth counts are 0 (ShpData / ClothData::GetNum stubs) until motion.c is written.

## Open questions from g3dLight.c (2026-10-16)

- **_IsBBLightingupSpot sense (g3dLightEx.c session decides).** FF3 0x1ce0f8 keeps FF2's ROM test: a box
  counts as lit when a corner's plane distance is `< 0` (c.lt.S f0, 0 at 0x1ce144). MioPan deviates to `> 0`
  because gra3d's spot cone opens along vDirection and the ROM sense never binds the flashlight to room
  meshes in front of the player. Port it as the ROM has it first; if the room draws with no flashlight once
  M4 runs, take MioPan's deviation and say so here.
  Decided 2026-10-16 (g3dLightEx.c session): ported with the ROM sense (`fDist < 0`). When M4 draws a room, check
  that the flashlight lights meshes in front of Rei; if not, flip the test in _IsBBLightingupSpot (MioPan's fix).
  2026-10-19: it does not; it lights behind her. 2026-10-20: the host cone was the half to change (it negated
  as MioPan's does); this test keeps the ROM's `< 0`, which is right.
- g3dLightEx.c keeps FF2's two-source _BlendEmulatedDirectionalLight bug (both operands from pSrc0; FF3 0x1ce238 /
  0x1ce278), so g3dGenerateDirectionalLightByEmulatedData drops emulated lights past the third. MioPan found the
  "fixed" blend turns characters black; do not fix it.
- FF3's g3dCalcSpotlightFalloff dropped FF2's fFalloff powf branch (always the linear ramp); G3DLIGHT.fFalloff
  is still defaulted to 1 and may be dead in FF3 -- check its readers if a cone edge looks too soft or hard.

## Open questions from gra3dMirror.c (2026-10-16)

- gra3dGenerateBlendedLight's argument order is a guess: FF3 passes pDest in a0, vTarget in a1 and the
  scale in f12 (EABI: a float's position cannot be read off the registers); written (pDest, vTarget,
  fScale). Confirmed 2026-10-16 by CharPlayerModel::DrawSecond 0x147300 (a0 the light, a1 the box centre on
  the stack, f12 0.25 / 0.4).
- Its light filter reads GRA3DLIGHTSTATUS +8, still named bEnableToShadow (FF2's name, unverified); if
  the character light looks wrong, check what +8 means in FF3 (gra3dGenerateLightDataToChar sets it?).
- Host std::sort may order equal powers differently from SGI STL's introsort; with fewer than four lit
  slots the top four then hold other zero-power entries (null lights, which g3dBlendLight treats as off).
  Harmless unless an enabled light has exactly zero power.
- The mirror's VF2.x -1 and the reflected matrices reach the host only through the graph3d bridge
  (reipan_graph3d.cpp, still to port): it must read the live camera's matWorldScreen at draw time and
  flip the winding when VF2.x < 0, or the reflection draws back-face culled.

## Open questions from model_object.c (2026-10-16)

- **GetCoord ignores its argument** (FF3 0x211b58 passes 0 to GetSgd: `move a1, zero`), so every SGD of a
  model draws with SGD 0's coordinates and SetMatrix / CalcBoneCoordinate only ever touch SGD 0's set. Kept as
  the ROM has it. If a multi-SGD model (several meshes with their own skeletons) draws its later SGDs in the
  wrong place, this is why -- but the ROM does the same.
- **MODEL_OBJECT_FLAG names are read off the passes**, not confirmed with data: [0] no_project (ReqDraw skips
  it), [1] shadow (only the shadow draws take it), [2] alpha, [3] multi, [4] vf2_zero (VU1 VF2 = 0; what VF2
  does in the VU1 program is not known -- gra3d.h says the default is (1,0,0,0)). Check against a model PK4's
  "flgs" members when one draws.
- draw_en_371300 / 371304 / 371308 (sdata, defined in ingame.c, written by IngameSetDrawEnable) sit right
  before Motion's sdata 0x37130c, so they are probably model_object.c's own variables that ingame.c writes;
  left in ingame.c as before (same doubt as draw_en_37126c in model_builder.c).
- DummyTextureModelObject (vtable 0x341248, ctor 0x2ac740, slots 0x2ac7a8..0x2ac800) sits with PssModel's
  code after the singleton getters; not written -- it belongs with PssModelBuilder (the sky dome / movie model).
- ~~gra3dshadowAddProjectModel 0x1e60a0 is a stub~~ (resolved 2026-10-16: gra3dShadow.c written; the parallel
  list 0x3eebf0 holds host SGDCOORDINATE pointers, drawn with their model in _DrawShadow).

## Open questions from gra3dShadow.c (2026-10-16)

- **No shadow is cast yet.** A --run 100 probe (reverted) saw one project model (the room) a frame but
  the projector alpha (VU1 image +0x18c) 0 every frame, so gra3dshadowDrawSGD skips both passes: the caster
  light's diffuse x g3dCalcLightAttenuation at the target is 0. The light comes from model_object.c's
  DrawShadow callers (MapDrawChars / mmanageDrawShadow: the room model's G3DLIGHT +0x50, or the player
  model's flashlight set by char_model.c _SetLightToShadow); check which light reaches gra3dshadowSetLight
  and why it is dark once Rei draws (M5). The first shot with a shadow under Rei or an object marks
  gra3dShadow.c done (compare with the PS2: the flashlight shadow is soft and faint, strength = alpha / 255).
- The host draws the shadow through MioPan's renderer path (caster -> shadow map from the light camera
  _ApplyCamera installs, receivers = the SRT_MAPSHADOW preset meshes replayed with a projective shader);
  the VU1 matrices are built but unused on PC, and s_matIP only gives the receiver culling its plane
  (2026-10-20, see Done). If a shadow shows in the wrong place, compare the host light camera
  (EndCaster's snapshot) with s_matIP first.
- CheckBoundingBoxShadowTrace is the real VU0 test now (MioPan's port passed everything): a receiver block
  outside the shadow volume is skipped. If shadows go missing on some floors, suspect ShadowBoundClip's CLIP
  flag emulation (bit 0/1 +x/-x, 2/3 y, 4/5 z against |w|) or AppendShadowClipCheck's two edge groups.
- ~~`graphics/graph3d/g3dRenderTarget.cpp` partial~~ (done 2026-10-16 as verify, with `graphics/graph3d/g3dSprite.cpp`
  0x1cf348..0x1cf838 + linkonce dtors 0x2bbd70/0x2bbd80, verify). Open: **the host never executes the g3d
  chain's DIRECT GIF packets** (reipan_renderer.cpp has no GIF / SPRITE interpretation), so CSprite::Draw and
  CRenderTarget::Clear produce nothing on PC; the shadow map is the host's (g_shadow_episodes) and is cleared
  there. If a later host pass starts replaying DIRECT packets, honour FRAME_1 / SCISSOR_1 (Begin redirects them
  to the 256x256 shadow buffer at TBP 0x3000) or the clear sprite lands on the main frame as a black square.
  FF3's Clear ignores its colour argument (always 0) and builds left = cx - h/2 (FF2 quirks, kept).
- CAutoTransform<0> is never pushed in FF3 (no Push call anywhere), so its Pop in the dtors does nothing.

## Open questions from mmanage.c (2026-10-16)

- ~~PC null guards on the model handle's mot~~ (resolved 2026-10-16: MapObjMdl.c written, the guards in
  MapObjWrk.c, MapDBuff.c, MapDraw.c, MapLight.c, MapPut.c, n_plyr_camera.c and IngameDrawRoomMorph are gone).
  Still there: MapObjSetWindCloth guards GetCcs() == nullptr (a model without a ccs member: FF3 writes through it)
  and mot == nullptr; MapDrawRoomObjRegist's effect case guards a failed MapObjRegist (FF3 reads address 0).
- **MAP_ROOM_MDL is gone**: MAP_ROOM_DAT.mdl is the EE address of the room's ModelManager (host layout: the
  vptr is 8 bytes, so +4 is not the object on the host). IngameDrawRoomMorph reads ani_ctrl, MapDrawPlyrShadow
  calls DrawShadow, MapLoadRoomMission adds / removes GetCollision(). MapDrawInitRoom must store
  ReiPan_GetPs2Address of a host-new'd ModelManager (operator_new allocates in EE memory).
- ~~ModelManager vtable +0x28, +0x30 and +0x48 argument guesses; the VtblCallXX names~~ (resolved 2026-10-16 by
  motion_model_manager.c: +0x28 SetFrame(float), +0x30 StopShapeAll(), +0x48 GetShapeNum(); +0x3c IsShapeEnd
  takes the shape index -- PlyrCameraFaceUpdate passes col->cur[i], fixed there).
- ModelCcs::SetCoordinate keeps the ROM's w = 1 in matrix rows 0 and 2 (it normalizes the point difference with
  w = 1 and crosses from row 0). If chained parts (hair) draw skewed once a MotionModelManager runs, look here.
- draw_en_37127c (sdata 0x37127c, initial 1) gates the chain points' collisions; it sits in this file's sdata
  but stays defined in ingame.c (IngameSetDrawEnable writes it); its initializer is now 1 as in the ROM.

## Open questions from MapLight.c (2026-10-16)

- Boundary: 0x1197c8..0x119fb8 (MapLBuff*, MapDoorVec*) works on MapLBuff.c's table 0x37fd40 and its current door vec, and its data
  (the scale table 0x2f14f0) sits right after MapLBuff.c's quad template 0x2f14b0. It was kept in MapLight.c because FF2 starts
  MapLight.c right after MapLBuffInit; if MapLBuff.c's session finds a reason, move the range there (tus.py define both).
- The hit vec table holds a MAP_PUT pointer at +0x120 and is save block 51 (MapLBuffSaveGet, MapHit.c: {0x37fd40, 0x2600}; see MapLBuff.c below). On the
  host the pointer is 8 bytes and points into this process: when MapHit.c writes the save getter, decide what a loaded save does with
  it (the EE reloads the same address; the port should rebuild the puts or clear the table on load). map_lbuff / map_door_vec are
  defined in MapLBuff.c.
- MapLightForEach 0x11a0a8 passes nullptr, not its light set, to the slot getters, so MapLightCountInitial counts the lights of the
  current gra3d bank, not of blend set 1 (ROM, kept). Its point range is 3..26 and spot range 30..37, while MapLightMakeDual fills
  3..27 and 30..38 (then switches 27 and 38 off).
- MapLightIsPlayerLightOn reads player flags 0x8000 (the light motion, PLYR_WRK._2ec 1) and CharPlayerModel +0x1bc (fl_on, writer
  not traced). PC guard in MapLightMakeRoomReal: with a null player model the player light counts as off.
- ~~PC guards in MapLBuffSetDoorCoord / MapDrawPrelightRoom~~ (dropped 2026-10-16 with MapObjMdl.c).
- Names in the ledger that come from the old wrong matches and still live in other files: MapEff.c's MapLightReg* (event-label walls
  in the hit vec table, not lights) -- rename when MapEff.c is next touched.

## Open questions from MapLBuff.c / MapHit.c (2026-10-16)

- Boundary kept: MapLBuff.c is 0x119390..0x1197c8, MapHit.c 0x119348..0x119390 (MapHitRegistCallback, MapLBuffSaveGet, MapHitRegistDoorVec);
  MapLight.c keeps 0x1197c8..0x119fb8 (already written there; moving it is a file reshuffle for an interactive session, nothing
  forces it). All three files share map_lbuff (defined in MapLBuff.c).
- Save block 51 (MapLBuffSaveGet) now reports the host table (sizeof map_lbuff, not 0x2600) and still carries host MAP_PUT
  pointers. Still to decide when saves load in a game: rebuild the door vecs from the rooms or clear the table after a load.
- MapLBuffSetDoorQuad writes the per-bank half widths into its corner template (data 0x2f14b0) before copying, as the ROM does;
  bank 0 is 6 units thick (half 3), bank 1 20 (half 10), leaf length 18 (kind 2 is shifted to x -18..0). Which player state picks
  bank 1 (flags2 bit 25) is not traced.
- MapLBuffRegist / MapLBuffLoad / MapLBuffInit keep the matcher's FF2 names (FF2 bodies differ completely: FF2's per-map load
  buffers are gone); the new ones are MapLBuffIsDoorLoaded, MapLBuffSetDoorQuad, MapLBuffGetFree, MapLBuffLoadVec.

## Open questions from MapDraw.c (2026-10-16)

- MapDrawIsEnableFlashlightOnly (sdata 0x36b7f8, initial 1, MapDrawLightReset sets 1) gates MapDrawSetRoomLight: 1
  sets the room's lights, 0 leaves the current ones (MapPutSetLight reads it the same way). The FF2 name says the
  opposite; its writers are the scene code 0x253728 / 0x253a08 (unassigned@00253620). Rename once those are read.
- MapDrawRoomObjRegist returns from the whole function when a door's MapObjRegist fails (ROM behaviour, kept):
  the rest of the objects, MapEffRoomRegist .. MapEffVisibleUpdate and MAP_ROOM_DAT stat 4 are skipped. If a room
  shows without its effects or lights, check for a door put that failed (150 puts, MapPutGetFreeArea).
- PC guard in MapDrawSetRoomLight: AllPlyrIsReady passes while plyr_wrk->mdl is nullptr (the player model's ctor
  0x145c50 is a stub), so a null model is treated as not ready. Drop the guard when CharPlayerModel is built.
- FURN_REC_FLAG1 (record flag 1) is cleared by FurnLoadClearFlag1 (MapDrawDeleteNoDraw) but no setter was found
  (FurnLoadSetRecFlag's only callers pass 2). FurnLoadGetSize / FurnLoadGetFlagPtr have no callers.
- FurnLoadSetMotPk4 tests the record for null (FF3 does not); MapDrawLoadFurnMot always runs after FurnLoadAddRec,
  except when the 48 records are full -- FF3 then writes through address 0x34.
- PlyrCameraMapObjDraw now takes (put, flags) (a MAP_PUT_DRAW_FUNC); its body does not read flags.

## Open questions from gra3dMisc.c (2026-10-16)

- ~~**MapDraw.c light getters misnamed.**~~ Resolved 2026-10-16 (MapDraw.c session): 0x117b90 is MapDrawGetLightPtr4BuffID
  now, 0x1176b0 FurnLoadDeleteRoom; bank 1 (0x37d0f0) is the light set the room was last drawn with (bank 0 after
  CLightCtl::Apply and the lantern, plus the player's flashlight in light 38), which the room's map objects use.
- gra3dDrawSGDShadowEveryObject hands gra3dshadowSetBoundingBox the coordinate's first matrix
  (pCoord->matCoord, as FF2 did with the model's own pCoord); if the per-object shadows land in the wrong place
  once mmanageDrawShadow runs, check whether matLocalWorld was meant.
- _SetLightToShadow copies MapDrawGetLightPtr's set without a null test, as the ROM does; a crash there means
  it ran with the player's area not loaded.

## Open questions from gra3dSGDData.c (2026-10-16)

- **SGD walks before sgdRemap.** MapPut.c's MapPutGetBoundingBox already calls sgdGetBoundingBox, and
  gra3d.c's gra3dExecPrelightMatrix sgdCalcCoordinate, but nothing calls sgdRemap yet (its callers are in
  unassigned@00210958). A walk over a model that was never remapped follows file offsets as EE addresses
  (low EE memory): wrong boxes, possibly an endless chain. The --run 100 smoke run shows no crash. Write the
  model managers' load path (sgdRemap) before trusting any SGD walk; if a walk misbehaves first, check
  SGDFILEHEADER.ucMapFlag.
- **MappingVertexList's word read.** FF3 sums each run's usNumVector and vOff as one word (lw) before storing
  vOff, so a second remap of the same data would overflow the size check; FF3 never remaps twice
  (sgdRemapInverse has no callers). Kept as FF3 does it.

## Open questions from gra3dSGD.c (2026-10-16)

- **Material packets in EE memory.** FF3 stores the material packet's address (& 0x0fffffff) in
  SGDMATERIAL.iTagAddressOld and REFs it while the light set is unchanged. The port stores
  SgdAddr(packet), which is 0 when g3dDmaOpenPacket's buffer is not in emulated EE memory; SetMaterialDataVU
  then rebuilds instead (a PC guard). Resolved 2026-10-16 by g3dDma.c: g3dDmaOpenPacket returns
  ReiPan_GetHostPointer(PACKET_BUF_MGR::GetPacket()), inside the emulated EE RAM, so SgdAddr(packet) is the
  real address and the cache works as on the PS2; the guard can stay as a safety net.
- **VU1 program / subroutine addresses** are passed as u_int (gra3dDmaLoadVu1MicroProgram(0x2df690, 0),
  gra3dCallMicroSubroutine2(0x12f8)): the first is an EE data address of a DMA chain the PC never loads, the
  second a VU1 micro-memory address. FF2 typed both const u_int *; host pointers would be meaningless.
  Their prototypes sit in gra3dSGD.c's unassigned@001ddf80 block (the rest of FF2's gra3dDma.c, with
  gra3dCallMicroSubroutine3 / 4 at 0x1de038 / 0x1de088 and Vu0LoadMatrices_4_25_8 0x1de0d8).
  g3dDma.c (2026-10-16) types them as FF3 uses them: g3dDmaLoadVu1MicroProgram(const u_int *pMPG) (one
  argument; FF3 dropped bImmediately) walks the program's DMA chain in memory, and
  g3dDmaCallVu1MicroSubroutine(u_int uiMSAddr). **The ELF's data is not mapped into the emulated EE RAM**,
  so gra3dDmaLoadVu1MicroProgram must NOT pass ReiPan_GetHostPointer(0x2df690): _AppendVUProgTag would walk
  zeros (never an END tag) and flood the packet buffer with empty REF tags. Pass nullptr instead: the cached
  program s_pMPGOld starts null, so g3dDmaLoadVu1MicroProgram(nullptr) does nothing (a PC comment there).
  Done 2026-10-16: gra3dDma.c (0x1ddf48..0x1de0d8, all six functions, verify) passes nullptr and keeps
  the VU1 addresses as u_int; its header now holds the prototypes gra3dSGD.c had in its unassigned block.
- **Vu0LoadMatrices_4_25_8** (0x1de0d8) only loads VU0 registers for gra3dCalcMeshScreenRect (0x1df2d8);
  on PC that pair needs host statics -- decide when unassigned@001df2d8 is written.
- **0x1dfc58** was a wrong ff2-match-low "gra3dDmaLoadVu1MicroProgram"; renamed MapBuffApplyBothFlag1
  (calls 0x1dfbc8(1): FUN_0011a3d0 on both map buffers). Its real role is unknown.
- gra3dsgdDrawPresetDataObjectNoClip 0x1e3290 is unreferenced in FF3 (reconstructed as a global so the
  compiler keeps it, like the ROM did).

## Open questions from movie.c (2026-10-15)

- Phase 39 Story_SceneMovie 0x2185a0..0x218674 was put at the end of `graphics/movie/movie.c` (it reads the projector state
  through movie_projecter.c's getters, so it is outside that file); it may be its own FF3 file. Move it if a later signal says so.
- `BORROW_BUF` is an invented name (no RTTI; 8 bytes {heap, adrs}). scene.c's 0x253288 / 0x253b18 use it too: when scene.c is
  written, take the type from movie.h.
- StartMovie passes SndGetGroupVolume(1) as FF2's SND_GROUP_BGM; confirm group 1 is BGM when snd.c's groups are named.
- ReqQuerySPUTransCoreGet / Release (ee_iop_q.c) are generated stubs: auto_dma_core stays 0 on PC (MioPan's are empty too).

## Open questions from ev_chapter.c / ev_exe.c (2026-10-15)

- **ev_chapter.c is an invented name.** FF3 links ingame/event/prg alphabetically (ev_change, ?, ev_exe,
  ev_get, ev_macro, ev_main, ev_open), so 0x19a0a0..0x19abe8 is a file whose name sorts between ev_change
  and ev_exe; chapter title, ghost tables and GetObjectPos share it. Rename it if a string or FF2 hint ever
  names it. The tiny file pinned as ev_get.c (0x199268..0x1992d0) sorts before ev_change, so its name is wrong
  too; FF2 ev_get.c's functions are the 0x19b158 run.
- **The chapter title has not been seen.** CHAPTER_DISP_START needs a running event program (ev_open.c's
  EventCtrlCenterMain + ev_macro.c's EventExeFuncCall). Reference: `python tm2png.py --file CHAPTER_TITLE_01_TM2`
  ("Hour I / The Sign", top 0x99 rows, x 0 y 295; the second sprite is rows 0x9c..0xe3 at x 254 y 377), over a
  shade darkening the lower half of the screen (effdat[68]). Language files: CHAPTER_TITLE_ + language * 0x10
  (E, F_, G_, S_, I_); chapters 00 and 15 have no file in any language (the title is dropped at once).
- EvGhostTblRegist asks EvGhostTblIsRegist and ignores the answer (kept: a ghost can be registered twice).
  EvGhostPreloadIsEnd stops at the first row whose preload bit is clear, so a preload ghost after a normal
  one is not waited for (kept as FF3 does it).
- PC: EvExeCtrlSaveGet returns the host size of ev_exe_ctrl (pointers make EV_EXE_CTRL 0x20 bytes); the save
  file is PC-only anyway. EvExeCtrlSaveFixup turns the slots' addresses into offsets as on the PS2, so saved
  slots do not depend on where the host put the event file.

## Open questions from ev_get.c (2026-10-15)

- FF3 dropped every guard MioPan's ev_get.c has: EvGetTblAddr / EvGetExeAddr never answer null, and
  EvSetSubId has no bound on its 50 slots. Kept faithful (EventDataLoad has event.obj in before EventMain).
  If an event reader runs before the file is loaded on PC (zeroed memory), EvGetParentID walks until it
  meets a -1 word and EvSetSubId writes past id_tbl: a hang or a stack smash early in an in-game run
  points here; MioPan's null guards (offset 0 = not loaded) are the fix.
- EvGetCmdArgs leaves a slot untouched for a digit other than 1 / 2 / 4 but still counts it; no format in
  the tables has one (evtbl.py), so the dispatcher's uninitialised 0x80-byte stack array never leaks.

## Open questions from ev_open.c (2026-10-15)

- EV_COND_WRK +0x10 (was `_10`) is `int *args`: EventCtrlCenterMain and EventConditionJudge decode a
  condition's arguments (EvGetCmdArgs, 0x40 bytes of stack) and store the array's address there before the
  test, as the command dispatcher does with EV_EXE_CTRL.args. No reconstructed condition reads it (they
  memcpy their bytes), so it is dead on PC unless a later condition uses it; IF_ / ELSEIF_ (EvCondCallJudge)
  do not set it, so a condition reading it from an IF_ would see the last centre frame's stack.
- PC guards where FF3 has none: SetEventPriority drops a claim when all 450 rows are used (the EE writes past
  the table), EventSetCondition skips posting when the centre is full (the EE writes a row at address 0),
  EventCtrlCenterMain / EventConditionJudge treat a row without a test as false (only EV_OR's, never posted).
- EvNowSynchroMode 0x21a8b0 moved to npc_act.c (it sits inside that object's range; an earlier note kept it
  in ev_open.c without a reason). Its name is FF2's low match; FF3's NOW_SYNCHRO_MODE condition (ev_change.c
  0x199768) is a different function that always answers 0.
- FUN_0022aa58 (bit test of the 0x18-byte flag block 0x3f7770, save block FUN_0022a9d0) is a stub (0) in
  unassigned@00229478: PHOTO_STATE_ and PHOTO_OBJ type 8 answer from it. Name it with its run.
- Event tracing: a printf of ctrl->event_id / com / ev_exe_wrk[com].name in EventExeCmdCall (ev_macro.c)
  and of event_id in SetEventInitStatus shows the story's progress in a smoke run; cap it (400 lines) and
  revert before finishing.

## Open questions from ev_main.c (2026-10-15)

- ~~**ev_open.c starts at 0x19baf8, not 0x19bb48**~~ (resolved 2026-10-15: pinned 0x19baf8..0x19c658) (ev_main.c's static init 0x19ba40..0x19baf8 ends its file,
  GCC emits it last): EvCtrlCenterSaveGet 0x19baf8, EvPriCtrlSaveGet 0x19bb10 and SetEvCtrlCenter 0x19bb28
  (FF2 ev_open.c's static) are an unassigned run now. The ev_open.c session pins
  `ev_open.c 0x19baf8-<end>` (the end is open too: EvCtrlCenterSaveFixup 0x19c3a8 and its two callers
  0x19c450 / 0x19c468 act on ev_open's table and are probably its last functions).
- ~~**The ladder list is a class without a vtable**~~ (done 2026-10-20: ev_ladder.c written, class
  LadderList with member Load / Release / GetData / IsLoadEnd, callers in ev_main.c and plyr_sub.c
  fixed; LADDER_DAT moved to ev_ladder.h. Verify: hasigo.ecb loads, 9 ladders id 0..8, but the smoke
  pad never climbs one -- the first session whose pad reaches a ladder marks it done).

## Decisions waiting for the user

- ~~(2026-10-14) MioPan moved to ../MioPan-Mikompilation~~ resolved the same night: the user recloned ../MioPan with SLES_523.84 and functions.txt. The fallbacks stay (reipan.MIOPAN tries ../MioPan, then ../MioPan-Mikompilation; features.py keeps cached FF2 features and export.py / ctxcard.py use matches.json files when the FF2 files are missing). The recloned MioPan has no cmake-build-*/_deps, so ReiPan's build dir fetches SDL3 / spdlog / cglm itself.

- ~~**Renderer colour clamp**~~ resolved 2026-10-17 (interactive session): textured vertex colours reach
  255/128 and sprite.frag clamps the product (hazards.md "Textured vertex colours above 0x80"). Text shows
  its label colours; logos, title and the opening's 3D frame (read 2040) are unchanged.
- **Real code parked in `src/lib/unassigned_stubs.c`**: EvBinChangeAddr4 (0x19b278, inside FF3's ev_get.c
  at 0x19b158..0x19b4a8); move it when that run is defined. (Get1/2/4Byte moved to common/get_byte.c,
  2026-10-14.)
  (cmp_eeiopGetWrkSize / cmp_eeiopInit moved to cmp_eeiop.c, 2026-10-13.)

- ~~**The window freezes while it is dragged**~~ resolved 2026-10-15 (the user chose option a): the game
  now runs on its own thread, hazards.md "Two host threads". Old text: (reported by the user during the story_movie.c run):
  Windows' modal move/size loop blocks SDL_PollEvent on the main thread, which is also the game loop, so
  nothing draws until the mouse is released (audio keeps playing; the movie catches up afterwards since the
  libmpeg_video.cpp fix). Options: (a) run the game loop on its own thread and keep the main thread for
  window events (the real fix, a platform-layer restructuring: every SDL video / GPU call would have to
  stay on one thread); (b) an SDL event watch that re-presents the last frame on SDL_EVENT_WINDOW_EXPOSED
  during the drag (the window keeps its picture and redraws on resize, but the game still pauses).
  Not changed in an automated run.

## Ghidra fixes waiting for a session with write access

Autopilot sessions cannot run `ghidra.py` (hazards.md "Autopilot sessions"); do these interactively.

- ev_get.c (2026-10-15): 0x199130 is named SetSave_EvExeCtrl (an FF2 match) but is the _GLOBAL__I_ that
  calls the static init 0x199108 (zeroes the 0x3e0068 pair on (1, 0xffff)); it closes the file before
  ev_ladder.c. ~~The ladder list's two ctors 0x199150 / 0x199158 have no functions~~ (mkfunc'd
  2026-10-20: LadderListCtorBase / LadderListCtor).
- plyr_sub.c (2026-10-14; Ghidra went down before the second batch): ~~name 0x23ec08 `PlyrMdlChange`~~
  (done in the next plyr_sub.c session, batch plyr_sub_003). Check that 0x23e208 PlyrLadderStartBottom exists as a function (mkfunc ran,
  its save hit an active transaction; name_ctx saved later). Hidden functions the code calls by
  address: 0x145528 (CharPlayerModel vtable +0x34, joint -> coordinate index) and 0x1d2cf0 (GameModel
  vtable +0x10, loaded) -- mkfunc both.
- Functions Ghidra hides inside neighbours (mkfunc, then name_ctx):
  - snd_buffer.c: 0x26ace0 (after SndBufFadePitch's end 0x26acdc, before ee_iopInitSub): looks a
    buffer up with 0x26a368 and tail-calls snd3DSetSET 0x26ed80 with its +0x14 member (FF2
    SndBufSet3D).
  - pad: `mkfunc 0x2256a8 PadGetCount`, `0x225840 PadGetStickFloat` (CheckPadNowPlaySe must end at
    0x2256a8, PadGetStickWord at 0x225840); chunk-table accessors 0x1efe20.. inside operator_delete_array.
  - IngameMain's GS callback 0x1d1c38; capCtl's complete dtor 0x2ad408 (vtable 0x341560 slot 0).
  - B2d/Bp: 0x106cd8 B2dDrawQuadFlatDirect (ends 0x106e58), 0x106e58 / 0x106e60 (8 bytes each),
    0x106520, 0x107408, 0x107568 BpCtlStepPlay (ends 0x1075c0), 0x108228 B2dItemGrowBounds (ends
    0x1082d0); B2dFile.c's 11 command handlers were created 2026-10-13 (0x104660..0x104df8); B2dCtl.c: 0x1024d8 B2dCtlFreePartKeys, 0x102700 B2dCtlAddPartKey, 0x102798
    B2dPartGetKeyIndex, 0x1027d0 B2dPartGetKeyByIndex, 0x102860 B2dCtlRemoveItem, 0x1028f8 (return 0),
    0x1029f0 B2dCtlDeleteItem, 0x102fc0 B2dCtlKeepFirstKey; hook getters 0x100b98, 0x100bc0, 0x100bc8,
    0x100bd8, 0x100be8.
  - anim_2d.c: 0x151400 HudDrawFire (inside HudDrawLens), 0x1517d0 (anim_2d's `_GLOBAL__I_`), 0x152040,
    0x1520f0, 0x152398, 0x152490, 0x1526d8, 0x152820, 0x152d70.
  - anicode.c: 0x14d350, 0x14dac0, 0x14e040, 0x14e2f8, 0x14e570, 0x14e710 (code after tail calls);
    hook bodies 0x14f058, 0x14f100, on_close 0x14f338, getter 0x14f348 (word 0x3918c0); CcmLoading
    vtable members 0x14ec50 (Normal OnReady), 0x14ece8 (Normal StepLeave), 0x14ee20/0x14ee50/0x14ee68/
    0x14eea0 (Mission OnReady/StepShow/StepLeave/End); ctor copies 0x14eb58, 0x14ed38; base-class
    inline defaults 0x2adce0..0x2add38 (inside FUN_002adc78); empty 0x1ad1b8 (inside FUN_001ad0d0);
    type 8 on_open 0x14f798 / empty 0x14f848 / on_close 0x14f850 (inside MenuWdwOnUpdate), pause menu
    steps 0x14fbc8 / 0x14fca0 (inside PauseMenuStepSelect), type 7 on_open 0x150190 (inside
    SlotSelDrawChoice) and on_close 0x150290 (inside SlotSelOnUpdate).
- title_menu.c (2026-10-14): rename 0x243b00 `SixPuzzleBookSelArrowAnim` (wrong FF2 match) to
  `TitleObjDtor` (it is TITLE_OBJ's destructor: MapSkyReset, file -1); 0x25dd48 `TitleMovieFlagClear`
  zeroes the saved title menu cursor (gp+0x500), 0x25e120 `TitleMenuFlagSet` sets the difficulty to 1,
  0x1cff08 `TitleMenuIsItemOpen` is "difficulty i unlocked" (better names: TitleMenuCsrClear,
  TitleDifficultyReset, TitleDifficultyIsOpen; callers in main.c / ingame.c / ingame_wrk.c).
  mkfunc 0x2bf308 (a destructor, vtable 0x3425a8, calling 0x2603a8, right after
  TitleDifficultyGlowUpdate 0x2bf270). 0x25e3f0's mkfunc save hit an active transaction (name_ctx
  saved later; check it exists).
- Unique name for 0x13e5d0 (cmn_fade_layer.c's `__static_initialization_and_destruction_0`).
- Ghidra's function at 0x13e648 is broken (decompiles as GPhaseCtrlTerm): check its asm.
- Label the 112 vtables / typeinfos with their RTTI class names (`rtti.py` has them) so decompilations
  show `GameModel` instead of `PTR_..._00341f80`.
- Repo cleanup the autopilot could not do: delete `src/lib/crt0.c` (comment-only, stubgen made it) and
  `src/outgame/gallery.c/.h` (empty placeholders).

## Doubts from memo.c (2026-10-16)

- ~~`IsFinderEnable`~~ (done 2026-10-20: renamed MemoMenuIsEnable, written in menu_file_list.c; MEMO_FLG._4 is
  file_seen, MEMO_PAGE._4 was pic) 0x2070b0 (unassigned@002070b0, a stub) gates the memo menu: it tests MemoGetOpenPageNum,
  and cmn_fade_layer.c / story_menu.c pair it with pad_key[PAD_KEY_MEMO]. Rename it (MemoMenuIsEnable?) when
  its run is written; MemoMenuReq 0x207148 (named this session, still a stub) requests phase 0x3e with the
  memo at 0x3f6048, mode 2 / 0 at 0x3f604c and ret at 0x3f6050. That run is the memo menu's; StoryMenu_File
  0x204810 (right after memo.c) builds MEMO_LISTs.
- A second MEMO_NOTICE lives in unassigned@001b1648 (0x1b3df0 ctor, 0x1b3e70 dtor, 0x1b4258 MemoSetPage +
  Add, 0x1b42f0 Step): probably the mission mode's own notice; take it with that run.
- MEMO_PAGE._4 (1875..1928, mostly ascending per memo) is unread here: a picture or file number for the
  memo menu? Name it when the menu is written. MEMO_FLG._4 (0x37115c) is read by 0x204d28.
- MemoNoticeDraw passes the title flag from GPHASE_CTRL.next != 0x2c (the requested phase, not the current
  one): kept as in the ROM.
- Picture that marks memo.c done: the new-memo notice (top right, lang pack picture 10 with the memo title)
  after an event's MEMO_OPEN with SET_MEMO_OPEN_FALG 1; needs the event core running.

## TU names known to be wrong (rename later; decompiling comes first)

File names come from FF2's link order and are guesses (hazards.md "Inferred file names"). These are
known to name the wrong thing; rename them (file move + `tus.py define` + includes) in a quiet moment or
when the TU is next touched, recording the deciding signals (`tuid.py <TU>`):
- `ingame/photo/n_plyr_camera.c` — FF3's companion-NPC controller (CharNpcModel, the NPC_* event
  commands, the NPC act hooks); FF2's player camera has nothing in common with it (5 low matches, all
  renamed 2026-10-15). A better name: `ingame/npc/npc_ctrl.c` (users: ingame.c, ev_command.c,
  save_data.h). The neighbours 0x219e60..0x21e360 (n_equip_tray.c + unassigned runs) are the same NPC
  subsystem: the act machine, its op tables and the NPC data are `ingame/npc/npc_act.c` since
  2026-10-15 (0x21a678..0x21a8b0, 0x21a8d8..0x21d330); 0x219e60..0x21a678 is still unassigned.
  0x21d330..0x21e360 is `ingame/photo/n_equip_tray.c` (pinned and written 2026-10-15, verify): the
  NPC helpers (player turn, label motions, model data, accessories, motion loop SE); FF2's
  n_equip_tray.c is the camera's equipment tray and shares nothing. A better name:
  `ingame/npc/npc_sub.c` (users: n_plyr_camera.c, npc_act.c, ev_command.c). When n_plyr_camera.c
  moves to ingame/npc/, move this one with it (or fold both into one file).
- `graphics/motion/anicode.c` — FF3 menu windows / layout-type callbacks (message viewer, yes/no, item
  get, slot select, pause menu) and the `CcmLoading*` classes; 6 of 95 functions match FF2 anicode.
- (done 2026-10-19: the CmnLayer* names are CatCtrl* / CatMdlCreate, cat_ctrl.c written.) `alg_manageReqAlg`
  0x147990 is char_model.c's GPhaseCtrl atexit thunk. 0x1479b0.. (CharPlayerShapeStatusFace / Hand
  methods, then 0x148930.. model draw helpers) is still inferred as alg_manage.c; check before writing.
- `album/prg/scr_play.c` — `AnimeCodeManager`, FF3's anime-code player (FF2 anicode's real successor);
  the name was invented from the album neighbours.
- `ingame/plyr/plyr_gauge.c` (defined 2026-10-14, album_save.c session) — invented name for the
  player-1 gauge at photo mode work +0x3c0 (every function tests PlyrGetCurNo() == 1 and player flag
  0x100000; damage fills it, PlyrEquip4Rate drains it, an SE at 0). Probably Kei's ability gauge; name it
  for what it is once its draw (FUN_00129c98) / per-frame (FUN_00129d70) code is read. Its getter in
  finder_photo.c is still called GetAlbumSaveDisp (field album_save_disp): rename both then.
- `ingame/puzzle/kai/kai_pzl.c` (inferred) — held the language select (moved to `outgame/lang_sel.c`) and, 0x1ec000..0x1ec978, the item menu's cursor / picture / scroll-bar helpers (now `ingame/menu/menu_item.c`, 2026-10-21); the real kai puzzle code is elsewhere
  and the `Story_Menu` phase besides 8 puzzle functions. Its last inferred member KaiPzlTerm 0x1f2990
  is door code (now in `ingame/story_door.c`); the src/ files were deleted 2026-10-15.
- `ingame/story_door.c` (defined 2026-10-15) — invented name for FF3's area table + door module
  (phase 0x22 Story_Door_Open). KaiPzlTerm 0x1f2990 renamed MapDoorAreaAtPos (2026-10-15).
  HcBaseLineStraddle 0x1e7d70 was a wrong FF2 match too (FF3's tests a point in a 4-vertex
  rectangle): renamed HcBaseIsInRectXZ. FurnWorkFree 0x10dad8 / FurnWorkAlloc 0x10d970 were wrong
  FF2 names as well: renamed LoadReqAlloc / LoadDtor (CLoad, common/Load.c) 2026-10-15.
- `ingame/map/FurnCtl.c` (inferred, 0x10bbb0..0x116fa0) — a dozen FF3 C++ class files, one class
  each, linked alphabetically (map under "Boundaries seen, not defined"); 11 low FF2 matches. CProcCtl
  and CWindowMsg were split out 2026-10-14 (`common/ProcCtl.c`, `common/WindowMsg.c`), CMapRec and
  CMarkDat 2026-10-15 (`ingame/map/MapRec.c`, `ingame/map/MarkDat.c`), CLoad and CMapDBuff
  2026-10-15 (`common/Load.c`, `ingame/map/MapDBuff.c`), CLightCtl 2026-10-15 (`ingame/map/LightCtl.c`), CGameClear 2026-10-15 (`ingame/clear/GameClear.c`), CmemShelt / CmenuCamBase / CmenuCamMain 2026-10-15 (`common/memShelt.c`, `ingame/menu/menuCamBase.c`, `ingame/menu/menuCamMain.c`). MDL_MOT (motion.h) gained EE vtable slot
  +0x08 VtblCall08(matrix) from MapDBuffDraw; MAP_PUT's draw callback (word +0x18, MapPutSetDrawFunc
  0x11c5d8) takes (put, MapPutDraw flags), unlike MapEff's one-argument MAP_PUT_FUNC.
- `ingame/map/MapPhf.c` — the generic PHF pack reader (used by BpCtl for every layout), not map code.
- `ingame/puzzle/roku/roku_pzl.c` (inferred 0x2447b0..0x245990 since 2026-10-19; was 0x23c6f8..) — no roku puzzle code in it: the nine
  SixPuzzle* names are low FF2 matches scattered over the whole run. Split out 2026-10-14:
  `ingame/plyr/plyr_sub.c` (0x23e208..0x243898), `outgame/title_obj.c` (0x2439d8..0x243e00) and
  `ingame/puzzle/puzzle.c` (0x243e00..0x2447b0, FF3's puzzle driver; RokuPzlWrkClear was its clear-flag
  reset). The rest is listed under "Boundaries seen".
- **The five puzzles** (2026-10-14, puzzle.c session): phase 49 (was "Story_Overlay") is the puzzle
  driver; its children 72..76 are the puzzles, by layout file and SE bank: 72 KUSI (combs, 3 boards,
  PZL_KUSI_YLW/BL/RD), 73 KAZA (2 boards), 74 KAMON, 75 SIZUMEDAMA (Ghidra/gphase.c call it
  Story_StonePedestal), 76 ROUSOKU (candles: the "R_rousokuhi_00/01" sprites; called
  Story_SaveLoadScreen). Their code is inferred as roku_pzl.c 0x2447b0..0x245990,
  save_load/prg/save_load_disp.c, ingame/savepoint/savepoint*.c (open / close / has-item callbacks
  0x24a760.., 0x2486f0.., 0x245f18.., 0x24b5b8, 0x244868: declared for now in puzzle.h). The FF2
  SavePoint* / SixPuzzle* / SaveLoad* names there are suspect. Define them as
  `ingame/puzzle/<name>/<name>_pzl.c` (FF2's layout) when they are ported; FF2 had kaza_pzl.c too.
- `ingame/plyr/plyr_sub.c` (pinned 2026-10-14) is a descriptive name (sorts after plyr_mdl.c as FF2's
  basename link order needs). No atexit thunk inside 0x23e208..0x243898, so it may be several FF3
  files (the ladder 0x23e208..0x23e880, the model / costume code, the move / crawl code, the fade
  envelope 0x243390..); split it if a better signal turns up.

- `ingame/event/ev_queue.c`, `outgame/outgame_bg.c`, `ingame/story_save_mc.c`, `ingame/story_save.c`
  (defined 2026-10-14 from unassigned@0024f548) — descriptive names. They break the basename link
  order (they sit between savepoint_top.o and scene.o, so FF3's names sort between those); no FF3
  string names them. outgame_bg.c and story_save_mc.c have no marker between them and may be one object.

- `ingame/map/MapObjWrk.c` (0x1f8af8), `ingame/map/MapRoomFile.c` (0x1f9b48), `ingame/map/FurnSave.c`
  (0x1fa310), named 2026-10-15 by what they do: by the link-order rule (hazards.md, ASCII by basename) the
  files between `man_data.c` and `map_bgm.c` must sort between those two names (lowercase `map_a...`,
  or `map` + an uppercase letter), in this order. They may also be one FF3 file (the three share the
  furniture rows). Rename when evidence turns up.

## Boundaries seen, not defined

- ~~0x23c028..0x23e208~~ (defined 2026-10-19 as `ingame/plyr/plyr_shot.c`, see architecture.md; the
  descriptive name sorts between plyr_mdl.c and plyr_sub.c; the atexit thunks 0x23e1c8 / 0x23e1e8 close it).

- 0x1f73b8..0x1f7480 (2026-10-15, MapSnap.c session): a small table of load-address pairs (bss
  0x3f16e8 + i * 8: FUN_001f73b8 getter, 0x1f73d0 / 0x1f7400 / 0x1f7428 / Sub1f73b8GetWord4) used by
  LoadingCtrlInit, MapLoadCallFurn, RoomFileLoadReq / RoomObjPackLoadReq / ...LoadEnd and
  MapDrawRoomObjRegist; left unassigned (not MapSnap's data). Probably belongs with loading.c (0x1f7480)
  or MapRoomFile.c.

- 0x199160..0x199268 (2026-10-15, ev_change.c session): a small FF3 class (load of file 0xad9 to
  0xfcd000 via LoadReqGetAddr 0x199160, a release 0x199190, an Ecb row search by id 0x1991a8, IsLoadEnd
  0x199250, inferred as ev_gaze.c's Init) whose global sits at sdata gp-0x4d0; its getter 0x199268 and
  the static-init pair 0x199278 / 0x1992b0 were pinned to `ingame/event/prg/ev_get.c` so the run
  0x1992b0 does not dangle. The whole 0x199160..0x1992d0 is probably one file (ev_gaze.c's slot);
  define it together when that TU is taken.

- ~~0x203310..0x203940~~ (resolved 2026-10-16: 0x203310..0x203828 is `ingame/map/MapObjMdl.c`, the
  MAP_OBJ_MDL handle; ManmdlWrkInit/Clear/End 0x203828..0x203940 head `graphics/motion/mdlwork.c`, pinned
  0x203828..0x20480c; 0x371154 is MapObjMdl's cloth speed 1.0, 0x371158 mdlwork's; mdlwork.c is now `ingame/memo/memo.c`, FF3's memos, 2026-10-16). Old text (2026-10-14, mc_write.c session): was inferred as the tail of
  `system/mc/prg/mc_write.c`; it is FF3-only C++ map/furniture model-object code (a {obj, model} pair:
  vtable slots 0x08/0x0c/0x14/0x24, 0x2035d0 builds it with MdlPk4GetPart parts 5/3 and the
  0x20e448/0x20dfd0 ctors, 0x2037b0 releases it; callers MapPut/MapDraw/FurnCtl/MapLight) followed by
  ManmdlWrkInit/Clear/End (0x203828..0x20393c, renamed from McWriteWrk*/MemoryCardFileWriteReq; stubs
  now in lib/unassigned_stubs.c, prototypes in mdlwork.h). It shares statics 0x371154/0x371158 and
  runs straight into ManmdlSetAlpha, so it is most likely the head of `graphics/motion/mdlwork.c`
  (or a map-model file before it). Define it with mdlwork.c when that TU is taken.

- 0x189b90..0x18c800 (2026-10-14, ene_main.c session): inferred as `graphics/effect/effect_scr.c` (low FF2
  matches CallFocus2, CallNega, MakeDitherPattern, EffectLeavesFallCtrlInit), but it is enemy code: ENE_WRK
  callers of SearchEneWrkNo, GetAlbumSaveDisp / PlyrGaugeDmgRate damage, EneGaugeInit, the damage
  reactions FUN_0018a0c8 / FUN_0018a338 (EneRule calls 0x18a040..0x18a808). Most likely the rest of
  ene_main.c (extend its range) or the next enemy file; FF3's enemy.c is inferred at 0x192470.
- `graphics/effect/effect_oth.c` (pinned 2026-10-16 to 0x178368..0x17f8b8 + 0x185908..0x186888, partial) still holds several FF3
  modules under one name (architecture.md's address map lists them): the screen filters (the rest of FF2 effect_scr.c, which
  begins in unassigned@001766c8 with SetBlur..SetNega -- most likely the same object; define 0x1766c8..0x178368 into this file
  when it is taken), the sky (SORA_PK4), a sound-bank list (0x17ca48..0x17d830), the map particles of map effects 200 / 201 and
  a camera shake (0x17d830..0x17f8b8), and 0x185908.. (FF2 effect_oth.c's particle objects). Splitting them into their own
  files is open; the name does not block the code. Still to write: the deform kernels (0x1786c8, 0x178c90, 0x179320, 0x179820,
  0x179ce0, 0x17a1c8), SubDither3 0x17aaa0 / SubDither4 0x17b1c8 / MakeRDither3 0x2b5be8 / SubDither4Disp 0x2b5e38 /
  DitherPatternUpdate 0x17a9f8 (the map particles are done, 2026-10-16: rs101's snowfall shows; the sky grid and the
  sound-bank list's release path are done, 2026-10-17), the camera shake, 0x17c2a8, 0x1786c8.. and 0x185908..0x186888.
  None of the sprite filters can show before EffDispSprD2Direct 0x182930 (unassigned@00180a78) is written.
- 2026-10-17 (effect_oth.c, the sky): **the cloud sky shows** above the roofs in the opening event's fly-over (--run 170,
  shot ~2040: SORA_PK4's 128x128 grey cloud picture tiled 4 x 4 over the 23 x 23 grid at y -3000, scrolling in U).
  EffSoraMakeGridPacket is FF3's copy of FF2's MapSky grid (MioPan MapSky.c, same TRIFAN cells, same {1,1,1,1} index
  step). PC: each cell goes to ReiPan_RendererDrawTexturedQuadClamp under EffSoraDraw's CLAMP (REPEAT; the renderer's
  DrawSkyQuad clamps, which would show one picture stretched), z 0 against the GEQUAL draw env. Doubt: the host quad has
  one colour and no fog, so the GS fog is folded in as colour x mean F / 256 per cell and the FOGCOL term (measured 22 grey
  in rs101) is dropped: near the horizon the PS2 would show about 15 grey where the port shows black. If that matters, a
  second additive solid pass (ALPHA 0x48, FOGCOL x alpha (1 - F)) restores it exactly. A vertex behind the camera (w < 0)
  gets F > 255 and wraps in 8 bits as on the GS; F >= 0x800 (ADC bit) is not modelled.
- 2026-10-17 (effect_oth.c): **the sound-bank list 0x17ca48..0x17d830 is dead in FF3** apart from SndBankListInit /
  SndBankListRelease: SndBankListMain 0x17cab8, LoadReq 0x17cb70 (reached only from Play), ReleaseReq 0x17ced8, Play
  0x17d170 / PlayNoKey 0x17d2d0 / PlayKey 0x17d2e8, Stop 0x17d300 / StopKey 0x17d3f8, PauseAll 0x17d578 / RestartAll
  0x17d5e8 have no jal, j, data word or lui/addiu pair (ghidra.py refs). Only the release path is written (GetBank, AllRelease,
  ReleaseReqClear); the rest is named in Ghidra and listed in effect_oth.h. Write it only if a caller turns up (a function
  pointer table built at run time would hide it).
- 2026-10-16 (effect_oth.c, map particles): **the prologue snow shows** -- map effect 200 in rs101 is type 0, 400 soft flakes
  (effdat 0x3c, white, alpha 0x20..0x40) drifting +x and falling, z-tested behind the walls, landing and fading at the floor.
  The user confirmed the picture is the game's snowflake effect. PC: the ROM's DIRECT sprite packet is built but not walked;
  EffMapPartHostSprite sends each visible flake as a camera-facing ReiPan_Graph3dDrawEffectQuad of the ROM's screen size
  (one host call per flake, 400 a frame: batch them if the renderer's cost shows). Doubt: the host clamps the vertex colour
  0xff to 1.0 where the GS modulate would double the texel; flakes may look dimmer than on a PS2 (clamp removed 2026-10-17). Type 1 (map effect 201) and
  type 2 (FUN_0017dac8 / 0x17db58, its own camera, not written) are not on screen yet.
- 2026-10-16 doubt (effect_oth.c): BrightnessAdjustmentFilterDraw now draws FF3's default brightness filter, a black 0x2b
  (34 %) darkening over every in-game frame (option level 6 -> 0x55 from sdata 0x372a28). That is what the ROM computes, but
  the PC picture got visibly darker; if it looks darker than PS2 captures, check first whether the host renders the 3D scene
  at the PS2's brightness (vertex colour / texture modulate scale), not the filter. The opening event's MONO_DISP 1
  (EvMonoDisp, frame ~1694 of a --run 160) turns the prologue grey through EffectMonochromeSet, also new on screen.

- 0x1ed658..0x1edd90 (2026-10-14, menu_csr.c session): a yes/no menu module on MenuCsr* and LayoutAnim*
  (FUN_001ed7f8 news an object, MenuCsrInit(0x2b, ...), YesNoSetMsg; FUN_001edb08 is its pad step,
  FUN_001edd08 its release) between lang_sel.c and common/csr_ctrl.c. Its own FF3-only file.
- 0x128a58..0x128e30 (2026-10-14, album_save.c session): the phase glue of phases 27 (album save) and 28
  (album load): init/one/end at 0x128a68/0x128ab8/0x128aa8 and 0x128c28/0x128c78/0x128c68, end 0x128e10
  (GPhaseCtrlTerm), plus a gp-0x7bb8 get/set pair 0x128a58/0x128a60. Its own file between album_save.c
  and album_mem.c. Done 2026-10-21: defined as `album/prg/album_phase.c` (verify).
- 0x12a068..0x12bef8 is album/prg/album_view.c since 2026-10-20 (message slots, page selection, cover
  pictures, thumbnails; verify). Open there: what ALBM_CMN item 0x45a is (AlbumViewShowItem45a, called by
  album_edit.c 0x122b50) and what the message variant means (AlbumViewSetMsgVariant swaps help icons
  0x464 / 0x465 and lights line 1 or 2; set by the 0x1240e8 run). Confirm both when album_edit.c runs.

- What is left of the old inferred `album/prg/album_view.c` run (2026-10-14: album_view.c is now pinned to
  0x12b928..0x12b9c8 + 0x12bef8..0x12d9f8 and album_load_scr.c to 0x12d9f8..0x12e798, both verify; the rest
  is unassigned): 0x12e798..0x12f470 is album/prg/album_zoom_view.c since 2026-10-21 (the texture loader there
  loads the album kind's mount papers, not the photo); 0x134f60..0x135690 photo caption objects (0x134f78 ctor,
  MenuFilePhotoCaptionDisp); 0x1374d8..0x13b320 FF3 effects / camera code, two objects by .ctors
  (_GLOBAL__I_ 0x139fd0 ends 0x1374d8..0x13a010: player view / camera helpers 0x138800 / 0x138d18,
  AlbumViewDataLoad 0x137d50; the camVci class 0x139e10..0x13a010 became ingame/camera/camVci.c on 2026-10-16; _GLOBAL__I_ 0x13b300 ends 0x13a010..0x13b320:
  the death / game-over camera 0x13a178, 0x13ac48.., the screen effect number 0x13b270 / 0x13b280; its
  static init 0x13b2f8 is empty, 0x13a158 is not one despite its name): their stubs sit in
  src/lib/unassigned_stubs.c, their prototypes stay in album_view.h in a marked block until defined.
  (0x13b320..0x13dba8 is ingame/candle_ctrl.c since 2026-10-15.) (Carved out earlier: save_load_scr.c, save_info_disp.c,
  BpObj.c, B2dGridSpr.c, ScrollBar.c, save_scr.c, gameover_menu.c, savepoint_menu.c, album_save_scr.c; the
  savepoint menu's GPhaseCtrl atexit thunk 0x135e08 is still pinned to ScrollBar.c's range -- move it when
  ScrollBar.c is next touched.)
- **Merged** (2026-10-20): album/prg/album_view.c is 0x12a068..0x12d9f8 in one range; its 13 stubs left
  unassigned_stubs.c, the prototype block left album_view.c.

- 0x25cc20..0x25d000 (2026-10-14, title_menu.c session): C clock / play-time helpers between tim2.c's
  end and title_menu.c's first C++ code: CdClockBcdToInt 0x25cc20 (27 callers), sceCdReadClock wrapper
  0x25ccd0, a clock compare 0x25cdd0, frames -> h/m/s 0x25ce58 (PAL aware), PlayTimeAdd 0x25cf40 (stub,
  reached every boot). No statics, so no atexit thunk marks its start; FF2 kept such helpers in
  ingame/menu/play_data.c or common/. Name it for what it is (e.g. common/cd_clock.c or a play_time
  file) when it is next needed.

- The rest of the inferred roku_pzl.c run (2026-10-14, plyr_sub.c session). Atexit thunks (architecture.md
  "TU ends") close objects at 0x23e208 (GPhaseCtrlTerm 0x23e1c8 + AlbumViewCtrl 0x23e1e8, hidden inside
  Ghidra's FUN_0023e078), 0x2447b0 (0x244790), 0x244e38 (0x244e18) and 0x245568 (0x245548). Pieces:
  0x23c6f8..0x23e208 photo score / lens / enemy targeting (GetPlayData_Score, IngameWrkAddPoint, Lens*,
  PhotoMode*; 0x23e078 picks the nearest type-3 enemy within sdata 0x371c58); 0x243898..0x2438e8 two
  list accessors called by FurnCtl 0x117940 / 0x117de8; 0x2438e8..0x2439d8 a PHF member-name iterator
  (FUN_002438e8: suffix after '.' vs a name, callback per 0x50-byte record; callers ingame/map/MapRoomFile.c, "area_pos");
  0x2439d8..0x243e00 and 0x243e00..0x2447b0 are defined now (title_obj.c, puzzle.c);
  0x2447b0..0x244e38 phase 76, the ROUSOKU candle puzzle; 0x244e38..0x245568 its display (B2d
  'R_rousokuhi_00/01' candle sprites, SaveLoadCaptionDisp); 0x245568..0x245990 small window classes
  (wrong names SixPuzzleMsgReadWinDisp / SixPuzzleCmnWinDisp) up to save_load_disp.c 0x245990.
  FF2's basename link order wants names between plyr_sub.c and save_load_disp.c (e.g. `save_load/...`
  files sorting before `save_load_disp.c`, or a `ph*`/`s*` name).

- player.c's pinned range 0x231628..0x2394a8 holds a file end (2026-10-14, last player.c session):
  0x236920 is the GPhaseCtrlTerm(0x41e468) atexit thunk and 0x236940 (not a Ghidra function) the
  AlbumViewCtrl static's dtor thunk 0x2ad380(0x41e540) -- the architecture.md "TU ends" pattern. So
  0x231628..0x236960 is one object (init, PlayerMain, damage, death, the view checks, vibration, doors,
  PlyrEquipBackup/Restore) and 0x236960..0x2394a8 another (PlyrModeInit, mode 10, flash / equipment,
  CheckPlyrAnimeEnd, SetPlyrAnime and its tables, PlyrAnimeCtrl, PlyrDirAnimeNo, CheckPlayerNeckSW).
  All of it is reconstructed in src/ingame/plyr/player.c. A later session may split the second part
  into its own file (a basename sorting after player.c and before plyr_mdl.c, e.g. plyr_anime.c or
  plyr_ctrl.c) with tus.py define; nothing depends on it, so it waits.

- 0x2295c8..0x231628 (2026-10-14, left unassigned when player.c was pinned to 0x231628..0x2394a8): link
  order put it in player.c, but it is other code: play-data / HP-down timers 0x229448..0x22a9e8
  (PlyrHPdownCtrl / PlyrNAnimeCtrl FF2 matches here are suspect), a 2D effect drawer 0x22add8..0x22c7d8
  (DispSprD, LocalCopyLtoB, Vu0Sinf), a save-file list with CdClockBcdToInt 0x22d130..0x22eb20, and the
  StoryMenu_Film phase 0x22ec88..0x231438 (strings menu_in / menu_out, B2d hooks, GetPlyrEquipmentFilmType
  0x22f6e8; PlyrFlagInit 0x22f628, called from IngameInit, sits in it). Basenames must sort between
  `play_data.c` and `player.c` for a FF2-style split, or name them for what they are (story menu film).

- Rest of unassigned@001b1648 (2026-10-14, after finder_photo.c / finder_view.c / finder_view_hud.c were pinned):
  FF2 has finder_camera.c (ends FinderInCameraCtrl, FF2 0x192fd0) right before fly_ctrl.c (starts CheckEffectFly,
  FF2 0x1930c8), so every file in FF3 0x1b1648..0x1bf448 is FF3-new and its basename sorts between
  `finder_camera.c` and `fly_ctrl.c`; the three pinned ones are `finder_photo.c` < `finder_view.c` <
  `finder_view_hud.c`, so the earlier pieces need names in (`finder_camera.c`, `finder_photo.c`). Pieces:
  0x1b1648..0x1b3078 a photo-viewing layout menu calling finder_camera.c's FUN_001b1340 (B2d items 'slider',
  'csl', 'wasi', 'arw', FILE_PHOTO hook; maybe still finder_camera.c); 0x1b3078..0x1b3580 Tbl325260Init/NewGame/End
  (frontier from ingame.c) and small accessors used by finder.c / photo_dat.c; 0x1b3580..0x1b57d8 a phase
  (GPhaseCtrl, MenuCsr, LayoutAnim; phase table entry 65 = 0x1b5220/0x1b55c8/0x1b5538); 0x1bf108..0x1bf448 the
  FlyModel methods (vtable 0x341d68; its dtor is 0x2b9cb8 -- function created 2026-10-14 -- and the
  deleting dtor 0x2b9ce0; 0x2b98c8/0x2b9ae0 are NOT FlyModel code but finder_view_hud.c's out-of-line
  HudFilamentCoreDraw / HudFilamentGlowDraw) -- a file named < `fly_ctrl.c` (e.g. `fly.c`), or the head of fly_ctrl.c.

- 0x1cf838..0x1cf910 (unassigned after g3dSprite.cpp): FF2 g3dTexture.cpp -- 0x1cf838 is CTexture::PreLoad (named 2026-10-16)
  (vtable 0x341e58 +8), 0x1cf870 probably CTexture::Create. `g3dTexture.h` has a minimal CTexture
  (2026-10-14, for g3dSetTexture); define the file when g3dSprite.cpp / textures are ported.
- ~~0x1dd8b0..0x1ddf48~~ (resolved 2026-10-16): gra3dBoundingBox.c is 0x1dd8b0..0x1ddec0 and gra3dConst.c
  0x1ddec0..0x1ddf48 (FF2 link order); 0x1de0d8..0x1df968 is NOT gra3dBoundingBox.c but FF3-only model-draw
  code, now `graphics/graph3d/gra3dMirror.c` 0x1de0d8..0x1dfb90 (written 2026-10-16). Its wrong FF2 names were
  replaced: 0x1de250 ClipEdgeListByFlags (was gra3dbbIsInFogArea), 0x1df298 gra3dsgdGetMirrorBlock (was
  gra3dbbCopy, then FindListNodeType40).
- ~~0x1e6ae8~~ (resolved 2026-10-20): the run 0x1e6ae8..0x1e6b98 is `graphics/graph3d/gra3dVu0.c` (FF2 link
  order, after gra3dTRI2.c), all four functions written (architecture.md address map). Was:
  0x1e6ae8 `Vu0InitConstRegisters` (was HinaPuzzleSubOutAnimCheck; inferred as hina_pzl.c): loads VU0
  vf1/vf2 from data 0x35aa40, called by gra3dInit; it sits right before gs_reg.c (0x1e6b98). Stub in
  unassigned_stubs.c; on PC there are no VU0 registers, so its body will be a no-op. Its run goes on to
  0x1e6b18 Vu0SetConstVector (was HinaPuzzleGetBlankPos: stores into table 0x328fa0[idx - 1], idx 1..2; only
  gra3dMirror.c calls it, with idx 2 next to gra3dSetVu1ConstVF2) and two getters 0x1e6b48 / 0x1e6b78 that
  Ghidra has no functions for and nothing calls; nothing reads 0x328fa0 either, so a PC body can just store.

- 0x20b530..0x20c400 (between menu_soul's static ctor and message.c): menu code, not message.c. Four
  0x18-byte thunks 0x20b530..0x20b590 (`f(p) -> FUN_001eeXXX(p, p + 0x1c / 0x2c)`, still carrying the
  wrong FF2 names SetFontPacketHeader / GetStrWidth / SetMessageV3 / SetMessageV3_2), a heap object of
  0x44 over menu_csr_obj.c's MenuCsrObjInit / MenuCsrObjSetPos (0x20b590..0x20b878), the `R NUM_0` B2d hooks
  (0x20b878..0x20ba20, hook list 0x35c5f0), phase StoryMenu_Top (init/one/end 0x20bd28..0x20c3e0, its
  Bp file 0x11c6 + language, labels menu%d-in / -back / _cancel) and a static dtor 0x20c3e0
  (GPhaseCtrlTerm(0x41e468)). Probably one FF3 menu file (story menu top); kai_pzl.c holds the other
  Story_Menu phase.

- ~~outgame/framerate.c gets 0x1c8388..0x1c83d0~~ (resolved 2026-10-16: those two are foot_se.c's static
  initialiser, renamed FootSe_static_init / FootSe_GLOBAL_I; 0x3732a8 / 0x3732ac is FootSeInit's footstep
  file mask, read by foot_seGetSeStat and FootSePlayFloor. foot_se.c is defined 0x1c7cb8..0x1c83d0.)
- The rest of the old unassigned@00108ef8 run (defined 2026-10-13: BpTex.c to 0x108f78, B2dBase.c
  0x108f78..0x109748, BgSe.c ..0x109dd8): 0x109dd8..0x10aa88 is FF3 menu/in-game helpers used by FurnCtl.c's
  menu code (id table 0x2ef890 for 2000..2004; a step sequencer {16 function pointers, count +0x44, cur
  +0x48, jump +0x4c, stop +0x50} 0x109f58..0x10a128 (named StepSeq* 2026-10-15; STEP_SEQ and the prototypes sit in
  ingame/map/LightCtl.h until this run is defined -- move them, and write the five functions, they are stubs); per-character checks via PlyrGetCurNo; finder-state
  checks 0x10a640 with gp flags set by 0x10aa78; NumGetDigits and MsgPrintPage number helpers
  0x10a830..0x10aa78) -- probably one file, maybe two at 0x10a830. 0x10aa88..0x10b0b8: a 2-axis menu
  cursor {per axis min, max, cur, skip-bit mask; flags +0x20/+0x28, SE +0x2c} with pad repeat
  (0x10ada8). 0x10b0b8..0x10b478: RTTI `CEditRoot : IEditObject` (vtable 0x340f68) ctors/dtors, the head
  of `graphics/graph3d/CDebugEdit.cpp` (redefine that TU from 0x10b0b8). rtti.py lists 0x10ada8 among
  CEditRoot's ctors: wrong, it is the cursor update.
- ~~0x1ecd30..0x1ecdd8~~ (resolved 2026-10-21: `ingame/item/prg/item_tbl.c`, ITEM_RESET's item sets; the
  thunk 0x1ecd10 before it ends `ingame/menu/menu_item.c`, the item menu, not kai_pzl.c).
- 0x1f7050..0x1f7480 (the old lang_sel.c inference) is unassigned again: kaza puzzle neighbourhood
  (0x1f7050 renamed Wrk_gpdac0RemapByteB, 0x1f7460 Sub1f73b8GetWord4; both were wrong FF2 matches).
- 0x13e648..0x13efa0: FF3-only sprite display of file `CAPTION_PK4` (0xad0; load 0x13e7a8 into gp-0x7628,
  0x13e810 sends it with PK4SendVram (was the wrong FF2 match DispTitleCursorL), free 0x13e848; static init 0x13e890 fills SPRT
  data at bss 0x3910c8.., `_GLOBAL__I_` 0x13ef80 ends it). 0x13efa0.. is the next run (memset user,
  CmnLayer* 0x141ff0.., camera code).
- 0x11cea8..0x11d648: **defined 2026-10-15** as `ingame/map/MapCtrl.c` (one FF3-only file: shared sdata
  gp-0x7c58 start area / gp-0x7c5c chars-drawn flag and the static `CClassPtr<CLightCtl>` 0x38e6d0 whose
  `_GLOBAL__I_` 0x11d608 / `_GLOBAL__D_` 0x11d628 end it; the old MapSave.c slot 0x11d160/0x11d2e0 was not FF2
  MapSave code). Name invented; doubt: FF3 may have called it something else (no string evidence).
- 0x10bbb0..0x116fa0 (the inferred `ingame/map/FurnCtl.c`): FF3 class files in vtable order = code
  order (vtables 0x340fe8..0x341168 are alphabetical: CExtraMissionShelt, CGameClear, CLightCtl, CLoad,
  CMapDBuff, CMapRec, CMarkDat, CMarkDoor, CMarkEvent, CProcCtl, CWindowMsg, CmemShelt, CmenuCam*).
  Ctor/dtor positions (rtti.py --class): ExtraMissionShelt 0x10b698..0x10b8d8 (before the range);
  GameClear **defined 2026-10-15** (`ingame/clear/GameClear.c` 0x10bba0..0x10ce28, verify: the whole
  0x10bbb0..0x10c318 head is its item hooks; 0x10bba0 its layout getter; 0x10cd28..0x10cdc0 its
  GAME_CLEAR_ helpers; 0x10cdf0 / 0x10ce10 the atexit thunks of the GPhase singletons); LightCtl **defined 2026-10-15** (`ingame/map/LightCtl.c` 0x10ce28..0x10d8b8, verify);
  Load and MapDBuff **defined 2026-10-15** (`common/Load.c` 0x10d8b8..0x10dbe8: LightCtl's file ends
  with its empty static init 0x10d890 + `_GLOBAL__I_` 0x10d898, which had swallowed CLoad's ctor twin
  0x10d8b8; `ingame/map/MapDBuff.c` 0x10dbe8..0x10e230 incl. the special-event put pair
  0x10e120..0x10e230, kept there for want of a class or static init of their own); MapRec and
  MarkDat **defined 2026-10-15** (0x10e230..0x10e650, 0x10e650..0x10f150; FurnBpLoadAll became
  MarkDatLoadAll);
  MarkDoor and MarkEvent **defined 2026-10-15** (`ingame/map/MarkDoor.c` 0x10f150..0x10f820 + dtors
  and its inline point helper 0x2abf80..0x2abfe0; `ingame/map/MarkEvent.c` 0x10f820..0x10fb80 + dtors
  0x2abfe0..0x2ac010, both verify: the map screen only). MarkEvent.c holds the SET_2DMAP_DRAW flags
  (renamed FurnCtlFlag* -> MarkEventFlag*), the map sprite hook's checks MarkEventIsSpecialRoom /
  MarkEventSpecialSprite / MarkEventIsDrawn and LERP_F (0x10fab0 is LerpFClear's unreferenced twin, so
  LERP_F has a constructor; it may be a file of its own: nothing proves where CMarkEvent's file ends
  before ProcCtl). ProcCtl / WindowMsg defined. memShelt, menuCamBase, menuCamMain **defined
  2026-10-15** (`common/memShelt.c` 0x110368..0x1105f0, FurnWorkFreeAll 0x1104d0 was MemSheltFree;
  `ingame/menu/menuCamBase.c` 0x1105f0..0x1106d8 + default virtuals 0x2ac018..0x2ac048;
  `ingame/menu/menuCamMain.c` 0x112150..0x112db0; all verify, in-game only). CmenuCamStat **defined
  2026-10-15** (`ingame/menu/menuCamStat.c` 0x1146a8..0x116fa0 + 0x2ac1b8..0x2ac330, verify): it
  starts at 0x1146a8, not 0x114820 -- the lens-table helpers 0x1146a8..0x114820 read 0x34d968,
  inside Stat's data 0x34d100..0x34db40 (StLens' data ends before 0x34d100); vtable +0xc 0x116f78
  is its Draw, not a static initializer. CmenuCamBasic **defined 2026-10-20**
  (`ingame/menu/menuCamBasic.c` 0x1106d8..0x112150 + 0x2ac048..0x2ac128, done: the Capabilities page,
  seen through a reverted probe; Ghidra had swallowed the ctor twin 0x110c48, the hooks 0x110fe0 /
  0x1119d8 / 0x111c68 and Draw 0x1120b8 into their neighbours; 0x1120c0 is an unreferenced empty
  function). CmenuCamStLens **defined 2026-10-21** (`ingame/menu/menuCamStLens.c` 0x112db0..0x1146a8 +
  0x2ac128..0x2ac1b8, done, seen through a reverted probe; the 0x112db0..0x113db0 run is its own helpers / steps /
  hooks, not a fourth class; 0x1146a0 is a lone unreferenced `jr ra`). FurnCtl.c has nothing left (comments only).
  CExtraMissionShelt **defined 2026-10-21** (`ingame/mission/ExtraMissionShelt.c` 0x10b788..0x10bba0,
  verify): DbFurnPreGetObjNum 0x10b9a8 became MissionCarryStoreAll, IngameRetryPrepare 0x10bb20
  ExtraMissionSheltCarryStore (not a retry: it carries a mission's results over the story game's
  reload), the ChrSort* low matches its destructors and getter; ingame/plyr/ChrSort.c/.h removed.
- (resolved 2026-10-16: GameModel / GameMotionModel are `graphics/game_model.c`, GameAnimeModel
  0x1d36f0..0x1d3b40 `graphics/game_anime_model.c`; the FF2 names GalDispCaption 0x1d3a48 /
  LiberateGalleryTexMem 0x1d3728 were wrong matches.) Doubt left from game_anime_model.c: the file
  name is the class name (FF3 might have kept it in game_model's object; nothing tells). Still open:
  identify MDL_MOT (probably MotionModelManager, vtable 0x342080) and MDL_OBJ (probably ModelObject).
- 0x1502a0..0x150b40 now belongs to `ingame/menu/anim_2d.c` (decided 2026-10-13, see Decisions); its
  HUD functions are not reconstructed: 0x1502b0 / 0x1503c8 (two identical HUD_LAYOUT fill + load
  copies), 0x150540 release, 0x1505a0 data ready, 0x1505c0 end request, 0x150620 (still FF2's
  `motAniCodeReadMIM` name: the HUD state machine 0 load / 1 run / 2 done), 0x1506d8, 0x1506f0, 0x150aa8,
  0x150ac0. Name them `Hud*` when anim_2d.c is reopened.
- B2d files: B2dFile.c 0x104320..0x1055d8, B2dInp.c ..0x105998, B2dSpr.c ..0x106370 (all defined);
  BpTex.c from 0x1089e8 (hooks FUN_00108cf0/FUN_00108e30 use the B2D_HOOK_ARGS ABI); 0x2ab488 (draws a
  B2d item with DispSprD) has no file.
- B2dSpr.c's head 0x105998..0x105f78 (sin/cos tables, B2dSprFreeze*) has no assert string: it may be a
  third FF3 file (a B2d maths/util object) between B2dInp.c and B2dSpr.c. Evidence for one file: the bss
  tables follow B2dInp's table directly and the sdata it uses (0x36b560, 0x36b568) is contiguous; move it
  out if a string or section evidence turns up.
- After the pad driver: 0x1ef370..0x1efcf0 is `common/vu0math.c` (defined 2026-10-14); 0x1efe20..0x1f0138
  chunk-table accessors; 0x1f0d78..0x1f20c8 defined 2026-10-14 as common/get_byte.c, ingame/story_load_mc.c,
  ingame/story_menu.c. Doubts: get_byte.c has no start evidence (no statics; it may be the head of
  story_load_mc.c's object); the three names are descriptive. 0x2500e8..0x250268 (OutgameBgmPlay/Stop,
  OutgameBgObj*) is inferred as graphics/scene/scene.c only because of the wrong SceneManModelLoadWait
  match: it belongs with OutgameBg* (0x24f548..) or phase 69's file. sdata 0x3720a4 (OutgameBgObjIsUsed)
  has no store anywhere: always 0, so the in-game load fades. Phase names 58..64 from phases.py do not
  match the story-menu rows (row 3 'R photo' opens 0x3c, row 2 'R camera' opens 0x40): check when the
  StoryMenu_* phases are written. Ghidra: function 0x243dd8 TitleObjIsShown created (was glued into
  FUN_002501b0).
- 0x20daa0..0x20de38: message.c code (DebugMsgCtrlInit, MsgGetPageBreakNum, Msg* page byte 0x32d68c /
  page-wait 0x32d68d, GetMsgDataAddr), inferred as graphics/motion/mime.c; the Msg* prototypes sit in
  `graphics/motion/mime.h` until the run is defined (graphics/graph2d/message.c/.h now exist).
- 0x223e80..0x224670: phase 19's file (own GPhaseCtrl thunk 0x2245f8); option screens 20-23 before it.
- 0x226138..0x226e00: float approach/rate helpers (FUN_00226138..), not pause code.
- 0x2597b0..0x2597f0: hidden function between eecdvd.c and system.c (writes D_ENABLEW 0x1000f590 and clears
  bit 8 of D1_CHCR 0x10009000, then j 0x276458): probably system.c's (VIF1 DMA reset); no Ghidra function.

## Open questions

- **g3dGeom.c kept two ROM quirks (2026-10-16).** g3dMatrixGetRoll's length is sqrt(1 + m02^2) (the VU0 macro
  reads the constant vf1.x, FF2 too), so gra3dcamGetRoll returns -atan2(m01, sqrt(1 + m02^2)), not the true roll;
  map_camera.c undoes the camera roll with it (sceVu0RotMatrixZ by -gra3dcamGetRoll()). g3dCalcIntersectionEllipseAndLine maps hits back component-wise
  ((e0+e1+e2) * p + e3), right only for an axis-aligned ellipsoid, and its result w is a stale VU lane (taken as
  e3.w). If a camera roll or an ellipse hit test looks wrong on screen, these are the places; both are kept as
  the ROM does it (hazards.md "VU0 macros that read vf1").
- **Item data fields (2026-10-15, item.c).** ITEM_DAT (rodata 0x35ad38) +4 (1..3), +6 (4 films, 1 recovery and
  item 0x11, 8 the rest), +0xc (even 0x2a..0xd2: message or picture number?) and +0xe (an index 0..0x74) are not read
  by item.c; name them when the item menu (0x1eac20.., StoryMenu_Item) or the item-get window reads them.
  ItemUseConsume (item 0x11) sets CandleControl +0x20 (grade per candle_ctrl.h) to 4 when it is 0 before SetPower(100):
  kept as the ROM does it; if the candle draws wrongly after using that item, check what +0x20 really is.
  0x1ebb20 ItemMenuListNew (was ItemLost) and 0x1ea8c8 ItemGetWorldKind are named from their bodies only.
- **Four FF3-only effects nobody requests (2026-10-15, effect_obj.c).** EffFallPartReq 0x171718 (list B,
  falling bluish particles), EffRainReq 0x174c78 (306 rain streaks with a roof shell, boxes at data
  0x3545f0 / 0x354610), EffAreaPartReq 0x175620 (two area emitters, area tables 0x353d30 / 0x353ec0
  with absolute-looking coordinates of one place) and EffDropReq 0x1760c0 (drops falling from the 35
  points of 0x354630 to y 160) have no caller (no jal, j, data word or lo16 reference), so lists B,
  the rain, the area particles and the drops stay empty in the retail game; their exec / release run
  every frame anyway. Reconstructed
  faithfully. If a caller turns up (an event command table, a pointer built at run time), name it
  here. EffFallPartTilt 0x1722c0 writes rot y = 0 (not the axis's own angle) for a target within
  +-1, reproduced as the ROM does it.
- **Seal door burn and SCL buffers unseen (2026-10-16, effect_obj.c).** EffSealDoorBurn* (FF2 DoorSealDisappear,
  bss 0x3a7750) is complete but only EvSetGhostSealDoor (ev_command.c) starts it, which the opening never
  runs; its burn pass draws into GS work frame 0x3020 through EffDrawFrameFillRect / EffDispSprD2Direct
  (unassigned@00180a78, still stubs), so check both when a seal door event first plays. The SCL records
  (EFF_OBJ_BUF, data 0x2f9210) keep their 0x4b parameter words raw: decode them as a struct when
  effect_oth.c's FUN_0017de50 / FUN_0017eb70 (their only readers) are written. Doubt: EffThunderApplyLight's
  stack G3DLIGHT is zeroed on the PC (the ROM leaves specular / ambient / position as garbage); if the
  lightning looks different from the PS2, check what gra3dSetLight reads of light 1.
- **No host draw path for 3D effect packets (2026-10-15, effect_obj.c).** The candles, halo and kind
  0x1e glows build FF3's DIRECT GIF packets (StartDmaDirectTrans) or call SetEffSQITex / FUN_00183308
  (stubs), but nothing on the host walks a 3D effect's chain: MioPan drew them with RendererPacket3DUV
  (CandleFlameHostQuad), which ReiPan's renderer does not have. Needed once the game reaches a room.
  **Partly done 2026-10-16:** `ReiPan_Graph3dDrawEffectQuad` (reipan_graph3d.cpp) is RendererPacket3DUV's port
  (world corners through matWorldClipObject -> ReiPan_RendererDrawBillboardTriangles, GS draw env from
  SetGsRegsCached); effect_obj.c's EffHostQuad feeds it from the candle flame, flare, blue grid flare and
  EffCandleFlameOneDraw (altars / lanterns). Still packet-only: SubHalo, kind 0x1e's glows, the lamps, the particle
  draws (sprite / quad / distort fan / streak line, 2026-10-16), the lens flare and SetStarRay, the deforms. The
  lamps and the flare ghosts also wait on unassigned@00180a78's stubs Set3DPosTexure / Set3DPosTexure2 (FF2
  Set3DPosTexureSub 0x2b8220) and SetEffSQTex 0x181128; EffPlyrLightConeIn / Get2PLength / GetCamFaceRot /
  Vector2Rot / WorldToScreenPos / Get2PosRot are stubs too (unassigned@0017f2b0 / 00180a78, effect_sub.c), so even
  reached, the lamps would not blink right. Also waiting on unassigned@00180a78 (2026-10-16, the TU's last slice):
  the flare ghost EffRenzFlareGhost draws through Eff3DFaceSprite 0x1834f8 (also FUN_001861f0's), the file
  screen's flip panel through EffMatTexQuad 0x183800 (both stubs, packet-only like the rest), and list C types 7 / 8
  read EffSparkColGet 0x186a78 (unassigned@00186888, a stub returning nullptr: a type 7 / 8 burst would crash on
  the host until it is written -- it only returns data 0x354c20). The tourou steppers and EffectLightComeInExecOne
  wait on ingame_effect.c, the flip panel on menu_file.c (both mostly stubs). effect_obj.c is complete (verify):
  mark it done when a run shows the lamps, a particle burst, the flare or a lantern. None of it runs in rs101 at --run 160. Unchecked until the effect textures load (VRAM_TEX_PK4, unassigned@00158ed0): the flame's
  ST orientation (MioPan's note: u along local +x, v along -y) and the blend (ALPHA 0x48 additive).
  Also open there: effdat / lang_effdat's owning TU (FF2 g2d_main.c; FF3 data 0x351b00 / 0x351470,
  defined in lib/unassigned_stubs.c); the matcher names ManyCandleCtrlInit 0x17e068, ManyCandleParticleUpdate
  0x17f218 and EffectManyCandleReq 0x178698 are probably wrong (the real FF2 counterparts are
  0x163508 / 0x163d78 / 0x163498, named Eff* because the names were taken); FF3's thunder exec stops
  the thunder machine while the lightning is off (a delay over 17 frames never sounds) -- a ROM quirk
  reproduced, check against the game when an EV_EFF_THUNDER_REQ event plays.
  Fourth session (same day): pass 2 of the parts deform (SubPartsDeform2) also needs the host to
  sample a copy of the displayed frame: it LocalCopyLtoL's the frame to GS 0x2bc0 and draws a 17 x 17
  textured strip over it (TEX0 0x2000000264116bc0) -- the same render-to-texture gap as the room glow;
  MioPan bridged it with MioPan_RendererDrawTexturedTriangles2D. CalcPartsDeformXYZ takes the two
  local matrices as arguments (FF3 leaves them in vf4..vf11): EneDmgLargeHitMakePacket in effect_ene.c
  must load them itself when it is written. Fifth session: pass 1 (SubPartsDeform1 0x168860 and its
  eight kernels) is written too, so both passes now wait on the same host gap. Pass 1 page 1 (FF3)
  restores the 320 x 224 EE stash 0x117b000 into 0x2bc0 with LocalCopyBtoLAdrs and samples it as a
  512 x 256 texture (TEX0 0x2000000224116bc0): the host must serve that stash, which only the
  LtoB / BtoL capture slots in g2d_draw.c do today (the Adrs form skips the effect stashes). Unchecked
  which effect records use page 1 or the bounce / lens kernels (6..8; renz sdata 0x36e3bc is 1.0 and
  nothing writes it) -- look when an in-game deform plays.
  Water flow (same session): FF3 drives every surface from data 0x2f8918 (12 move types x 0x34: S/T
  of three legs, alpha min / max / period, leg frames) and pulses the alpha through the new
  gra3dSetSGDMaterialAlpha 0x1e2f60 (stub, gra3dSGD.c) next to gra3dChangeST (stub): the scrolling
  water shows only once gra3dSGD.c is written. Move table word 9 (1 or 2) is not read by the exec side;
  probably EffectWaterFlowRegist's (FUN_001e9ef8?) sound or kind -- check when Regist is written.
  Second session (same day): the room glow (SetRoomGlow, kind 0x10) is a bloom that renders into two
  GS work frames (0x2bc0 / 0x3020) by switching FRAME_1 and samples the display buffer as a texture
  (EffSetDrawFrame / EffDrawFrameFillRect / EffDispSprD2Direct / EffResetDrawFrame, unassigned@00180a78,
  stubs) -- the host needs render-to-texture for it, a renderer decision. **Done 2026-10-26** without
  render-to-texture: SetRoomGlow hands its settled values to ReiPan_RendererDrawRoomGlow (reipan_renderer.h),
  a renderer pass at a capture point (prefilter + three separable blurs in quarter-size buffers, then an
  additive composite); the work-frame draws above build their packets and draw nothing -- muted
  (ReiPan_EffHostMuteBegin / End), so their work frames are no longer marked dead: that mark had been
  dropping every sprite sampling 0x2bc0 (the overlap cross-fade, the nega's second pass, the film shake /
  flicker copy) while a room glow ran. The enemy aura and the seal door band now have renderer work frames
  too (see the aura note above; the band is an image-space work frame at 0x3020, 320 x 448, built under
  ReiPan_RendererSet2dAlphaTest, seen with a reverted EffSealDoorBurnLoad(0) probe in the house). The parts deform has been drawn since the same day: MakePartsDeformPacket
  hands its grid to ReiPan_RendererDrawTexturedTriangles2D (MioPan's bridge, with CalcPartsDeformXYZ's
  z / w for the depth test); its frame copies were already capture slots. Seen by a reverted probe
  (CallPartsDeform3 at Rei, types 21 and 11, in the chapter-2 house); no retail trigger reached yet -- ghosts
  with ene_deform_dat entries, scene effect 0x16, the seal door. The floating lanterns (kind
  600, areas 0x29 / 0x2b / 0x2c) wait on GameModel (unassigned@001d2a40: ctor twin 0x1d2a80, deleting
  dtor 0x1d2b70, loaded 0x1d2cf0, matrix 0x1d3010, alpha 0x1d30b0, draw 0x1d2d98, all stubs) and on
  this TU's flame draws FUN_001625d0 / FUN_00163e50. LanternLipMakeDefault leaves the 40 start rows as
  the heap had them (the ROM does too; only matters when an _LIP file is missing). The ROM's EH cleanup
  in LanternLipInit (delete of the points) is not reproduced.
  Sixth session (same day): the haze (kind 0x1b) is one more frame-sampling pass: it stores the shown
  frame with LocalCopyLtoBAdrs(1, 0x11c1000, 0x38e0) and restores it with LocalCopyBtoLAdrs to 0x2bc0
  every frame -- a third EE stash (beside 0x10ef000 / 0x117b000), served slot to slot by g2d_draw.c's
  capture bridge since 2026-10-26 (G2D_EE_STASH_2); it then needs EneDmgLargeHitMakePacket (effect_ene.c, stub; load the two local
  matrices there, see above) and the 3D sprite FUN_00181868 -> 0x2b8220 (unassigned@00180a78, stub).
  The kind 0x1d particle clouds (SetEffKind1D / EffKind1DCtrl) build ordinary DIRECT strips, so they
  wait only on the host 3D-effect packet path above; sets 6..8 need CH036_04_TM2 from the file
  cache (file_cache.c, written 2026-10-15) and MakeTim2Direct with tbp -1. Doubt: set 5's draw test reads a
  float the ROM never sets (always 0 < 0.01, so set 5 never draws) -- reproduced; check in the game
  whether a set-5 cloud (which map effect / event uses it?) is really invisible.

- **effect.c's FF3-only kinds and the SetEffects argument contract (2026-10-15, effect.c).** Kinds 0x10
  (no args; SetRoomGlow 0x165578, the bloom, written), 0x11 (byte, float, byte, float,
  float; FUN_0017b970), 0x12 (one pointer; FUN_0015b5c0), 0x1d (pos, rot, v, no, alpha; FUN_0016fdc0,
  ended by FUN_0016fed0) and 0x1e (pos, float; FUN_00164970) are named SetEffects_KindNN until their
  drawers are read. SCREEN_EFFECT_PARAMETER's eight FF3 words (+0x60..) are unnamed until
  ingame_effect.c (IgEffectStoryMainScreenEffectReq) is written. SetEffects reads its tail with va_arg:
  host callers must pass doubles for float slots and real pointers (nullptr, never 0) for pointer slots
  (NEGA without fl bit 2, RENZFLARE, HALO, FIRE, PDEFORM, WATERDROP, TORCH2, SPARK, TORCH_FREA, HAZE,
  MANY_CANDLE, 0x12, 0x1d, 0x1e, ENEIN) -- an int 0 there is a wild pointer on x64. EffectPhotoPhase
  0x158d50 hands EffectInPhoto the model bank (as the ROM does), so its slot-18 / slot-8 checks read
  model slots; harmless while those kinds never land there. CheckSetEffectsOK (0x156f50) answers 1 when
  any of FUN_00159148 / FUN_00165d98 / IsLoadEndAll answers non-zero: check against IngameNowLoadingMain
  once those are written.

- **HUD candle draw on PC (2026-10-15, candle_ctrl.c).** CandleControlDraw sends the grade's TIM2
  (Tim2PackSend, default TBP from the pack) and draws through DispSprD with TEX0 0x20058405dd30abc0
  (TBP 0x2bc0, data 0x2f2fa8). Unchecked: that the pack's TBP matches 0x2bc0 and that DispSprD's host
  path finds the texture by that TEX0; also that phase 48's FUN_0017bdc8 / FUN_001dfbc8 (stubs) do
  the monochrome switch. The candle furniture (area 0x2b, kind 0, id 4, label 2) and event 0x6f3 are
  guesses at meaning: CandleFurnReset only ever hides it (the candidate table sdata 0x36bdec is all -1).
- **The CEditRoot run 0x10afa8..0x10b478 has no TU (2026-10-15, cmn_util.c session).** The old
  unassigned@00109dd8 run also held CDebugEdit's CEditRoot (RTTI, vtable 0x340f68, base IEditObject
  0x340fa0) ctors 0x10afa8 / 0x10b030 (created with mkfunc), 0x10b0b8 / 0x10b248 (std::string name
  arg) and dtors 0x10b308..0x10b478; common/cmn_util.c now ends at 0x10afa8, so that slice falls back
  to inference. It belongs before graphics/graph3d/CDebugEdit.cpp (0x10b478 Create): define it as part
  of that file when CDebugEdit is ported. Also open: the file name common/cmn_util.c is invented; FF3's
  alphabetical class-file order puts the real one between CBgSe and CDebugEdit (CB* / CC*).
- **PlayTimeMax 0x25cdd0 is a stub** (unassigned@0025cc20), so ClearRankGet always gives rank 5 (E)
  until that run is written; it is a 0x84-byte field compare (later PLAY_TIME, nullptr when equal).

- **The cursor grid CSR_GRID 0x10aac0..0x10b0b8** (CsrGridInit / SetRange / SetPos / SetSkip /
  Move, plus 0x10abb8 / 0x10acd0 / 0x10ad40 / 0x10b0b8..0x10b3f8 unnamed) and the camera levels
  CamLvGetKind / GetSpKind / Get / GetBase 0x10a158..0x10a5a8 are stubs in lib/unassigned_stubs.c,
  declared in menuCamStat.h / menuCamStat.c: the unassigned@00109dd8 run (StepSeq, ClearRank,
  NumGetDigits, PrintNumber*, these) needs its files defined.
- **Phase 42 (0x2a, the game clear)** — its init / one / end 0x258a68 / 0x258ad8 / 0x258aa0 sit at
  the end of the inferred `ingame/sound/snd_ctrl.c` (0x258a68 carries the wrong name
  ChangeForceTraceMode and swallowed the end function 0x258aa0; no Ghidra function there). They new
  a CGameClear (0x10 bytes, sdata gp-0xef0), run GameClearRun + BpCtlMain, delete it. Write them with
  GameClear.c's first in-game test (they are 3 tiny functions; probably their own FF3 file, e.g.
  `ingame/clear/prg/gameclear.c`). Phase 0x43 is linked while the ending movie plays, 0x2b follows a
  story clear, 0xb a mission clear / failure. GameClearRun returns 1 the frame the movie phase is
  requested (game_clear_movie_req): check what phase 42's one returning 1 does then.
- **MarkDat.c / MapRec.c (2026-10-15, verify)**: (1) MapRec's measuring hooks call FUN_001f7528,
  MapSnapItemGetState 0x1f75c0 and MapSnapNameIsRsaRex 0x1f7948 (m_plyr_camera.c's slot, all stubs
  returning 0): with them stubbed FUN_001f7528 says "special item" for everything and MarkDatMeasure
  returns an untouched rect. Written 2026-10-19 (FUN_001f7528 = MapScreenItemGetDoor). (2) MarkDatSetPos takes
  every floor's origin from MapSnapGetStat(2, 0) (1F's first layer) — FF3 as written; PC guards skip
  a nullptr stat and a floor with no layout (FF3 would read layer -1). (3) Seen on screen when the map
  menu (FUN_001ff1d0, MapSnapMarkDatDraw's caller) is written; boot now holds 23 Bp entries from
  MarkDatLoadAll and the title menu still draws (75 s smoke run).

- **MapSnap.c / MapEff.c (2026-10-15)**: FF2's kaza_pzl.c slot held no puzzle code; the file was
  split into ingame/map/MapEff.c and ingame/map/MapSnap.c and kaza_pzl.c/.h deleted (the KazaPzl* /
  KazaPuzzle* names there were wrong). Both are `verify`: in-game only. (1) MapEffCreate returns the
  address of a static word that types 200/201/300..303/400..403/600 never write, so their rows keep the
  previous call's result as their "effect" (FF3 behaviour, kept). (2) MapSnapItemHitRect's exact mode
  (work +0x10, never set) has FF3's runaway inner loop; kept as dead code. (3) CMarkDat is written now (ingame/map/MarkDat.c, 2026-10-15, verify); the map layouts are loaded
  at boot, so MapSnap now sees real handles once MarkDatIsReady. (4) The effect families called
  by MapEffCreate (effect_obj/oth, unassigned@00186888) are stubs: candle flames will not show in a room
  until they are written.

- **MapLoad.c (2026-10-15)**: (1) The room model object at MAP_ROOM_DAT +0x3c is an EE C++ object
  built by MapDrawInitRoom 0x117cc0 through FUN_002035d0; no host class exists, so MapLoadRoomMission
  0x2ac3a8 cannot call its vtable +0x50 (mission id) and skips MissionCtrlAdd / MissionCtrlRemove.
  Decide the room model class when MapDraw.c / the model manager is written. (2) IngameDrawRoomMorph
  (ingame.c) does not test room->mdl == 0; now that MapLoadGetCurRoomAddr returns a real buffer once an
  area loads, it will resolve EE address 0 while MapDrawInitRoom is a stub: guard it there when the
  first in-game run gets that far. (3) PC guards added where FF3 reads through nullptr (bad buffer id):
  MapLoadCheckLoadEnd, MapLoadSetDrawFlg2, MapLoadGetFileAddr. (4) MapLoadSetNowRoom 0x11b928 took
  FF2's name by position only (FF2 sets MyPosLabel; FF3 notifies event SE, enemy set and play data).
  (5) Callers in other TUs were renamed with the Ghidra fix (FUN_0011b1b8 -> MapLoadGetRoomNoNow,
  b6e0 -> MapLoadGetBuffID, b8b8 -> MapLoadGetAreaData, b870 -> MapLoadGetRoomData, old
  MapLoadSetNoRegList(mask, sw) -> MapLoadSetDrawFlg, old MapLoadInitRoom(room, floor, pos) ->
  MapLoadCheckBuffPos, MapLoadGetRoomNo4BuffID -> MapLoadGetOffset); RegListGetBuffID /
  RegDatGetBuffIDG now take float *pos.
- **story_door.c (2026-10-15)**: (1) gp-0x30 (0x3734c0, a common symbol) is the player's mh per room
  while a door mode runs (MapDoorRoomMhGet); the only other access (exact lw/sw/addiu scan) is
  init_Story_Door_Open 0x1f34f4 (addiu t4, gp, -0x30), so it is this file's (sbss/common); defined
  as map_door_room_mh[2] in story_door.c; init_Story_Door_Open writes [room] and [room ^ 1], so 2 is right. (2) MapDoorHitCallback's rectangle size mixes x
  and z terms ((x1-x2)(x2-x0) where a cross product would use z); kept as FF3 computes it. (3) The door
  object at 0x370f10 is CMapDBuff (RTTI, ctor 0x10dc78, dtors 0x10dd10 / 0x10dd50): written
  2026-10-15 in ingame/map/MapDBuff.c (verify: in-game only; its draw callback needs MapPut.c and the
  model builder FUN_002035d0, both stubs, so PC guards skip null models). (4) PC guard: the door hit table
  (4 rows) stops counting new doors at 4; FF3 would overrun into the kaza puzzle bss at 0x3ef3b0.
  (5) MapLoadInitRoom 0x11b608 (MapLoad.h, stub) passes the player pos pointer as the third
  argument of RegListGetBuffID / RegDatGetBuffIDG, typed int b in MapRoomFile.h / RegDat.h: make it
  a float * when MapLoadInitRoom is written. (6) MapDoorRoomChange calls FUN_00203330 with the door
  vec's model even when it is nullptr; the PC skips that call. (7) Verify all of it once a door is
  taken in game (phase 34 now wired in gphase.c).
- ev_change.c (2026-10-15): FF3's name for the file 0x1992d0..0x19a0a0 is unknown (it sorts between
  ev_get.c and ev_macro.c in the link order; FF2 has no such file). Named after FF2's ev_change.c.
  It may be two files (conditions + table up to 0x199ad8, then the state core); no static-init or
  other marker separates them. TEL_STATUS (0x1997f0) and CINE_FLAG_CHECK (0x1998d0) pass the
  condition's label byte as the flag / mask instead of an argument; reproduced as FF3 does it - check
  against the event data when the telephone / cine film code runs. Req_CompulsionSetEventState writes
  row -1 on the EE when the queue is full; the port skips it (PC comment).

- 2026-10-15 (start_menu.c, verify): the TU is written whole (114 / 114), but nothing shows it until a
  game is cleared (Story_Start opens 55 only when FinderStateHasAny). Open: split the range into its four
  FF3 files (thunks 0x1a5d68 / 0x1a80d8 / 0x1a8a30 / 0x1aa908; FinderModeWrkStart 0x1aa940 sits after the
  last thunk, probably the finder-mode file's first function); phase 8 (115's target for the mission sub
  menu's second entry) is unnamed; MISSION_RECORD.state: 7 = never played (MissionWrkInit), 5 / 6 the top
  results (MissionIsComplete(1) wants every mission at 5 or more); which result gives which state is
  computed in mission_end.c (not written). The mission-list run is ingame/mission/mission_list.c since
  2026-10-15 (verify); the
  CExtraMissionShelt is written (ingame/mission/ExtraMissionShelt.c, 2026-10-21, verify).

- 2026-10-15 (mission_list.c session): the inferred finder_camera.c run 0x1aaef0..0x1b1648 was six FF3
  files; five are pinned now (mission_mode.c, mission_list.c, mission_end.c, mission_start.c,
  start_menu_top.c; architecture.md). Left inferred: 0x1b0628..0x1b1648 (FinderCamFlag* 0x1b0628..0x1b0798
  with save getter 0x1b0780, heap new/delete 0x1b0868 / 0x1b08d8, phase 61 StoryMenu_Photo 0x1b0930..)
  -- find its file end and name it (FF2's finder_camera.c matches there are low). FF3's real
  finder_camera.c (FF2 FinderModeCameraCtrl) is elsewhere. Open: the mission table's PAL times are
  dead code in FF3 (GetPALMode is 0 during static init); ReiPan keeps that behaviour.

- (2026-10-14, title_obj.c session) TitleObjShow / IsShown / Hide were renamed TitleObjPause /
  IsPaused / Resume: +0x10 goes to MapSkyMovieProc, where non-zero returns before decoding (the frame
  stays); story_load_mc.c / story_save_mc.c call them around card access. Their wrappers
  OutgameBgObjShow / IsShown / Hide (outgame_bg.c 0x250178..0x2501f0) want the same rename.
- (2026-10-14, puzzle.c session) FF3 0x2b90f8 is an out-of-line clamp(v, lo, hi) used by several
  files (end_Story_Puzzle; brief lists it under unassigned@001aa9e8). Written as a static PuzzleClamp
  in puzzle.c; give it a shared home (a common header inline) when a second caller is ported.
- **fndr_effect.c roles** (2026-10-14): read from callers, not strings. PHOTO_WRK_180 flag bits: 0x01 PlayDataMain,
  0x02 special enemy on screen, 0x04 charger ready, 0x08 no setter found, 0x10 charging with the charger ready
  (0x23d178), 0x20 shutter chance / no gauge -- what each colour row means on screen is unchecked. PHOTO_WRK_1E8
  "spirit charge" (600 per step, FUN_002395c8 steps per player and camera level; player 1's second charge with
  equipment 5) and PHOTO_WRK_288's icons (on[1..12], set by code not yet reconstructed: who writes on[], level,
  corner_a/b, _12c, _130..) need the in-game screen. StopPlayerMim / ReqPlayerMim / IsPlayerMimParts keep FF2
  body-match names but are camera tables in FF3 (PLYR_WRK +0x230 camera level; table 0x361328 by equipment 6):
  rename when plyr_mdl.c is done. The FUN_001bb4c8 / bb5d8 'flag' argument (FUN_001b7718's 9th) is open.
- ~~(2026-10-14, enemy.c session) **Enemy ROM data still zero on PC.**~~ Done 2026-10-19 (door crash
  hunt): tools/reverse/enedat.py writes the three row tables and the route lists to
  ingame/enemy/ene_dat.inc (included by enemy.c); the pointer table at 0x323ff0 has 25 entries, not 16.
- (2026-10-14, enemy.c) **ENE_WRK host layout boundary moved to +0x128**: `_128` (= &_0a8, SetCommonDat) is a
  real pointer, so every later field's offset is asserted as a lower bound only. +0x130 / +0x138 hold EE
  addresses into the file at EE 0x12ef000 (FUN_0018a468, effect_scr.c) as u_long words: decide their host
  type when effect_scr.c is written.
- (2026-10-14, enemy.c 2nd session) **Wrong FF2 matches found** (not renamed, they hold names): 0x2674b8
  "LineStraddle" is a wrapper of FUN_002a38b0 (so FF3's real copy at 0x198238 is EneLineStraddle);
  sceGifPkAddGsData was sceVu0UnitMatrix 0x283008 (renamed). FUN_0013b270 (bss 0x391004, effect TU
  unassigned@001374d8) switches the near enemy stream to YKN_19_IREZUMI2B: its meaning is not traced.
  (2026-10-19: it is TattooEvGetToggle, tattoo_event.c: flipped 0 / 1 each time a tattoo event's camera returns, so
  every second grab plays the other stream.)
  EneStreamNearMain stays in state 3 forever when the stream id is already -1 there (faithful to FF3).
- (2026-10-14, enemy.c 3rd session; **decided 2026-10-19**) **The enemy-script interpreter is its own file**:
  `ingame/enemy/enemy_act.c` 0x18a338..0x1931e0 (EneActSet / EneBlinkSet start it; ene_main.c ends at its static
  initializer 0x18a280..0x18a338); the special-object run at 0x198980..0x199150 is `ingame/enemy/ene_spe_obj.c`
  (its wrong FF2 names EJobC3F / EJobC43 / EJobB04 renamed EneSpeObjSePlay / EneSpeObjCanStart / EneSpeObjTblStart).
  All 154 table entries are Ghidra functions named by slot; ENEALG_WRK is in ene_main.h. Written so far: the two
  machines and all 154 handlers (complete 2026-10-19, verify until a ghost acts on screen).
  Open: the ALG pak ENE_ACT01_OBJ (0xac3 -> EE 0x12ef000) is read through ReiPan_GetHostPointer; 0xac4 -> 0x12f7000
  (ENE_ACT02?) has no reader yet. ENE_WRK.act_req (+0x172) is read by EJobB1E.
- (2026-10-14, enemy.c 3rd session) **ENE_WRK.level (+0x588)**: -1 at EneWrkInit; EneMotScrWinMain reads it as the
  row (1..4) of the script's shutter-chance / fatal-frame windows, shrinking them by the block's rate per level
  above 3. Set by EneLevelByDmgReact 0x189bf0 (1 after a plain photo-hit reaction, + 1 after a shutter-chance one;
  its caller 0x23d5f0, roku_pzl.c's inferred range, is not written). ENE_MOT_SCR.attr (+8, 0..2, EneMotScrAttrGet for op
  0x18aaa0) has no meaning yet. EneMotScrSet's FF3 test of the anime byte against -1 never holds (kept as always).
  EneMotScrSetFrame does not rewind its command pointer (FF3 bug kept: only frame 0 runs the commands).
- (resolved 2026-10-16: the Char* classes are real; EneMdlCreate news a CharEnemyModel. Left: the watch
  control's +0x08 is still the free stub FUN_00148f30 -- make it a virtual of CharWatchControl /
  CharCatWatchControl (0x149ad0) when alg_manage.c is written.) (2026-10-14, enemy.c 3rd session) **Model objects still by address**: EneMdlCreate allocates
  sizeof(CharEnemyModel) and calls FF3's ctor 0x144948 (stub in char_model.c); the look control is called as
  FUN_00148f30 (CharWatchControl vtable 0x341b48 +8; the cat's 0x149ad0 differs, so make it virtual when the
  classes are reconstructed). FUN_001d3180 (GameModel +0x0c / +0x10 hook words) is a stub in unassigned_stubs.c.
- album_disp.c (2026-10-14, verify): 0x1d97a8 (FUN_001d97a8, b * 3 + a; AlbumPhotoPrintSubject passes (0, obj_no), GameOverMenuTopDisp too) is unnamed: a message-id helper in the gameover/ghost-list run; name it with that run. The photo decoder 0x228878..0x22b780 (PhotoDecodeSetAdrNo / PhotoDecodeDraw / PhotoThumbDraw, state gp-0x1c40..-0x1c3e, Ghidra names SetPlyrFinderEnd 0x229bd8 / PlyrMovePad 0x229bf8 / SetPlayerHitRadius / PlyrNAnimeCtrl there are wrong FF2 matches) sits in the unassigned 0x228xxx..0x22b7xx run (FF2 photo.c's neighbours: photo_make.c, play_data.c); album_disp.c declares the three in a marked block. AlbumDispCtrlInit 0x121cc8 stays in album.c (pinned there) although only album_disp.c reads its bytes.

- (2026-10-14, story_main.c session) **0x372c00 / gp-0x7bd0 meanings.** AlbumIngameFlagSet (0x121590) sets
  0x372c00 on an album-edit step (0x123ce0 state 2 -> 3) when opened in game; AlbumIngameFlagGet == 0 sends
  the album's exit through phase 54 (unnamed, init 0x124f88) first. It is saved with 0x372c04 (callback
  AlbumIngameSaveGet {0x372c00, 8}; ALBUM_INGAME_SAVE in album.h keeps the 8 bytes together on PC). AlbumSetIngame's second argument goes to sdata 0x36b920 (album_open_arg,
  no reader found yet; callers 0x10c928 / 0x14ca90 not reconstructed). Name both when album_edit.c is done.
- ~~(2026-10-14) FUN_001a3610~~ resolved: EventMacroLoad_StreamInit (ev_command.c), restarts the saved looping event streams after a load.
- (2026-10-14, ev_command.c session) **The old finder.c range is split**: event handlers -> ingame/event/prg/ev_command.c (0x19e538..0x1a4be0), start menu -> ingame/menu/start_menu.c (0x1a4be0..0x1aa9a8). What is left of ingame/photo/finder.c is FinderBankGetEntry 0x1aa9a8 and FinderBankIsLoopSnd 0x1aa9c8 (mission-table getters): fold them into the mission run unassigned@001aa9e8 when that is defined (FinderModeWrk* there is probably mission mode, "Finder" a wrong guess). The start menu's Top / Difficulty / Costume phases (0x1af878..0x1b0600) sit inside the inferred finder_camera.c run: pin them (start_menu2.c or part of the same module) when that run is settled.
- (2026-10-14, start_menu.c session 2) **ingame/menu/start_menu.c is four FF3 files**: GPhaseCtrl atexit thunks (calls to GPhaseCtrlTerm 0x1d3fc0 at 0x1a5d7c, 0x1a80ec, 0x1a8a44, 0x1aa91c) end the bonus shop (0x1a4be0..0x1a5d70), the save works + gallery (0x1a5da0..~0x1a80f0), StartMenu_Main (..~0x1a8a40) and the album / Story_Opening part (..~0x1aa918). Everything is written into start_menu.c for now; when the TU is done, split it with tus.py define (suggested: ingame/menu/start_shop.c, start_gallery.c, start_menu.c, start_album.c) and move the code with its comments. Decide together with where the Top / Difficulty / Costume phases (0x1af878..) go.
- (2026-10-14) **"Finder*" names that are not finder code**: FINDER_STATE_WRK (clear counts), FINDER_SLOT_WRK (unlock slots), FINDER_BANK_WRK (wakeup / sleep movies, getter 0x2b8f28, init 0x25fd38 still a stub so the rows start at 0 instead of -1). Rename them (Ghidra + src) in one pass when start_menu.c / the mission run are written; kept for now to avoid churn across ingame.c / mc_set_data.c / unit_ctl.c.
- ~~(2026-10-14) Short generic Ghidra names left in ev_command.c's range~~ resolved (second ev_command.c session): EvMsgCtrlInit 0x19e950, EvMsgCtrlRelease 0x19e960, AreaChangeEnd 0x19f1e0.
- (2026-10-14, ev_command.c session 2) Wrong FF2 matches around the event handlers, renamed or flagged: 0x23de58 was SixPuzzleDispInit, is PlyrGetFinderState (reads PLYR_WRK.flags; its address lies in the roku_pzl.c run, so its prototype sits in roku_pzl.h until the plyr run 0x23d..0x23f is settled); 0x23a158 was SetPlyrMdlNo, is PlyrLookDirectStop. Not renamed for lack of evidence: KazaPuzzleDispMain 0x1f5428 (FURN_CANDLE_BLAZE_UP: walks the 128-row furniture-candle table 0x3ef3b0 of the current room and flares rows up; FF3 has no kaza puzzle) and FUN_001f5138 next to it: name both when kaza_pzl.c is reconstructed (likely a furniture-candle module).
- (2026-10-14, ev_command.c session 3) Names kept although doubtful: SavePointTopDispInit 0x24f2e8 is RANDOM_GHOST_OFF's worker and FUN_0024f2a8 RANDOM_GHOST_ON's (savepoint_top.c's slot holds the random ghost table 0x3f7ab0); FurnCtlGetSize 0x10cdc0 is called by GAME_CLEAR_ with nothing read back; ItemUse 0x1ea790 is ITEM_GET's (FUN_001ea608 is ITEM_USE's): rename when items / furniture are reconstructed. GetEvGhostExist 0x19a880 was renamed EvGhostTblDelete (GHOST_DELETE; the FF2 name is a query). FINDER_MODE_WRK got a partial layout in finder_camera.h (+0x28 the mission result EV_MISSION_CLEAR sets) and FINDER_BANK_ENTRY +6 hp_rate (MISSION_SETUP); unassigned_stubs.c forward-declares MAP_OBJ_ROW for the map-row stubs.
- (2026-10-15, ev_command.c session 4) ev_command.c is complete (status verify): nothing on screen runs an event until the dispatcher 0x19b5b8 and the argument decoder 0x19b158 (unassigned run 0x19b158, ev_macro.c's slot) are written; that run, with the 0x199xxx conditions, is what makes these handlers visible. Unconfirmed: FUN_0021a940 (NPC_INTO_PLYR_SIGHT) taken as (npc, dist, sight) from the asm (a0, f12, f13), Ghidra's order differs; GET_HIDE_STATUS_ is true for argument 0 while the player is in mode 10 (inverted-looking, but that is the asm); PLAYER_STAT_CHECK state 0 tests the crawl flags PLYR_FLG_PZL.
- (2026-10-14, ev_command.c session 2) The condition table 0x3242e8 has 72 rows, not 48 (evtbl.py stopped at EV_OR, row 48, handler 0; fixed). Rows 49..71 are named after their keywords now (EvCheckGameWorld .. EvMissionCounterIsMore; CHECK_GAME_WORLD and CHECK_DAY_FLAG replaced the wrong FF2 matches EvSisGazePointDefSet / EvItemNameDispEnd); 0x199338 GET_DEVELOP_STATUS_, 0x19f008 SELECT_CHOICE (MSG_CHOICE's answer, sdata 0x370248) and 0x1a4108 SP_GAMEOVER_ were mkfunc'd. Most of them sit in the 0x199xxx run (ev_macro.c's slot): write them with the dispatcher. The EvWait* names of rows 53..61 came from bodies, not keywords; check them against PEEPING_END / TEL_TALK_END / TAPE_NOT_HAVE / CINE_MENU_END / CINE_FILM_END / CINE_FILM_NOT_HAVE when written.
- (2026-10-14) PlyrMdlChange 0x23ec08 returns PlyrMdlCreate's int (a tail jump); plyr_sub.c now returns it (PLYR_FLASHLIGHT_SET waits on it).
- (2026-10-14, resolved 2026-10-16) **GameModel stand-in vs AcsModel.** FF3's GameModel ends at 0x1c;
  EE +0x1e is GameMotionModel's second file number (shp_no), not an enemy kind: the stand-in's `kind`
  is gone. If PlyrEneTouchCheck's 0x60..0x66 test is written, it reads the enemy model's shp_no.

- **Save block 54 saved in the host layout** (2026-10-15, n_plyr_camera.c): PlyrCameraSaveGet returns
  &plyr_camera and sizeof on the host (the works hold the model pointer; the load path clears it). The
  first reconstructed owner with a pointer chose this; a card file written on PC differs from the PS2's.
  Revisit with the policy item below if EE-compatible saves are wanted.
- **Save blocks vs. host layouts** (2026-10-14, mc_set_data.c): the 65 save callbacks (save_data.h,
  most still FUN_ stubs in their owners' files) report EE block sizes, and the card file is the raw
  bytes. Owners whose structs differ on the host (pointers, e.g. INGAME_WRK.plyr_wrk at +0x2c, the
  event cursors MioPan fixes up) must either keep an EE-layout save copy or serialise field by field;
  decide when the first owner's callback is reconstructed (IngameWrkSaveGet is the obvious first).
  MioPan's MemoryCardFixupSavePointers (mc_set_data.c PORT note) is the FF2 precedent. Until then a
  stub callback leaves {nullptr, 0}: the gather stops at the first one, the size sum skips it.
- **Save callbacks to name** (2026-10-14): 51 entries of save_data.h keep FUN_ names; each comment
  gives the EE block {address, size}. Name them in their owner TU's session (move the prototype into
  the owner's header then).
- **B2dScrollBarDrawKnob3 without a middle piece** (2026-10-14, ScrollBar.c): when the middle item has
  no height FF3 scales both ends by (top h + bottom h) / len, which enlarges them when the knob is shorter
  than its ends (the inverse of fitting them). Reconstructed as FF3 has it; check on screen when a
  B2d_ScrollBar3 with a zero-height middle is first drawn (menu_cassette / FurnCtl / kai_pzl).
- **Event queue kinds** (2026-10-14, ev_queue.c): the four ROM lists use message types 114, 112, 121,
  121 (msg0); what a kind stands for (chapter? character?) and which event ids queue which entries is
  open. Read the event command 0x1a0d78/0x1a0da0 owners (event script) and 0x221360/0x221820 (kind 1).
- **Scene code after scene.c's static init** (2026-10-14): 0x253620..0x253c68+ (unassigned) still
  holds scene code (SceneWrkRelease 0x253c38, FUN_00253b18 called by scene.c's 0x253288). Either a
  second scene file or the static init is not scene.c's end; decide when scene.c is ported.
- **MENU_CSR_MAX** (2026-10-14, menu_csr.c): menu_csr_wrk at bss 0x391b28 is sized 128 (ids up to 0x77
  seen at MenuCsrInit/InitWithFilter/InitTbl call sites); the next bss object after it was not found
  (Ghidra has no xrefs into the computed array). Settle when a neighbour global is identified.
- **menu_csr_tbl** (2026-10-14): data 0x2f63f0 holds one known MENU_CSR_TBL (2 x 4 grid at 0x2f63e0,
  used by MenuCsrInitTbl(0x6c, 0, 0) in 0x1a8110 and by MenuCsrObjInitTbl 0x20b148, which takes any
  tbl_no and adds nothing on the table's size); the zero words after it may be more entries. Settle from
  the tbl_no values MenuCsrObjInitTbl's callers pass (0x113db0, 0x1b1500, MenuDispMain..., none written).
- **Phase 18 exit** (2026-10-14, unit_ctl.c): one_Title_FrameRate_Sel sends both OK and cancel (with no
  mode change pending) to phase 5 Title_Mode, not 17 Title_Menu, i.e. through Title_Mode's full init
  (layouts, movie) again; as the ROM reads (GPhaseCtrlSetNext(ctrl, 5, 0) at 0x25fad8). Check on screen
  that this lands on the title menu and not the press-start screen once the title draws.
- **TU split** (2026-10-14): `ingame/plyr/unit_ctl.c` holds FF2 unit_ctl helpers, FF3 stick readers and
  the video-mode phases 6 / 18 (FF2 outgame/framerate.c's role) in one object. No signal splits it yet;
  if one turns up, the phases could move to `outgame/framerate.c`.

- ~~The menu-backdrop work 0x3f6058~~ (resolved 2026-10-20): `ingame/menu/menu_bg.c` 0x208370..0x2093c8.

- **plyr_sub.c movement, unconfirmed roles** (2026-10-14): PLYR_WRK._193 is compared with anime_no by
  PlyrMotMoveGet (flags bit 0 = a new anime) but its writer is not traced; FUN_001476a0 (CharPlayerModel)
  gives the frame the viewfinder idle 0x18 restarts from; FUN_00194ea0 runs when Miku's crawl ends;
  PlyrMoveSpeedCtrl's model step goes to sbss 0x373844, which nothing in the ROM reads. PlyrMdlDraw's
  caller FUN_00148b80 and the pass bit 0x200 (assumed the viewfinder pass) are not traced yet.
  PlyrMoveSpeedCtrl takes a float (the callers load f12) that it never reads.
- **player.c's model calls are devirtualized** (2026-10-14): SetPlyrAnime calls `FUN_001450e0` and
  PlyrAnimeStartPhase `FUN_00146640` directly where FF3 goes through the player model's vtable +0x40 /
  +0x60 (CharPlayerModel 0x341800). Correct while PLYR_WRK.mdl is always a CharPlayerModel (PlyrMdlCreate);
  when the CharModel classes are written (unassigned@0013e648), turn these into virtual calls.
- **PLYR_WRK._2e0's object** (0x14 bytes, ctor 0x243390): FUN_00243398(obj, a, b, c, d, kind) stores three
  floats (a, b, c), d at +0xc, kind at +0xe, clears +0xd and +0x10, priority by kind; PlyrDamageCtrl sends
  (0x3c, 0x3c or hold_pow, 0x1e, 0x80, 1) on hits. Looks like a camera shake / blur request; typed `void *`.
- **Unreferenced code at 0x2373a0** inside PlayerChangeMode's old body (now PlyrFlashNumInit 0x237360):
  `flash_num = a0 & 0xffff` (a PlyrFlashNumSet nobody calls; `ghidra.py refs` finds nothing). Not
  reconstructed.

- **ingame/photo/fndr_parts.c is probably several FF3 files** (2026-10-14): film, filament, charger,
  markers, HP gauge and lenses are separate FF2 object files (film_no.o, filament.o, photo_charger.o,
  hp_bar.o ...); one TU with a descriptive name for now. A boundary did turn up (2026-10-14, second
  session): the global constructor 0x1c4050 (calls FilmChargeStaticInit 0x1c3fa8(1, 0xffff), the film
  charge times) ends an object file at 0x1c4070, so 0x1c2d80..0x1c4070 (+0x1e8 SE, damage number, +0x4bc
  gauge, film, +0x274, filament, charger) and 0x1c4070..0x1c67c0 (shake, effect-slot gauge, markers, HP,
  lenses) are at least two files. Splitting is a rename/move of finished code (fndr_parts.c/.h into two
  files sorting between fly_ctrl.c and fod.c); do it when a name for either half turns up. The +0x180 / +0x1e8 / +0x288 sub-works
  were split off fly_ctrl.c as `ingame/photo/fndr_effect.c` (0x1c1418..0x1c2d80, 2026-10-14); 0x1c2d60 was
  not a static initializer (an FF2 body match, now PhotoWrk1e8GetSpeedEquip, called from 0x23d178).
- **Roles still open in fndr_parts.c**: PHOTO_WRK_274 (two turning angles, pulsing alpha while
  effect_oth's FUN_00188b80() == 2; camera function 1), PHOTO_WRK_4BC (a falling gauge; camera function
  4, PhotoGage4bcDraw draws it only while EffectThunderLightGetLightningFlg, values wrapped to (0, 1]),
  ENE_GAUGE (EFF_SLOT_WRK +0x5a4: 0..100 draining by 'dec' after a wait; who fills it is enemy code),
  FilmDrawTypeBox 0x1c3630 (no callers), PHOTO_WRK_1E8's looping SE
  0x1b7f0009 (player 1, FUN_00237840(5)); the "spirit stone" reading of LENS_WRK.stone (charge / 100,
  lens cost table byte) is from FF3's lens system, not from strings.

- **FF3's IOP module registers no RPC service on PC** (system/iop/iop.c is empty), so every EE-side
  bind goes once instead of spinning (fileload.c RPC 3, file_stream.c RPC 4) and calls return without
  effect. file_stream.c (the movie reader's ring-buffer stream) therefore moves no data; movies use the
  platform's reipan_moviestream.cpp. When system/playpss my_strfile.c is ported, either port MioPan's
  iop_load_stream.c as the RPC 4 server or route MyStrRead to the host stream -- a platform decision.

- **Sound library run 0x269fe0..0x270800 is partly undefined** (2026-10-13, snd_ctrl.c): it holds FF2's
  snd_buffer.c, snd_pcmstream.c, snd_stream.c, an FF3 stream request layer (`SndStrm*`, 0x26c8c0..
  0x26de78), snd_bank.c (0x26e1a0..), snd_3d.c (0x26ebd8..0x26f340), snd.c (defined 0x26f770..0x26fd50,
  reconstructed), FF2 snd_pcmstream.c's Init/Main (0x26fd50..) and snd_util.c (0x270650..). Its
  prototypes sit in snd_ctrl.c's and snd.c's unassigned blocks and SND_3D_ENV / SND_LISTENER in
  snd_ctrl.h; move them when the files are defined. Open on PC: SndInit's stubs return 0 for the work
  buffer cursors (SndBankInitAll etc.), so ee_iopInit must not call SndInit before snd_bank.c,
  stream_auto.c and snd_util.c are real or bridged (ee_iop.c's session decides). SndSetEffect clears
  the packet fields FF3 sends unset (end_adrs on an unchanged mode, delay, feedback). Unsettled: the meaning of SndStrmReqPlay's flag
  bits 1/2 (`opt1`, always 1 from snd_ctrl; `opt2` = the caller's flag), why the same `time` fills two
  request fields (0x3c, 0x40), FF3's bank count (the bridge uses FF2's 30), and
  `SndStreamSetPosition` (0x26db58 calls it with one argument: probably FF2's SndStreamRestart --
  settled 2026-10-13: 0x26c610 is SndStreamRestart, the real SndStreamSetPosition is 0x26c658 and
  unreferenced).
- **SndSystemInit's listener `top` is (1,0,0,0)** as FF3 builds it on the stack (sp+0x50 gets the 1.0);
  in-game IngameSndListenerSet passes (0,-1,0,0). Harmless until the 3D code runs; recheck then.

- **First capture into a new renderer slot is lost** (g2d_draw.c LocalCopy bridge, 2026-10-13):
  `ReiPan_RendererCaptureGsBlock` only declares a slot the first time an address is seen; its texture is
  created at the next frame, so a one-shot copy (ScreenCaptureSave at pause start: LtoB into 0x10ef000,
  then BtoL 0x10ef000 -> 0x2bc0 each frame) does nothing the first time and the pause backdrop stays
  empty until the second pause. MioPan avoided it for FF2's pause by using RendererCaptureScreen /
  DrawCapturedScreen. Fix in the renderer (allocate on declare when no draw holds a slot texture) or in
  gs_reg.c ScreenCaptureSave/Disp (pre-declare 0x10ef000 / 0x2bc0 at boot); check when a pause or menu
  backdrop is first on screen.
- **Frame-buffer to frame-buffer LtoL** (photo_dat 0x227f98: LtoL 0 of the shown buffer onto the drawn
  one) is not bridged on PC (MioPan left it too); decide when the photo path runs.


- **Method names for RTTI classes**: the C++ types now carry FF3's class names, but their functions keep
  the old prefixes (CmnFadeLayer* = capCtl, SelWin* = CWindowMsg, ScrPlay*/ScrHdl* =
  AnimeCodeManager/Interpretor, MotScr* = ...Game, MsgPic* = CTutorial, AlbumViewCtrl* = CandleControl).
  Rename them (Ghidra + code) when their TU is next touched, as `<Class><Method>` (`CapCtlShow`)?
- Phases 8 (album container 24-28), 10, 20-23 (option screens: which is which), 33, 35 (film projector?),
  36 (day/chapter transition with save?), 38, 42, 43 (game-over menu?), 45-48, 50/77: roles unconfirmed.
  Phase 11: init/end return 1, one -> 83 (0x53) — album start menu? Reached from pause "quit" in title
  start mode 2 (finder mode?).
- B2d item hook ABI: HUD hooks read a0 = item name, t1/t2 = x,y (film icon) or t1 = rect, alpha in
  stack[1] (film icon, HP, fire) or stack[2] (numbers, lens, message window); on PC t1 xy, t3 rgba,
  stack[2] alpha for text-kind hooks. Confirm with the B2d draw loop 0x1038b0.
- `B2D_PART` 11 (text id?) is set by every item creator (kind 3 = 100): confirm with its readers.
  B2dCtlRewind (0x1012b8) and B2dCtlDraw return early while the sprite hook (gp-0x7fc8) is unset.
  The item creators are called by B2dFile's commands 4..7 (B2dCmdSprite etc., wired 2026-10-13).
- Bp names: `BpCtlIsLoadEnd` is really "label animation ended", `BpCtlSetField20` a draw flag: rename with
  their callers? BpCtlLoadReq with a full table dereferences null, SetFlag/GetFlag/SetField20/DrawOne do
  not check the handle (FF3 behaviour, kept).
- HUD names from bodies only: GetFilmWrk/FilmGetType/FilmGetNum (0x3e7b58), GetLensWrk/LensGetSlotNum/
  LensGetEquip (0x3e7ce4), PlyrGetFlashNum (player +0x268), AlbumViewCtrlGetGrade (= CandleControl,
  grades float +0x1c), B2dCtlSetItemDispByName (0x105f78 flag), HUD_LAYOUT.kaku.
- MsgGetPicKey (0x2adbf0) returns a short: whether 0xffff or -1 reaches MsgPicHasKey depends on lh/lhu.
- DispSqrRect (0x1c8528): settled 2026-10-13 in g2d_debug.c as (x0, y0, x1, y1, pri, r, g, b, a):
  the third/fourth floats are the far corner, not w/h (the core takes x1 - x0). Floats-first is a guess
  (the ABI hides float/int interleaving) but changes nothing on the EE or PC.
- Item-get window flag bit 1 hides item 0x3ec (ITEM_GET_NO_3EC): what 0x3ec shows is unknown.
- The CcmLoading objects: init_Story_NowLoading (0x120d18) news them into gp-0x900; find who calls
  Main/End and deletes them (one_/end_Story_NowLoading) when that phase's TU is done.
- Boot names to double-check: SubTitleDataLoadReq (file 0xf11, guessed from position), MarkDatLoadAll
  (was FurnBpLoadAll; settled 2026-10-15, MarkDat.c), ScreenEffectDataSetup's callee (now PK4SendVram, settled).
- ~~Object 0x2ab688 singleton~~ resolved 2026-10-14: FINDER_STATE_WRK 0x41e490 = clear counts per difficulty (ingame/menu/start_menu.h).
- `FUN_00198f58` (enemy main candidate, SE banks 0x1b7d/0x1bf5, EJob*) vs FF2 `EnemyMain`.
- `FUN_00228de8` (score/photo bookkeeping per frame).
- 0x32d6c0 mission object, 0x32d700 is the file cache (resolved 2026-10-15: `system/eeiop/file_cache.c`;
  its static init clears it, so the ROM contents do not matter).
- Button masks: OK/CANCEL/MENU/MAP/PAUSE are known (pad_ctrl.h); the other 21 slots, the EU circle/cross
  swap, and 0x32dd00 holding a pointer (0x371730) — are the masks part of a bigger object?
- Pads on PC: the libpad shim reports pressure 0xff for all 12 buttons, state 6 and press mode 1, so
  PadSyncOne goes 0 -> 99 via InfoAct = 2; check whether vibration needs SetActAlign.
- ENE_WRK (ingame/enemy/ene_main.h, 2026-10-14) open fields: +0x1f4 is an ENE_CMN data record (floats at
  +4..+0xc, +0x30/+0x34; id +0x14, kind +0x1e), not a GameModel -- confirm where it is set (enemy.c
  0x19xxxx). The enemy data tables 0x301610 / 0x311600 / 0x315f80 (GetEneDatP) are extracted since 2026-10-19
  (enedat.py, ene_dat.inc). The three paralysis
  looks (SetEneMahiMode 0x193d98 with (1, 0.95, 0) / (1, 0.6, 0.85) / (0.1, 0.82, 1)) and their triggers
  (player 0x23cad0) are not traced. EneGetJointPos calls CharEnemyModel's vtable +0x2c (0x144d70) directly:
  if any enemy model is another class, dispatch through the real vtable.
- player.c (2026-10-14, third session): `FUN_0025e9e0 / 0x25eb40 / 0x25ebc8` (yaw/pitch to a point,
  out-of-view test, midpoint) and `FUN_0025ee70` (2D length) sit in title_menu.c's inferred range but are
  generic vector helpers; TitleMenuMovePhase 0x25ef78 reads the left stick for PlyrGetMashInput (name
  probably wrong). `PlyrGetCheckActionKey(0x1b)` 0x23fbc8 returns the player's facing for PlyrViewRate
  (the name is wrong). ENE_WRK +0x50c is a pointer (`tpos`, the point the camera aims at), not an id:
  fndr_parts.c's marker keeps its low 32 bits on PC (EneMarkId). PlyrSetEneWait 0x232ba8 gets &plyr->flags
  (+0xd0) from its callers on the EE: reconstruct those callers (ene_main.c EneAttack* done 2026-10-14; fly_ctrl.c
  0x1c0f00) to pass the PLYR_WRK. CheckEneView 0x198328's 2nd / 3rd float (9999, fov) roles unverified.
- PLYR_WRK +0x2e4 "model in use" is stored 0 on both PlyrMdlCreate paths: find the writer. +0x2e8/+0x2ec
  roles (PlyrMdlCreate args 2/3; finder-mode start sets 2/1). `PlyrGetCheckActionKey`'s float.
- INGAME_WRK b01..b04, +0x24, +0x28 unnamed (save-slot header writer 0x202a80 copies b00/b02/b04/+0x28;
  FF2 had mChapterNo at +0); title_start_flg word sizes unverified.
- Options: who sets `option_cancel` (expected: screens 20-23 on cancel); OPTION_DAT b00 (6) and +4 (1);
  OPTION_WRK may be bigger than 0x3c; OptionGetSndVolume0/1 — which is BGM, which SE.
- draw_en_* words (0x371300.., ingame.h): owner TU unknown, defined in ingame.c for now.
- GPhaseCtrlTerm does not free the node stack (FF3 relies on InitCppAndSystemHeap rebuilding the heap);
  on PC a few nodes leak per soft reset. Free them on PC, or leave as FF3 does?
- Provisional names (role guesses; rename when the module is read): gra3dSetDrawParam(int, float;
  GameSystemStart passes 0, 1.3f; read back by gra3dGetDrawParamI/F in _CalcValidLightIndexByType<4>, so
  light-selection parameters), TitleMovieFlagClear / TitleMenuFlagSet (gp+0x500/+0x504),
  GetLayoutSeWrk (0x377f10), GetEneSpeObjTbl (0x3e0068), StoryMapRelease,
  GetPhotoModeWrk (0x3e7918), PlyrIsMode233Over6, PlyrSetB260Mode2, MotCtrlMakeBlendWork / MakeWork44 and
  AnimeCodeManagerGame.blend / MOT_CTRL fields. Named after their data address because the module is
  unknown: Tbl328fe0Init, Wrk3f7c90Init, Wrk391850Init/Main/Draw, Wrk3fd0e8Init/Main,
  Obj3739d0Main, Sub1cae98Init, File0ad6Load/Apply (file 0xad6 = `DOOR_STAT_ECB`), Wrk_gp50Clear,
  Draw104d000.
- (resolved 2026-10-21: ChrSortGetSgdMatrix 0x10b928 is GetExtraMissionShelt; IngameRetryPrepare
  0x10bb20 is the static-like member CExtraMissionShelt::CarryStore, called through the instance.)
  Doubt: CarryStore / CarryRestore are written as static members called as ptr->CarryStore()
  (FF3 passes the instance in a0, never read; one_Story_Pause calls it only in start mode 2, where
  the instance exists). MissionCarryRestoreAll calls Restore on each shelter unchecked, as FF3 does.
- `album/prg/scr_play.c` (= AnimeCodeManager): file name and directory invented (link order
  album_mem.c < it < album_save.c). Next there: ScrPlayStart 0x129248, ScrPlayStep 0x129800, cursor runner
  FUN_00128f38, FUN_00129490/001295d8.
- EEIOP_DEF (FF3 0x50 bytes): fields +0x3c (0x5010) and +0x40 (0x1faff0) unknown; settle in ee_iop.c.
- sys_wrk language ids 0 and 1 (GetLanguage maps them to English; GetSystemLanguage never produces
  them): Japanese / US English left over from other regions?
- SwapDBuff parity: on even frames sceGsSwapDBuffDc puts draw env 0 (draw01) but pdrawenv1 = draw11.
  Check what the 0x372608.. readers expect before relying on the names "this/other frame".
- The platform SDK's `SCE_GS_INTERLACE` 0 / `SCE_GS_NOINTERLACE` 1 look swapped against Sony's
  libgraph (INTERLACE 1); system.c passes numbers. Verify before using the macros.
- `graphics/gs_reg.c` + `system/os/sys_heap.c` (0x1e6b98..0x1e75f0) were split by content: no section,
  data-order or eh_frame evidence says one object or two. Merge them if evidence turns up (e.g. a shared
  static).
- ScreenCaptureDisp draws types 1 and 3 with TEX0 = 0 (only 0 and 2 get a TEX0): check whether
  DispSprD2 substitutes a texture when tex0 is 0, or whether those types draw garbage in FF3 too.
- SetGsRegCached/SetGsRegsCached third argument: only -1 is tested (send nothing); constant call sites
  pass 1 (78 of 81 SetGsRegCached sites, 12 of 48 SetGsRegsCached sites; the rest load it from
  elsewhere). Look at one when such a caller is reconstructed.

- map_bgm / FurnSave / MapObjWrk / MapRoomFile (2026-10-15): FF3's map_bgmGetStrFile reads
  map_bgm_tbl's column directly; the saved work copy aMapWrkBGMTbl (seeded at init, edited by
  EvMapStreamChange = map_bgmChangeTbl) is never read back in FF3. Check whether MAP_STREAM_CHANGE
  then has any audible effect once in-game runs (it should not, if the asm is right).
- The room work at 0x3f16d0 (FUN_001f5f28), the load slots 0x3f16b0 (FUN_001f5a60 / FUN_001f5b58) and
  the per-room load addresses 0x3f16e8 (unassigned@001f6fa0) are room-loading code inside the inferred
  kaza_pzl.c / unassigned runs; prototypes sit in kaza_pzl.h for now. A session on 0x1f5xxx..0x1f7xxx
  should split them out (they are not puzzle code).
- MAP_ROOM_DAT keeps EE addresses (u_int); MapRoomFile.c converts to host pointers for the loads
  (LoadReqGetAddr takes host pointers). Whoever writes MapLoad.c must keep that convention or change
  both sides.

- **NPC talk path stubs (2026-10-15, npc_act.c session).** The act machine and its data are written
  (verify), but a talk cannot finish on PC until these stubs are real: ~~SubTitleIsEnd 0x2595a0 and
  FUN_00259668~~ (written 2026-10-15 in subtitle.c as SubTitleIsEnd / SubTitleIsExec) --
  PlyrCameraSerifIsEnd waits on them; ~~FUN_001415b0 / FUN_001422e0 / FUN_00142358~~ (cat_ctrl.c written 2026-10-19: CatGetWrk / CatIsHeld / CatSetFixPos); the n_equip_tray.c helpers
  (0x21d408 / 0x21d468 / 0x21d498) are written since 2026-10-15. PC guards in npc_act.c (each marked `// PC:`): PlyrCameraSetPosLabel
  does nothing for label < 0 (the ROM reads the 0x90 bytes before the table when no label is free),
  PlyrCameraGetMotFace returns -1 past its tables, an op outside the three tables ends the script.

## Decisions made

- 2026-10-16: **g3dCamera.c keeps MioPan's clip-space PORT DEVIATION** (decided in an unattended run, open to
  the user's veto): g3dCalcViewClipMatrixPerspective negates mat[1][1], g3dCalcViewClipMatrixOrtho negates
  mat[1][1] and both Z terms, exactly as MioPan does. reipan_renderer.cpp is MioPan's renderer and the
  graph3d bridge still to port (miopan_graph3d.cpp) reads matViewClipObject in that orientation. FF3's
  ROM values are in the comments (sdata 0x370a10..0x370a20). Consumers seen in FF3 so far: the
  bounding-box cull (invariant under the flip) and effect_obj.c's matLocalClip (VU1 only). If an FF3-only
  consumer turns up that projects clip Y to the screen on the CPU, it must account for the flip.
- 2026-10-15: **Methods are members, not C-style functions** (the user's rule): a function whose first
  argument is a C++ class object is written `Class::Method`, declared in the class with its `FF3 0x`
  address, even when it is not virtual (reconstruction.md "Methods, not free functions"). callcheck now
  matches calls by the annotated address and stubgen stubs undeclared members, so methods cost nothing
  extra. The ~140 existing C-style methods were converted the same day (a3f8c90..bee22c7); GameModel's
  stubs and AcsModelCtor wait for `unassigned@001d2a40`. The types without RTTI that have constructor
  code in FF3 followed (over 300 functions: B2dBase's, cmn_util's, LERP_F, the variable templates,
  menu_draw_tex's, BP_OBJ, TITLE_OBJ, the system / gphase / map-sky / finder singletons, the photo
  work, the lanterns, PLYR_MDL_FX, HUD_LAYOUT, CandleFurn; reconstruction.md lists the evidence).
- 2026-10-15: **Priority is now "reach the game, then 3D"** (user's call after a New Game hung on
  "Now Loading"; the re-assessment is "Next up" above). The pinned queue holds 55 TUs in milestone order
  (M1 loading screen .. M5 Rei on screen); effect_obj.c (partial) and the rest of the in-game systems wait
  until a room draws. Decided in that re-assessment, open to the user's veto: MioPan's graph3d bridge
  (miopan_graph3d.cpp) is ported into src/reipan/rendering/reipan_graph3d.cpp by the gra3dSGD.c session,
  the one platform-layer change M4 needs; settings.smoke_pad gained 1600:CROSS so runs enter a new game.
- 2026-10-14: **The IOP side is MioPan's FF2 iopsys port** (src/system/iop/, copied from MioPan's
  zero_rom/zero2np/src/system/iop/; the sdk/iop_*.cpp shims were already identical). FF3's own IOP
  module (ZEROIRX.DIL / .HIL) is not reconstructed; FF3's EE speaks FF2's command numbers and payloads,
  so FF2's server is a faithful stand-in where they agree. FF3 differences applied: STREAM_START is 0x3c
  (no file name), IOP_SND_INIT.stop_block an int; iop_snd_def.h asserts every payload size the EE sends.
  Doubts: FF3's PCM stream payloads (PCM_STREAM_START 0x110 is FF2's) are unchecked until
  snd_pcmstream (0x26fd50..0x270240) is written; FF3's IOP may differ in behaviour (stream loop, pitch)
  where the EE side cannot show it. RPC 3 (file load) is registered but unused (the EE serves loads
  through ReiPan_FileLoadServe); RPC 4 (file stream) now exists, so file_stream.c's fallback path is live.
- 2026-10-13: **Temporary FF2 load bridge** (user: "temp sub with FF2/MioPan code"): `src/lib/load_bridge.c`
  answers FileLoadReqEE / FileLoadIsEnd / AllFileLoadIsEnd / FileLoadCancel* / GetFileSize from ZERO3.FHD,
  serving each load synchronously through MioPan's host loader (`ReiPan_FileLoadServe`: IMG_BD.BIN span +
  CMP decode), so drawing TUs can run on real data now. Marked `REIPAN_BRIDGE` (counted as stubs);
  replaced by the real fileload.c / GetFileSize / ee_iop.c TUs at the end of the 2D queue (done
  2026-10-13: the file is deleted; snd_bridge.c is the remaining bridge).
- 2026-10-13: **cmp_eeiop on PC** (cmp_eeiop.c): the decode thread is created but never started
  (cmp_eeiopCreateDecodeThread skips TerminateThread/StartThread), no SIF command handler is installed
  and cmp_eeiopWaitSema returns at once -- the host loader decodes CMP files itself and no IOP posts
  blocks; a started thread would block in WaitSema for ever and a host thread cannot be terminated.
  cmp_eeiopCancel keeps FF3's TerminateThread + SignalSema (harmless on a dormant thread).
- 2026-10-13: GetSPUMemory keeps the ROM bug of returning without SignalSema when all 50 SPU_BUF
  records are in use (FF2 and MioPan too); never seen, but a hang in the sound code points here first.
- 2026-10-13 (done, spu_mem.c reconstructed; the bridge is deleted): **snd_bridge.c bridged spu_mem.c**, not snd_bank.c: snd_bank.c is reconstructed and
  needs GetSPUMemory to succeed (a stub's 0 would make SndSeBankLoad(-1) fail and the boot wait for
  ever). SPUMemoryInit / GetSPUMemory / ReleaseSPUMemory are FF2's best-fit allocator (MioPan
  spu_mem.c) with FF3's (start, size) init. Delete them when unassigned@00270268's spu_mem part is
  reconstructed (FF3: SPU_BUF[50] at 0x411240, sema gp+0x5cc).
- 2026-10-13: snd_stream.c SetIRQCore: FF3 spins forever when both IRQ cores are taken; the port prints
  the reason and aborts instead (a silent hang helps nobody; the game is dead either way). The ROM's
  reversed SndStreamFadePitch step and the REQ_STREAM_ABORT the IOP never handles are kept.
- 2026-10-13: StreamAutoSetPlayNum evicts the playing stream with the *lowest* priority number (the
  most important one, by cmpStreamQueue's order) when it must disable a busy slot -- FF3's code, kept
  as is (FF2 picked differently). If BGM drops oddly when movies/scenes take exclusive mode, look here.

- 2026-10-13: `graphics/motion/anicode.c` ends at 0x1502a0: 0x1502a0 returns hud_fire (bss 0x391988,
  anim_2d.c's) and the HUD_LAYOUT code after it runs on into anim_2d.c's 0x150b40, while anicode.c's
  work areas end at 0x391984. `ingame/menu/anim_2d.c` is pinned 0x1502a0..0x153460 (FUN_001502a0 moved
  there). Message picture key table 0x358a10 sits in `src/lib/unassigned_stubs.c` (its owner
  unassigned@001b1648 is not defined), typed by anicode.h's MSG_PIC_KEY_DAT.

- Platform layer copied from MioPan into a fresh tree (src/reipan, src/sdk); FF2 game code not copied.
- FFmpeg/shadercross binaries reused from the sibling MioPan checkout, not committed.
- Names: FF2's where the role matches (tagged), FF3-inferred otherwise; provenance tags in Ghidra.
  Callees whose module is unknown are named after their data address (`Wrk3f7c90Init`) until read.
- Library code (Sony, libgcc, newlib, libstdc++/libsupc++) is not a naming priority.
- 2026-10-12: work is cut into translation units (one FF3 object file = one `src/` file), one per
  session, from claim to commit (`tus.py`); the skill + `tus.json` + git carry everything between
  sessions. The user starts each session (`/reipan-port next`, the app's scheduled task
  `reipan-next-tu`, or autopilot.py).

- 2026-10-12: FF3's `main()` stays in `src/system/os/system.c` (where FF3 linked it); `main/main.c`
  is FF3's GPhase root (0x1d2610..0x1d2a40). SYS_WRK/PACKET_BUF_MGR are C++ singletons with free-function
  methods named as in Ghidra. Platform SDK gained FF3's needs (sceGsDBuffDc + Dc swap/put, sceGsPutDrawEnv,
  REG_DMAC_ENABLEW, sceDevVif1Reset; sceGsClear.rgbaq fixed to libgraph's layout).

- 2026-10-12: the `sceVu0FVECTOR`/`IVECTOR`/`FMATRIX` and `qword` typedefs are 16-byte aligned as on the EE
  (`__attribute__((aligned(16)))` / `__declspec(align(16))`; user's decision), diverging from MioPan's
  plain arrays. Struct members of these types now land at EE offsets without `alignas`.

- 2026-10-12: FF3 classes with vtables are written as real C++ classes with virtuals (host vptr, host
  layout); EE vtable slot offsets are kept in comments. Classes not identified yet get minimal
  polymorphic stand-ins (MDL_MOT, MOT_CTRL) holding only the slots called so far.
- 2026-10-12: structs that hold EE pointers go host layout (PLYR_WRK, INGAME_WRK, PHASE12_PARAM,
  GPHASE_*): offsets before the first pointer are asserted against the ROM, the rest is free.
- 2026-10-12: unwritten phases keep empty table slots; `GPhaseCallMissing` reports them (architecture.md).

- 2026-10-13: **2D first** (user): the pinned queue walks file loading -> layouts -> textures/sprites ->
  text -> language select, so every following session can be checked on screen.
- 2026-10-13: **Port from FF2 first** (user's analysis: FF3 objectified FF2's renderer and kept its file
  formats; FF2's debug asserts are why shapes differ): modules FF2 has start from MioPan's source, become
  FF3's classes, lose the asserts, and are verified against FF3 function by function
  (reconstruction.md).
- 2026-10-13: **Class names from RTTI** (`rtti.py`): the invented stand-ins were renamed — MDL_OBJ ->
  GameModel, SCR_PLAY/SCR_HDL -> AnimeCodeManager/AnimeCodeInterpretor, MOT_SCR/MOT_SCR_HDL ->
  AnimeCodeManagerGame/AnimeCodeInterpretorGame, CMN_FADE_LAYER -> capCtl, SEL_WIN -> CWindowMsg,
  MSG_PIC -> CTutorial, ALBUM_VIEW_CTRL -> CandleControl. Function names follow later (Open questions).
- 2026-10-13: **File numbers are `CD_FILE_DAT` names**, generated from ZERO3.FHD (`fhd.py --header`).
- 2026-10-13: **File and sound RPCs are answered EE-side on PC** (MioPan's design, already in the platform
  layer: `reipan_fileload` reads IMG_BD.BIN spans and CMP-decodes them); FF3's IOP module ZEROIRX is not
  reconstructed unless something needs it.
- 2026-10-13: FF3's global operator new/delete (common/cpp_heap.c) allocate from the 128 KB C++ heap;
  ReiPan keeps the host global new (platform and C++ runtime use it, some before InitCppHeap); the FF3
  bodies exist as plain operator_new/operator_delete/... functions.
- 2026-10-13 (superseded 2026-11-23, before the repo goes public): every session's commit was pushed, the
  repo being an archive. Now commits stay local until the user asks for a push. The app's scheduled task `reipan-next-tu` runs one TU every 20 minutes.

## Done

- 2026-10-21 `ingame/map/MapObjReg.c` (claimed by `tus.py next`) was the tail of `ingame/map/MapLoad.c`: 0x11bdc8 (wrong FF2 match MapObjCheckNuno) is MapLoadChangeArea (reads MapLoadStat, reloads the current area's buffer with another area / side; caller SceneRoomLoadMain), 0x11beb8 / 0x11bf08 (misnamed MapObjRegistPhf) are MapLoad's static init and _GLOBAL__I_, 0x2ac330 is MAP_ROOM_DAT's constructor (area -1, RegList 0xff). MapLoad.c redefined 0x11afa0..0x11bf28 + 0x2ac330..0x2ac428; MapObjReg.c/.h deleted (FF2's MapObjReg code is FF3's MapObjWrk.c). Seen: --run 200 still loads room 2 through rs101's door. Not seen: MapLoadChangeArea (only in-game scenes call it; check it when a scene changes rooms). 0x11beb0 is an empty unreferenced function Ghidra folds into BorrowBufCtor's body (0x2183e8): mkfunc it in an interactive session.
- 2026-10-21 `system/encodes/cmp.c` (0x271b08..0x271c30, done) and `system/encodes/encodes.c` (0x2712a0..0x271b08,
  verify): both were MioPan copies from the platform bring-up with FF2 addresses; checked against every FF3 body
  (identical logic), re-annotated, ranges pinned. Seen: every compressed file the boot loads goes through
  CMP_Decode / SlideDecode, and the 15 s boot window's frame and Yes / No buttons match
  work/ref/MEMOCA_TTL_PHF/wdw_YN.png. SlideEncode runs on a photo (photo_comp.c) but its output is only kept
  when it beats the 0.274 bar and shows only in the album: encodes.c is verify until an album thumbnail
  decoded from an LZSS photo shows. Note: the queue's six unassigned@ keys are stale (all defined under other
  names), so tus.py next keeps falling back to the frontier.

- 2026-10-21 unassigned@002093c8 (the item-hook draw helpers B2dHookPrintMsg 0x2093c8, B2dHookPrintNumber 0x209470,
  B2dDrawSpriteDirectLinear 0x209518) joined `ingame/menu/menu_bg.c` (range now ..0x209610, after B2dHookPrintMsgLine,
  same shape). Seen on the title's Video Mode screen (phase 18, unit_ctl.c): the pad
  `100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:LEFT,1300:CROSS,1700:RIGHT` with `build.py --run 80` opens it
  (the title cursor wraps LEFT from New Game to item 4) and moves to Interlace 60hz: the confirm text (0x84, 3), the
  countdown digits and the cursor glow (E_SETTINGS_PHF's brown bar) 150 px right all show. unit_ctl.c marked done.
  The pinned queue's six entries had all been defined already (stale keys), so `tus.py next` fell back to the
  frontier for this pick: refill the queue from a --run 250 log before more frontier picks.
- 2026-10-20 ingame/cine_menu.c (the cine film menu, phase 47; was the alg_manage.c guess) written and seen
  on screen through a temporary patch, reverted: IngameMain's 150th frame set the items 0x3c..0x44 to 1 and
  called CineMenuReq. The list (8 films, 7-row view with scroll bar), cursor, help bar and description window
  drew as the cassette menu does (progress/ shot of this session). Not seen: the real way in (event 84 in
  area 19 by the projector) and "Yes" starting a film there -- that waits for a run that reaches area 19
  (cine_film.c is verify for the same reason). The same patch trick shows any menu phase the smoke path
  never reaches: give the items, call its Req from IngameMain once.
- 2026-10-20 Rei's shadow projected twice, on the floor and on the ceiling above her (the user's bug,
  done). The host receiver shader projected the silhouette onto everything in the light camera's depth
  range; the ROM's projection microcode (VU1 0x2e6ac0, byte-identical to MioPan's vu1/ff2_03.vsm
  SgSuShadow_dma_main) culls each receiver triangle: projector z < 0 at all three vertices (PLOOP_TYPEP2
  FMAND 0xe0 on the last three vf20.z), the camera's back face, and a camera-visible side that faces away
  from the light (face normal . matLIP's w column; strips FMAND 0x30, fans S_w then CLEAR_MESH). Fix:
  _DrawShadow passes s_matIP's z row, s_vDirection and the camera position
  (ReiPan_RendererShadowSetProjector -> uShadowPlane / uShadowLight / uShadowEye); shadow_receiver.frag
  rejects pz < 0 per fragment (the ROM keeps a straddling triangle whole) and faces whose camera side looks
  away from the light (screen-derivative face normal, per triangle). hazards.md "A host shader that
  replaces microcode drops the microcode's culling".
- 2026-10-20 the flashlight lit behind Rei (the user's bug, done). Cause: two negations. FF3's
  MapDrawSetRoomLight 0x1183e0 negates the player model's beam (vmul by -1 on a spot's vDirection) before
  gra3dSetLight(38), because the VU1 spot kernel (CalcIntens) opens the cone along -vDirection; ReiPan
  ports that. The host kernels came from MioPan, which drops that negation at its install sites and negates
  in the kernels instead, so the host opened the cone along the anti-beam vector. Fix: the four kernel
  transcriptions (gra3d.c gra3dCalcVu1VertexColor, reipan_renderer.cpp's vertex spot loop,
  reipan_vu1_lighting.hlsli ReiPanVu1Spot, mikupan_common.hlsli's unused CalcPS2LitColor) dot vDirection
  with light - vertex as the microcode does. FodSetSpotLights (light - interest) and _IsBBLightingupSpot
  (`< 0`) already take the ROM sense. The beam-sense users are unchanged: the characters' and map puts'
  copies of the model's fl (gra3dGenerateLightDataToChar, MapLightMakeRoomReal) are emulated as
  directional lights through g3dCalcSpotlightFalloff (beam sense), and gra3d.c drops emulated lights from
  the realtime set, so they never reach the spot kernel; the prelight bake is its own path. Seen with the smoke pad
  (--run 170), vertex and fragment modes: in room 2's corridor (frame 3900) the floor and lattice in front
  of Rei are lit, and in rs101 (3060) the stones she faces; before, they were dark.

- 2026-10-19 `ingame/plyr/plyr_shot.c` (verify; pinned 0x23c028..0x23e208, 19 functions; the brief claimed
  roku_pzl.c, whose FF2 SixPuzzle / CostumeSetSave matches here were wrong): the charge, the judge, damage,
  score, the picture's record, the view turns. Names: SoulListSetHave 0x1d82e0, GetSavePhotoNo 0x22cae8 /
  AddPhotoData 0x22cce0 (FF2 photo.c), FinderModeAddScore / FinderModeSetMaxLevel 0x1aae20 / 0x1aaef0 (was
  PlyrCamCondChk). Types: SUBJECT_WRK, PHOTO_WRK_DEF methods, ENE_CMN score / soul_no / soul_sp_no,
  PHOTO_WRK_288 score / kill_cnt / hit_cnt / sp_cnt, PLYR_WRK sc_state_old / sc_state. Seen: the photo
  sequence with a real definition (progress/*_ingame_plyr_plyr_shot.c.png, frame 4940: the developed frame).

- 2026-10-19 `fix/second_room_camera.c` (the user's bug, done): after the door into room 2 the camera
  stayed on the event's shot. Reproduced with the scripted pad (now in settings.smoke_pad: door 106140 at
  read ~3830, event 164 at ~3990) and temporary probes (player / camera state in one_Story_Normal, an
  event-command trace in EventExeCmdCall; reverted). Cause: GHOST_APPEAR waited for a ghost that
  enemyIsReadyData never passed (EneFlyModelInitWait stub) -> fly_ctrl.c's FlyAct / EneFlyModelInitWait /
  GetFlyWork / CheckEffectFly written (partial). Also phase 33 Story_Photo (nullptr handlers froze the game
  after a photo) -> ingame/photo/photo_dat.c written (verify). --run 250 shows the ghost behind the lattice
  (frame ~4020), the map camera following Rei down the corridor and back (4140, 4680), the photo's flash
  (4920), the viewfinder after it and Rei in room 2 at the end. hazards.md: "A frozen camera after an event",
  "A phase with no handlers freezes the game silently".

- 2026-10-19 `fix/new_game_movie.c` (the user's regression, done): SCENE0010 played audio over black. Bisected
  in a worktree (e623248 good, 88a5833 good, 5efa7a7 first bad); probes showed the decoder publishing and
  MovieDispMain drawing TEX0 TBP 0x2bc0 every frame while the renderer never sampled the video texture:
  effect_sub.c's work-frame list held 0x2bc0 (room_glow_buf[0], marked when the room glow ran in rs101's
  Story_Normal before the movie phase) forever, so G2dHostSprite dropped the movie sprite. Fix in
  effect_sub.c: marks lapse 2 renderer frames after their last EffSetDrawFrame / ReiPan_EffHostRedirectBegin.
  --run 85 without the START skip shows the movie again, frame for frame like e623248 (TECMO over rain at
  frame ~1920, the project zero logo over the snowy ridge ~2100); the title backdrop still animates; the
  --run 100 opening event in rs101 looks as before. Not traced: why 5efa7a7 (effect_ene.c, whose own paths
  stay off in the opening) is the first commit where the glow marks 0x2bc0 before the movie. Watch: the
  user reported a "degraded" picture -- the shots show it complete; if anything still looks off, compare a
  shot at 31a3016.

- 2026-10-19 `ingame/event/telephone.c` (verify; was the ev_ambient_se.c guess, range now 0x25bae0..0x25c300):
  all 28 functions, the TEL_* event commands' telephone. Defined `ingame/event/tel_talk.c` 0x258b20..0x2590e0
  (TelTalkScript, the call player; 4 member stubs) and extended `graphics/graph2d/tim2.c` to 0x25c300 with
  Tim2CalcBufWidth (made a function in Ghidra; it sat in TelTalkIsEnd's body). Renamed 43 functions (Tel*,
  TelTalkScript*; seven wrong FF2 matches in 0x258b20.. replaced).

- 2026-10-16 `graphics/model_object.c` (verify; pinned 0x2113d0..0x2125f8, every function): RTTI ModelObject /
  TextureModelObject / MultiTextureModelObject, cut out of motion.c's fuzzy range. Created in Ghidra:
  MultiTextureModelObjectCtorBase 0x211f98 (inside 0x211f80), DtorBase 0x212088 (inside the ctor),
  SetTexFrameOne 0x2125d0, GetTexFrame 0x2125e8. Wrong FF2 matches replaced: MorphGetHandle, MorphGetCoord,
  motGetCoordPos, motSetAlpha, motInitOneEnemyMdl, motGetAniWorkArea. morph.c / morph.h deleted (no FF3 code).

- 2026-10-16 `graphics/motion_model_manager.c` (verify; pinned 0x210818..0x2113d0, 20 functions): RTTI
  MotionModelManager : ModelManager. The queue's `graphics/motion/morph.c` held only its base-object constructor
  (FF2's MorphDell, a wrong match); seven vtable targets (0x2109e0, 0x210a18, 0x210b98, 0x210dc8, 0x210ec0,
  0x210f50, 0x211170) and 0x2128d8 / 0x212bc8 had no Ghidra function and were created. ModelManager's slots renamed
  in every caller; mdl_flip_yz (data 0x329400) is shared from mmanage.c.

- 2026-10-15 `ingame/plyr/plyr_mdl.c` (verify; pinned 0x239ac0..0x23c028, 38 functions): the
  player's look (ten rows 0x330a00, works 0x3f7830, spots 0x32fec0, watch prm 0x330aa0) and the
  viewfinder start / end / cancel / shutter. Five look rows (0x23a258, 0x23a468, 0x23a658, 0x23a858,
  0x23aa28) had no Ghidra function and were created. plyr_mdlBankPlay -> PlyrLookReset; FUN_0023ac68 ->
  PlyrLookMain; the 0x23b000..0x23bf98 FUN_ names -> PlyrFinder*. CharPlayerModel gained ctrl (+0x38),
  CAT_WRK mdl (+0x34) and _8c / _90 / _94, pad keys 3 / 4 / 19 / 20, LENS_FUNC_FREE_FILM 7,
  PLYR_FLG_LOOK; plyr_hold_anime is extern now. The opening event now passes EV_CAM's finder wait.

- 2026-10-15 `system/eeiop/file_cache.c` (verify; was `unassigned@00219688` + `@00219540` and the
  link-order slots movie_room.c / movie_room_menu.c): 0x219540..0x219b48 is a ref-counted file cache,
  not movie code. FILE_CACHE (global 0x32d700, 48 FILE_CACHE_ENT of 0x14: done, addr, file_no, ref,
  size) with members ClearAll / PollAll / Release / IsLoaded / UsedSize / Req / ReleaseAll and the
  entry's Clear / Load / Poll / Release; buffers from the system heap, loads FileLoadReqEE priority 6,
  total capped at 0x860000; the static init 0x219a38 (+ _GLOBAL_I_ 0x219b08 / _D_ 0x219b28, both new
  Ghidra functions) is C++ construction / destruction. Renamed in Ghidra (batch file_cache_001, four
  wrong FF2 matches replaced): MovieRoomLoadGetAddr -> FileCacheIsLoaded -- it returns the entry's
  done flag, not an address (effect_obj.c and menu_draw_tex.c callers fixed). Nothing visible uses it
  in the --run 100 smoke (the opening movie); done when a Menu_DrawTexMulti picture (album icons) or
  the kind 0x1d sets 6..8 (CH036_04_TM2) show. Remaining callers to write: album_edit.c 0x123480 /
  0x1239c8 and the game-model loader 0x1d3190 / 0x1d3238 / 0x1d35b8 / 0x1d2d08 (unassigned@001d2a40).
- 2026-10-15 `system/playpss/audiodec.c` (verify): MioPan's audiodec.c, 13 functions, same bodies in
  FF3 except audioDecEndPut drops the SShd/SSbd printfs and audioDecDelete is gone. AUTODMA_CH
  0x373b08, audio_vol_percent 0x373b0c (gp+0x618/0x61c). Nothing calls audioDecCreate with an IOP
  ring yet (MapSky's title backdrop sets iopBuff 0); the first movie with audio (M3 story movie)
  marks it done if it plays through and is heard.
- 2026-10-15 `system/playpss/ldimage.c` (done): MioPan's ldimage.c plus FF3's setLoadTexTags /
  setLoadTexTagsYX / setVIFcodes; both entry points arm the direct picture path. The title backdrop
  movie (OUTGAME_BG_PSS, TBP 0x2bc0) is on screen at --run 45 after a renderer fix (vertex alpha 0
  under a FIX add was discarded by sprite.frag). Reference frames of the movie: work/ref/movie/ (dumped
  from the decoder; tm2png has no PSS path). Open: the movie's fade-in (FIX < 0x80) is still invisible
  on PC (no blend constant), see hazards.md; in-game uses of setLoadTexTags (MapSkyData's dome movie,
  160x128 on the dome texture's TBP) not seen yet.

- 2026-10-15 `graphics/ecb.c` (was unassigned@00154cf0; verify): the 7 ECB readers written, the three
  stubs and five local prototype blocks replaced by graphics/ecb.h. Ghidra had swallowed EcbGetFileSize
  0x154d98 into EcbGetSize and both CFile ctors 0x154df0 / 0x154e30 into EcbGetData (mkfunc). The rest of
  the run (CFile ctors / dtors 0x154df0..0x154f7c) belongs to `graphics/graph3d/eeFile.cpp`, now pinned
  0x154df0..0x155d58 and not written (FF2 host file I/O, no caller on the game path; low priority).
- 2026-10-15 re-assessment (interactive): new-game path traced and measured (Wrk38e6d8IsReady hang,
  then the ItemGetCnt null crash on the first Story_Normal frame; with both patched temporarily the
  in-game loop runs on a black screen), pinned queue refilled (55 TUs, M1..M5), smoke pad extended into
  the game, "Next up" rewritten, SKILL.md priority line updated.
- 2026-10-15 graphics/effect/effect_obj.c sixth slice: lists A / B / C identified (EffKind1DInit's
  three lists are map lamps, falling particles and particle bursts, not kind 0x1d's), list B whole,
  list A front end, list C core (types / draws still stubs), the rain module; out-of-line range
  extended to 0x2b5998; 7 list C type setups mkfunc'd (0x173258 .. 0x174220).
- 2026-10-15 graphics/effect/effect_obj.c pinned (0x162318..0x1764f8 + 0x2b12a0..0x2b2d70) and its
  first slice written (partial, in-game only): candle flame requests, the many-candle altars (FF2
  effect_oth.c via MioPan), SubHalo, the thunder light, seal door burn, room glow init, kind 0x1e's
  glows, kind 0x1d front ends, EffPlyrHazeReq; CallFire2 0x162338 and EffectManyCandleLoadReq 0x163230
  mkfunc'd (hidden in neighbours); CallPartsDeform2 renamed EffBlueFireReq (a FIRE request); effdat
  and lang_effdat extracted.

- 2026-10-15 graphics/effect/effect.c defined (0x156e08..0x158ed0 + EffectInPhoto 0x2afd10) and written
  whole (59 functions, verify; in-game only): EFFECT_CONT / EFF_WRK / SCREEN_EFFECT_PARAMETER laid out,
  FF3's id -> kind map settled (effect.h table), 55 names (the SetEffects_* unpackers re-matched to
  FF3's ids; wrong FF2 matches EFFECT_MALLOC / EFFECT_FREE / EffectExecCheck / SetEffects_DITHER /
  _OVERLAP / _ENEDMG / _BLACKFILTER / _DOOR_SEAL, SetHalo, Work, global.constructors.keyed.to.SmallHit,
  sceVu0CopyVectorXYZ replaced), the per-kind draw callees named after FF2 (SetBlur, SetFocus, SetNega,
  SetDither3, SetFire, SetHaze, ...). MapEff.c / player.c call EffectRelease / EffectModelKindIsSet.

- 2026-10-15 ingame/npc/npc_act.c defined (0x21a678..0x21a8b0, 0x21a8d8..0x21d330; was unassigned@0021a8d8
  plus the act-block helpers of unassigned@00219e60 / 0021a818 and n_equip_tray.c's 0x21a768) and
  written whole (78 functions, verify): the NPC_OBJ byte-code act machine, 35 op handlers (17 were
  hidden in neighbours, mkfunc'd) and their dispatcher, the NPC data tables generated from the ROM
  (labels, motion / door lists, serif lines, talk cameras, area remap, motion faces), labels, serif
  lines, talk cameras, the player push / sight / distance checks. 73 Ghidra names (ConvertGage2StockNum,
  PlyrCameraActClockLoad / Save replaced). PLYR_CAMERA_ACT's +0..+0x1c and +0x40 are real fields now;
  n_plyr_camera.c, ev_command.c and player.c call the new names through ingame/npc/npc_act.h.
- 2026-10-15 ingame/photo/n_plyr_camera.c extended to 0x21e360..0x222170 (0x221e90 / 0x222080 /
  0x222088 are its kind-4 chat step and greet-motion hooks; 0x222170 is InitDrawFLG's) and finished:
  all 91 functions written (verify: nothing in-game is on screen; the first shot with Miku or Kei as
  an NPC marks it done). 20 act-hook names (PlyrCameraGreet* / Report* / Report1* / Chat3* / Chat4* /
  GreetMot*; FF2 FrameReset replaced). EvQueueGetNext is public now (ev_queue.h).

- 2026-10-15 ingame/photo/n_plyr_camera.c pinned 0x21e360..0x221e90 (the companion-NPC controller, not
  FF2's player camera) and its slice written (71 of 88 functions; partial: 17 act hooks 0x220bb0..0x221e8c
  left). 68 Ghidra names (PlyrCamera*); FF2 low matches FinderOut / DrawLock / DrawUnlock /
  PhotoInfoDisp / SetRemainParticle replaced. ev_command.c's NPC calls renamed; FUN_0021d498 takes the
  event's third argument (a2 pass-through, hazards.md). Nothing in-game is on screen yet.

- 2026-10-15 ingame/candle_ctrl.c defined (0x13b320..0x13dba8 + 0x2acfb8..0x2ad3e0) and written in full
  (verify, 88 functions): the HUD candle CandleControl : IEventGraph2D and its six CandleAnime* states,
  phase 48 Story_CandleMono, the CandleFurn helper. AlbumViewCtrl* renamed CandleControl* in Ghidra and
  src (GetCandleControl, CandleControlInit / Term / DataLoad / LoadRestore / Main / Draw / GetGrade /
  IsOut / SaveGet); CandleControlSetPower now takes (ctrl, power). 20 Ghidra functions created
  (vtable entries and atexit thunks Ghidra had swallowed). In-game only: nothing on screen yet; the
  first in-game screenshot in the dream world should show the candle bottom left (reference
  work/ref/CANDLETEXTURE_PK4: halo, candle, glow, flame; member = grade).
- 2026-10-15 system/b2d/B2dBase.c written (verify): all 33 functions of 0x108f78..0x109748. CB2dBase
  is now a C++ class (ctor handle -1, virtual dtor B2dBaseRelease); CGameClear / CmenuCamMain use
  new / delete, CmenuCamBase relies on the implicit base ctor / dtor, the B2dBaseDelete stub is gone.
  Five unreferenced entries got Ghidra functions: B2dSelInitTwin 0x109250, B2dPlaceInitTwin 0x109410,
  B2dPlaceMoveItemByNames 0x109500, B2dPlaceGetH 0x109678, B2dUnusedInit18 0x109728 (owner unknown).
  Nothing on the boot / title path calls it: the first screenshot of the camera menu or the game clear
  screen marks it done.

- 2026-10-14 outgame/title_menu.c done: B2dDrawSpriteByItem 0x2ab488 written (was a stub); title menu
  entries, arrows and difficulty select on screen and matching TITLE_PHF. menu_csr.c and csr_ctrl.c
  marked done on the same run (the menu cursor moves the entry). Default smoke pad reaches the menu.

- 2026-10-14 system/eeiop/stream_auto.c + snd_stream.c done: title BGM preload completes and plays
  (IOP module ported from MioPan, src/system/iop/); the title layout is on screen.

- 2026-10-14 `ingame/map/MapSky.c` defined 0x11d890..0x11e820 (the old unassigned@0011d890 / @0011dfc0 /
  @0011e3b8 runs and the MapSky* functions inferred as MapSp.c) and written with `system/playpss/pss_loop.c`
  (0x219b48..0x219e60) and `system/playpss/my_strfile.c` (all verify). Wrong FF2 names replaced:
  MapSkyRegist -> MapSkyDataProc, MapSpMoviRegistObj -> MapSkyMovieChange, IsStockMax -> PssLoopHeadRewind.

- 2026-10-14 (BpCtl.c PC bridge, Next up 0): BpCtl's draw hooks (B2dDrawSpriteDirect, Rect, Quad,
  QuadFlat, B2dNum glyphs) also draw renderer quads (BpHostTexQuad / BpHostFlatQuad, TEX0 shadowed in
  BpTex.c by BpTexGetHostTex0). The quad hooks are TRIFANs, not strips (hazards.md "Decode the primitive
  type"). Seen on screen: boot card windows with Yes/No and cursor, CAPTION_PHF's "× Next", the logos.
  BpTex.c, B2dFile.c, B2dSpr.c and MapPhf.c went from verify to done on that screenshot.
- 2026-10-14 (outgame/autoload.c session, user's request to unblock the main menu): boot memory-card
  auto load + the card library it uses; `REIPAN_AUTOPAD` scripted pad (sdk/libpad.cpp, `build.py --pad`,
  `settings.smoke_pad`); libmc answers -4 for a missing "DIR/*"; `tus.py next` takes the pinned queue
  before partial TUs.
- 2026-10-14 TU `ingame/photo/finder_view.c` (verify, 20 of 20 + the table): the viewfinder camera
  FinderViewCameraCtrl and its view turn (PLYR_WRK view_turn +0x80 / view_from +0x90, PLYR_FLG_VIEW_EASE,
  flags2 0x200000), all HUD sprite helpers, HudTexSend, and hud_sprt's 247 rows from the static initializer
  (sinit.py was cut short by GhidraMCP's 1000-instruction limit; ghidra.disasm now chunks). HudLensSlotsDraw's
  divide by hud_sprt[0xc5].h is safe now. Unseen: CameraMain (map_camera.c) and the in-game HUD are not
  reached by the smoke run (language select). Doubt: the row order of hud_sprt was not compared with the
  HUD textures yet -- the first in-game screenshot decides.
- 2026-10-14 TU `ingame/photo/finder_view_hud.c` (verify, 37 of 37): every camera HUD draw written from the
  assembly (Ghidra's float-argument mapping is wrong throughout: read f12.. / a0.. in order), incl. the four
  out-of-line helpers 0x2b9610..0x2b9cb8 (HudPartDraw, HudNumberDraw, HudFilamentCore/GlowDraw) and the
  HUD_PART ring-gauge record. 47 names (Hud*), the finder_view.c sprite helpers included (HudSprtSet*/Draw*,
  HudSprtDrawMirror4, HudGetCamType, HudIsViewOn; 0x1b76a8 HudSprtDrawCenter was inside 0x1b7588 and got a
  function); callers in finder_photo.c / fndr_effect.c / fndr_parts.c renamed. finder_view.c now defines the
  zero table hud_sprt[0xf7] and has 8 helper stubs. In-game only: nothing on screen yet.

- 2026-10-14 TU `ingame/photo/fndr_effect.c` (verify, 31 of 31): split off the inferred fly_ctrl.c (now pinned to
  0x1bf448..0x1c1418, FF2's fly code; PhotoFlyChk 0x1c1290 and the fly_wrk loop 0x1c13d0 end it). The photo mode
  work's +0x180 glow, +0x1e8 spirit charge and +0x288 pulse panel written with full layouts (types moved from
  finder_photo.h to fndr_effect.h); 31 names in Ghidra (PhotoWrk180*/1e8*/288*). New stubs: CallFocus2
  (effect_scr.c), ReqPlayerMim / StopPlayerMim (plyr_mdl.c), FUN_00239550 / 239580 / 2396b8 (unassigned@002394a8),
  11 finder_view_hud.c draws (FUN_001bb4c8 .. FUN_001bcfb8). In-game only: nothing on screen yet.

- 2026-10-14 `ingame/enemy/enemy.c` pinned to 0x192470..0x198980 and 58 of its 124 functions written
  (partial): SetCommonDat / EnemyStart / Jibaku- and AutoEnemyStart / EneRelease(no, force) /
  EneReleaseReq / enemyIsReadyData (FF2 names), per-id flag tables + save blocks, effects (ENE_EFF,
  tables from the ELF), the motion script, the enemy sound work. ENE_DAT / ENE_CMN / AENE_DAT and
  CharEnemyModel (ingame.h) laid out; wrong FF2 names GetEnePos (-> EneSePlayRand) and SetEneMahiClear
  (-> EneMotScrClear) overridden.
- 2026-10-14 `ingame/enemy/ene_main.c` defined (0x186a88..0x189b90, carved out of the inferred
  effect_oth.c) and written whole (57 functions, verify: in-game only): the enemy main loop, ENE_WRK moved
  out of effect_oth.h (was EFF_SLOT_WRK / eff_slot_wrk) with ENE_DAT / ENE_CMN; EnemyActOne split off
  EnemyWrkOne in Ghidra; effect_rain.c (one stub) deleted.
- 2026-10-14 unassigned@0013e648 (206 functions) split by the TU-end markers (architecture.md "TU ends"):
  cmn_fade_layer.c takes its static-init tail (to 0x13e6a8), `common/cmn_caption.c` 0x13e6a8..0x13efa0
  (written whole, done: a temporary probe in LangSelDispMain drew caption line 1 exactly as
  CAPTION_PK4's pages show; progress shot is that probe), `ingame/cat/cat_ctrl.c` 0x13efa0..0x1441d8
  and `ingame/char_model.c` 0x1441d8..0x1479b0 pinned for later sessions. Ghidra: 0x13e668, 0x13e688,
  0x13e6a8 created (swallowed by FUN_0013e648), batch cmn_caption_001.
- 2026-10-14 the inferred `ingame/menu/menu_map.c` run (nothing of FF2's menu map in it: the FF2
  matches were tiny-function collisions) split into `ingame/menu/menu_draw_tex.c` (0x209610..0x20b148,
  also taking the Menu_DrawTex part of unassigned@002093c8 / @00209770) and
  `ingame/menu/menu_csr_obj.c` (0x20b148..0x20b4d0); both written whole, verify. menu_map.h is gone:
  MENU_MAP_PIC is `MENU_PIC` (fields scale / z / pri / u / v / enable named) and MENU_MAP_LOAD the RTTI
  class `Menu_DrawTex`, callers in album_load_scr.c, album_save_scr.c, anicode.c, anim_2d.c and
  title_menu.c updated. Ghidra: 9 hidden functions created (0x209cc0, 0x209d40, 0x20a770, 0x20a988,
  0x20a9b0, 0x20aaf0, 0x20abc0, 0x20ac08, 0x20ace8), batches menu_draw_tex_001/002 (63 names; ten wrong
  FF2 matches replaced). New stubs: MovieRoomLoadReq / Release / GetAddr (unassigned 0x2195c0..0x219798,
  movie room), so Menu_DrawTexMulti pictures (MenuPicCtor multi = 1: album icons) load nothing on PC
  until that run is written; Menu_DrawTex pictures (multi = 0: MsgView, ItemGet, CTutorial) load for real.

- 2026-10-14 `system/b2d/ScrollBar.c` written (32: ScrollBar, B2d_ScrollBar3, B2d_ScrollBar1 as C++
  classes with FF3's vtable order, the 11 inline virtuals 0x2acae0.. pinned to it as satellites) and
  `system/b2d/BpObj.c` finished (BpObjAdd/Del/DrawScrollBar), both verify. MenuScrollArrowDisp 0x2090a8
  (was FF2's MenuFileMapSmallTexDisp, wrong match) written in menu_file.c; its swallowed neighbour
  0x209240 made a function. The GPhaseCtrl atexit thunk 0x135e08 moved to savepoint_menu.c's range.
  Batch scrollbar_001 (33 names). BP_OBJ_SCROLL_POS is {first, cursor, rows, page}; the arrows argument
  is BP_OBJ_SCROLL_ARROWS {up, down, up_fx, down_fx}.

- 2026-10-14 out of the inferred album/prg/album_view.c run: `outgame/save_load_scr.c` (41, the phase 68
  memory-card load screen, verify), `outgame/save_info_disp.c` (12, save / album slot info printers,
  verify), `system/b2d/B2dGridSpr.c` (7, grid-placed layout item, verify), `system/b2d/BpObj.c` (16,
  partial: 13 written, the scroll bar three are stubs), `system/b2d/ScrollBar.c` defined (21, not
  written). Ghidra functions created: 0x130788 B2dGridSprCtor2, 0x131378 SaveLoadScrDrawNone, 0x131b20
  SaveLoadScrDrawClear. Wrong FF2 matches replaced: AlbumViewPhotoProtectionFrameDisp ->
  BpObjDrawScrollBar, AlbumViewLeftThumbnailDisp -> SaveInfoPrintDifficulty,
  AlbumViewRightThumbnailDisp -> SaveInfoPrintInfo1D. 73 names (batch save_load_scr_001). BP_OBJ moved
  from album_view.h to system/b2d/BpObj.h.

- 2026-10-14 unassigned@0024f548 split into four FF3-only files, all reconstructed (verify; nothing on
  the boot path reaches them): `ingame/event/ev_queue.c` (17, the event result queue with its ROM
  lists), `outgame/outgame_bg.c` (20, the outgame backdrop), `ingame/story_save_mc.c` (phase 69, 3 +
  the atexit thunk) and `ingame/story_save.c` (phase 41, 6); 0x250d40 pinned into scene.c
  (0x250d40..0x253620). Ghidra functions created: 0x24f670, 0x24f750, 0x24f868, 0x24f948, 0x24fa08.
  Wrong FF2 match replaced: SavePointTopTitleDisp -> EvQueueStart; Wrk3f7c90* -> EvQueueInit/Reset/
  End. 53 names (batch outgame_bg_001: BpObj*, SaveScr*, SavePointMenu*, McSaveGame*, TitleObjFadeIn/
  Out, MenuBgIsInEnd/Stop, ManmdlAdvanceState, FinderCamFlagSet). Phases 41 and 69 wired in gphase.c.
- 2026-10-14 TU `ingame/menu/menu_csr.c` (44 functions) + `common/csr_ctrl.c` (31, defined 0x1edd90..
  0x1eefe8), verify: every MenuCsr* / MenuCsrWork* wrapper over the FF3-only grid cursor engine
  (CSR_ITEM / CSR_VIEW). Ghidra functions created at 0x153818, 0x153890, 0x1538b8 (merged wrappers).
  Wrong FF2 matches replaced: KaiPzlClear -> CsrSetId, KaiPzlDrawMsg -> CsrSetXY,
  Anim2D_CalcAlphaAnim -> MenuCsrWorkInitNoFilter. Engine checked against the ROM on mipsemu:
  400 random grids / configs / pad sequences, 56018 commands, all states equal. MENU_CSR_FILTER is
  (tbl, w, h); TitleNewGameCsrFilter updated. Shows on screen once the title menu (phase 17) or the
  frame-rate select (phase 18) is reached.
- 2026-10-14 TU `ingame/plyr/unit_ctl.c` (26 of 27 functions, verify; the atexit thunk 0x25fcc0 is not written): FF2 unit_ctl helpers (UnitGetTrgtRot,
  GetTrgtRotY, RotRngChk, OutSightChk, ConvertRot2Dir) + GetMidPos / GetRndRingPos / GetLength2D /
  IsPosInView, the stick readers (GetStickRaw was the wrong FF2 match TitleMenuMovePhase; GetStickRot /
  GetStickDir8 / GetDirTrg), phases 6 / 18 with three E_SETTINGS_PHF hooks, FinderBankSaveGet. Ghidra
  functions created: 0x25edc0, 0x25fae8, 0x25fbe0, 0x25fcb8, 0x25fce0 (0x209518 already existed).
  Callers fixed: OutSightChk's argument order in player.c (the stub prototype had the floats first),
  PlyrLadderCtrl's ConvertRot2Dir(angle, 0) (was a one-argument guess). MenuItemUse 0x209470 renamed
  B2dHookPrintNumber (wrong FF2 match).
- 2026-10-14 TU `album/prg/album_save.c` (46 functions, verify): redefined to 0x126f68..0x128a58 (FF2
  order CtrlInit .. DispInit); the old inferred range also held album_mem.c (defined 0x128e30..0x128f38,
  stubs), scr_play.c (now starts at 0x128f38: 0x128ed0 was AlbumMemFree), the player gauge (moved to the
  new ingame/plyr/plyr_gauge.c with its stubs, type ALBUM_SAVE_DISP renamed PLYR_GAUGE) and the album page
  layout run (left undefined, above). Ported from MioPan's FF2 album_save.c; FF3 differences in the file
  banner. Names: AlbumSaveCtrlInit / TexLoadWait / ModeMain (was the wrong AlbumSaveOutReq) / SlotSelPad /
  McAlbumLoadInit / McAlbumLoadWait / McErrorConfInit / McErrorConfPad / McEndConf / End / DispInit,
  AlbumMem*, MemoryCardCheckEveryFrameInit 0x2001d8, the gates AlbumSlotSelWinIsReady / AlbumSelIsReady /
  AlbumConfWinIsReady (0x12fdc0 was a wrong SystemBankPlay), SpriteCmn's AlbumSlotInfo* / AlbumCurInfoStamp,
  PlyrGaugeInit (was a wrong AlbumSaveDispInit). Not reachable: phase 27's glue is undefined. Open: the
  message ids are FF2's with FF2's texts (FF3's msg file not checked; 0x2d is FF3-only).

- 2026-10-14 TU `common/vu0math.c` (19 functions, verify): the old unassigned@001ef370, a new FF3-only
  file (no FF2 matches, no strings; shares the sdata run 0x370e58..0x370eb4 and calls only itself).
  Ghidra had hidden HermiteVector 0x1ef918 and BSplineVector 0x1efa08 inside IntLogBase (mkfunc done).
  Names: Vu0AtanPoly, Vu0Acosf, LogBasef, IntLogBase, HermiteVector, BSplineVector, GetTrgtRot (FF2,
  replacing the matcher's GetTrgtRot on 0x263a10, now RefPtrNewObj20), logf 0x294800. Checked offline
  against libm (sin/cos within 4e-6, atan/atan2/acos within 5e-7). Open: whether the name vu0math.c
  should be something else (no FF3 evidence for a file name); other TUs still declare these functions
  locally, they can include common/vu0math.h when touched.

- 2026-10-14 TU `ingame/plyr/player.c` (partial, third session): the remaining PlayerMain handlers (death
  scene mode 7 PlyrDeathCtrl, mode 8, mode 10 start/run/end, PlyrActionCtrl, PlyrAnimeCtrl and its four
  anime choosers with the tables 0x360c08 / 0x360fa8), the viewfinder enemy check and FrameInsideChk,
  the view / distance rates 0x235050..0x235548 (PlyrViewDistCheck 0x235458 created in Ghidra), HP restore,
  map position update and map-object test, small accessors (FF2 matches InFinderMode / PlayerModeIsFinder /
  InDamageState replaced). EFF_WRK eff_wrk (0x2f6a10) in effect.h; EFF_SLOT_WRK _401 / fdr_dist / dead_eff /
  tpos; PLYR_WRK rot / _108 / mh_no / anime_step / death_step / _320 / _324 / mode10_time; new MhCtl.h,
  ev_open.h. 52 Ghidra names (work/batches/player_003.json).

- 2026-10-14 TU `ingame/plyr/player.c` (partial, second session): pinned to 0x231628..0x2394a8 (the
  inferred head 0x2295c8.. is other code, see Boundaries). PlyrDamageCtrl (holds, hits, escape windows),
  the init functions, the mode 0 / camera-up / door / mode 11 handlers, the flash (count, input, release,
  enemy reactions), SetPlyrAnime with its three anime tables (checked against the ROM for all 65536 input
  pairs with the new tools/reverse/mipsemu.py), GetPlyrAreaNo, PlyrDeathStart. EFF_SLOT_WRK gained dat /
  hold_pow / kind / hit_dmg / _528, ENE_DAT; PAD_KEY_06/07; PLYR_WRK the escape / anime / equipment fields.
  Callers renamed: FUN_002371e8 -> PlyrFlashAble, FUN_002374a0 -> PlyrFlashIsReady, FUN_00237840 ->
  PlyrGetEquipLv (finder_photo.c, fndr_parts.c). Not visible yet (in-game only).

- 2026-10-14 TU `ingame/photo/fndr_parts.c` (partial, 72 of 124 functions + 2 hidden): the camera HUD
  parts out of unassigned@001c2d80, FF2 photo widgets rewritten (see architecture.md). Sub-work types
  moved from finder_photo.h to fndr_parts.h with full layouts (PHOTO_WRK_220 -> PHOTO_CHARGER,
  PHOTO_WRK_244 -> FILAMENT_WRK, LENS_WRK, PHOTO_ENE_MARK.alpha, HUD_HP_WRK disp/now/tail); EFF_SLOT_WRK
  gained mark_no / flags / flags2 / scr_x / scr_y / mark_x / mark_y / id / dmg_disp; PAD_KEY_LENS_NEXT /
  PREV; 13 finder_view_hud.c draw prototypes. Ghidra: FilmBlinkOn / FilmBlinkOff created (swallowed by
  FilmWork); 0x2ba2b0 was a wrong FF2 match (_Vu0CalcColorDataLambert) -> CPulseVariable_50_Req.

- 2026-10-14 TU `ingame/photo/finder_photo.c` (54 functions, verify): the photo mode work 0x3e7918 out of
  unassigned@001b1648, with `finder_view.c` / `finder_view_hud.c` pinned behind it (object ends found from the
  static-initializer pairs 0x1b6ec8/0x1b6f00 and 0x1b7998/0x1bb4a8). PHOTO_MODE_WRK laid out in finder_photo.h
  (HUD_HP_WRK moved there from anim_2d.c; FILM_WRK / LENS_WRK completed); new minimal headers album_save.h,
  fly_ctrl.h, finder_view.h, finder_view_hud.h; PLYR_WRK gained hp / hp_max / finder_x / finder_y / _250,
  EFF_SLOT_WRK dist / sub590, PAD_KEY_18 / ZOOM_IN / ZOOM_OUT. Shows on screen only in game (camera HUD).
- 2026-10-13 TUs `system/eeiop/stream_auto.c` (40 functions + 2 hidden), `snd_bank.c` (22 + 3 hidden)
  and `snd_3d.c` (15 + 1 hidden), the old unassigned@0026c8c0: FF2's stream_auto / snd_bank / snd_3d
  ported onto FF3 (verify: no audio path yet). Old names replaced: SndStrm* -> StreamAuto*,
  SndStreamPauseClear -> StreamAutoAllStop; SND_LISTENER -> SND_3D_SET (snd_3d.h). Ghidra: created
  cmpStreamQueue, StreamAutoNowPlayPercentage, intr_LoadCancelBDFunc, intrSndBankFileBD/HXD,
  Snd3DSetCalcFunc; named the libvu0 helpers 0x282d58..0x282f90 and six snd_stream.c functions.

- 2026-10-13 TUs `system/eeiop/cddat.c` (12 functions, done) and `system/eeiop/snd.c` (14, verify), from
  the run unassigned@0026f498: defined 0x26f340..0x26f770 and 0x26f770..0x26fd50 and ported MioPan's
  cddat.c / snd.c onto FF3 (one FHD image with a 0x30-byte header instead of FF2's four arrays; FF3's
  table checks and host-size branch; snd.c without FF2's checks and prints). The load bridge lost the
  file table: its ee_iopInit now reads ZERO3.FHD like FF3 and calls cddatInit. Created Ghidra functions
  GetFileExtLabel / GetFileNum (inside cddatInit's body) and GetFileNameBuffer (inside GetFileName);
  replaced the matcher's SndStreamStartHeaderOnMemory / SndStreamPrintStatus with SndStreamInit /
  SndStreamMain; named 37 functions (cddat, snd, spu_mem/spu_voice, snd_3d, stream_auto, snd_util
  init/main). EEIOP_DEF +0x3c/+0x40 are the SPU RAM range. fhd.py reads table [1] as bytes now.

- 2026-10-13 TU `system/eeiop/fileload.c` (26 functions, done): defined 0x268e70..0x269fa0 and ported
  MioPan's fileload.c (FF3: no prints/asserts, 7-word load command without file name, `use_cd_image`
  flag, raw sizes from GetFileSize). Created Ghidra functions swallowed by neighbours:
  FileLoadCancelWait2/FileLoadCancelWait (in FileLoadCancelWaitSub), cmpFileWrkPri/Sector (in
  FileLoadCancelAll), intr_SifEnd_FileLoad (in FileLoadReq), FileLoadChangeThreadPriority (in
  thFileLoad). PC: requests go to ReiPan_FileLoadServe on the loader thread (MioPan's choice), the
  SIF bind no longer waits for an IOP file service, and the EE main thread got a real wakeup counter
  (eekernel.cpp) so FileLoadCancelAll / the *Wait calls actually sleep until the loader wakes them.
  The loader's file-table lookups and ee_iopInit's loader bring-up were bridges in load_bridge.c (gone
  since cddat.c and ee_iop.c, 2026-10-13).

- 2026-10-13 TU `ingame/sound/snd_ctrl.c` (38 functions, done): the claimed `ingame/plyr/sis_algo.c` was
  the FF3-only sound API; defined 0x257ae0..0x258a68 (absorbing the sis_mdl.c slot and two unassigned
  runs) and removed the sis_algo/sis_mdl placeholders. Renamed wrong FF2 matches (EneDistCount ->
  SndSeBankReleaseAll, SaAnimEnd -> SndSeBankIsEntry, SaRndJump -> SndSeBankRelease, SetupSisMdl ->
  SndSeAutoPlay, sis_mdlBankPlay -> SndSeIsLoop, ReqSisterMim -> SndBgmStop, InitSisterTracePos ->
  SndBgmVolFade, SetIRQCore -> snd_utilAutoRelease) and named 31 library callees from FF2's snd*.c;
  created 0x2583b0 SndSeSetPosition (hidden in 0x258398). Six files declared their own
  `void SndSePlay(...)` (an overload bound to an old stub); they include snd_ctrl.h now.

- 2026-10-13 TU `graphics/graph2d/message.c` (43 functions, verify): defined 0x20c400..0x20df28 (the
  inference had 0x20b530..0x20dac0 with StoryMenu menu code; the page helpers and GetMsgDataAddr were
  in unassigned@0020dac0, LangDataLoadReq / SetCurColorRGB in outgame/mission_ctl.c, whose files and
  queue entry went). Ported from MioPan's message.c. Renamed wrong FF2 matches: MesPassCheckInit ->
  MsgInit, MesPassCheck -> MsgReset, MesSetBeforePage -> MsgExeCtrlInit, FontDispSample -> MesBankExe,
  DebugMsgCtrlInit -> MsgSetColorRGB, MisSetNextFunc -> MsgSetSubtractMode; mime.h merged into
  message.h; Ghidra function GetAccessMcSlot_Msg created at 0x20da10 (was inside MesExNumExe).
- 2026-10-13 TU `graphics/graph2d/font.c` (3 functions, verify): the old unassigned@001c7c18 run.
  0x1c7c38..0x1c7cb8 is FF3-only: FontTexLoadReq (was SysDataLoadReq) loads FONT_TEX_PK4 to EE
  0x104d000 and FONT_EX_TEX_PK4 to 0x10be000; FontTexSendVram (was Draw104d000) / FontExTexSendVram
  (was FUN_001c7c90) PK4SendVram them. Both packs load in the smoke run; nothing on the boot path
  draws text yet (AutoLoadScr* are stubs). 0x1c7c10/0x1c7c18 (Fod_static_init / Fod_GLOBAL_I, split
  with mkfunc) went to fod.c, pinned 0x1c67c0..0x1c7c38 (its start not checked).

- 2026-10-13 TU `graphics/graph2d/g2d_debug.c` (9 functions, verify): the old unassigned@001c83b0 run,
  pinned 0x1c83d0..0x1c8e28 (FF2's g2d_debug.o slot; FF3-only helpers). Ghidra had hidden 0x1c8388,
  0x1c8560, 0x1c8730, 0x1c8b78 inside neighbours (mkfunc done). DispSqrRect's stub is gone; nothing on
  the boot path calls it yet (pre_Title_Mode, effects, movie, anicode do), so it waits for the logos.
  Vu0Atanf (0x1ef620) named; it and Vu0Sinf/Vu0Cosf stay in unassigned@001ef370.

- 2026-10-13 TU `graphics/graph2d/tim2.c` (12 functions, verify) redefined to 0x25c368..0x25cc20 and
  ported from MioPan; `common/pk4.c` (6, PK2/PK4 accessors) defined 0x1eff18..0x1f0138 and written
  (0x1effc8 split out with mkfunc); g2d_draw.c's SetDmaDirectImageTrans 0x1cadf0 written. The old
  inferred range 0x25a758..0x25c368 is FF3-only: pinned as `ingame/menu/menu_cassette.c` (to 0x25bae0)
  and `ingame/event/ev_ambient_se.c` (names guessed; the latter is `ingame/event/telephone.c` since
  2026-10-19); their stubs moved there. Wrong FF2 matches
  renamed (Tim2IsClut2, Tim2LoadPicture/Image/Clut, Tim2CalcBufWidth in that range).

- 2026-10-13 TU `system/b2d/BpTex.c` (13 functions, verify) defined 0x1089e8..0x108f78 from the head of
  unassigned@00108ef8 (B2dBase.c and BgSe.c pinned from the same run); names BpTex*, MakeClutDirect,
  sprintf; Tim2GetFirstPicture written in tim2.c (its stub returned null and crashed the smoke run).

- 2026-10-13 TU `outgame/lang_sel.c` (phase 2, 13 functions, verify) defined 0x1ecdd8..0x1ed658 and
  ported from FF2; phase 2 wired into gphase.c; DISP_SPRT/SPRT_DAT in g2d_draw.h; g2d_draw.c's
  StartDmaDirectTrans/EndDmaDirectTrans written; wrong FF2 matches renamed (DispTitleCursorL ->
  PK4SendVram, KaiPuzzleBlackBgDisp -> LangSelNationalFlagDisp, two in 0x1f7050..).

- 2026-10-13 TUs `system/b2d/B2dInp.c` (11 functions, done: its visible effect, B2dInpUpdate, is never
  called in retail) and `system/b2d/B2dSpr.c` (22 functions, verify) defined from unassigned@00105750 and
  reconstructed; functions 0x105e38, 0x105ef8 (inside 0x105da0) and 0x105990 created; newlib sin/cos named.

- 2026-10-13 TU `graphics/motion/anicode.c` done: menu window (type 8), pause menu (type 4), slot select
  (type 7), Ccm destructors, satellite MsgGetPicKey; boundary moved to 0x1502a0 (HUD code -> anim_2d.c).

- 2026-10-12 `78d528a` symbol-recovery tools and first naming pass (2,280 FF2 matches, 419 context names).
- 2026-10-12 `5a682f3` platform layer brought up; builds and runs (window, D3D12, shaders).
- 2026-10-12 skill `reipan-port` + automation scripts (stage, stubgen, callcheck, build, status, refresh).
- 2026-10-12 TU `system/os/system.c` reconstructed (32 functions); `main/main.c` extent pinned; 30 names.
- 2026-10-12 TUs `graphics/gs_reg.c` (GS register cache, screen capture, 9 functions) and
  `system/os/sys_heap.c` (4) reconstructed; FF3 memory/VRAM map recorded; 13 names.
- 2026-10-12 types are recreated as rediscovered, with size/offset asserts proven by MinGW GCC and MSVC
  (`layoutcheck.py`, run by `tus.py check`, required by `finish`); GRA3DCAMERA (FF3 0x220) settled.
- 2026-10-12 `48c8966` `main/main.c` (GameSystemStart/Frame/End, super phase); stubgen `(unknown)` path fixed.
- 2026-10-12 `ded0995` `main/gphase.c` + `main/gphase_tree.c`: the phase engine runs on PC; phase-function
  types settled (int(void) init/one/end, void(void) pre/after); tree table filled from ROM.
- 2026-10-12 `9ea7bf4`..`42058e7` `ingame/ingame_wrk.c` + `ingame/ingame.c` (split from the gallery
  inference): game work, play clock, phase 11, phase 12, IngameInit/Main/End/DrawSub, model resets;
  `804e7d9` the anime-code classes and `album/prg/scr_play.c` defined; callcheck `// callcheck:` comments.
- 2026-10-12 `c4ca82b` `common/cmn_fade_layer.c` (GetCmnFadeLayer no longer a null stub); `.vs/` ignored.
- 2026-10-12 `c48ebbf` `outgame/option.c` (option work, phase 7).
- 2026-10-12 `d40da78` `system/pad/pad_ctrl.c`, `outgame/pad_check.c` (phase 1), `ingame/pause/prg/pause.c`
  (phase 44); boot now runs phase 1.
- 2026-10-12 skill references caught up with the autopilot sessions' handoff notes (their writes to
  `.claude/` were denied).
- 2026-10-13 `9ed55af`..`7b44766` `ingame/menu/anim_2d.c` (layout animations, HUD, CTutorial); `bff0140`
  `system/b2d/BpCtl.c`; `fc93e0c` `outgame/boot.c` (phases 3, 13, 14, 4); `63e6bba`..`5034046`
  `system/b2d/B2dCtl.c`; `bbeca51` `system/pad/pad.c` + heapctrl / my_malloc / cpp_heap; `3d5c4b7`
  `graphics/motion/anicode.c` (partial: message viewer).
- 2026-10-13 `4c1cf8d` `fhd.py` + `cd_file_dat.h` (7,724 file numbers), `rtti.py` (113 classes), stand-in
  classes renamed to RTTI names, 2D queue pinned, `tus.py next` skips stale queue keys; references
  caught up again.
- 2026-10-13 smoke-run screenshots (`work/shots/`, renderer `REIPAN_SHOT_DIR/_EVERY`); data paths accept
  ISO `;1` names and quoted `data_folder` values; the data root and archive are logged at startup.
- 2026-10-13 FF2 load bridge (`src/lib/load_bridge.c`, `REIPAN_BRIDGE`) + `system/os/eecdvd.c` (4 FF3
  functions): files load from IMG_BD.BIN; boot passes phase 1 (libpad shim: scePadInfoPressMode now says
  pressure mode is supported) and reaches phase 2.
- 2026-10-13 unassigned@00270268 defined as five FF2 files and ported: `system/eeiop/cmp_eeiop.c`
  (done, PC decode path off), `snd_util.c`, `file_stream.c`, `spu_mem.c` (replaces snd_bridge.c),
  `spu_voice.c` (verify: nothing audible yet). Six functions created in Ghidra (decode thread, release
  predicates, GetVacantAutoBDWrk, PrintBufferState, InitSPUVoiceCore).

- 2026-10-14 TU `save_load/prg/game_data_load.c` (verify, 24 of 24; was unassigned@001d4678): the FF3-only
  memory-card load of phase 68 (McLoadGameInit/Main/End + 21 McLoadGameMc* steps), MC_LOAD_WRK moved to
  game_data_load.h with every field named (snap/chapter textures and file numbers, timers);
  game_data_save.c pinned 0x1d5b40..0x1d7a58 and its McSaveGameInit/CtrlInit/Main written. Names:
  MapSnapGetNo 0x1f6e70 (kaza_pzl.c run, really a map snapshot table lookup), EventAfterLoad 0x19b9e8.
  Doubts: the message ids' texts were not read from the message table (names come from where they are
  set); MC_LOAD_WRK.slot_flg -1 is never set by the load steps (only kept).
