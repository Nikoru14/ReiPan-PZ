# ReiPan status (generated)

Generated 2026-10-22 08:32 at commit `44bcf94` by `tools/reverse/status.py` (`refresh.py` runs it). Do not edit by hand.

## Naming (Ghidra, FF3)

- functions: **11261**, named **10115** (90%)
- by provenance: ff2-match 1064, ff2-match-low 509, ff2-context 1451, ff3-inferred 7082
- context-name ledger: 8533 entries; overrides of wrong FF2 matches: 708

Least-named game-code regions (64 KB blocks, library ranges excluded):

| block | named | inferred files |
|---|---|---|
| `00260000` | 243/375 (65%) | utility.c, stream_auto.c, fileload.c |
| `002a0000` | 188/223 (84%) | char_model_sub.c, candle_ctrl.c, eeFindFile.cpp |
| `002b0000` | 193/207 (93%) | (unknown), effect_obj.c, fndr_parts.c |
| `00130000` | 400/414 (97%) | candle_ctrl.c, save_scr.c, save_load_scr.c |
| `00230000` | 269/277 (97%) | player.c, plyr_mdl.c, plyr_sub.c |
| `00140000` | 449/459 (98%) | cat_ctrl.c, anicode.c, char_model.c |
| `001f0000` | 421/427 (99%) | MapSnap.c, map_camera.c, story_door.c |
| `00170000` | 218/220 (99%) | effect_oth.c, effect_obj.c, effect_scrsaver.c |

## Game phases

- 120 phases, 438/438 phase functions named (table: `tools/reverse/data/phases.json`, map: `references/architecture.md`)
- phases with unnamed functions: 

## Reconstruction (src/)

- reconstructed FF3 functions: **9332**; stubs: **0**

| file | reconstructed | stubs |
|---|---|---|
| `src/album/prg/album.c` | 34 | 0 |
| `src/album/prg/album_disp.c` | 24 | 0 |
| `src/album/prg/album_edit.c` | 61 | 0 |
| `src/album/prg/album_load.c` | 28 | 0 |
| `src/album/prg/album_load_scr.c` | 34 | 0 |
| `src/album/prg/album_mem.c` | 5 | 0 |
| `src/album/prg/album_phase.c` | 9 | 0 |
| `src/album/prg/album_save.c` | 46 | 0 |
| `src/album/prg/album_save_scr.c` | 34 | 0 |
| `src/album/prg/album_view.c` | 78 | 0 |
| `src/album/prg/album_zoom.c` | 17 | 0 |
| `src/album/prg/album_zoom_view.c` | 26 | 0 |
| `src/album/prg/scr_play.c` | 7 | 0 |
| `src/common/Load.c` | 7 | 0 |
| `src/common/ProcCtl.c` | 3 | 0 |
| `src/common/SingleLinkList.c` | 10 | 0 |
| `src/common/WindowMsg.c` | 18 | 0 |
| `src/common/cmn_caption.c` | 6 | 0 |
| `src/common/cmn_fade_layer.c` | 28 | 0 |
| `src/common/cmn_time.c` | 6 | 0 |
| `src/common/cmn_util.c` | 39 | 0 |
| `src/common/cpp_heap.c` | 6 | 0 |
| `src/common/csr_ctrl.c` | 31 | 0 |
| `src/common/get_byte.c` | 3 | 0 |
| `src/common/heapctrl.c` | 6 | 0 |
| `src/common/memShelt.c` | 11 | 0 |
| `src/common/my_malloc.c` | 5 | 0 |
| `src/common/pk4.c` | 6 | 0 |
| `src/common/spline.c` | 4 | 0 |
| `src/common/utility2.c` | 18 | 0 |
| `src/common/vu0math.c` | 19 | 0 |
| `src/graphics/ecb.c` | 7 | 0 |
| `src/graphics/effect/effect.c` | 59 | 0 |
| `src/graphics/effect/effect_butterfly.c` | 32 | 0 |
| `src/graphics/effect/effect_data.c` | 12 | 0 |
| `src/graphics/effect/effect_ene.c` | 133 | 0 |
| `src/graphics/effect/effect_mapsky.c` | 10 | 0 |
| `src/graphics/effect/effect_obj.c` | 330 | 0 |
| `src/graphics/effect/effect_oth.c` | 165 | 0 |
| `src/graphics/effect/effect_scrsaver.c` | 17 | 0 |
| `src/graphics/effect/effect_sub.c` | 43 | 0 |
| `src/graphics/effect/effect_torch.c` | 33 | 0 |
| `src/graphics/game_anime_model.c` | 16 | 0 |
| `src/graphics/game_model.c` | 40 | 0 |
| `src/graphics/graph2d/fade.c` | 6 | 0 |
| `src/graphics/graph2d/font.c` | 3 | 0 |
| `src/graphics/graph2d/g2d_debug.c` | 8 | 0 |
| `src/graphics/graph2d/g2d_draw.c` | 17 | 0 |
| `src/graphics/graph2d/message.c` | 43 | 0 |
| `src/graphics/graph2d/tim2.c` | 13 | 0 |
| `src/graphics/graph3d/CDebugEdit.cpp` | 28 | 0 |
| `src/graphics/graph3d/eeFile.cpp` | 30 | 0 |
| `src/graphics/graph3d/eeFindFile.cpp` | 38 | 0 |
| `src/graphics/graph3d/g3dCamera.c` | 6 | 0 |
| `src/graphics/graph3d/g3dCore.c` | 31 | 0 |
| `src/graphics/graph3d/g3dDma.c` | 9 | 0 |
| `src/graphics/graph3d/g3dGeom.c` | 19 | 0 |
| `src/graphics/graph3d/g3dLight.c` | 5 | 0 |
| `src/graphics/graph3d/g3dLightEx.c` | 14 | 0 |
| `src/graphics/graph3d/g3dRenderTarget.cpp` | 11 | 0 |
| `src/graphics/graph3d/g3dSprite.cpp` | 3 | 0 |
| `src/graphics/graph3d/g3dVif1.c` | 5 | 0 |
| `src/graphics/graph3d/gra3d.c` | 129 | 0 |
| `src/graphics/graph3d/gra3dBoundingBox.c` | 7 | 0 |
| `src/graphics/graph3d/gra3dConst.c` | 2 | 0 |
| `src/graphics/graph3d/gra3dDma.c` | 6 | 0 |
| `src/graphics/graph3d/gra3dMirror.c` | 16 | 0 |
| `src/graphics/graph3d/gra3dMisc.c` | 13 | 0 |
| `src/graphics/graph3d/gra3dSGD.c` | 70 | 0 |
| `src/graphics/graph3d/gra3dSGDData.c` | 24 | 0 |
| `src/graphics/graph3d/gra3dShadow.c` | 34 | 0 |
| `src/graphics/graph3d/gra3dTRI2.c` | 5 | 0 |
| `src/graphics/graph3d/gra3dVu0.c` | 4 | 0 |
| `src/graphics/gs_reg.c` | 9 | 0 |
| `src/graphics/mmanage.c` | 54 | 0 |
| `src/graphics/model_builder.c` | 31 | 0 |
| `src/graphics/model_object.c` | 35 | 0 |
| `src/graphics/motion/anicode.c` | 118 | 0 |
| `src/graphics/motion/motion.c` | 144 | 0 |
| `src/graphics/motion_model_manager.c` | 17 | 0 |
| `src/graphics/movie/movie.c` | 17 | 0 |
| `src/graphics/movie/movie_title.c` | 9 | 0 |
| `src/graphics/movie/movie_title_dat.c` | 3 | 0 |
| `src/graphics/movie/story_movie.c` | 10 | 0 |
| `src/graphics/scene/IngameScene.c` | 37 | 0 |
| `src/graphics/scene/fod.c` | 20 | 0 |
| `src/graphics/scene/scene.c` | 55 | 0 |
| `src/graphics/scene/scene_dat.c` | 17 | 0 |
| `src/graphics/scene/scene_effect.c` | 82 | 0 |
| `src/ingame/camera/camVci.c` | 8 | 0 |
| `src/ingame/camera/cam_rail.c` | 40 | 0 |
| `src/ingame/camera/dmg_cam_shot.c` | 7 | 0 |
| `src/ingame/camera/event_camera.c` | 42 | 0 |
| `src/ingame/camera/map_camera.c` | 46 | 0 |
| `src/ingame/candle_ctrl.c` | 71 | 0 |
| `src/ingame/cat/cat_ctrl.c` | 155 | 0 |
| `src/ingame/char_model.c` | 74 | 0 |
| `src/ingame/char_model_sub.c` | 95 | 0 |
| `src/ingame/cine_film.c` | 44 | 0 |
| `src/ingame/cine_menu.c` | 41 | 0 |
| `src/ingame/clear/ClearMenu.c` | 8 | 0 |
| `src/ingame/clear/GameClear.c` | 33 | 0 |
| `src/ingame/enemy/ene_main.c` | 70 | 0 |
| `src/ingame/enemy/ene_spe_obj.c` | 19 | 0 |
| `src/ingame/enemy/enemy.c` | 101 | 0 |
| `src/ingame/enemy/enemy_act.c` | 164 | 0 |
| `src/ingame/enemy/fly_ctrl.c` | 32 | 0 |
| `src/ingame/enemy/random_ghost.c` | 37 | 0 |
| `src/ingame/event/ev_queue.c` | 19 | 0 |
| `src/ingame/event/prg/ev_change.c` | 50 | 0 |
| `src/ingame/event/prg/ev_chapter.c` | 22 | 0 |
| `src/ingame/event/prg/ev_command.c` | 324 | 0 |
| `src/ingame/event/prg/ev_exe.c` | 16 | 0 |
| `src/ingame/event/prg/ev_get.c` | 8 | 0 |
| `src/ingame/event/prg/ev_ladder.c` | 9 | 0 |
| `src/ingame/event/prg/ev_macro.c` | 5 | 0 |
| `src/ingame/event/prg/ev_main.c` | 12 | 0 |
| `src/ingame/event/prg/ev_open.c` | 29 | 0 |
| `src/ingame/event/prg/ev_se.c` | 10 | 0 |
| `src/ingame/event/prg/ev_timer.c` | 10 | 0 |
| `src/ingame/event/tel_talk.c` | 15 | 0 |
| `src/ingame/event/telephone.c` | 28 | 0 |
| `src/ingame/gameover/prg/gameover.c` | 10 | 0 |
| `src/ingame/gameover_menu.c` | 13 | 0 |
| `src/ingame/ingame.c` | 37 | 0 |
| `src/ingame/ingame_effect.c` | 60 | 0 |
| `src/ingame/ingame_wrk.c` | 42 | 0 |
| `src/ingame/item/prg/item.c` | 26 | 0 |
| `src/ingame/item/prg/item_tbl.c` | 2 | 0 |
| `src/ingame/item/prg/soul_list.c` | 14 | 0 |
| `src/ingame/map/BgSe.c` | 16 | 0 |
| `src/ingame/map/DoorStat.c` | 20 | 0 |
| `src/ingame/map/FurnLoad.c` | 24 | 0 |
| `src/ingame/map/FurnSave.c` | 15 | 0 |
| `src/ingame/map/LightCtl.c` | 21 | 0 |
| `src/ingame/map/MapCtrl.c` | 14 | 0 |
| `src/ingame/map/MapDBuff.c` | 15 | 0 |
| `src/ingame/map/MapDoor.c` | 7 | 0 |
| `src/ingame/map/MapDraw.c` | 25 | 0 |
| `src/ingame/map/MapEff.c` | 22 | 0 |
| `src/ingame/map/MapFog.c` | 12 | 0 |
| `src/ingame/map/MapHit.c` | 3 | 0 |
| `src/ingame/map/MapLBuff.c` | 8 | 0 |
| `src/ingame/map/MapLayer.c` | 17 | 0 |
| `src/ingame/map/MapLight.c` | 32 | 0 |
| `src/ingame/map/MapLoad.c` | 43 | 0 |
| `src/ingame/map/MapObjMdl.c` | 10 | 0 |
| `src/ingame/map/MapObjWrk.c` | 25 | 0 |
| `src/ingame/map/MapPhf.c` | 7 | 0 |
| `src/ingame/map/MapPut.c` | 39 | 0 |
| `src/ingame/map/MapRcb.c` | 26 | 0 |
| `src/ingame/map/MapRec.c` | 14 | 0 |
| `src/ingame/map/MapRoomFile.c` | 20 | 0 |
| `src/ingame/map/MapSky.c` | 38 | 0 |
| `src/ingame/map/MapSnap.c` | 35 | 0 |
| `src/ingame/map/MarkDat.c` | 15 | 0 |
| `src/ingame/map/MarkDoor.c` | 12 | 0 |
| `src/ingame/map/MarkEvent.c` | 14 | 0 |
| `src/ingame/map/PosFile.c` | 3 | 0 |
| `src/ingame/map/RegDat.c` | 42 | 0 |
| `src/ingame/map/RoomLoadAddr.c` | 7 | 0 |
| `src/ingame/map/foot_se.c` | 11 | 0 |
| `src/ingame/map/hit_check_base.c` | 9 | 0 |
| `src/ingame/map/map_bgm.c` | 14 | 0 |
| `src/ingame/map/map_height.c` | 12 | 0 |
| `src/ingame/map/map_hit_check.c` | 6 | 0 |
| `src/ingame/map/map_rectangle.c` | 24 | 0 |
| `src/ingame/memo/memo.c` | 44 | 0 |
| `src/ingame/menu/anim_2d.c` | 98 | 0 |
| `src/ingame/menu/menuCamBase.c` | 11 | 0 |
| `src/ingame/menu/menuCamBasic.c` | 33 | 0 |
| `src/ingame/menu/menuCamMain.c` | 18 | 0 |
| `src/ingame/menu/menuCamStLens.c` | 28 | 0 |
| `src/ingame/menu/menuCamStat.c` | 46 | 0 |
| `src/ingame/menu/menu_bg.c` | 32 | 0 |
| `src/ingame/menu/menu_cassette.c` | 46 | 0 |
| `src/ingame/menu/menu_csr.c` | 44 | 0 |
| `src/ingame/menu/menu_csr_obj.c` | 17 | 0 |
| `src/ingame/menu/menu_draw_tex.c` | 50 | 0 |
| `src/ingame/menu/menu_file.c` | 28 | 0 |
| `src/ingame/menu/menu_file_list.c` | 88 | 0 |
| `src/ingame/menu/menu_item.c` | 56 | 0 |
| `src/ingame/menu/start_menu.c` | 114 | 0 |
| `src/ingame/menu/start_menu_top.c` | 37 | 0 |
| `src/ingame/mission/ExtraMissionShelt.c` | 10 | 0 |
| `src/ingame/mission/mission_end.c` | 31 | 0 |
| `src/ingame/mission/mission_list.c` | 27 | 0 |
| `src/ingame/mission/mission_mode.c` | 15 | 0 |
| `src/ingame/mission/mission_start.c` | 4 | 0 |
| `src/ingame/movie_room_menu/prg/movie_projecter.c` | 14 | 0 |
| `src/ingame/npc/npc_act.c` | 79 | 0 |
| `src/ingame/pause/prg/pause.c` | 4 | 0 |
| `src/ingame/photo/finder_photo.c` | 54 | 0 |
| `src/ingame/photo/finder_view.c` | 21 | 0 |
| `src/ingame/photo/finder_view_hud.c` | 38 | 0 |
| `src/ingame/photo/fndr_effect.c` | 31 | 0 |
| `src/ingame/photo/fndr_parts.c` | 126 | 0 |
| `src/ingame/photo/m_plyr_camera.c` | 20 | 0 |
| `src/ingame/photo/n_equip_tray.c` | 28 | 0 |
| `src/ingame/photo/n_plyr_camera.c` | 91 | 0 |
| `src/ingame/photo/photo.c` | 28 | 0 |
| `src/ingame/photo/photo_comp.c` | 6 | 0 |
| `src/ingame/photo/photo_dat.c` | 30 | 0 |
| `src/ingame/photo/photo_file.c` | 21 | 0 |
| `src/ingame/photo/photo_make.c` | 19 | 0 |
| `src/ingame/photo/photo_obj.c` | 27 | 0 |
| `src/ingame/photo/photo_obj_dat.c` | 15 | 0 |
| `src/ingame/plyr/acs_model.c` | 5 | 0 |
| `src/ingame/plyr/man_data.c` | 10 | 0 |
| `src/ingame/plyr/player.c` | 139 | 0 |
| `src/ingame/plyr/plyr_gauge.c` | 13 | 0 |
| `src/ingame/plyr/plyr_mdl.c` | 38 | 0 |
| `src/ingame/plyr/plyr_param.c` | 18 | 0 |
| `src/ingame/plyr/plyr_shot.c` | 20 | 0 |
| `src/ingame/plyr/plyr_sub.c` | 75 | 0 |
| `src/ingame/plyr/unit_ctl.c` | 26 | 0 |
| `src/ingame/puzzle/kamon/kamon_board.c` | 29 | 0 |
| `src/ingame/puzzle/kamon/kamon_draw.c` | 15 | 0 |
| `src/ingame/puzzle/kamon/kamon_pzl.c` | 20 | 0 |
| `src/ingame/puzzle/kaza/kaza_board.c` | 14 | 0 |
| `src/ingame/puzzle/kaza/kaza_draw.c` | 5 | 0 |
| `src/ingame/puzzle/kaza/kaza_pzl.c` | 17 | 0 |
| `src/ingame/puzzle/kusi/kusi_board.c` | 31 | 0 |
| `src/ingame/puzzle/kusi/kusi_draw.c` | 17 | 0 |
| `src/ingame/puzzle/kusi/kusi_pzl.c` | 29 | 0 |
| `src/ingame/puzzle/puzzle.c` | 23 | 0 |
| `src/ingame/puzzle/rousoku/rousoku_board.c` | 16 | 0 |
| `src/ingame/puzzle/rousoku/rousoku_draw.c` | 9 | 0 |
| `src/ingame/puzzle/rousoku/rousoku_pzl.c` | 17 | 0 |
| `src/ingame/puzzle/sizumedama/sizumedama_board.c` | 12 | 0 |
| `src/ingame/puzzle/sizumedama/sizumedama_draw.c` | 12 | 0 |
| `src/ingame/puzzle/sizumedama/sizumedama_pzl.c` | 32 | 0 |
| `src/ingame/radio.c` | 24 | 0 |
| `src/ingame/savepoint/savepoint_menu.c` | 14 | 0 |
| `src/ingame/sleep_movie.c` | 3 | 0 |
| `src/ingame/sound/snd_ctrl.c` | 38 | 0 |
| `src/ingame/story_develop.c` | 71 | 0 |
| `src/ingame/story_door.c` | 45 | 0 |
| `src/ingame/story_game_clear.c` | 3 | 0 |
| `src/ingame/story_load_mc.c` | 4 | 0 |
| `src/ingame/story_main.c` | 18 | 0 |
| `src/ingame/story_menu.c` | 33 | 0 |
| `src/ingame/story_menu_camera.c` | 8 | 0 |
| `src/ingame/story_menu_film.c` | 46 | 0 |
| `src/ingame/story_menu_ghost_list.c` | 34 | 0 |
| `src/ingame/story_menu_map.c` | 19 | 0 |
| `src/ingame/story_menu_photo.c` | 70 | 0 |
| `src/ingame/story_menu_photo_view.c` | 24 | 0 |
| `src/ingame/story_menu_top.c` | 20 | 0 |
| `src/ingame/story_night.c` | 17 | 0 |
| `src/ingame/story_save.c` | 6 | 0 |
| `src/ingame/story_save_mc.c` | 4 | 0 |
| `src/ingame/subtitle/subtitle.c` | 16 | 0 |
| `src/ingame/tattoo_event.c` | 20 | 0 |
| `src/main/gphase.c` | 25 | 0 |
| `src/main/gphase_tree.c` | 11 | 0 |
| `src/main/main.c` | 9 | 0 |
| `src/outgame/autoload.c` | 17 | 0 |
| `src/outgame/autoload_scr.c` | 16 | 0 |
| `src/outgame/boot.c` | 17 | 0 |
| `src/outgame/lang_sel.c` | 14 | 0 |
| `src/outgame/option.c` | 30 | 0 |
| `src/outgame/option_akarusa.c` | 9 | 0 |
| `src/outgame/option_button.c` | 10 | 0 |
| `src/outgame/option_sousa.c` | 9 | 0 |
| `src/outgame/option_system.c` | 9 | 0 |
| `src/outgame/option_top.c` | 10 | 0 |
| `src/outgame/outgame_bg.c` | 20 | 0 |
| `src/outgame/pad_check.c` | 6 | 0 |
| `src/outgame/save_info_disp.c` | 12 | 0 |
| `src/outgame/save_load_scr.c` | 43 | 0 |
| `src/outgame/save_scr.c` | 45 | 0 |
| `src/outgame/sel_menu.c` | 15 | 0 |
| `src/outgame/title_menu.c` | 35 | 0 |
| `src/outgame/title_obj.c` | 11 | 0 |
| `src/save_load/prg/game_data_load.c` | 24 | 0 |
| `src/save_load/prg/game_data_save.c` | 49 | 0 |
| `src/system/b2d/B2dBase.c` | 32 | 0 |
| `src/system/b2d/B2dCtl.c` | 93 | 0 |
| `src/system/b2d/B2dFile.c` | 34 | 0 |
| `src/system/b2d/B2dGridSpr.c` | 6 | 0 |
| `src/system/b2d/B2dInp.c` | 11 | 0 |
| `src/system/b2d/B2dSpr.c` | 23 | 0 |
| `src/system/b2d/BpCtl.c` | 76 | 0 |
| `src/system/b2d/BpObj.c` | 16 | 0 |
| `src/system/b2d/BpTex.c` | 13 | 0 |
| `src/system/b2d/ScrollBar.c` | 22 | 0 |
| `src/system/compress/compress.c` | 18 | 0 |
| `src/system/eeiop/cddat.c` | 12 | 0 |
| `src/system/eeiop/cmp_eeiop.c` | 9 | 0 |
| `src/system/eeiop/ee_iop.c` | 13 | 0 |
| `src/system/eeiop/ee_iop_q.c` | 5 | 0 |
| `src/system/eeiop/file_cache.c` | 15 | 0 |
| `src/system/eeiop/file_stream.c` | 5 | 0 |
| `src/system/eeiop/fileload.c` | 26 | 0 |
| `src/system/eeiop/snd.c` | 14 | 0 |
| `src/system/eeiop/snd_3d.c` | 16 | 0 |
| `src/system/eeiop/snd_bank.c` | 25 | 0 |
| `src/system/eeiop/snd_buffer.c` | 25 | 0 |
| `src/system/eeiop/snd_pcmstream.c` | 12 | 0 |
| `src/system/eeiop/snd_stream.c` | 26 | 0 |
| `src/system/eeiop/snd_util.c` | 6 | 0 |
| `src/system/eeiop/spu_mem.c` | 13 | 0 |
| `src/system/eeiop/spu_voice.c` | 4 | 0 |
| `src/system/eeiop/stream_auto.c` | 42 | 0 |
| `src/system/encodes/cmp.c` | 3 | 0 |
| `src/system/encodes/encodes.c` | 5 | 0 |
| `src/system/mc/prg/mc.c` | 3 | 0 |
| `src/system/mc/prg/mc_check.c` | 2 | 0 |
| `src/system/mc/prg/mc_check_broken.c` | 3 | 0 |
| `src/system/mc/prg/mc_check_card.c` | 12 | 0 |
| `src/system/mc/prg/mc_check_dir.c` | 9 | 0 |
| `src/system/mc/prg/mc_check_empty.c` | 2 | 0 |
| `src/system/mc/prg/mc_close.c` | 3 | 0 |
| `src/system/mc/prg/mc_cmn.c` | 5 | 0 |
| `src/system/mc/prg/mc_del_all_file.c` | 3 | 0 |
| `src/system/mc/prg/mc_del_dir.c` | 2 | 0 |
| `src/system/mc/prg/mc_del_file.c` | 3 | 0 |
| `src/system/mc/prg/mc_format.c` | 3 | 0 |
| `src/system/mc/prg/mc_icon.c` | 3 | 0 |
| `src/system/mc/prg/mc_icon_sys.c` | 2 | 0 |
| `src/system/mc/prg/mc_load.c` | 2 | 0 |
| `src/system/mc/prg/mc_make.c` | 2 | 0 |
| `src/system/mc/prg/mc_make_all_file.c` | 2 | 0 |
| `src/system/mc/prg/mc_make_dir.c` | 3 | 0 |
| `src/system/mc/prg/mc_make_file.c` | 2 | 0 |
| `src/system/mc/prg/mc_open.c` | 3 | 0 |
| `src/system/mc/prg/mc_read.c` | 3 | 0 |
| `src/system/mc/prg/mc_save.c` | 2 | 0 |
| `src/system/mc/prg/mc_set_data.c` | 38 | 0 |
| `src/system/mc/prg/mc_write.c` | 3 | 0 |
| `src/system/os/eecdvd.c` | 4 | 0 |
| `src/system/os/sys_heap.c` | 4 | 0 |
| `src/system/os/system.c` | 32 | 0 |
| `src/system/pad/pad.c` | 11 | 0 |
| `src/system/pad/pad_ctrl.c` | 16 | 0 |
| `src/system/pad/vib_pulse.c` | 5 | 0 |
| `src/system/playpss/audiodec.c` | 13 | 0 |
| `src/system/playpss/ldimage.c` | 13 | 0 |
| `src/system/playpss/my_strfile.c` | 3 | 0 |
| `src/system/playpss/playpss.c` | 23 | 0 |
| `src/system/playpss/pss_loop.c` | 11 | 0 |

## Translation units (one session each: `tus.py`)

- 353 TUs: todo 106, verify 191, done 155
- next: **None** (nothing on the frontier and no queued TU)
- pinned queue: unassigned@00180a78, unassigned@00158ed0, unassigned@001de0d8, unassigned@001d2a40, unassigned@0020df28, unassigned@00118768
- verify: `album/prg/album.c` 34/34 — Complete; the album is not reachable in the smoke run (verify). Renamed in Ghidra: SpriteCmnFlagClear -> AlbumIngameFlagClear, SpriteCmnSetFlag -> AlbumSetIngam
- verify: `album/prg/album_disp.c` 24/24 — Verify: nothing on the boot path reaches the album (smoke run stays in the language select; screen unchanged). First visible through album_view.c's ALBM_HENSYU 
- verify: `album/prg/album_load.c` 28/28 — Seen on screen (pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1450:CROSS,1750:CROSS,1900:RIGHT,2000:CROSS,2300:RIGHT,2450:CROSS,28
- verify: `album/prg/album_load_scr.c` 34/34 — Complete; nothing calls it yet: the phase 28 glue 0x128c78 (unassigned 0x128a58..0x128e30) runs Init / In / IsInEnd / Main / Close / OutWait / Release, and albu
- verify: `album/prg/album_mem.c` 4/4 — Mark done when a run opens the album save or load screen and a card copy goes through (phase 27 / 28 glue 0x128a58..0x128e30 is unassigned and must be written f
- verify: `album/prg/album_phase.c` 9/9 — Phase 28 is seen: Album_Open bottom row, Load (pad 100:CROSS,250:CROSS,320:LEFT,380:CROSS,1000:START,1150:RIGHT,1250:RIGHT,1450:CROSS,1750:CROSS,1900:RIGHT,2000
- verify: `album/prg/album_save.c` 46/46 — Nothing reaches AlbumSaveMain yet: phase 27/28 glue 0x128a58..0x128e30 is undefined (see open-items Boundaries seen); define and port it (e.g. album/prg/album_p
- verify: `album/prg/album_save_scr.c` 34/34 — Complete code, nothing reaches it: phase 27 one (0x128ab8, in the unassigned 0x128a58..0x128e30 phase glue) is not written, so no screenshot can show it. Verify
- verify: `album/prg/album_view.c` 78/78 — Complete; nothing on the boot path reaches the album edit page (callers album_edit.c 0x1229a0..0x1240e8, the 0x1240e8 and 0x126738 runs and the phase 27/28 glue
- verify: `album/prg/album_zoom.c` 17/17 — Verify: the zoom page opens from Album_Menu row 0 (menu_kakudai), which AlbumMenuCondHasPhoto gates, and no album holds a photo yet (card load is stubbed), so n
- verify: `album/prg/album_zoom_view.c` 26/26 — Defined from the unassigned run 0x12e798..0x12f470 (FF3-only, only album_zoom.c calls it). Two hooks had no Ghidra function (0x12eaf0, 0x12f048; created). 0x12e
- verify: `common/Load.c` 7/9 — Verify: only CMapDBuff (the opened door's model, in-game) uses it, so nothing on the boot path shows it. Mark done when a door opens in game and its D000_PK4 mo
- verify: `common/ProcCtl.c` 3/5 — Complete. Shows nothing on screen; GameSystemEnd only runs on a reset. The one subclass is CClassPtr<CLightCtl> (template code 0x2ac428.., vtable 0x3411a0) owne
- verify: `common/SingleLinkList.c` 10/10 — Complete. A container for effect lists (butterflies, candles, drops, tourou, water flow); SingleLinkListInit runs in the smoke run, nothing on screen can show i
- verify: `common/WindowMsg.c` 18/21 — Complete; verify because nothing on the smoke path opens a CWindowMsg (phase 12, the option screen and the start menu use it; the run stays in the language sele
- verify: `common/memShelt.c` 11/13 — Users are still stubs: CExtraMissionShelt ctor/Save/Load/Restore and the retry backup 0x10b9a8 (wrongly named DbFurnPreGetObjNum) / restore 0x10ba58, all in the
- verify: `common/spline.c` 4/4 — Complete; verify. FF3-only and unreferenced in the ROM (ghidra.py refs: no jal, data word or address pair to SplineGetPoint), so nothing will ever show it; mark
- verify: `common/vu0math.c` 19/19 — Complete; verify because nothing on the boot path draws a rotated 2D item yet (language select screenshot pixel-identical to the previous progress shot). The fi
- verify: `graphics/ecb.c` 7/7 — Complete, nothing on screen can show it yet: the --run 100 smoke run loads area_data, map_menu, map_menu_stat, furn_save and meimetu.ecb and room rs101, then St
- verify: `graphics/effect/effect.c` 59/59 — Complete; nothing on screen yet: the effect system runs in-game only (IngameInit / IngameDrawSub / SceneDraw), the 15 s smoke run ends on the boot card dialog. 
- verify: `graphics/effect/effect_butterfly.c` 32/33 — Unseen: nothing in the opening requests a butterfly (IgEffectButterflyReq has no caller in FF3; guides come from SET_BUTTERFLY / MOVE_BUTTERFLY / RELEASE_BUTTER
- verify: `graphics/effect/effect_ene.c` 123/123 — Complete; verify. Everything visible in this TU needs a photographed ghost (see the previous sessions: blue sparkle bloom flying to the lens, end burst, aura); 
- verify: `graphics/effect/effect_mapsky.c` 11/11 — Verify: only the tattoo event (TattooEvCameraMain state 4, EffMapSkyReq at Rei) requests the effect and no tattoo ghost is on the smoke path; --run 100 unchange
- verify: `graphics/effect/effect_obj.c` 330/330 — Verify: all code is written; at --run 160 the picture is unchanged (Rei in rs101 under the map camera, the wax candle flames lit, same as progress 20261016-2049
- verify: `graphics/effect/effect_oth.c` 165/166 — Verify: nothing in the opening requests kinds 3 / 5 / 10..13 (blur, focus, nega, contrast), so none of the new filters is on screen; the first likely one is Cal
- verify: `graphics/effect/effect_sub.c` 44/44 — Verify: --run 170 picture matches the previous session (rs101 under the map camera); a reverted probe showed EffDispSprD2Direct called once per event frame for 
- verify: `graphics/effect/effect_torch.c` 33/33 — verify: all 33 written (FF2 effect_torch.c ported; FF3 drops the 25x flame scale, uses EffCandleFreaDraw for the flare, plays no torch sounds, has no EffectTorc
- verify: `graphics/graph2d/fade.c` 6/6 — Complete (6/6). Defined from unassigned@001cae98: FF2 fade.c bodies on fade_ctrl 0x327550 (FADE_MODE_CTRL 0xc). Renamed in Ghidra to FF2 names: FadeInit -> Fade
- verify: `graphics/graph2d/g2d_debug.c` 8/9 — Status verify: code complete, but nothing on the boot path calls DispSqrRect yet (callers: pre_Title_Mode, effect_obj/effect_oth, movie, anicode, player). Mark 
- verify: `graphics/graph2d/g2d_draw.c` 17/17 — Status verify: the code is complete, but nothing on the boot path calls LocalCopy, DispSqrD or DispSprD2 yet (the language select screen is unchanged and still 
- verify: `graphics/graph3d/g3dCamera.c` 6/6 — All 6 functions written (0x1cb460..0x1cb88c), no stubs left in the TU. gra3dApplyCamera and gra3dCalcWorldScreenMatrix now get real view-screen and view-clip ma
- verify: `graphics/graph3d/g3dCore.c` 31/31 — Verify: nothing 3D draws yet, so no screenshot can show this; the language-select shot is unchanged. g3dInitialize runs at boot via gra3dInit without trouble. M
- verify: `graphics/graph3d/g3dDma.c` 9/9 — All of FF3 0x1cced0..0x1cd17c written from MioPan g3dDma.c: open/close/add/flush/copy go through system.c PACKET_BUF_MGR (cached in s_pPacketBufMgr, sdata 0x372
- verify: `graphics/graph3d/g3dGeom.c` 19/19 — Verify: the gra3d camera now runs g3dMatrixSetTarget instead of a stub, but nothing 3D draws yet; the run-100 shots are unchanged (opening movie, then black in 
- verify: `graphics/graph3d/g3dLight.c` 5/5 — Complete: FF2's five functions minus asserts; FF3 uses Vu0Cosf and drops the fFalloff powf branch (linear cone ramp only). G3DLIGHT scalar offsets 0x54..0x64 no
- verify: `graphics/graph3d/g3dLightEx.c` 14/20 — All of FF2's g3dLightEx.c is written from MioPan's port, checked against FF3: box tests (_IsBBLightingupSpot keeps the ROM sense fDist < 0), convert, both blend
- verify: `graphics/graph3d/g3dRenderTarget.cpp` 11/21 — Complete; no stubs left. Clear follows FF3: the colour argument is never stored (sprite data memset, colour 0) and left = cx - h/2 (FF2 quirks, kept). Not visib
- verify: `graphics/graph3d/g3dSprite.cpp` 3/6 — Complete. Only user is CRenderTarget::Clear (shadow pass). Nothing on screen can show it: the host renderer does not execute DIRECT GIF packets from the g3d cha
- verify: `graphics/graph3d/g3dVif1.c` 5/5 — FF3 is FF2's g3dVif1.c without asserts: command table is static data 0x359d10 (same 10 entries as FF2, shadow 0x3eb310), default code NOP FLUSH at 0x370aa0, and
- verify: `graphics/graph3d/gra3d.c` 129/129 — Code complete; status verify because nothing 3D is drawn on PC (smoke run still shows the language select, unchanged). Mark done when a 3D scene (title or in-ga
- verify: `graphics/graph3d/gra3dBoundingBox.c` 7/7 — Complete, verify: nothing on screen runs it yet (gra3dSGD.c's draw, gra3dShadow.c and char_model.c's box callers are stubs or unreached; in-game run is black af
- verify: `graphics/graph3d/gra3dConst.c` 2/2 — Done. g_xv0000 is declared in gra3dConst.h; g3dLightEx.c still writes literal zeros where FF3 reads it (same values). FF2's other constants (g_v1000.., g_matUni
- verify: `graphics/graph3d/gra3dDma.c` 6/6 — Complete; nothing on screen can show it: the VIF1 packets go into the recorded chain (no VU1 on PC) and the --run 100 smoke run ends black after Now Loading as 
- verify: `graphics/graph3d/gra3dMirror.c` 16/19 — Verify: nothing on PC reaches it yet. IngameDrawRoomMorph returns early while room->mdl is 0 (MapDrawInitRoom / model managers are stubs); the first mirror room
- verify: `graphics/graph3d/gra3dMisc.c` 13/14 — Verify: only engine scenes call gra3dPrelightScene (ScenePlayStart, SceneRoomRestore, SceneEffectMonochroModeOnOff); no scripted run plays an engine scene, so i
- verify: `graphics/graph3d/gra3dSGD.c` 69/69 — Code complete, no stubs left in the TU (0x1e0330..0x1e3850 + linkonce 0x2bcad0..0x2bcb80). Verify: nothing calls _gra3dDrawSGD yet (MapDrawInitRoom / MapDrawRoo
- verify: `graphics/graph3d/gra3dSGDData.c` 24/24 — Code complete, no stubs left in the TU (0x1e3850..0x1e4760 + linkonce 0x2bcb80..0x2bcbc0). Verify: sgdRemap's callers FUN_002113d0 / FUN_00211450 (unassigned@00
- verify: `graphics/graph3d/gra3dShadow.c` 34/36 — All of FF3's gra3dShadow.c is written from MioPan's port, checked against FF3: FF3 lists each project model's coordinates (parallel fixed_stack 0x3eebf0) and pr
- verify: `graphics/graph3d/gra3dTRI2.c` 5/5 — Verify: nothing on screen can show TRI2 textures until the graph3d host bridge (reipan_graph3d.cpp, M4) draws a model; the run 100 shots are unchanged (title me
- verify: `graphics/scene/IngameScene.c` 37/40 — Verify: no scripted run requests an engine scene (phase 39 sends only SCENE0010 to 67), so phases 66 / 89 never ran; at --run 250 the picture is unchanged (Rei 
- verify: `graphics/scene/fod.c` 20/20 — Verify: no scripted run plays an engine scene (open-items scene.c section), so nothing on screen shows FOD output yet; the --run 100 shots are unchanged (openin
- verify: `graphics/scene/scene.c` 55/55 — Verify: no scripted run plays an engine scene (phase 66/89), so nothing here is on screen; --run 170 is unchanged (Rei walks rs101 into room 2). Mark done when 
- verify: `graphics/scene/scene_dat.c` 17/17 — Defined as graphics/scene/scene_dat.c (0x253620..0x254018 plus out-of-line 0x2be808 SceneMapObjShow and 0x2bef98 StrGetNumber), the old unassigned@00253620 run:
- verify: `graphics/scene/scene_effect.c` 82/82 — Whole TU written (82 functions, pinned 0x254018-0x256da8). 15 SceneEffectFunc targets (rodata 0x362aa8) had no Ghidra function: created, and the shifted FF2 nam
- verify: `ingame/camera/cam_rail.c` 39/39 — Verify: the scripted pad never walks Rei into a rail rectangle (types 100 / 101 / 109 in area 1) or enters mode 10, so the run cannot show this file; mark done 
- verify: `ingame/camera/dmg_cam_shot.c` 7/7 — Verify: no ghost attacks in the smoke run, so nothing can show the shots yet; the first run where a ghost hits or holds Rei (APPROACH_CAMERA_FLOW_SHOT) marks it
- verify: `ingame/candle_ctrl.c` 71/100 — Verify: in-game only; the smoke run (title menu, difficulty select) cannot show it. The first dream-world screenshot should show the candle bottom left (halo 14
- verify: `ingame/cat/cat_ctrl.c` 155/155 — Verify: the cat lives in the real-world house and the --run 100 path goes to the dream world (GO_DREAM_WORLD deletes its models), so nothing of it has drawn; th
- verify: `ingame/cine_film.c` 45/45 — Verify: no projector room in the smoke runs (CINE_FILM_PLAY in areas 0x13 / 0x30 / 0x1c); --run 100 is unchanged (opening event in rs101). Mark done when a run 
- verify: `ingame/enemy/ene_main.c` 70/70 — verify: in-game only, no enemy acts in the opening (--run 100 picture unchanged: rs101 opening event). ENE_WRK gained the script cursor alg (ENEALG_WRK, +0xa0..
- verify: `ingame/enemy/ene_spe_obj.c` 19/19 — Range widened to 0x1985c0..0x199150 (RegSecList helpers were unassigned; BgSe.c now includes ene_spe_obj.h). Both types are classes with members (ctor, operator
- verify: `ingame/enemy/enemy.c` 100/100 — All 100 functions of the range are written; status verify because enemies are in-game only (the smoke run stays on the language select, same picture as before: 
- verify: `ingame/enemy/enemy_act.c` 164/164 — verify: all 164 functions written, no stubs left in enemy_act.c. Nothing on screen can show it yet: the --run 100 opening shows the rs101 room in monochrome wit
- verify: `ingame/enemy/fly_ctrl.c` 32/33 — verify: no event in the smoke run launches a fly yet (EJobC18 not reached; the room 2 ghost is a type-2 enemy without flies), so nothing on screen shows them. -
- verify: `ingame/enemy/random_ghost.c` 37/37 — Verify: the opening switches no random-ghost area on (RandGhostMain returns at area -1 / off) and no radio on; --run 170 is unchanged (Rei walks to the gate pos
- verify: `ingame/event/ev_queue.c` 19/19 — Verify: reached only from event script commands 0x1a0d78/0x1a0da0 and the state machines 0x221360/0x221820 (kind 1), none on the boot path; the first screenshot
- verify: `ingame/event/prg/ev_change.c` 50/50 — Verify: event code is in-game only; the 15 s smoke run shows the memory-card start-anyway dialog, unchanged. Mark done when an in-game event runs (conditions ar
- verify: `ingame/event/prg/ev_chapter.c` 22/22 — Verify: the 100 s run reaches Story_Normal unchanged (title, difficulty select fine, then black, no crash); EvChapterDispMain runs every frame but no CHAPTER_DI
- verify: `ingame/event/prg/ev_command.c` 324/324 — 324 of 324 functions written; status verify because nothing reaches the handlers yet: the event dispatcher 0x19b5b8 and the argument decoder 0x19b158 (unassigne
- verify: `ingame/event/prg/ev_exe.c` 16/16 — Verify: EventExeInit and EventExe run every Story_Normal frame (100 s run, no crash) but no slot is ever used: EventCtrlCenterMain (ev_open.c) is what calls Set
- verify: `ingame/event/prg/ev_get.c` 8/8 — Status verify: nothing running calls these yet (EventSetOpenCondition and EventCtrlCenterMain in ev_open.c and the dispatcher 0x19b5b8 in ev_macro.c are stubs);
- verify: `ingame/event/prg/ev_ladder.c` 9/9 — Verify: hasigo.ecb loads every new game (9 rows, ids 0..8, row size 0x90 checked against the disc file) but no smoke run climbs a ladder, so nothing on screen s
- verify: `ingame/event/prg/ev_macro.c` 5/5 — Complete; nothing runs it yet: EventExe only steps slots that EventCtrlCenterMain / EventSetOpenCondition (ev_open.c, stubs) fill, so the smoke run (--run 100) 
- verify: `ingame/event/prg/ev_main.c` 12/15 — Verify: the 100 s run reaches Story_Normal and EventMain runs every frame (EvTimerMain, EventCtrlCenterMain, EventExe stubs reached), event.obj (265008 bytes) a
- verify: `ingame/event/prg/ev_open.c` 29/29 — The event core runs now: a temporary trace showed the unconditional events auto-start at Story_Normal and judged open conditions start 17..32 and 148..151 the n
- verify: `ingame/event/prg/ev_se.c` 10/10 — Complete: ev_seSetSave (save block 9), ev_seReleaseHalf, ev_seFindFile, ev_seInit, ev_seRelease, ev_seRegisterFile (was FUN_0019c7e0; ev_command callers renamed
- verify: `ingame/event/prg/ev_timer.c` 10/10 — Verify: EvTimerCtrlInit / EvTimerMain now run every in-game frame (no longer reached stubs in a --run 200, which still ends in room 2 / the photo as before), bu
- verify: `ingame/event/tel_talk.c` 15/15 — Complete; nothing on the scripted path places a phone call (no TEL_TALK_START in the main event file, EV_MISSION_OBJ decode of event 379 looks wrong), so verify
- verify: `ingame/event/telephone.c` 28/28 — Verify: TelInit/TelMain/TelTalkMain run every in-game frame without trouble at --run 100 (the screen is the opening event's monochrome rs101 as before, progress
- verify: `ingame/ingame_effect.c` 60/60 — Seen at --run 170: the screen look now applies in rs101 (lighter, soft glow, film grain; the stub left it dark) -- no PS2 capture to judge its strength, so veri
- verify: `ingame/item/prg/item.c` 26/27 — Verify: complete code, but the 100 s smoke run still ends on Now Loading (phase 30 waits on the Wrk38e6d8IsReady stub, unassigned@0011d398, next in the queue). 
- verify: `ingame/item/prg/soul_list.c` 14/14 — Complete, verify: SoulListInit runs every new game, SoulListSetHave after a ghost photo, but nothing on screen reads the list yet (the ghost list menu, game_res
- verify: `ingame/map/DoorStat.c` 20/20 — Complete, nothing on screen can show it yet: the --run 100 smoke run loads door_stat.ecb at IngameInit, leaves Now Loading and runs Story_Normal to the end of t
- verify: `ingame/map/FurnLoad.c` 24/24 — Verify: first visible when a furniture model draws (needs FUN_002035d0 and the graph3d bridge). A --run 100 trace showed rs101 record its 9 obj_pack models (d00
- verify: `ingame/map/FurnSave.c` 15/15 — Verify: in-game only; the smoke run ends on the title difficulty select (unchanged), so nothing can show it. Mark done when an in-game room shows furniture whos
- verify: `ingame/map/LightCtl.c` 21/21 — In-game only (area 0x12 room lights), so verify: the first session that reaches a map with area 0x12 loaded should see lights named meimetu pulse between 20 and
- verify: `ingame/map/MapCtrl.c` 14/17 — Verify: mark done when a room or Rei draws in game (MapDrawAll -> MapDrawRooms 0x118650 is a stub; GameModel::Draw 0x1d2e08 is a stub in unassigned@001d2a40). T
- verify: `ingame/map/MapDBuff.c` 15/17 — Verify: in-game only (story_door.c's opened door; MapDraw registers the special-event puts). Mark done when a door opens in game and both door models draw in tu
- verify: `ingame/map/MapDoor.c` 7/7 — Verify: the door code itself shows only when a door model draws (model handle builder FUN_002035d0 in unassigned@00203310 and the graph3d bridge are stubs); mar
- verify: `ingame/map/MapDraw.c` 25/25 — Verify: complete code, nothing on screen yet: the shots are unchanged (title, difficulty select, opening movie, then black in-game). A temporary trace (reverted
- verify: `ingame/map/MapEff.c` 22/34 — Complete; in-game only, so verify. MapEffCreate returns the address of a static word some types never write (FF3 behaviour kept). Effect families (effect_obj, e
- verify: `ingame/map/MapFog.c` 12/12 — Code complete; MapFogProc runs every in-game frame in the --run 100 smoke (MapHitRegistCallback, RegDatGetBuffIDAll, RegDatGetVecPtr/Num reached as stubs) but t
- verify: `ingame/map/MapHit.c` 3/3 — Verify with MapLBuff.c: nothing on screen yet (in-game is black, rooms not drawn). Save block 51 carries host MAP_PUT pointers and reports the host size; decidi
- verify: `ingame/map/MapLBuff.c` 8/10 — Verify: complete code, nothing on screen yet: the --run 100 shots are unchanged (logos, opening movie, then black in-game; no room is drawn). Door vecs are stil
- verify: `ingame/map/MapLight.c` 32/35 — Verify: complete code, nothing on screen yet: the --run 100 shots are unchanged (title, difficulty select Normal, then black in-game). MapLBuffLoadDoor now runs
- verify: `ingame/map/MapObjMdl.c` 10/10 — Code complete; nothing on screen can show it yet: at --run 100 a temporary trace (reverted) counted 26 MAP_OBJ_MDL builds in a new game (7 plain, 1 cloth, 18 wi
- verify: `ingame/map/MapObjWrk.c` 25/25 — Verify: in-game only (the smoke run ends on the difficulty select). Mark done when an in-game room draws its doors and furniture. Nearly every callee is a stub:
- verify: `ingame/map/MapPut.c` 39/39 — Verify: in-game only (MapPutResetAll runs from MapPlayStartReset, MapPutDraw from IngameDrawSub); the 75 s smoke run ends on the title difficulty select, unchan
- verify: `ingame/map/MapRoomFile.c` 20/20 — Verify: in-game only; nothing reaches MapLoad's room loads in the smoke run. Mark done when a room loads in game (the map draws). MAP_ROOM_DAT keeps EE addresse
- verify: `ingame/map/MapSky.c` 36/36 — Code complete; verify until a session reaches the tattoo event (TattooEvCameraMain state 4, the only in-game user of the map-sky effect) and sees the sky dome i
- verify: `ingame/map/MarkEvent.c` 13/14 — Verify on the map screen (the sprite hook 0x1f7978 in m_plyr_camera.c, which calls MarkEventIsDrawn / MarkEventSpecialSprite, is a stub). LERP_F stays in this f
- verify: `ingame/map/PosFile.c` 3/3 — Verify: the room origins now come from area_pos (rs106 at -132, 0, 490 in the trace); seen on screen once a room draws. POS_FILE_ENT moved out of roku_pzl.h (FU
- verify: `ingame/map/RoomLoadAddr.c` 7/7 — Verify with MapDraw.c. Replaces the old FUN_001f7428 / Sub1f73b8GetWord4 stubs (callers in MapLoad.c / MapRoomFile.c renamed to RoomLoadAddrSetBase / RoomLoadAd
- verify: `ingame/map/map_bgm.c` 14/14 — Verify: map_bgm is in-game only; the 75 s smoke run still ends on the title difficulty select (unchanged), 15 s on the card message. The two reached stubs were 
- verify: `ingame/memo/memo.c` 44/52 — Nothing left to write. Unseen: --run 100 shows the title, difficulty select, then black in-game as before (no crash); the notice appears only after an event run
- verify: `ingame/menu/menu_cassette.c` 46/46 — Status verify: the menu opens only from an event TAPE_START (or the unreferenced CassetteMenuReq), which no scripted smoke run reaches; --run 100 still reaches 
- verify: `ingame/menu/menu_csr_obj.c` 17/17 — Verify: none of the callers is written yet (MenuDispMain, MenuCam, finder_camera 0x1b1500 / 0x1b16a0, item.c 0x1ebef0 / 0x1ec028, FurnCtl 0x113db0 / 0x113fa0 / 
- verify: `ingame/menu/menu_file.c` 28/29 — Verify: complete, but the detail page opens only from phase 62 (the file list), and 62 and 57 are not written, so no screenshot shows it; the 100 s smoke run st
- verify: `ingame/menu/menu_file_list.c` 88/88 — Verify: complete, nothing left to write, but no run shows it yet: phase 62 opens from the story menu top (37 -> 57, phase 57 not written) or the memo button (Me
- verify: `ingame/mission/ExtraMissionShelt.c` 10/12 — Pinned 0x10b788..0x10bba0 (ctor twin 0x10b788 made a function). IngameRetryPrepare was renamed ExtraMissionSheltCarryStore: it keeps what a mission changed (mis
- verify: `ingame/mission/mission_end.c` 31/31 — Verify: nothing on screen reaches it yet. The smoke pad never starts an extra mission (start menu mission list -> phase 31 -> mission play -> EV_MISSION_CLEAR o
- verify: `ingame/mission/mission_list.c` 27/27 — Code complete but unseen: the mission menu opens only from the start menu after a clear, so the smoke run still ends on the title difficulty select (unchanged, 
- verify: `ingame/mission/mission_mode.c` 15/15 — Complete; verify because only title start mode 2 (an extra mission) runs Main/Draw and no warp starts a mission yet. The --run 100 smoke (story mode) is unchang
- verify: `ingame/mission/mission_start.c` 4/4 — Complete code; nothing reaches it in a smoke run (EV_MISSION_START runs only inside an extra mission, which needs a clear save; see open-items Open questions fr
- verify: `ingame/npc/npc_act.c` 79/79 — Verify: nothing in-game is on screen (the 15 s smoke run ends on the boot card message, same as before); the first in-game shot where Miku or Kei stands at a la
- verify: `ingame/photo/finder_photo.c` 54/54 — Verify: complete, but nothing on the smoke path reaches in-game (the run stays in the language select, unchanged); mark done when a screenshot shows the in-game
- verify: `ingame/photo/finder_view.c` 21/21 — Code complete, unseen: CameraMain (ingame/camera/map_camera.c, todo) and the in-game camera HUD are not reached by the smoke run (it stays in the language selec
- verify: `ingame/photo/finder_view_hud.c` 37/37 — Complete; nothing on screen can show it (in-game only, smoke run sits in the language select). Its output also needs finder_view.c: the ten sprite helpers 0x1b7
- verify: `ingame/photo/fndr_effect.c` 31/31 — Code complete, in-game only: the screen is still the language select, so verify; the first photo-mode screenshot showing the finder glow / charge gauge marks it
- verify: `ingame/photo/fndr_parts.c` 126/126 — Code complete; nothing draws yet (screen is the language select, unchanged), so verify: the first in-game photo-mode screenshot that shows the HUD (lens, stones
- verify: `ingame/photo/n_equip_tray.c` 28/28 — Verify: the 15 s smoke run still ends on the boot card message (Do you want to start the game anyway?), as before; nothing in-game is reached, so no NPC, access
- verify: `ingame/photo/n_plyr_camera.c` 91/91 — Code complete, unverified on screen: the 15 s smoke run still ends on the boot card message (Do you want to start the game anyway?), so no NPC is ever shown; th
- verify: `ingame/photo/photo.c` 28/29 — Defined from unassigned@00226138 + unassigned@00226e38. Not on the smoke path: event 8 (area 5, map-hit rect 20004, facing 90 +- 40, OK) and 9..11 (area 17) req
- verify: `ingame/photo/photo_comp.c` 6/6 — Complete; verify because the stored photo only shows in the album and every real photo takes the DCT path: the --run 250 photo in room 2 reached CompressData, L
- verify: `ingame/photo/photo_dat.c` 30/30 — Verify: a shot now flashes white and returns to Story_Normal (--run 250 smoke run: flash at frame 4920, viewfinder after it), but nothing it draws is visible ye
- verify: `ingame/photo/photo_file.c` 21/21 — Defined from unassigned@0022ca90 as ingame/photo/photo_file.c 0x22ca90..0x22ec88 (name invented: FF2 photo.c's album half, kept apart from FF2 photo.c's other F
- verify: `ingame/photo/photo_obj.c` 27/32 — Verify: the --run 100 and --run 170 shots are unchanged (opening event and Rei walking in rs101); a temporary probe showed PhotoObjAreaSet running for areas 1 a
- verify: `ingame/photo/photo_obj_dat.c` 14/14 — Verify: nothing on screen can show it until an event runs SET_PHOTOABLE_ (EvSetPhotoable is the only setter of the photoable bits); photo_obj.c draws the glow. 
- verify: `ingame/plyr/acs_model.c` 5/6 — Complete; nothing on screen reaches the player models yet (verify). Ghidra functions created at 0x121000 and 0x1210b8 (both were swallowed). The host stand-in i
- verify: `ingame/plyr/plyr_gauge.c` 13/13 — Complete; checked against the asm (float order, clamps, the 0x129ea8 jump table). Nothing on screen can show it: it is in-game HUD for player 1 (Miku); the smok
- verify: `ingame/plyr/plyr_mdl.c` 38/38 — Verify: nothing on screen can show the look (needs CharPlayerModel and its CharWatchControl, ctor 0x145c50 still a stub; PC guards return early when mdl or ctrl
- verify: `ingame/plyr/plyr_param.c` 18/18 — Complete, no stubs. Defined ingame/plyr/plyr_param.c 0x2394a8-0x239ac0 (the run plus ReqPlayerMim..PlyrMdlGetHpMax taken from plyr_mdl.c, whose real code starts
- verify: `ingame/plyr/plyr_shot.c` 20/21 — Verify: no ghost is in the viewfinder in the smoke run, so the charge gauge, damage numbers, score panel icons, particles, lens effects, the auto turn (lens fun
- verify: `ingame/puzzle/puzzle.c` 23/23 — Verify: no event on the boot path starts a puzzle, so no screenshot shows it; the first in-game puzzle (event command 0x1a1c80 = PuzzleSet + request phase 49) m
- verify: `ingame/radio.c` 24/24 — Verify: nothing in the opening runs RADIO_TURN_ON, so RadioMain returns with empty slots. The first run whose event turns a radio on should show the caption at 
- verify: `ingame/savepoint/savepoint_menu.c` 14/14 — Complete, no stubs. Verify: phase 41 is in-game (save point) and the smoke run stays in the language select; the first run that reaches a save point should show
- verify: `ingame/sleep_movie.c` 1/1 — Verify: MoviePlay runs only from WAKEUP_MOVIE_PLAY / SLEEP_MOVIE_PLAY with a movie set (event 2, the dream exit choice, after event 5 SET_WAKEUP_MOVIE 27); the 
- verify: `ingame/story_develop.c` 70/70 — Was unassigned@001b3078, now ingame/story_develop.c 0x1b3078..0x1b57d8 + 0x2b91a8..0x2b94b0 (out of line after DocListFill). Verify: --run 100 unchanged (openin
- verify: `ingame/story_door.c` 45/45 — verify: in-game only; the smoke screenshot is still the memory-card start-anyway dialog (unchanged). Mark done when a door is taken in game (PlyrCheckAction ret
- verify: `ingame/story_main.c` 18/19 — Complete; the smoke run stays in the language select, so phase 9 is never entered (verify: the first run that starts a game from the title marks it done). Story
- verify: `ingame/story_menu_camera.c` 8/8 — Verify: complete; reached only from StoryMenu_Top (phase 57, not written) so no screenshot can show it yet. CmenuCamMain (menuCamMain.c) and its pages are writt
- verify: `ingame/story_menu_film.c` 46/48 — Verify: nothing opens phase 60 in a normal run -- its caller is the story menu top list, phase 57 StoryMenu_Top (gphase.c nullptr). Probe used: one_Story_Menu r
- verify: `ingame/story_menu_photo.c` 70/70 — Verify: DocFlagInit runs in IngameInit (the smoke run to the dream graveyard looks as before); the menu itself cannot be seen until phase 57 StoryMenu_Top (0x20
- verify: `ingame/story_menu_photo_view.c` 24/24 — Complete code; nothing on screen can show it until the story menu opens (phase 57 StoryMenu_Top is unwritten and the smoke pad never presses the menu key). Phas
- verify: `ingame/story_night.c` 17/17 — Verify: nothing in the scripted run changes the side, so the phase never starts; the --run 200 shots still show room 2 and the ghost event unchanged. The first 
- verify: `ingame/story_save.c` 6/6 — Verify: reached from a save point (0x1a1cb8 calls StorySaveGameReq). Callees still stubs: SavePointMenu* (album_view.c 0x135800..0x135d70, SAVEPOINT_PHF + langu
- verify: `ingame/story_save_mc.c` 4/4 — Verify: reached from the save point menu (phase 41) or the start menu option/album saves; needs a game in progress. Callees still stubs: SaveScr* (album_view.c 
- verify: `ingame/subtitle/subtitle.c` 14/14 — Verify: no subtitle is requested before the opening event stalls, so nothing on screen shows this file yet. The first session whose run shows a spoken-line capt
- verify: `ingame/tattoo_event.c` 19/19 — Verify: nothing in the smoke path starts the event (needs a ghost whose script runs EJobE04, tattoo kinds 0x1f / 0x20). --run 170 unchanged: the map camera foll
- verify: `outgame/autoload_scr.c` 16/16 — Verify: the smoke run never leaves the language select (no scripted pad input), so phase 14 is not reached; the first run that shows the boot memory-card window
- verify: `outgame/outgame_bg.c` 20/20 — Verify: used by options (7), start menu, story load (68) and save (41, 69), album, finder photo screens; none is reached by the smoke run (it stays in the langu
- verify: `outgame/save_info_disp.c` 12/12 — Complete (12 of 12, range 0x1340f8..0x134f60; 0x134f60 is an unreferenced hidden thunk to 0x2acac0 left to the photo caption file after it). Verify: shows on th
- verify: `outgame/save_scr.c` 45/45 — Complete (45 of 45). Verify: nothing reaches phase 69 in a smoke run (it stays in the language select; the save screen needs the title, a game and a save point)
- verify: `save_load/prg/game_data_load.c` 24/24 — Complete; verify: nothing reaches phase 68 in a smoke run yet (the title menu waits on the BGM preload, open-items Next up 0). The first screenshot of the Load 
- verify: `save_load/prg/game_data_save.c` 49/49 — Verify: complete, but phase 69 (the in-game save) is not reached by the smoke run (boot card prompt), so nothing on screen shows it. Mark done when the save scr
- verify: `system/b2d/B2dBase.c` 32/32 — Complete; nothing on the boot or title path calls it (camera menu pages, CGameClear, anicode menu windows, HUD layouts do). The first screenshot that shows a ca
- verify: `system/b2d/BpObj.c` 16/16 — Complete, no stubs left in BpObj.c. Verify: OutgameBg (title backdrop, load/save screens) is the first visible user of BP_OBJ; the scroll bar and arrows first s
- verify: `system/b2d/ScrollBar.c` 22/32 — Complete, no stubs. Verify: nothing on the boot path draws a scroll bar; the first visible users are BpObjDrawScrollBar (menu_cassette 0x14a1f8, 0x25b070), the 
- verify: `system/compress/compress.c` 18/18 — Checked against FF3 function by function. The only FF3 differences are that the printfs are gone and pi is correctly rounded (0x40490fdb). The --run 250 photo a
- verify: `system/eeiop/file_cache.c` 15/15 — Complete; verify because nothing on screen uses the cache yet: the --run 100 smoke plays the opening movie (unchanged), IngameInit / IngamePre / IngameEnd call 
- verify: `system/eeiop/file_stream.c` 5/5 — PC: no IOP stream service exists (system/iop/iop.c registers nothing), so FileStreamInit binds once instead of spinning and the calls return without effect (ope
- verify: `system/eeiop/snd.c` 14/14 — Verify: nothing on PC calls SndInit/SndMain yet (the ee_iopInit bridge skips SndInit; ee_iopMain is a stub). The setters run from snd_ctrl.c. Before ee_iopInit 
- verify: `system/eeiop/snd_3d.c` 16/16 — Verify: Snd3DInit and Snd3DMain run every frame in the smoke run and SndSystemInit sets env, 1 metre = 20 units and the listener; no source is created on the bo
- verify: `system/eeiop/snd_bank.c` 25/25 — Verify: the smoke run creates the default SE bank for real (SYS_BASE_BD.bd 0x1a9b loaded into SPU RAM at 0x5050, its HXD into ee_iopMalloc memory) and the boot 
- verify: `system/eeiop/snd_buffer.c` 25/25 — Complete: all 25 functions 0x269fa0..0x26ad2c, no stubs left. Ported from MioPan snd_buffer.c and checked against FF3: FF3 drops the range warnings, the Cannot 
- verify: `system/eeiop/snd_util.c` 6/6 — Verify: SndInit carves the auto works and SndMain runs AutoBDMain every frame, but nothing calls SndSeAutoPlay before the title and nothing is audible (snd_buff
- verify: `system/eeiop/spu_mem.c` 13/13 — Verify: the real allocator places SYS_BASE_BD at SPU 0x5050 during the boot as the bridge did, and the language select is drawn unchanged; mark done when sound 
- verify: `system/encodes/encodes.c` 5/5 — Complete. SlideDecode is seen through every compressed file the boot loads (with cmp.c, done). SlideEncode only runs when photo_comp.c CompressData stores a pho
- verify: `system/mc/prg/mc_check_broken.c` 3/3 — Verify: runs only when the game directory exists (boot auto load with a save, load screens).
- verify: `system/mc/prg/mc_check_dir.c` 9/9 — Verify: the listing runs at boot (-4 on a fresh card); the count / size / name accessors only run once a save directory exists (MemoryCardCheckDirBroken, the lo
- verify: `system/mc/prg/mc_check_empty.c` 2/2 — Verify: CheckEmpty runs at boot on a fresh card (room -> the 'no save' message); EmptyBroken only on a damaged save.
- verify: `system/mc/prg/mc_close.c` 3/3 — Verify: with mc_load.c.
- verify: `system/mc/prg/mc_cmn.c` 5/5 — Verify: ExeEndSync runs at boot; the checksum helpers run with saves (mc_set_data gather, the broken checks).
- verify: `system/mc/prg/mc_del_all_file.c` 3/3 — Complete; only caller is MemoryCardDirDelMain (mc_del_dir.c, verify), reached when a save/album directory is deleted, which the boot smoke run never does. Scree
- verify: `system/mc/prg/mc_del_dir.c` 2/2 — Verify: only album_save.c and game_data_save.c remake-dir paths call this (replacing a foreign or damaged save directory); the smoke run stops at the boot memor
- verify: `system/mc/prg/mc_del_file.c` 3/3 — Complete. MC_FILE_DEL_CTRL at bss 0x3f5828 (0x44, name at 0x0c, memset 0x37). Step 0 checked against FF3 decomp: retry_cnt<=0 gives -10 first; -100 and -210 re-
- verify: `system/mc/prg/mc_format.c` 3/3 — Complete. FF3 matches FF2 except the -100 (sceMcInit missing) assert is gone: -100 retries without using a retry. MC_FORMAT_CTRL 0xc at bss 0x3f5870. Not reacha
- verify: `system/mc/prg/mc_icon.c` 3/3 — Verify: the save path calls it; MemoryCardMakeNewFileInit/Main (mc_make_file.c) are still stubs, so nothing is written yet. LiberateMemoryCardIconDataMem runs a
- verify: `system/mc/prg/mc_icon_sys.c` 2/2 — Complete: both functions ported from FF2, without the MemoryCardAssert on a bad step (FF3 returns 0). The ctrl block is 0x3d4 bytes: step, port, slot, dir_label
- verify: `system/mc/prg/mc_load.c` 2/2 — Verify: runs only when a system file exists (boot auto load with a save).
- verify: `system/mc/prg/mc_make.c` 2/2 — Complete; only the game-data and album save screens call it (mkdir then mc_make_all_file), so the boot smoke run cannot show it: shot is the boot card dialog Do
- verify: `system/mc/prg/mc_make_all_file.c` 2/2 — Verify: only reached from MemoryCardNewMakeMain when a new save directory is created on the card; the smoke run stops at the boot card check message (No record 
- verify: `system/mc/prg/mc_make_dir.c` 3/3 — Complete; same as FF2 minus the asserts: -100 and -210 from sceMcMkdir now retry without using a retry. Not on screen yet: only the save path (MemoryCardNewMake
- verify: `system/mc/prg/mc_make_file.c` 2/2 — Complete: open with create (0x203), write, close state machine over MC_MAKE_FILE_CTRL (bss 0x3f5d20, 0x50 bytes, name[0x37] at 0x18). Same as FF2 except the def
- verify: `system/mc/prg/mc_open.c` 3/3 — Verify: with mc_load.c.
- verify: `system/mc/prg/mc_read.c` 3/3 — Verify: with mc_load.c.
- verify: `system/mc/prg/mc_save.c` 2/2 — Complete: FF3 is FF2 minus the default-case assert (unknown step returns 0); step is a byte (sb/lbu), open mode 3 (RDWR, no create). Nothing on screen can show 
- verify: `system/mc/prg/mc_set_data.c` 38/38 — Still verify (the rest is unchanged). The lock pair runs at every MemoryCardExeInit / End.
- verify: `system/mc/prg/mc_write.c` 3/3 — Complete; nothing on the boot path writes a card file (smoke run ends on the autoload no-save message), so verify: first visible when a save (mc_save.c / mc_mak
- verify: `system/pad/vib_pulse.c` 5/5 — Complete; verify. The pulse is sound (SE 0x1a9b0009) plus pad 0 big motor while peeping (one_Story_Peeping, phase Story_Peeping), which the smoke pad does not r
- verify: `system/playpss/audiodec.c` 13/13 — Complete code, nothing on screen can show it yet: the only movie that runs (title backdrop, MapSky) passes iopBuff 0, so audio_play_flg is 0 and audioDecCreate 

Recent sessions:

- 2026-10-22 08:31 `graphics/graph3d/CDebugEdit.cpp` done: CDebugEdit.cpp: IEditObject / CEditRoot from MioPan, checked against FF3 (vptr first, root is its own parent, Clear deletes children, TypeString Root, TypeInfo 
- 2026-10-22 08:16 `graphics/graph3d/eeFindFile.cpp` done: FF2 eeFindFile.cpp ported from MioPan and checked against FF3: CFindFile ctor, dtor, IsMatchSpec (FF3 adds the prefix*.ext rule, inlined stricmp), AnalyzeSpec, 
- 2026-10-22 08:04 `graphics/graph3d/eeFile.cpp` done: FF2 eeFile.cpp ported from MioPan and checked against FF3: CFile (ctors, dtors, Open, Read, Write, Close, Seek, GetLength, SeekToEnd / Begin, GetFileName / Titl
- 2026-10-22 07:46 `ingame/map/MapSky.c` verify: Annotated the dome classes' six implicit destructors (PssModel 0x2ac5c8/0x2ac590, PssModelBuilder 0x2ac6d0/0x2ac6f8, DummyTextureModelObject 0x2ac7a8/0x2ac7b8),
- 2026-10-21 22:53 `ingame/map/MapSky.c` partial: The sky dome's model classes, FF3's inline code 0x2ac590..0x2ac870 pinned to MapSky.c: PssModel::Build (always a PssModelBuilder) and DrawMdlPre (the player's a
- 2026-10-21 22:22 `common/utility2.c` done: Finished the TU: pinned 0x268ac0..0x268e70 and wrote all 18 functions (16 new: the string offset and extension helpers, RingBufAdd / RingBufCalcDiff, GetClampVa
- 2026-10-21 22:09 `ingame/story_game_clear.c` done: Phase 42 Story_GameClear written (init news CGameClear into sbss 0x372600, one runs it with the fade layer and BpCtlMain, end deletes it) and hooked into gphase
- 2026-10-21 22:07 `ingame/menu/start_menu.c` done: Seen on screen 2026-10-21 (story_game_clear.c session): phase 42 results and reward lists, phase 43 Clear Menu, and with cleared=1 the start menu Setup page

## Frontier (called by reconstructed code, not written yet)

| FF3 addr | name | call sites | provenance | state | inferred file |
|---|---|---|---|---|---|
| `001f72c0` | MapSnapGetStat | 11 | ff3 | missing | ingame/map/MapSnap.c |
| `001f5c78` | MapSnapWorldToMapY | 7 | ff3 | missing | ingame/map/MapSnap.c |
| `001194f0` | MapDoorVecGetPut | 4 | ff3 | missing | ingame/map/MapLBuff.c |
| `00119500` | MapDoorVecSetDisp | 4 | ff3 | missing | ingame/map/MapLBuff.c |
| `00208380` | MenuBgGetHandle | 3 | ff3 | missing | ingame/menu/menu_bg.c |
| `001f59e0` | MapLightRegReset | 3 | ff3 | missing | ingame/map/MapEff.c |
| `002113d0` | ModelObjectCtorBase | 3 | ff3 | missing | graphics/model_object.c |
| `001f5ab8` | MapLoadSlotReset | 3 | ff3 | missing | ingame/map/MapEff.c |
| `001f6f20` | MapSnapClearFlagAll | 2 | ff3 | missing | ingame/map/MapSnap.c |
| `001306e0` | B2dGridSprCtorByName2 | 2 | ff3 | missing | system/b2d/B2dGridSpr.c |
| `0011a078` | MapLightGetLightPtr | 2 | ff3 | missing | ingame/map/MapLight.c |
| `0024b598` | SizumeFadeLayerTerm | 2 | ff3 | missing | ingame/puzzle/sizumedama/sizumedama_pzl.c |
| `002abef0` | GameClearTblFindName | 2 | ff3 | missing | ingame/clear/GameClear.c |
| `001f5f38` | MapSnapGetFloor | 2 | ff3 | missing | ingame/map/MapSnap.c |
| `00144f40` | CharModelDtorBase | 2 | ff3 | missing | ingame/char_model.c |
| `002bcc30` | fill<SGDCOORDINATE**,SGDCOORDINATE*> | 2 | ff3 | missing | graphics/graph3d/gra3dShadow.c |
| `00135e78` | ScrollBarDtorBase | 2 | ff3 | missing | system/b2d/ScrollBar.c |
| `002bb210` | __introsort_loop<LIGHTCOMPAREDATA*,LIGHTCOMPAREDATA,int,LIGHTCOMPAREDATA::greater> | 2 | ff2 | missing | (unknown) |
| `002bbac0` | IAutoState::PopStack | 2 | ff2-ctx | missing | graphics/graph3d/g3dRenderTarget.cpp |
| `00177380` | RunBlur | 1 | ff2-ctx | missing | graphics/effect/effect_oth.c |
| `00210608` | ModelManagerDtorBase | 1 | ff3 | missing | graphics/mmanage.c |
| `002083c0` | MenuBgGetFade | 1 | ff3 | missing | ingame/menu/menu_bg.c |
| `001f69e0` | MapSnapStateUpdate | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `001f6a68` | MapSnapEnd | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `001f5958` | MapLightRegDel | 1 | ff3 | missing | ingame/map/MapEff.c |
| `00209610` | MenuDrawTexCtorBase | 1 | ff3 | missing | ingame/menu/menu_draw_tex.c |
| `002096d0` | MenuDrawTexDtorBase | 1 | ff3 | missing | ingame/menu/menu_draw_tex.c |
| `0010b248` | EditRootCtorLabelBase | 1 | ff2-ctx | missing | graphics/graph3d/CDebugEdit.cpp |
| `001f5ab0` | MapLoadHookNop | 1 | ff3 | missing | ingame/map/MapEff.c |
| `00203c90` | MemoNoticeSetLast | 1 | ff3 | missing | ingame/memo/memo.c |
| `00119e38` | MapDoorVecRelease | 1 | ff3 | missing | ingame/map/MapLight.c |
| `001f5528` | MapEffDeleteAll | 1 | ff3 | missing | ingame/map/MapEff.c |
| `001f5c98` | MapSnapMapToWorldX | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `001f5cb0` | MapSnapMapToWorldZ | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `00154e70` | CFileDtor | 1 | ff2-ctx | missing | graphics/graph3d/eeFile.cpp |
| `001f7348` | MapSnapStatLoad | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `001f6b38` | MapSnapCross2D | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `001f6c28` | MapSnapFindPlace | 1 | ff3 | missing | ingame/map/MapSnap.c |
| `002078b8` | MenuFileDetailPageCsrRelease | 1 | ff3 | missing | ingame/menu/menu_file.c |
| `002bb3b0` | __final_insertion_sort<LIGHTCOMPAREDATA*,LIGHTCOMPAREDATA::greater> | 1 | ff2 | missing | (unknown) |

