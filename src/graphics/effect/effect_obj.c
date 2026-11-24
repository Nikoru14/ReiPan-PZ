/* ==========================================================================
 *  graphics/effect/effect_obj.c
 *
 *  Object effects (FF3 0x162318..0x1764f8, out-of-line statics at
 *  0x2b12a0..0x2b5998; see effect_obj.h). Reconstructed so far: the SetEffects
 *  front ends of the candle flames, the many-candle altars (FF2 effect_oth.c's
 *  ManyCandle*, from MioPan), the halo (SubHalo), the thunder light, the seal
 *  door burn file, the room glow (an FF3-only bloom pass), kind 0x1e's four
 *  circling glows, the floating lanterns of rooms RS504 / RS506 / RS507 (map
 *  effect kind 600, FF3-only), the player's haze request, the kind 0x1d
 *  front ends, the candle flare / single flame draws, FF2 effect_obj.c's
 *  init / release (deform slots and the five model lists, their deletes)
 *  and pass 2 of the parts deform (SetPartsDeform, SubPartsDeform2, its four
 *  wave kernels, clip check and strip packet), pass 1, the water flow, the
 *  haze, kind 0x1d's particle cloud, the falling particles (list B), the
 *  map lamps' list (A) and the particle bursts' core (C), the rain, the
 *  kind 0x14 flame itself (SubCandleFlameDraw / Step, the blue flare), the
 *  lamp draws, the four particle draws, the lens flare (kind 8, FF2
 *  SetRenzFlare and SetStarRay) and the light come-in rates.
 *
 *  FF3 differences from FF2 noted per function. PC: the candle and glow
 *  packets are built as FF3 builds them (StartDmaDirectTrans), but nothing on
 *  the host walks a 3D effect's DIRECT chain: the candle flames and flares
 *  (EffHostQuad) also queue their quads through ReiPan_Graph3dDrawEffectQuad,
 *  MioPan's RendererPacket3DUV; the halo, kind 0x1e and the rest do not yet.
 *  The room glow renders into two work frames in GS memory (0x2bc0 / 0x3020)
 *  through FRAME_1 switches; the host has no equivalent yet either.
 * ======================================================================== */

#include <new>                          // placement new on FF3's C++ heap (operator_new)
#include "common/reipan_stub.h"
#include "graphics/effect/effect_obj.h"

#include <string.h>                     // memset

#include "graphics/effect/effect.h"     // EFFECT_CONT, SetEffects, ResetEffects, eff_wrk, EFFECT_MALLOC / FREE
#include "graphics/effect/effect_sub.h" // Get2PosRot, Get2PLength, WorldToScreenPos, the effect sprites and frame passes
#include "graphics/effect/effect_ene.h" // EneDmgLargeHitMakePacket
#include "graphics/effect/effect_oth.h" // ScreenFlashReq (screen flash)
#include "graphics/graph2d/tim2.h"      // MakeTim2Direct, MakeClutDirect
#include "graphics/graph2d/g2d_draw.h"  // StartDmaDirectTrans, EndDmaDirectTrans, SPRT_DAT, effdat
#include "graphics/graph3d/gra3d.h"     // gra3dGetCamera, gra3dcamGetPosition / Direction
#include "graphics/graph3d/gra3dSGD.h"  // gra3dChangeST, gra3dSetSGDMaterialAlpha
#include "graphics/gs_reg.h"            // GS_REG_SET, SetGsRegsCached
#include "common/vu0math.h"             // NormalizeAngle, Vu0Sinf, Vu0Cosf
#include "common/pk4.h"                 // Pk2GetData
#include "common/heapctrl.h"            // heapCtrlFree
#include "common/SingleLinkList.h"      // SingleLinkListFree
#include "system/os/sys_heap.h"         // GetSystemHeapWrkP
#include "system/os/eecdvd.h"           // LoadReq, IsLoadEnd
#include "system/eeiop/cddat.h"         // GetFileSize
#include "system/eeiop/cd_file_dat.h"   // FUUIN_*_TEX_PK4, ESE_RAKURAI_*, CH036_04_TM2
#include "system/eeiop/snd_util.h"      // snd_utilAutoBDPlay, SND_3D_SET
#include "ingame/sound/snd_ctrl.h"      // SndSePlay
#include "graphics/graph2d/g2d_debug.h" // DispSqrRect
#include "ingame/ingame.h"              // IngameGetDrawFlags
#include "ingame/ingame_wrk.h"          // ingame_wrk.plyr_wrk
#include "ingame/plyr/player.h"         // PLYR_WRK, VibrateRequest1
#include "ingame/plyr/plyr_sub.h"       // PlyrGetJointPos
#include "ingame/menu/anim_2d.h"        // LangTexSend
#include "reipan/os/reipan_rand.h"      // ReiPan_Rand, REIPAN_RAND_MAXF
#include "common/cpp_heap.h"            // operator_new, operator_delete
#include "graphics/graph3d/g3dLight.h"  // G3DLIGHT, g3dutilSetLightDefault
#include "main/gphase.h"                // GetGPhaseCtrl
#include "reipan/rendering/reipan_graph3d.h" // PC: ReiPan_Graph3dDrawEffectQuad / BeginWorldAnchor
#include "reipan/rendering/reipan_renderer.h" // PC: ReiPan_RendererGetViewBounds, EndAnchor
#include "system/os/system.h"           // GetSysWrk, GetPALMode
#include "ingame/camera/map_camera.h"   // MapCamGetEventCameraFlg
#include "ingame/map/MapLoad.h"         // MapLoadGetBuffID, MapLoadGetOffsetVector, MapLoadGetCurRoomAddr, MapLoadGetRoomData
#include "ingame/map/MapRcb.h"          // RcbLineHit
#include "ingame/map/MapPut.h"          // MapPutSetFunc, MapPutSetMatrixPtr, MapPutSetWork, MapPutGetWork, MapPutDelete
#include "ingame/map/MapLight.h"        // MapLightSetPlayerOnly
#include "ingame/map/map_hit_check.h"   // MapHitTourouPushOut
#include "ingame/movie_room_menu/prg/movie_projecter.h"  // IsMoviePhase
#include "ingame/story_door.h"          // MapAreaSideGet, MapAreaNoGet
#include "system/eeiop/file_cache.h"    // file_cache
#include <math.h>                       // fabsf
#include <libgraph.h>                   // SCE_GS_* register numbers

// ──────────────────────────────────────────────────────────────────────
// Callees in runs whose file is not settled yet (unassigned@ in tus.json).
// Move each block to its real header when that run is defined.
// ──────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────
// Data
// ──────────────────────────────────────────────────────────────────────

static MANY_CANDLE_LOAD_CTRL ManyCandleLoadCtrl;    // bss 0x3a6a40
static THUNDER_LIGHT_CTRL    ThunderLightCtrl;      // bss 0x3a6a80
static SEAL_DOOR_BURN_CTRL   SealDoorBurnCtrl;      // bss 0x3a7750 (FF2 DoorSealDisappearCtrl)
static sceVu0FVECTOR         eff_kind1e_pos;        // bss 0x3a7740: EffKind1EReq's copy of the position
static ROOM_GLOW_WRK         room_glow;             // bss 0x3a7870
static int  lantern_effect_on;                      // sdata gp-0x5ac (EffLanternEffectEnable)
static GameModel   *lantern_mdl;                    // sdata gp-0x5a8 (EffLanternMdlLoad)
static LANTERN_SLOT lantern_slot[2];                // bss 0x3a7900
static G3DLIGHT     lantern_light;                  // bss 0x3a7890 (EffLanternInit; no other reader found yet)

// The room glow's two work frames (sdata 0x36e240, gp-0x52b0): block
// addresses of 320 x 448 PSMCT24 buffers past the two display frames.
static u_int room_glow_buf[2] = { 0x2bc0, 0x3020 };

// The radii of the three blur passes (rdata 0x3536c0); each pass draws the
// other buffer eight times, at +-r diagonally and +-1.4r on the axes.
static const float room_glow_blur[3] = { 1.4f, 2.044f, 2.38f };
static void *kind1d_tex;                            // sdata gp-0x4d30: CH036_04_TM2's buffer (EffKind1DTexLoad)

// The seal door burn textures, by EffSealDoorBurnLoad's row (data 0x352e70).
static const int seal_door_burn_file[3] = {
    FUUIN_TEX_PK4, FUUIN_SP1_TEX_PK4, FUUIN_SP2_TEX_PK4,
};

// The two pictures of the burn pack (rdata 0x353678): the seal (TBP 0x2bc0)
// and its burn mask (TBP 0x2cc0), 256 x 256 PSMT8 with CLUTs at 0x2dc0 /
// 0x2dc4.
static const SPRT_DAT seal_door_burn_spr[2] = {
    { 0x2005b80621312bc0ULL, 0, 0, 0x100, 0x100, 0, 0, 0, 0x80, 0, 0 },
    { 0x2005b88621312cc0ULL, 0, 0, 0x100, 0x100, 0, 0, 0, 0x80, 0, 0 },
};

// The parts deform rows of the play data screen (data 0x2f88a8).
static EFF_PDEFORM_PRM eff_pdeform_prm[3] = {
    { 0x13, 0x47, { 0.39999998f, 0.79999995f }, { 0.39999998f, 0.39999998f }, { 0x80, 0x80, 0x80 } },
    { 0x18, 0x2b, { 0.39999998f, 0.79999995f }, { 0.864999f, 0.16999899f },  { 0x82, 0x91, 0x9b } },
    { 0x17, 0x37, { 0.5f, 1.0f },               { 1.0f, 1.0f },               { 0x60, 0x90, 0xa0 } },
};

// The candle flame quad in flame-local units (data 0x352f30; FF2
// CandlePolyDat): 1.93 high, 1.93 wide, y up from just below the wick.
static sceVu0FVECTOR CandlePolyDat[4] = {
    { 0.965643f, -0.136467f, 0.0f, 1.0f },
    { 0.965643f, 1.9368179f, 0.0f, 1.0f },
    { -0.965643f, -0.136467f, 0.0f, 1.0f },
    { -0.965643f, 1.9368179f, 0.0f, 1.0f },
};

// The flame's 150-frame width / height flicker (data 0x352f70; FF2
// CandleFlameScaleData).
static const float CandleFlameScaleData[150][2] = {
    { 0.999935f, 1.0001359f },
    { 1.0f, 1.000345f },
    { 1.0f, 1.003377f },
    { 1.0f, 1.009151f },
    { 1.0f, 1.017195f },
    { 1.0f, 1.027041f },
    { 1.0f, 1.038216f },
    { 1.0f, 1.0502509f },
    { 1.0f, 1.0502509f },
    { 1.0f, 1.075017f },
    { 1.0f, 1.086808f },
    { 1.0f, 1.0975759f },
    { 1.0f, 1.1068519f },
    { 1.0f, 1.114165f },
    { 1.0f, 1.119044f },
    { 1.0f, 1.1210189f },
    { 1.000003f, 1.119581f },
    { 1.0002279f, 1.111815f },
    { 1.000729f, 1.097273f },
    { 1.001395f, 1.077567f },
    { 1.0021169f, 1.054313f },
    { 1.002783f, 1.029125f },
    { 1.0032829f, 1.0036169f },
    { 1.0035069f, 0.979404f },
    { 1.0033439f, 0.95809996f },
    { 1.002684f, 0.94131994f },
    { 1.001416f, 0.93067795f },
    { 0.99943894f, 0.927556f },
    { 0.99688196f, 0.92798597f },
    { 0.993871f, 0.929917f },
    { 0.990464f, 0.933231f },
    { 0.98672295f, 0.93780994f },
    { 0.98270696f, 0.943536f },
    { 0.978476f, 0.95029294f },
    { 0.97409f, 0.957962f },
    { 0.96960795f, 0.96642596f },
    { 0.96509194f, 0.975567f },
    { 0.960601f, 0.985267f },
    { 0.956194f, 0.99540895f },
    { 0.95193297f, 1.005876f },
    { 0.947877f, 1.016549f },
    { 0.94408494f, 1.027311f },
    { 0.940618f, 1.0380449f },
    { 0.93753594f, 1.0486319f },
    { 0.934899f, 1.058955f },
    { 0.932767f, 1.0688969f },
    { 0.93119895f, 1.078339f },
    { 0.93025696f, 1.087165f },
    { 0.929999f, 1.095256f },
    { 0.930542f, 1.1033479f },
    { 0.93190897f, 1.112209f },
    { 0.93401796f, 1.1217189f },
    { 0.936786f, 1.1317569f },
    { 0.94013095f, 1.1422069f },
    { 0.94396895f, 1.1529479f },
    { 0.94821894f, 1.163862f },
    { 0.952797f, 1.1748279f },
    { 0.957621f, 1.1857289f },
    { 0.962608f, 1.1964439f },
    { 0.967676f, 1.2068549f },
    { 0.97274196f, 1.2168429f },
    { 0.97772294f, 1.226288f },
    { 0.982538f, 1.2350719f },
    { 0.987102f, 1.2430749f },
    { 0.99133396f, 1.2501789f },
    { 0.99514997f, 1.2562629f },
    { 0.998469f, 1.2612089f },
    { 1.001207f, 1.264898f },
    { 1.003282f, 1.2672099f },
    { 1.004612f, 1.2680279f },
    { 1.0051129f, 1.2672299f },
    { 1.0047079f, 1.2646669f },
    { 1.0034599f, 1.259617f },
    { 1.0014629f, 1.252013f },
    { 0.99878097f, 1.242114f },
    { 0.995478f, 1.2301769f },
    { 0.991615f, 1.216461f },
    { 0.98725796f, 1.2012229f },
    { 0.98247f, 1.184721f },
    { 0.977314f, 1.167213f },
    { 0.971854f, 1.1489569f },
    { 0.966154f, 1.130212f },
    { 0.96027595f, 1.111234f },
    { 0.95428395f, 1.0922829f },
    { 0.94824296f, 1.073615f },
    { 0.94221395f, 1.055488f },
    { 0.93626297f, 1.038162f },
    { 0.930452f, 1.0218929f },
    { 0.924845f, 1.0069389f },
    { 0.91950595f, 0.99355894f },
    { 0.91449696f, 0.98200995f },
    { 0.90988296f, 0.97255f },
    { 0.90572697f, 0.96543795f },
    { 0.902093f, 0.96093f },
    { 0.89897597f, 0.95946795f },
    { 0.896024f, 0.96209496f },
    { 0.893195f, 0.968533f },
    { 0.89050496f, 0.978301f },
    { 0.887976f, 0.99091595f },
    { 0.88562495f, 1.005898f },
    { 0.88347095f, 1.0227649f },
    { 0.881534f, 1.041034f },
    { 0.87983096f, 1.060226f },
    { 0.878383f, 1.079857f },
    { 0.877207f, 1.0994459f },
    { 0.876324f, 1.118513f },
    { 0.87575096f, 1.1365739f },
    { 0.87550795f, 1.1531489f },
    { 0.875613f, 1.167756f },
    { 0.876086f, 1.1799129f },
    { 0.87694496f, 1.1891389f },
    { 0.87820995f, 1.1949519f },
    { 0.879896f, 1.1969479f },
    { 0.881947f, 1.1970099f },
    { 0.884313f, 1.196172f },
    { 0.886974f, 1.1944859f },
    { 0.88990796f, 1.1920029f },
    { 0.89309394f, 1.188775f },
    { 0.89651096f, 1.184853f },
    { 0.900136f, 1.180288f },
    { 0.90394896f, 1.175133f },
    { 0.907928f, 1.1694379f },
    { 0.91205096f, 1.1632559f },
    { 0.916299f, 1.156637f },
    { 0.920648f, 1.1496329f },
    { 0.92507696f, 1.142296f },
    { 0.92956597f, 1.1346779f },
    { 0.92956597f, 1.1346779f },
    { 0.93863595f, 1.118802f },
    { 0.93863595f, 1.118802f },
    { 0.94768494f, 1.102417f },
    { 0.95214796f, 1.094162f },
    { 0.95654297f, 1.085935f },
    { 0.96084595f, 1.0777869f },
    { 0.96503794f, 1.06977f },
    { 0.96909595f, 1.061934f },
    { 0.972999f, 1.054331f },
    { 0.976726f, 1.047014f },
    { 0.98025596f, 1.040033f },
    { 0.983566f, 1.03344f },
    { 0.986636f, 1.0272869f },
    { 0.98944396f, 1.021624f },
    { 0.991969f, 1.0165039f },
    { 0.99418896f, 1.011979f },
    { 0.99608195f, 1.0080979f },
    { 0.997629f, 1.004915f },
    { 0.99880695f, 1.002481f },
    { 0.999594f, 1.0008459f },
    { 0.99996996f, 1.000063f },
    { 1.0f, 1.0006549f },
};

// The blue flame's 75-frame flicker (data 0x353420; FF2
// CandleFlameScaleData2).
static const float CandleFlameScaleData2[75][2] = {
    { 0.542632997f, 0.650425971f },
    { 0.480803996f, 0.561570942f },
    { 0.341887981f, 0.363259971f },
    { 0.195740998f, 0.157871991f },
    { 0.112219997f, 0.047789f },
    { 0.142718986f, 0.284503996f },
    { 0.251763999f, 0.66534096f },
    { 0.381288975f, 0.690425992f },
    { 0.473352998f, 0.690991998f },
    { 0.494225979f, 0.679895997f },
    { 0.497095972f, 0.659153998f },
    { 0.490185976f, 0.630783975f },
    { 0.475844979f, 0.596804976f },
    { 0.456423998f, 0.559233963f },
    { 0.456423998f, 0.559233963f },
    { 0.411745995f, 0.481389999f },
    { 0.391190976f, 0.445151985f },
    { 0.374958992f, 0.413393974f },
    { 0.365401f, 0.388134986f },
    { 0.364868999f, 0.371391982f },
    { 0.374498993f, 0.365182996f },
    { 0.388182998f, 0.371525973f },
    { 0.404469997f, 0.39169398f },
    { 0.423082978f, 0.423645973f },
    { 0.443744987f, 0.465727985f },
    { 0.466179997f, 0.516409993f },
    { 0.490107983f, 0.574158967f },
    { 0.515253961f, 0.637444973f },
    { 0.54133898f, 0.704733968f },
    { 0.568086982f, 0.774496973f },
    { 0.568086982f, 0.774496973f },
    { 0.622459948f, 0.915312946f },
    { 0.649530947f, 0.983302951f },
    { 0.676154971f, 1.04763794f },
    { 0.702054977f, 1.10678792f },
    { 0.72695297f, 1.15922093f },
    { 0.750571966f, 1.20340395f },
    { 0.772634983f, 1.23780692f },
    { 0.792864978f, 1.26086295f },
    { 0.810982943f, 1.27129197f },
    { 0.826713979f, 1.27028191f },
    { 0.839777946f, 1.25953794f },
    { 0.851111948f, 1.24076498f },
    { 0.861933947f, 1.21566689f },
    { 0.872253954f, 1.18594992f },
    { 0.882081985f, 1.15331793f },
    { 0.882081985f, 1.15331793f },
    { 0.900307f, 1.0861299f },
    { 0.908722997f, 1.0549829f },
    { 0.916689992f, 1.02774096f },
    { 0.924217999f, 1.00610793f },
    { 0.931315958f, 0.99179095f },
    { 0.937994957f, 0.984554946f },
    { 0.944265962f, 0.979067981f },
    { 0.950139999f, 0.974799991f },
    { 0.955624998f, 0.971657991f },
    { 0.960732996f, 0.969546974f },
    { 0.965474963f, 0.968373954f },
    { 0.965474963f, 0.968373954f },
    { 0.973898947f, 0.968464971f },
    { 0.973898947f, 0.968464971f },
    { 0.980978966f, 0.971174955f },
    { 0.980978966f, 0.971174955f },
    { 0.986799955f, 0.97575295f },
    { 0.986799955f, 0.97575295f },
    { 0.991443992f, 0.981444955f },
    { 0.991443992f, 0.981444955f },
    { 0.994992971f, 0.987498999f },
    { 0.994992971f, 0.987498999f },
    { 0.99753195f, 0.993160963f },
    { 0.99753195f, 0.993160963f },
    { 0.999142945f, 0.997677982f },
    { 0.999142945f, 0.997677982f },
    { 0.99990797f, 1.00029695f },
    { 1.0f, 1.00065994f },
};

// The flame packet's ST corners, half a texel in (data 0x368248).
static const float CandleFlameST[4][2] = {
    { 0.99309695f, 0.99309695f },
    { 0.99309695f, 0.0069029997f },
    { 0.0069029997f, 0.99309695f },
    { 0.0069029997f, 0.0069029997f },
};

// Kind 0x1e's four parts (data 0x2f8778; EffKind1EGetPart).
static EFF_KIND1E_PART eff_kind1e_part[4] = {
    { 1, { -0.22999999f, 0.25999898f, 0.0f }, { 128, 128, 128 }, 6.0f, 4.0f, 0.339999f, 0.19999999f, 0.12f, -0.45999998f, 3.1399999f, 1.0f, 0.32000098f, 0.03f, 0.73999995f, 157.0f },
    { 1, { -1.069999f, 0.679994f, 0.0f }, { 128, 171, 128 }, 8.0f, 4.0f, -0.50000095f, 0.33999997f, 0.12f, -0.041592997f, 0.24159399f, -1.0f, 0.460001f, 0.06f, -0.159999f, 218.0f },
    { 1, { 0.0f, 1.499998f, 0.0f }, { 128, 128, 128 }, 20.0f, 10.0f, -0.15840499f, 0.02f, 0.02f, 0.0f, 0.0f, 0.0f, 0.530001f, 0.049999997f, 0.54159397f, 261.0f },
    { 1, { 0.0f, 1.499998f, 0.0f }, { 128, 128, 128 }, 24.0f, 10.0f, 1.1399989f, 0.02f, 0.02f, 0.0f, 0.0f, 0.0f, 0.55000097f, 0.049999997f, 0.94000095f, 274.0f },
};

// The effect GS state the candles and kind 0x1e draw with: additive ALPHA
// 0x48, bilinear TEX1, CLAMP 0, TEST 0x5000d, ZBUF 0x10a000118 (z test, no
// z write).
static void EffSetAddDrawEnv(void)
{
    GS_REG_SET regs[5];

    regs[0].data = 0x48;          regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;         regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;             regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x5000d;       regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL; regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);
}

// rand() / RAND_MAX as the ROM writes it (2147483520.0f, rdata 0x36e100).
// callcheck: rand (PC: ReiPan_Rand, the ROM's 31-bit range)
static inline float EffRand01(void)
{
    return (float)ReiPan_Rand() / REIPAN_RAND_MAXF;
}

// ──────────────────────────────────────────────────────────────────────
// Init
// ──────────────────────────────────────────────────────────────────────

static void EffManyCandleLoadInit(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);

// InitEffects: FF2 InitEffectOth's last three calls (FF3's effect_oth.c
// keeps the rest of it).
// FF3 0x001622d0; FF2 InitEffectOth (MioPan effect_oth.c:418).

void EffectObjOthInit(void)
{
    EffManyCandleLoadInit(&ManyCandleLoadCtrl);
    EffectThunderLightInit();
    EffSealDoorBurnInit();
}

// InitEffectsEF: FF2 InitEffectOthEF's two clocks.
// FF3 0x001622f8; FF2 InitEffectOthEF (MioPan effect_oth.c:446).

void InitEffectObjEF(void)
{
    EffManyCandleLoadMain(&ManyCandleLoadCtrl);
    EffSealDoorBurnStep();
}

// EffectsReleaseAll: the altars and the seal door burn go. FF2's version
// reset the parts-blur bank and the tourou / light / water inits here; FF3
// moved those into InitEffects' own calls.
// FF3 0x00162318; FF2 InitEffectObj 0x00149e18.

void InitEffectObj(void)
{
    EffectManyCandleLoadCut();
    EffSealDoorBurnRelease();
}

// ──────────────────────────────────────────────────────────────────────
// The candle flames (kind 0x14)
// ──────────────────────────────────────────────────────────────────────

// FF2's two-colour flame request, flow 4: rgb / scl, then rgb2 / scl2, the
// fourth scale 0. Nothing in FF3 calls it.
// FF3 0x00162338; FF2 CallFire2 (mode 3 there).
// callcheck: fptodp (the double arguments)

void *CallFire2(void *pos, u_char r, u_char g, u_char b, float scl,
                u_char r2, u_char g2, u_char b2, float scl2)
{
    return SetEffects(0x14, 2, 4, pos, (int)r, (int)g, (int)b, (double)scl, (double)scl,
                      (int)r2, (int)g2, (int)b2, (double)scl2, 0.0, 0);
}

// A map candle (MapEffCreate types 0..3): white flame, flow 4, scales a..d.
// FF3 0x00162418.
// callcheck: fptodp (the double arguments)

void *EffCandleFireReq(float a, float b, float c, float d, float *pos)
{
    return SetEffects(0x14, 2, 4, pos, 0x80, 0x80, 0x80, (double)a, (double)b,
                      0x80, 0x80, 0x80, (double)c, (double)d, 0);
}

// The blue flame of MapEffCreate types 100 / 101: 0x30 0x30 0xff, max 1
// (SubCandleFlameStep's other jitter range). The matcher had FF2's
// CallPartsDeform2 here; that one is a PDEFORM request.
// FF3 0x001624c8.
// callcheck: fptodp (the double arguments)

void *EffBlueFireReq(float a, float b, float c, float d, float *pos)
{
    return SetEffects(0x14, 2, 4, pos, 0x30, 0x30, 0xff, (double)a, (double)b,
                      0x30, 0x30, 0xff, (double)c, (double)d, 1);
}

// The flame's width / height at frame (type 0: the candle, else the blue
// flame); frame is not checked.
// FF3 0x00162580; FF2 CandleFlameScaleWGet (effect_oth.c).

float CandleFlameScaleWGet(int type, int frame)
{
    if (type == 0)
        return CandleFlameScaleData[frame][0];
    return CandleFlameScaleData2[frame][0];
}

// FF3 0x001625a8; FF2 CandleFlameScaleHGet (effect_oth.c).

float CandleFlameScaleHGet(int type, int frame)
{
    if (type == 0)
        return CandleFlameScaleData[frame][1];
    return CandleFlameScaleData2[frame][1];
}

// A candle flame (kind 0x14) whose position lies within sqrt(800) of pos
// (pos raised 20) goes to flow 5 (6 when max is 1), cnt and keep cleared;
// flames in flow 1 .. 3 or 5 .. 7 are left alone. Flows 5 .. 7 are FF3's
// (SubCandleFlameStep, not reconstructed, runs them).
// FF3 0x00162f88.

void EffCandleFireNearCheck(void *eff, float *pos)
{
    EFFECT_CONT *ec = (EFFECT_CONT *)eff;
    const float *fpos;
    sceVu0FVECTOR d;

    if (ec == nullptr || ec->dat.uc8[0] != 0x14)
        return;
    if (ec->flow == 5 || ec->flow == 6 || ec->flow == 7 || ec->flow == 1 || ec->flow == 2 || ec->flow == 3)
        return;
    fpos = (const float *)ec->pnt[0];
    d[0] = fpos[0] - pos[0];                    // vsub.xyzw
    d[1] = fpos[1] - pos[1] + 20.0f;            // rdata 0x36e0f8
    d[2] = fpos[2] - pos[2];
    d[0] *= d[0];                               // vmul.xyzw
    d[1] *= d[1];
    d[2] *= d[2];
    if (d[0] + d[1] + d[2] <= 800.0f)           // rdata 0x36e0fc
    {
        ec->cnt = 0;
        ec->keep = 0;
        ec->flow = ec->max == 1 ? 6 : 5;
    }
}

// The flame flares up (FURN_CANDLE_BLAZE_UP): back to flow 1. FF3 checks the
// kind and no longer sets FF2's fw[1] / fw[2] = 25.
// FF3 0x00163068; FF2 EffOthCandleFlameFlareUpReq (effect_oth.c).

void EffOthCandleFlameFlareUpReq(void *eff)
{
    EFFECT_CONT *ec = (EFFECT_CONT *)eff;

    if (ec != nullptr && ec->dat.uc8[0] == 0x14)
    {
        ec->in = 0;
        ec->cnt = 0;
        ec->flow = 1;
    }
}

// EffectZSort's kind 0x14: a one-frame request ends, else the flame steps.
// FF2 set fw[0] = 1 and ran SubCandleFlame (step and draw); FF3 only steps
// here and draws from MapEffDrawFire (EffCandleFireDraw).
// FF3 0x00163090; FF2 SetFire (effect_oth.c).

void SetFire(EFFECT_CONT *ec)
{
    if (ec == nullptr || ec->dat.uc8[0] != 0x14)
        return;

    if (ec->dat.uc8[1] & 1)
    {
        ResetEffects(ec);
        return;
    }
    SubCandleFlameStep(ec);
}

// MapEffDrawFire's callback: draw the flame unless the frame hides effects.
// FF3 0x001630e0.

void EffCandleFireDraw(void *eff)
{
    EFFECT_CONT *ec = (EFFECT_CONT *)eff;

    if (ec == nullptr || ec->dat.uc8[0] != 0x14)
        return;
    if (IngameGetDrawFlags() & 0x10)
        return;
    if ((u_char)eff_wrk.disp_flg == 0)  // read as a byte
        return;
    SubCandleFlameDraw(ec);
}

// ──────────────────────────────────────────────────────────────────────
// The many-candle altars (kind 0x1c; from MioPan effect_oth.c)
// ──────────────────────────────────────────────────────────────────────

// One altar of the pack: header, then DataNum candle positions.
struct MANY_CANDLE_PAK                  /* 0x10 + 0x10 * DataNum */
{
    /* 0x00 */ int _00;
    /* 0x04 */ int DataNum;
    /* 0x08 */ int _08[2];
    /* 0x10 */ sceVu0FVECTOR Pos[1];    // [DataNum]
};
static_assert(offsetof(MANY_CANDLE_PAK, DataNum) == 0x04, "MANY_CANDLE_PAK.DataNum");
static_assert(offsetof(MANY_CANDLE_PAK, Pos) == 0x10, "MANY_CANDLE_PAK.Pos");

// PK2 +0: the member count (common/pk4.h); a null pack has none. FF3 inlines
// it at both reads.
static inline int EffPk2Num(const void *pk2)
{
    return (pk2 != nullptr) ? *(const int *)pk2 : 0;
}

// FF3 0x00163150; FF2 EffectManyCandleLoadInit (MioPan effect_oth.c:411).

static void EffManyCandleLoadInit(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    int i;

    pLoadCtrl->Status = 0;
    pLoadCtrl->LoadId = -1;
    pLoadCtrl->pLoadBuf = nullptr;
    pLoadCtrl->PackNum = 0;
    for (i = 4; i >= 0; i--)
        pLoadCtrl->pEffRet[i] = nullptr;
}

// Poll the load: 1 -> effects (2) when it lands; 3 (cut while loading) ->
// freed when it lands.
// FF3 0x00163190; FF2 EffectManyCandleLoadMain (MioPan effect_oth.c:429).

static void EffManyCandleLoadAllEffectReq(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);
static void EffManyCandleLoadEnd(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);

void EffManyCandleLoadMain(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    if (pLoadCtrl->Status == 2)
        return;

    if (pLoadCtrl->Status == 1)
    {
        if (IsLoadEnd(pLoadCtrl->LoadId) != 0)
        {
            EffManyCandleLoadAllEffectReq(pLoadCtrl);
            pLoadCtrl->Status = 2;
        }
    }
    else if (pLoadCtrl->Status == 3)
    {
        if (IsLoadEnd(pLoadCtrl->LoadId) != 0)
            EffManyCandleLoadEnd(pLoadCtrl);
    }
}

// Start the altar pack load (CANDLEPOS_PK2) unless one is under way; a
// failed allocation leaves Status 0. Nothing in FF3 calls it.
// FF3 0x00163230; FF2 EffectManyCandleLoadReq (MioPan effect_oth.c:450).
// callcheck: EFFECT_MALLOC (read as a macro)

void EffectManyCandleLoadReq(float *Offset)
{
    if (ManyCandleLoadCtrl.Status != 0)
        return;

    ManyCandleLoadCtrl.pLoadBuf = EFFECT_MALLOC(0x2300);
    if (ManyCandleLoadCtrl.pLoadBuf != nullptr)
    {
        ManyCandleLoadCtrl.LoadId = LoadReq(CANDLEPOS_PK2, ManyCandleLoadCtrl.pLoadBuf);
        ManyCandleLoadCtrl.Status = 1;
        ManyCandleLoadCtrl.Offset[0] = Offset[0];
        ManyCandleLoadCtrl.Offset[1] = Offset[1];
        ManyCandleLoadCtrl.Offset[2] = Offset[2];
        ManyCandleLoadCtrl.Offset[3] = Offset[3];
    }
}

// Cancel: mid-load only 1 -> 3; running, every altar is cut first.
// FF3 0x001632a0; FF2 EffectManyCandleLoadCut (MioPan effect_oth.c:471).

void EffectManyCandleLoadCut(void)
{
    int i;

    if (ManyCandleLoadCtrl.Status == 0)
        return;

    if (ManyCandleLoadCtrl.Status == 1)
    {
        ManyCandleLoadCtrl.Status = 3;
    }
    else if (ManyCandleLoadCtrl.Status == 2)
    {
        for (i = 0; i < ManyCandleLoadCtrl.PackNum; i++)
            EffectManyCandleCut(ManyCandleLoadCtrl.pEffRet[i]);
    }
    EffManyCandleLoadEnd(&ManyCandleLoadCtrl);
}

// One altar effect per pack member; the count is re-read every iteration.
// FF3 0x00163348; FF2 EffectManyCandleLoadAllEffectReq (MioPan effect_oth.c:498).

static void EffManyCandleLoadAllEffectReq(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    MANY_CANDLE_PAK *pak;
    int i;

    pLoadCtrl->PackNum = EffPk2Num(pLoadCtrl->pLoadBuf);
    for (i = 0; i < EffPk2Num(pLoadCtrl->pLoadBuf); i++)
    {
        pak = (MANY_CANDLE_PAK *)Pk2GetData(pLoadCtrl->pLoadBuf, i);
        pLoadCtrl->pEffRet[i] = EffManyCandleReq(pLoadCtrl->Offset, pak->Pos[0], pak->DataNum, i);
    }
}

// FF3 0x001633e0; FF2 EffectManyCandleLoadEnd (MioPan effect_oth.c:517).
// callcheck: EFFECT_FREE (read as a macro)

static void EffManyCandleLoadEnd(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    EFFECT_FREE(pLoadCtrl->pLoadBuf);
    EffManyCandleLoadInit(pLoadCtrl);
}

// EffectZSort's kind 0x1c: the altar block is made on the first frame
// (pnt[0] the centre, pnt[2] the positions, iv[1] DataNum, iv[2] Id), then
// stepped and drawn; a failed allocation or a finished altar ends the effect.
// FF3 0x00163410; FF2 SetManyCandle (MioPan effect_oth.c:6258).
// callcheck: EFFECT_MALLOC (read as a macro)

static void  EffManyCandleCtrlInit(MANY_CANDLE_CTRL *pMc, float *pCenterPos, float *pParticlePos, int DataNum, int Id);
static void *EffManyCandleCtrl(MANY_CANDLE_CTRL *pCandleCtrl);

void SetManyCandle(EFFECT_CONT *ec)
{
    MANY_CANDLE_CTRL *pMc;

    if (ec->pnt[1] == nullptr)
    {
        pMc = (MANY_CANDLE_CTRL *)EFFECT_MALLOC(sizeof(MANY_CANDLE_CTRL));
        ec->pnt[1] = pMc;
        if (pMc == nullptr)
            ResetEffects(ec);
        else
            EffManyCandleCtrlInit(pMc, (float *)ec->pnt[0], (float *)ec->pnt[2],
                                  ec->dat.i32[1], ec->dat.i32[2]);
    }

    ec->pnt[1] = EffManyCandleCtrl((MANY_CANDLE_CTRL *)ec->pnt[1]);
    if (ec->pnt[1] == nullptr)
        ResetEffects(ec);
}

// FF3 0x00163498; FF2 EffectManyCandleReq (MioPan effect_oth.c:524).

void *EffManyCandleReq(float *pCenterPos, float *pPos, int DataNum, int Id)
{
    return SetEffects(0x1c, 2, pCenterPos, (void *)pPos, DataNum, Id);
}

// FF3 0x001634c8; FF2 EffectManyCandleCut (MioPan effect_oth.c:533).
// callcheck: EFFECT_FREE (read as a macro)

void EffectManyCandleCut(void *pEffect)
{
    if (pEffect != nullptr)
    {
        EFFECT_FREE(((EFFECT_CONT *)pEffect)->pnt[1]);
        ResetEffects(pEffect);
    }
}

// Lay out one altar: centre, Id, colour 0x80 0x80 0x82, the positions with
// y and z negated (FF3 drops FF2's x25: the pak data is in world units now),
// each candle's flicker started at a random frame.
// FF3 0x00163508; FF2 ManyCandleCtrlInit (MioPan effect_oth.c:2225).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffManyCandleCtrlInit(MANY_CANDLE_CTRL *pMc, float *pCenterPos, float *pParticlePos, int DataNum, int Id)
{
    int i;

    if (pMc == nullptr)
        return;

    pMc->CenterPos[0] = pCenterPos[0];
    pMc->CenterPos[1] = pCenterPos[1];
    pMc->CenterPos[2] = pCenterPos[2];
    pMc->CenterPos[3] = pCenterPos[3];
    pMc->Id = Id;
    pMc->G = 0x80;
    pMc->B = 0x82;
    pMc->R = 0x80;
    pMc->DataNum = DataNum;

    for (i = 0; i < pMc->DataNum; i++)
    {
        MANY_CANDLE_PARTICLE *p = &pMc->Particles[i];

        p->Position[0] = pParticlePos[i * 4 + 0];
        p->Position[1] = pParticlePos[i * 4 + 1];
        p->Position[2] = pParticlePos[i * 4 + 2];
        p->Position[3] = pParticlePos[i * 4 + 3];
        p->Alpha = 0;
        p->Position[1] = -p->Position[1];
        p->Position[2] = -p->Position[2];
        p->Count = (int)(EffRand01() * 149.0f);     // rdata 0x36e104
    }
}

// PORT-ONLY. The host draw of one candle quad: the DIRECT packets below are
// built as FF3 builds them, but nothing on the host runs a 3D effect's
// chain, so the same quad is queued from its corners in local space (lw:
// local -> world) through the camera's world -> clip matrix, as MioPan's
// CandleFlameHostQuad does. st is the packet's own table, in strip order.

static void EffHostQuad(sceVu0FMATRIX lw, sceVu0FVECTOR *corners, const float (*st)[2], u_long tex0,
                        int R, int G, int B, int Alpha)
{
    sceVu0FVECTOR world[4];
    int i;

    for (i = 0; i < 4; i++)
        sceVu0ApplyMatrix(world[i], lw, corners[i]);
    ReiPan_Graph3dDrawEffectQuad(world, 4, &st[0][0], R, G, B, Alpha, tex0, 0);
}

// One candle's flame packet: a 4-vertex textured strip (PRIM 0x5c), every
// vertex at depth Depth (Depth < 0: its own z), the first two primers (ADC).
// FF3 0x002b12a0; FF2 CandleFlameMakePacket (MioPan effect_oth.c:2160).

static void CandleFlameMakePacket(sceVu0IVECTOR *pIvec, u_long Tex0, int Depth, int R, int G, int B, int Alpha)
{
    u_long *p = (u_long *)StartDmaDirectTrans();
    u_int  *q;
    float   tq;
    int     i;

    p[4] = Tex0;
    p[2] = 0;
    p[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);     // 0x1000000000008002
    p[1] = SCE_GIF_PACKED_AD;
    p[3] = SCE_GS_TEXFLUSH;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x302e400000008004ULL;       // 4 loops, PRIM 0x5c (strip, gouraud, textured, blended), 3 regs
    p[7] = 0x412;                       // ST, RGBAQ, XYZF2

    q = (u_int *)(p + 8);
    for (i = 0; i < 4; i++)
    {
        float *f = (float *)q;

        tq = 1.0f / (float)pIvec[i][3];     // rdata 0x36e1b0
        f[3] = 0.0f;
        f[2] = tq;
        f[0] = CandleFlameST[i][0] * tq;
        f[1] = CandleFlameST[i][1] * tq;
        q[4] = (u_int)R;
        q[5] = (u_int)G;
        q[6] = (u_int)B;
        q[7] = (u_int)Alpha;
        q[8] = (u_int)pIvec[i][0];
        q[9] = (u_int)pIvec[i][1];
        q[10] = (Depth < 0) ? (u_int)pIvec[i][2] : (u_int)Depth;
        q[11] = (i < 2) ? 0x8000 : 0;
        q += 12;
    }
    EndDmaDirectTrans((qword *)(p + 0x20));
}

// The flare's ST corners, half a texel in (data 0x352e10).
static const float CandleFreaST[4][2] = {
    { 0.0029819999f, 0.99701798f },
    { 0.0029819999f, 0.0029819999f },
    { 0.99701798f, 0.99701798f },
    { 0.99701798f, 0.0029819999f },
};

// A candle's flare: a square 8.53 * scale wide facing the camera (turned by
// Get2PosRot from the camera to pos), pushed h * scale along the view axis,
// texture effdat[0x46 + mono]. The first corner outside the guard band
// (x / y 0x4000..0xc000, z 1..0xffffff) drops it. Every vertex at depth
// Depth (< 0: its own z); the packet is CandleFlameMakePacket's, written out
// again with this ST table.
// FF3 0x001625d0.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

void EffCandleFreaDraw(float scale, float h, float *pos, int Depth, int R, int G, int B, int Alpha)
{
    static sceVu0FMATRIX wlm;           // bss 0x3a6ac0 (shared with SubHalo, EffCandleFlameOneDraw)
    static sceVu0FMATRIX slm;           // bss 0x3a6b00
    static sceVu0FMATRIX unit = {       // data 0x3293c0
        { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    sceVu0FVECTOR vpos[4];
    sceVu0IVECTOR ivec[4];
    GRA3DCAMERA *pCam;
    float k0 = scale * -4.262784f;      // rdata 0x36e0cc
    float k1 = scale * 4.262784f;       // rdata 0x36e0c8
    float hz = h * scale;
    float rot_x, rot_y;
    u_long *p;
    u_int *q;
    float tq;
    int i;

    vpos[0][0] = k0; vpos[0][1] = k0; vpos[0][2] = hz; vpos[0][3] = 1.0f;   // rdata 0x36e0c4
    vpos[1][0] = k0; vpos[1][1] = k1; vpos[1][2] = hz; vpos[1][3] = 1.0f;
    vpos[2][0] = k1; vpos[2][1] = k0; vpos[2][2] = hz; vpos[2][3] = 1.0f;
    vpos[3][0] = k1; vpos[3][1] = k1; vpos[3][2] = hz; vpos[3][3] = 1.0f;

    pCam = gra3dGetCamera();
    Get2PosRot(gra3dcamGetPosition(), pos, &rot_x, &rot_y);
    sceVu0RotMatrixX(wlm, unit, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += pos[0];                // vadd.xyz: sceVu0TransMatrix
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(ivec[i], slm, vpos[i], 0);
        if ((u_int)(ivec[i][0] - 0x4000) > 0x8000 || (u_int)(ivec[i][1] - 0x4000) > 0x8000
            || (u_int)(ivec[i][2] - 1) > 0xfffffe)
            return;
    }

    p = (u_long *)StartDmaDirectTrans();
    p[2] = 0;
    p[1] = SCE_GIF_PACKED_AD;
    p[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);     // 0x1000000000008002
    p[3] = SCE_GS_TEXFLUSH;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x302e400000008004ULL;       // 4 loops, PRIM 0x5c (strip, gouraud, textured, blended), 3 regs
    p[7] = 0x412;                       // ST, RGBAQ, XYZF2
    p[4] = effdat[eff_wrk.monochro_mode + 0x46].tex0;

    q = (u_int *)(p + 8);
    for (i = 0; i < 4; i++)
    {
        float *f = (float *)q;

        tq = 1.0f / (float)ivec[i][3];  // rdata 0x36e0c4
        f[3] = 0.0f;
        f[2] = tq;
        f[0] = CandleFreaST[i][0] * tq;
        f[1] = CandleFreaST[i][1] * tq;
        q[4] = (u_int)R;
        q[5] = (u_int)G;
        q[6] = (u_int)B;
        q[7] = (u_int)Alpha;
        q[8] = (u_int)ivec[i][0];
        q[9] = (u_int)ivec[i][1];
        q[10] = (Depth < 0) ? (u_int)ivec[i][2] : (u_int)Depth;
        q[11] = (i < 2) ? 0x8000 : 0;
        q += 12;
    }
    EndDmaDirectTrans((qword *)(p + 0x20));

    EffHostQuad(wlm, vpos, CandleFreaST, p[4], R, G, B, Alpha);  // PC
}

// The blue flame's flare (SubCandleFlameDraw when max is 1): the same square
// as EffCandleFreaDraw's, 8.53 * scale wide, h * scale along the view axis,
// texture effdat[0x46 + mono], but cut into a 7 x 7 grid of fans over 8 x 8
// vertices (bss 0x3a6b40), each vertex with its own STQ, and only the cells
// whose four corners stay inside the guard band are drawn (EffCandleFreaDraw
// drops the whole flare). The grid runs t with y, the reverse of CandleFreaST.
// FF3 0x001629b8.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

struct EFF_FREA_GRID_VTX                /* 0x30 */
{
    /* 0x00 */ sceVu0IVECTOR iv;
    /* 0x10 */ float s;
    /* 0x14 */ float t;
    /* 0x18 */ int   clip;              // 1: outside the guard band
    /* 0x1c */ float S;                 // s / w
    /* 0x20 */ float T;                 // t / w
    /* 0x24 */ float Q;                 // 1 / w
    /* 0x28 */ int   _28[2];
};
static_assert(sizeof(EFF_FREA_GRID_VTX) == 0x30, "EFF_FREA_GRID_VTX");
static_assert(offsetof(EFF_FREA_GRID_VTX, s) == 0x10, "EFF_FREA_GRID_VTX.s");
static_assert(offsetof(EFF_FREA_GRID_VTX, clip) == 0x18, "EFF_FREA_GRID_VTX.clip");
static_assert(offsetof(EFF_FREA_GRID_VTX, S) == 0x1c, "EFF_FREA_GRID_VTX.S");
static_assert(offsetof(EFF_FREA_GRID_VTX, Q) == 0x24, "EFF_FREA_GRID_VTX.Q");

void EffBlueFireFreaDraw(float scale, float h, float *pos, int Depth, int R, int G, int B, int Alpha)
{
    static sceVu0FMATRIX wlm;           // bss 0x3a6ac0 (shared with EffCandleFreaDraw)
    static sceVu0FMATRIX slm;           // bss 0x3a6b00
    static EFF_FREA_GRID_VTX grid[64];  // bss 0x3a6b40
    static sceVu0FMATRIX unit = {       // data 0x3293c0
        { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    static sceVu0FVECTOR world[64];     // PC: the host draw's corners
    static const int strip[4] = { 0, 1, 3, 2 };     // PC: fan corners in strip order
    GRA3DCAMERA *pCam;
    sceVu0FVECTOR v;
    float rot_x, rot_y;
    float hz, y, t;
    u_long *p;
    u_int *q;
    int qw;
    int i, j, k;

    pCam = gra3dGetCamera();
    Get2PosRot(gra3dcamGetPosition(), pos, &rot_x, &rot_y);
    sceVu0RotMatrixX(wlm, unit, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += pos[0];                // vadd.xyz
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    hz = h * scale;
    for (i = 0; i < 8; i++)
    {
        y = ((float)i * 8.525568f / 7.0f + -4.262784f) * scale;    // rdata 0x36e0e0 / 0x36e0e4
        t = (float)i * 0.99403596f / 7.0f + 0.0029819999f;          // rdata 0x36e0f4 / 0x36e0e8
        for (j = 0; j < 8; j++)
        {
            EFF_FREA_GRID_VTX *g = &grid[i * 8 + j];

            g->t = t;
            v[0] = ((float)j * 8.525568f / 7.0f + -4.262784f) * scale;   // rdata 0x36e0ec
            v[1] = y;
            v[2] = hz;
            v[3] = 1.0f;                // rdata 0x36e0f0
            g->s = (float)j * 0.99403596f / 7.0f + 0.0029819999f;
            sceVu0RotTransPers(g->iv, slm, v, 0);
            sceVu0ApplyMatrix(world[i * 8 + j], wlm, v);    // PC
            g->Q = 1.0f / (float)g->iv[3];
            g->T = g->t * g->Q;
            g->S = g->s * g->Q;
            g->clip = (u_int)(g->iv[0] - 0x4000) > 0x8000 || (u_int)(g->iv[1] - 0x4000) > 0x8000
                      || (u_int)(g->iv[2] - 1) > 0xfffffe;
        }
    }

    p = (u_long *)StartDmaDirectTrans();
    p[2] = 0;
    p[1] = SCE_GIF_PACKED_AD;
    p[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);     // 0x1000000000008002
    p[3] = SCE_GS_TEXFLUSH;
    p[5] = SCE_GS_TEX0_1;
    p[4] = effdat[eff_wrk.monochro_mode + 0x46].tex0;
    qw = 3;
    for (i = 0; i < 7; i++)
    {
        for (j = 0; j < 7; j++)
        {
            // The fan's corners, around the cell.
            int c[4] = { i * 8 + j, i * 8 + j + 1, i * 8 + j + 9, i * 8 + j + 8 };
            sceVu0FVECTOR wq[4];
            float st[4][2];

            for (k = 0; k < 4; k++)
            {
                if (grid[c[k]].clip != 0)
                    break;
            }
            if (k < 4)
                continue;

            p[qw * 2 + 0] = 0x302ec00000008004ULL;  // 4 loops, PRIM 0x5d (fan, gouraud, textured, blended), 3 regs
            p[qw * 2 + 1] = 0x412;                  // ST, RGBAQ, XYZF2
            qw++;
            q = (u_int *)(p + qw * 2);
            for (k = 0; k < 4; k++)
            {
                EFF_FREA_GRID_VTX *g = &grid[c[k]];
                float *f = (float *)q;

                f[0] = g->S;
                f[1] = g->T;
                f[2] = g->Q;
                q[3] = 0;
                q[4] = (u_int)R;
                q[5] = (u_int)G;
                q[6] = (u_int)B;
                q[7] = (u_int)Alpha;
                q[8] = (u_int)g->iv[0];
                q[9] = (u_int)g->iv[1];
                q[10] = (Depth < 0) ? (u_int)g->iv[2] : (u_int)Depth;
                q[11] = (k < 2) ? 0x8000 : 0;
                q += 12;
                qw += 3;
            }

            for (k = 0; k < 4; k++)     // PC: the host takes strips
            {
                const EFF_FREA_GRID_VTX *g = &grid[c[strip[k]]];

                memcpy(wq[k], world[c[strip[k]]], sizeof(sceVu0FVECTOR));
                st[k][0] = g->s;
                st[k][1] = g->t;
            }
            ReiPan_Graph3dDrawEffectQuad(wq, 4, &st[0][0], R, G, B, Alpha, p[4], 0);
        }
    }
    EndDmaDirectTrans((qword *)(p + qw * 2));
}

// One candle flame: CandlePolyDat scaled by the flicker row cnt times scale
// (y negated: the quad hangs from the wick in camera space), turned to the
// camera's yaw (Get2PosRot from the camera to its target), at pos; texture
// effdat[0x36 + mono], grey (the mean of r, g, b) in mono. Drawn at the
// first corner's depth; dropped when a corner leaves the guard band. FF2
// ManyCandleCtrl's per-candle body.
// FF3 0x00163e50.

void EffCandleFlameOneDraw(float scale, float *pos, int R, int G, int B, int Alpha, int cnt)
{
    static sceVu0FMATRIX wlm;           // bss 0x3a6ac0 (shared with SubHalo, EffCandleFreaDraw)
    static sceVu0FMATRIX slm;           // bss 0x3a6b00
    sceVu0FMATRIX m;
    sceVu0IVECTOR ivec[4];
    GRA3DCAMERA *pCam;
    u_long tex0;
    float rot_x, rot_y;
    int i;

    pCam = gra3dGetCamera();
    float *cam_pos = gra3dcamGetPosition();
    tex0 = effdat[eff_wrk.monochro_mode + 0x36].tex0;
    if (eff_wrk.monochro_mode != 0)
    {
        G = (R + G + B) / 3;
        B = G;
        R = G;
    }
    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);

    sceVu0UnitMatrix(m);                // built inline (vf0 rotations)
    m[0][0] = CandleFlameScaleData[cnt][0] * scale;
    m[2][2] = CandleFlameScaleData[cnt][0] * scale;
    m[1][1] = -CandleFlameScaleData[cnt][1] * scale;
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    memcpy(wlm, m, sizeof(wlm));
    wlm[3][0] += pos[0];                // vadd.xyz
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(ivec[i], slm, CandlePolyDat[i], 0);
        if ((u_int)(ivec[i][0] - 0x4000) > 0x8000 || (u_int)(ivec[i][1] - 0x4000) > 0x8000
            || (u_int)(ivec[i][2] - 1) > 0xfffffe)
            return;
    }
    CandleFlameMakePacket(ivec, tex0, ivec[0][2], R, G, B, Alpha);
    EffHostQuad(wlm, CandlePolyDat, CandleFlameST, tex0, R, G, B, Alpha);  // PC
}

// The kind 0x14 flame's draw, FF2 SubCandleFlame's first half (FF3 draws it
// from MapEffDrawFire through EffCandleFireDraw and steps it apart, in
// SubCandleFlameStep). Two quads of CandlePolyDat -- one in flows 5 / 6 --
// turned pi about X (upright) and to the yaw from the camera to the flame,
// at pnt[0], scaled by fw[0] (width) / fw[1] (height) times the flow's
// envelope: flow 1 (flare up) a sine bulge over 6 frames, 2 a random tall
// flame, 3 the settle, 4 / 7 the 150 / 75-frame flicker tables with the
// second quad five frames behind and its alpha dipping 0x80 -> 0x40 -> 0x80
// over in, 5 / 6 the near-flicker frames. The first corner outside the guard
// band drops the whole draw, flare included. Then the flare at pnt[0] with
// y - 1.2 (scale fw[2], height fw[3], alpha the jitter a; the blue
// flame its grid flare). Colours dat.uc8[2..4] for the flame, [5..7] for the
// flare, the blue flame (max 1) fixed 0x26 0x26 0x46 / 0x3f 0x69 0xb5, grey
// in mono. Textures effdat[0x36 / 0x38 + mono], flows 5 / 6
// effdat[0x18 + 2 * keep + mono].
// FF2 differences: the yaw aims at the flame, not the camera target; the
// flare has its own colour; flows 5..7 and the blue flame are new; FF2's
// fw[1] / fw[2] scale carry-over and the step are gone.
// FF3 0x002b1420; FF2 SubCandleFlame (MioPan effect_oth.c:1789).
// callcheck: SetGsRegsCached (in EffSetAddDrawEnv)

// Flow 6's texture row (sdata 0x36e0c0, gp-0x5430). The ROM indexes it by
// keep, which is 0 whenever flow 6 draws (SubCandleFlameStep leaves flow 6 in
// the step that makes it 1), so only this entry is ever read.
static const int CandleFlameFlow6Tex[1] = { 0 };

void SubCandleFlameDraw(EFFECT_CONT *ec)
{
    static sceVu0FMATRIX wlm;           // bss 0x3a6ac0 (shared with EffCandleFreaDraw)
    static sceVu0FMATRIX slm;           // bss 0x3a6b00
    static sceVu0FMATRIX unit = {       // data 0x3293c0
        { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    sceVu0FMATRIX LocalScreen[2];
    sceVu0FMATRIX ScaleMatrix[2];
    sceVu0IVECTOR ivec[2][4];
    sceVu0FVECTOR FreaPos;
    float ScaleW[2];
    float ScaleH[2];
    int Alpha[2];
    u_long tex0[2];
    float rot_x, rot_y;
    float sw, sh, frea_scale, frea_h;
    float sn, hs;
    float *pos;
    float *cam_pos;
    GRA3DCAMERA *pCam;
    u_int r, g, b, fr, fg, fb;
    int DispFlameNum = 2;
    int blue;
    int type, loop, back;
    int mono;
    int i, j;

    blue = (int)ec->max;
    sw = ec->fw[0];
    sh = ec->fw[1];
    frea_scale = ec->fw[2];
    pCam = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();
    r = ec->dat.uc8[2];
    g = ec->dat.uc8[3];
    b = ec->dat.uc8[4];
    fr = ec->dat.uc8[5];
    fg = ec->dat.uc8[6];
    fb = ec->dat.uc8[7];
    frea_h = ec->fw[3];
    if (blue == 1)
    {
        r = 0x26;
        g = 0x26;
        b = 0x46;
        fr = 0x3f;
        fg = 0x69;
        fb = 0xb5;
    }
    mono = eff_wrk.monochro_mode;
    if (mono != 0)
    {
        fr = ((fr + fg + fb) / 3) & 0xff;
        r = ((r + g + b) / 3) & 0xff;
        g = r;
        b = r;
        fg = fr;
        fb = fr;
    }
    tex0[1] = effdat[mono + 0x38].tex0;
    tex0[0] = effdat[mono + 0x36].tex0;
    for (i = 0; i < 2; i++)
    {
        ScaleW[i] = 1.0f;               // rdata 0x36e1b4
        ScaleH[i] = 1.0f;
        Alpha[i] = 0x80;
    }

    switch (ec->flow)
    {
    case 1:
        sn = Vu0Sinf(((float)ec->cnt / 6.0f) * 1.5707964f);       // rdata 0x36e1b8 / 0x36e1c4
        ScaleW[0] = sn * -0.10000002f + 1.0f;                       // rdata 0x36e1c8
        hs = sn * 9.0f + 1.0f;                                      // rdata 0x36e1cc
        ScaleH[0] = (GetRandValF(0.79999995f) + 0.25f) * hs;        // rdata 0x36e1bc / 0x36e1c0
        ScaleW[1] = ScaleW[0];
        ScaleH[1] = GetRandValF(0.79999995f) + 0.25f + hs;          // ROM: added, not multiplied
        break;
    case 2:
        ScaleW[0] = 0.9f;               // rdata 0x36e1d0
        ScaleH[0] = (GetRandValF(0.79999995f) + 0.25f) * 10.0f;     // rdata 0x36e1d4
        ScaleW[1] = 0.9f;
        ScaleH[1] = (GetRandValF(0.79999995f) + 0.25f) * 10.0f;
        break;
    case 3:
        sn = Vu0Sinf((float)ec->cnt * 0.125f * 1.5707964f);         // rdata 0x36e1d8
        ScaleW[0] = sn * 0.10000002f + 0.9f;                        // rdata 0x36e1dc
        hs = sn * -9.0f + 10.0f;                                    // rdata 0x36e1e0
        ScaleH[0] = (GetRandValF(0.79999995f) + 0.25f) * hs;
        ScaleW[1] = ScaleW[0];
        ScaleH[1] = (GetRandValF(0.79999995f) + 0.25f) * hs;
        break;
    case 4:
    case 7:
        type = (ec->flow != 4) ? 1 : 0;
        loop = (ec->flow != 4) ? 75 : 150;
        back = (int)ec->cnt - 5;        // the second quad, five frames behind, wrapped
        if (back < 0)
            back += loop;
        ScaleW[0] = CandleFlameScaleWGet(type, (int)ec->cnt);
        ScaleH[0] = CandleFlameScaleHGet(type, (int)ec->cnt);
        ScaleW[1] = CandleFlameScaleWGet(type, back);
        ScaleH[1] = CandleFlameScaleHGet(type, back);
        if (ec->in < 10)
            Alpha[1] = -(int)(ec->in * 0x40) / 10 + 0x80;
        else
            Alpha[1] = (int)((ec->in * 0x40 - 0x280) / 10) + 0x40;
        break;
    case 5:
        DispFlameNum = 1;
        tex0[0] = effdat[(int)ec->keep * 2 + mono + 0x18].tex0;
        tex0[1] = tex0[0];
        break;
    case 6:
        DispFlameNum = 1;
        tex0[0] = effdat[CandleFlameFlow6Tex[0] * 2 + mono + 0x18].tex0;
        tex0[1] = tex0[0];
        break;
    }

    pos = (float *)ec->pnt[0];
    Get2PosRot(cam_pos, pos, &rot_x, &rot_y);
    sceVu0RotMatrixX(wlm, unit, NormalizeAngle(3.1415927f));       // rdata 0x36e1e4
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += pos[0];                // vadd.xyz
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    for (i = 0; i < DispFlameNum; i++)
    {
        sceVu0UnitMatrix(ScaleMatrix[i]);   // built inline (vf0 rotations)
        ScaleMatrix[i][0][0] = ScaleW[i] * sw;
        ScaleMatrix[i][1][1] = ScaleH[i] * sh;
        ScaleMatrix[i][2][2] = ScaleW[i] * sw;
        sceVu0MulMatrix(LocalScreen[i], slm, ScaleMatrix[i]);
    }
    for (i = 0; i < DispFlameNum; i++)
    {
        for (j = 0; j < 4; j++)
        {
            // FF3 reads its own stack copy of the quad (data 0x352eb0), CandlePolyDat's values.
            sceVu0RotTransPers(ivec[i][j], LocalScreen[i], CandlePolyDat[j], 0);
            if ((u_int)(ivec[i][j][0] - 0x4000) > 0x8000 || (u_int)(ivec[i][j][1] - 0x4000) > 0x8000
                || (u_int)(ivec[i][j][2] - 1) > 0xfffffe)
                return;
        }
    }

    EffSetAddDrawEnv();
    FreaPos[0] = pos[0];
    FreaPos[1] = pos[1] + -1.2f;        // rdata 0x36e1e8
    FreaPos[2] = pos[2];
    FreaPos[3] = pos[3];
    if (blue == 1)
        EffBlueFireFreaDraw(frea_scale, frea_h, FreaPos, -1, (int)fr, (int)fg, (int)fb, ec->a);
    else
        EffCandleFreaDraw(frea_scale, frea_h, FreaPos, -1, (int)fr, (int)fg, (int)fb, ec->a);

    for (i = 0; i < DispFlameNum; i++)
    {
        sceVu0FMATRIX world;

        CandleFlameMakePacket(ivec[i], tex0[i], -1, (int)r, (int)g, (int)b, Alpha[i]);
        sceVu0MulMatrix(world, wlm, ScaleMatrix[i]);               // PC: local -> world
        EffHostQuad(world, CandlePolyDat, CandleFlameST, tex0[i], (int)r, (int)g, (int)b, Alpha[i]);
    }
}

// The kind 0x14 flame's clock, FF2 SubCandleFlame's second half (FF3 runs it
// from SetFire, apart from the draw). Unless effects are frozen, cnt counts
// and the flare alpha a jitters (5..12; the blue flame 23..25). Then the
// flow: 0 -> 4; 1 (flare up, 7 frames) -> 2 (4) -> 3 (9) -> 4; 4 / 7 run the
// 150 / 75-frame flicker (7 drops back to 4 at its end) with in cycling
// 0..19; 5 (EffCandleFireNearCheck) steps keep 0..14 every 2 frames, 6 (its
// blue flame) one step; both end in 7.
// FF3 0x002b1d88; FF2 SubCandleFlame's flow switch (MioPan effect_oth.c:2046).
// callcheck: rand (EffRand01: ReiPan_Rand)

void SubCandleFlameStep(EFFECT_CONT *ec)
{
    int lo = 5;
    int hi = 13;
    u_int loop;

    if (ec->max == 1)
    {
        lo = 23;
        hi = 26;
    }
    if (eff_wrk.stop_flg == 0)
    {
        ec->cnt++;
        if (ec->max == 1)
            ec->a = (u_char)(lo + (int)((float)(hi - lo) * EffRand01()));  // rdata 0x36e1ec
        else
            ec->a = (u_char)((int)(EffRand01() * 8.0f) + 5);              // rdata 0x36e1f0
    }

    switch (ec->flow)
    {
    case 0:
        ec->cnt = 0;
        ec->flow = 4;
        ec->in = 0;
        break;
    case 1:
        if (ec->cnt >= 7)
        {
            ec->cnt = 0;
            ec->flow = 2;
        }
        break;
    case 2:
        if (ec->cnt >= 4)
        {
            ec->cnt = 0;
            ec->flow = 3;
        }
        break;
    case 3:
        if (ec->cnt >= 9)
        {
            ec->cnt = 0;
            ec->flow = 4;
            ec->in = 0;
        }
        break;
    case 4:
    case 7:
        loop = (ec->flow != 4) ? 75 : 150;
        if (ec->cnt >= loop)
        {
            ec->cnt = 0;
            if (ec->flow == 7)
                ec->flow = 4;
        }
        ec->in++;
        if (ec->in >= 20)
            ec->in = 0;
        break;
    case 5:
        if (ec->cnt >= 2)
        {
            ec->cnt = 0;
            ec->keep++;
        }
        if (ec->keep >= 15)
        {
            ec->keep = 0;
            ec->flow = 7;
            ec->in = 0;
            ec->cnt = 0;
        }
        break;
    case 6:
        if (ec->cnt >= 2)
        {
            ec->cnt = 0;
            ec->keep++;
        }
        if (ec->keep != 0)
        {
            ec->keep = 0;
            ec->flow = 7;
            ec->in = 0;
            ec->cnt = 0;
        }
        break;
    }
}

// Draw a whole altar, then step it. Each candle is a flame (a Y-only
// billboard, height negated) and a flare 50 up (a full billboard, x3); a
// candle whose flame or flare leaves the guard band is skipped (FF3 stops a
// quad's corner checks at the first one out). The kept candles are sorted
// back to front on the flame's first corner and drawn flare first. Flame
// texture effdat[0x36 + mono], flare effdat[0x46 + mono] (FF2 0x30 / 0x4c).
// FF3 0x00163610; FF2 ManyCandleCtrl (MioPan effect_oth.c:2270).
// callcheck: SetGsRegsCached (in EffSetAddDrawEnv)

static void EffManyCandleParticleUpdate(MANY_CANDLE_CTRL *pCandleCtrl);

static void *EffManyCandleCtrl(MANY_CANDLE_CTRL *pCandleCtrl)
{
    static sceVu0IVECTOR ivec[MANY_CANDLE_MAX * 4];     // PC: off the stack (0x7800 bytes each)
    static sceVu0IVECTOR ivecFrea[MANY_CANDLE_MAX * 4];
    GRA3DCAMERA *pCam;
    float *cam_pos;
    sceVu0FMATRIX WorkMat;
    sceVu0FMATRIX LocalWorld;
    sceVu0FMATRIX LocalScreen;
    sceVu0FMATRIX BaseMat;
    sceVu0FMATRIX FreaBaseMat;
    sceVu0FVECTOR FreaPosition;
    int Order[MANY_CANDLE_MAX];
    int ToPolyNo[MANY_CANDLE_MAX];
    float RotX;
    float RotY;
    float FreaScale;
    float FreaHeight;
    u_long Tex0;
    u_long FreaTex0;
    int ColorR;
    int ColorG;
    int ColorB;
    int PolyNo;
    int ClipFlg;
    int i;
    int j;
    int k;

    FreaScale = 3.0f;                   // rdata 0x36e108
    pCam = gra3dGetCamera();
    FreaHeight = 50.0f;                 // rdata 0x36e10c
    cam_pos = gra3dcamGetPosition();
    PolyNo = 0;
    FreaTex0 = effdat[eff_wrk.monochro_mode + 0x46].tex0;
    Tex0 = effdat[eff_wrk.monochro_mode + 0x36].tex0;

    if (pCandleCtrl == nullptr)
        return nullptr;

    if (eff_wrk.monochro_mode == 0)
    {
        ColorR = pCandleCtrl->R;
        ColorG = pCandleCtrl->G;
        ColorB = pCandleCtrl->B;
    }
    else
    {
        ColorR = (pCandleCtrl->R + pCandleCtrl->G + pCandleCtrl->B) / 3;
        ColorG = ColorR;
        ColorB = ColorR;
    }

    EffSetAddDrawEnv();
    Get2PosRot(cam_pos, pCam->vTarget, &RotX, &RotY);

    // The flame's base: Y only, height negated.
    sceVu0UnitMatrix(WorkMat);
    WorkMat[0][0] = 1.0f;               // rdata 0x36e110
    WorkMat[1][1] = -1.0f;              // rdata 0x36e114
    WorkMat[2][2] = 1.0f;
    sceVu0RotMatrixY(BaseMat, WorkMat, NormalizeAngle(RotY));

    // The flare's base: a full billboard.
    sceVu0UnitMatrix(WorkMat);
    WorkMat[0][0] = 1.0f;
    WorkMat[1][1] = 1.0f;
    WorkMat[2][2] = 1.0f;
    sceVu0RotMatrixX(WorkMat, WorkMat, NormalizeAngle(RotX));
    sceVu0RotMatrixY(FreaBaseMat, WorkMat, NormalizeAngle(RotY));

    for (i = 0; i < pCandleCtrl->DataNum; i++)
    {
        MANY_CANDLE_PARTICLE *pc = &pCandleCtrl->Particles[i];

        ClipFlg = 0;

        // The flame (FF3 inlines the matrix products).
        sceVu0UnitMatrix(WorkMat);
        WorkMat[0][0] = CandleFlameScaleData[pc->Count][0];
        WorkMat[1][1] = CandleFlameScaleData[pc->Count][1];
        WorkMat[2][2] = CandleFlameScaleData[pc->Count][0];
        sceVu0MulMatrix(WorkMat, BaseMat, WorkMat);
        sceVu0TransMatrix(LocalWorld, WorkMat, pc->Position);
        sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld);

        for (j = 0; j < 4; j++)
        {
            int *iv = ivec[PolyNo * 4 + j];

            sceVu0RotTransPers(iv, LocalScreen, CandlePolyDat[j], 0);
            if ((u_int)(iv[0] - 0x4000) > 0x8000 || (u_int)(iv[1] - 0x4000) > 0x8000
                || (u_int)(iv[2] - 0xff) > 0xffff00)
            {
                ClipFlg = 1;
                break;
            }
        }

        // The flare, FreaHeight up.
        FreaPosition[0] = pc->Position[0];
        FreaPosition[1] = pc->Position[1] + FreaHeight;
        FreaPosition[2] = pc->Position[2];
        FreaPosition[3] = pc->Position[3];
        sceVu0UnitMatrix(WorkMat);
        WorkMat[2][2] = FreaScale;
        WorkMat[1][1] = FreaScale;
        WorkMat[0][0] = FreaScale;
        sceVu0MulMatrix(WorkMat, FreaBaseMat, WorkMat);
        sceVu0TransMatrix(LocalWorld, WorkMat, FreaPosition);
        sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld);

        for (j = 0; j < 4; j++)
        {
            int *iv = ivecFrea[PolyNo * 4 + j];

            sceVu0RotTransPers(iv, LocalScreen, CandlePolyDat[j], 0);
            if ((u_int)(iv[0] - 0x4000) > 0x8000 || (u_int)(iv[1] - 0x4000) > 0x8000
                || (u_int)(iv[2] - 0xff) > 0xffff00)
            {
                ClipFlg = 1;
                break;
            }
        }

        if (ClipFlg == 0)
        {
            ToPolyNo[PolyNo] = i;
            PolyNo++;
        }
    }

    for (i = 0; i < PolyNo; i++)
        Order[i] = i;

    for (i = 0; i < PolyNo - 1; i++)
    {
        for (j = i + 1; j < PolyNo; j++)
        {
            if (ivec[Order[j] * 4][2] < ivec[Order[i] * 4][2])
            {
                k = Order[j];
                Order[j] = Order[i];
                Order[i] = k;
            }
        }
    }

    for (i = 0; i < PolyNo; i++)
    {
        CandleFlameMakePacket(&ivecFrea[Order[i] * 4], FreaTex0, ivecFrea[Order[i] * 4][2],
                              ColorR, ColorG, ColorB,
                              pCandleCtrl->Particles[ToPolyNo[Order[i]]].FreaAlpha);
        CandleFlameMakePacket(&ivec[Order[i] * 4], Tex0, ivec[Order[i] * 4][2],
                              ColorR, ColorG, ColorB, 0x7f);
    }

    if (eff_wrk.stop_flg == 0)
        EffManyCandleParticleUpdate(pCandleCtrl);

    return pCandleCtrl;
}

// Every candle's flicker: Count runs 0..149, the flare alpha is re-rolled in
// 17..21 each frame.
// FF3 0x00163d78; FF2 ManyCandleParticleUpdate (MioPan effect_oth.c:2490).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffManyCandleParticleUpdate(MANY_CANDLE_CTRL *pCandleCtrl)
{
    int i;

    for (i = 0; i < pCandleCtrl->DataNum; i++)
    {
        MANY_CANDLE_PARTICLE *p = &pCandleCtrl->Particles[i];

        p->Count++;
        if (p->Count > 149)
            p->Count = 0;
        p->FreaAlpha = (int)(EffRand01() * 4.0f + 17.0f);  // rdata 0x36e11c / 0x36e120
    }
}

// ──────────────────────────────────────────────────────────────────────
// The halo (kind 0x13)
// ──────────────────────────────────────────────────────────────────────

// EffectZSort's kind 0x13: SubHalo(pnt[0], type uc8[2], texture uc8[6],
// z, rgb uc8[3..5], alpha 100, scale fl32[2]); a one-frame request ends.
// FF3 0x00164100; FF2 SetHalo (MioPan effect_oth.c:6072).

static void SubHalo(float *p, int type, int textp, u_int z, u_char r, u_char g, u_char b, u_char alp, float sc);

void SetHalo(EFFECT_CONT *ec)
{
    SubHalo((float *)ec->pnt[0], ec->dat.uc8[2], ec->dat.uc8[6], ec->z,
            ec->dat.uc8[3], ec->dat.uc8[4], ec->dat.uc8[5], 100, ec->dat.fl32[2]);
    if (ec->dat.uc8[1] & 1)
        ResetEffects(ec);
}

// A camera-facing 8x8 quad gives the halo's screen extent; the halo is two
// SetEffSQITex sprites on its centre, the outer twice the inner, flickering
// with rnbk (odd type 3..9, even 0..9; held while effects are frozen).
// FF3: scale 1 (FF2 25), guard band x / y within 0x4000..0xc000, z 1..0xffffff,
// the first corner out returns; texture textp * 2 + mono + 0xe (FF2 + 0xc).
// FF3 0x00164170; FF2 SubHalo (MioPan effect_oth.c:2611). z is unused.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void SubHalo(float *p, int type, int textp, u_int z, u_char r, u_char g, u_char b, u_char alp, float sc)
{
    static sceVu0FMATRIX wlm;           // bss 0x3a6ac0
    static sceVu0FMATRIX slm;           // bss 0x3a6b00
    static int rnbk = 0;                // data 0x36e124
    sceVu0FVECTOR wpos[4] = {
        { -4.0f,  4.0f, 0.0f, 1.0f },
        {  4.0f,  4.0f, 0.0f, 1.0f },
        { -4.0f, -4.0f, 0.0f, 1.0f },
        {  4.0f, -4.0f, 0.0f, 1.0f },
    };
    sceVu0IVECTOR ivec[4];
    sceVu0FVECTOR vpos;
    int ipos[4];
    GRA3DCAMERA *pCam;
    float rot_x;
    float rot_y;
    float f;
    int w;
    int h;
    int rn;
    int i;

    (void)z;

    pCam = gra3dGetCamera();
    vpos[0] = p[0];
    vpos[1] = p[1];
    vpos[2] = p[2];
    vpos[3] = p[3];
    Vector2Rot(gra3dcamGetDirection(), &rot_x, &rot_y);

    sceVu0UnitMatrix(wlm);
    wlm[0][0] = 1.0f * sc;              // rdata 0x36e128 (FF2 scw 25)
    wlm[1][1] = 1.0f * sc;              // rdata 0x36e12c (FF2 sch 25)
    wlm[2][2] = wlm[0][0];
    sceVu0RotMatrixX(wlm, wlm, rot_x);
    sceVu0RotMatrixY(wlm, wlm, rot_y);
    sceVu0TransMatrix(wlm, wlm, vpos);
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(ivec[i], slm, wpos[i], 0);
        if ((u_int)(ivec[i][0] - 0x4000) > 0x8000 || (u_int)(ivec[i][1] - 0x4000) > 0x8000
            || (u_int)(ivec[i][2] - 1) > 0xfffffe)
            return;
    }

    ipos[3] = 0;
    w = (ivec[3][0] - ivec[0][0]) / 2;
    h = (ivec[3][1] - ivec[0][1]) / 2;
    if (h < w)
        h = w;
    ipos[0] = (ivec[0][0] + ivec[3][0]) / 2;
    ipos[1] = (ivec[0][1] + ivec[3][1]) / 2;
    ipos[2] = (ivec[0][2] + ivec[3][2]) / 2;
    f = (float)h * 0.0625f;             // rdata 0x36e130

    if (eff_wrk.stop_flg == 0)
    {
        if (type & 1)
            rnbk = (int)(EffRand01() * 6.0f + 3.0f);    // rdata 0x36e13c / 0x36e140
        else
            rnbk = (int)(EffRand01() * 9.0f);           // rdata 0x36e138
    }
    rn = rnbk;

    // PC: the two sprites are laid out on screen around vpos, so they are
    // tied to it for frame smoothing's in-between cameras.
    ReiPan_Graph3dBeginWorldAnchor(vpos);
    SetEffSQITex(textp * 2 + eff_wrk.monochro_mode + 0xe, ipos, 1, f + f, f + f, r, g, b,
                 (u_char)((int)((rn / 2 + 7) * (u_int)alp) / 100));
    SetEffSQITex(textp * 2 + eff_wrk.monochro_mode + 0xe, ipos, 1, f, f, r, g, b,
                 (u_char)((int)((rn + 3) * (u_int)alp) / 100));
    ReiPan_RendererEndAnchor();         // PC
}

// ──────────────────────────────────────────────────────────────────────
// The thunder light
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00164568; FF2 EffectThunderLightInit (MioPan effect_oth.c:805).

void EffectThunderLightInit(void)
{
    ThunderLightCtrl.LightningStatus = 0;
    ThunderLightCtrl.LightningTime = 0;
    ThunderLightCtrl.LightningFlg = 0;
    ThunderLightCtrl.ThunderStatus = 0;
    ThunderLightCtrl.ThunderTime = 0;
}

// Arm a strike: the lightning starts at once (delay 0), the thunder after
// time frames.
// FF3 0x00164588; FF2 EffectThunderLightReq (MioPan effect_oth.c:816).

void EffThunderReq(float *pos, int time, float *v)
{
    ThunderLightCtrl.LightningDirection[0] = pos[0];
    ThunderLightCtrl.LightningDirection[1] = pos[1];
    ThunderLightCtrl.LightningDirection[2] = pos[2];
    ThunderLightCtrl.LightningDirection[3] = 0.0f;
    ThunderLightCtrl.ThunderPosition[0] = v[0];
    ThunderLightCtrl.ThunderPosition[1] = v[1];
    ThunderLightCtrl.ThunderPosition[2] = v[2];
    ThunderLightCtrl.ThunderPosition[3] = v[3];
    ThunderLightCtrl.ThunderTime = time;
    ThunderLightCtrl.ThunderStatus = 4;
    ThunderLightCtrl.LightningTime = 0;
    ThunderLightCtrl.LightningStatus = 4;
    ThunderLightCtrl.LightningFlg = 0;
}

// Step both machines (EffectInPhoto). Lightning: 4 delay, 1 flash 2 frames,
// 2 gap 2, 3 flash 12, off. Thunder: 4 delay, then ESE_RAKURAI at the
// strike's position, 1 rumble 2, 2 gap 2, 3 rumble 45.
// FF3 differences: nothing runs, the thunder included, once the lightning is
// off (status 0), so a thunder delay past the lightning's 17 frames never
// sounds; the rumble is VibrateRequest1 inlined.
// FF3 0x001645c8; FF2 EffectThunderLightExec (MioPan effect_oth.c:840).
// callcheck: GetOptionWrk (VibrateRequest1's, inlined in FF3)

void EffectThunderLightExec(void)
{
    THUNDER_LIGHT_CTRL *tl = &ThunderLightCtrl;
    SND_3D_SET s3s;

    if (tl->LightningStatus == 0)
        return;

    if (tl->LightningStatus == 4)
    {
        tl->LightningTime--;
        if (tl->LightningTime == -1)
        {
            tl->LightningStatus = 1;
            tl->LightningTime = 2;
        }
    }

    if (tl->LightningStatus == 1)
    {
        tl->LightningFlg = 1;
        tl->LightningTime--;
        if (tl->LightningTime == 0)
        {
            tl->LightningFlg = 0;
            tl->LightningTime = 2;
            tl->LightningStatus = 2;
        }
    }
    else if (tl->LightningStatus == 2)
    {
        tl->LightningTime--;
        if (tl->LightningTime == 0)
        {
            tl->LightningStatus = 3;
            tl->LightningTime = 12;
        }
    }
    else if (tl->LightningStatus == 3)
    {
        tl->LightningFlg = 1;
        tl->LightningTime--;
        if (tl->LightningTime == 0)
        {
            tl->LightningStatus = 0;
            tl->LightningFlg = 0;
        }
    }

    if (tl->ThunderStatus == 4)
    {
        tl->ThunderTime--;
        if (tl->ThunderTime != -1)
            return;
        memset(&s3s, 0, sizeof(s3s));   // FF3 clears 0xc: three EE pointers
        s3s.pos = tl->ThunderPosition;
        snd_utilAutoBDPlay(ESE_RAKURAI_BD, ESE_RAKURAI_HXD, 0, 0, 0x3200, 0x1000, 0, &s3s);
        tl->ThunderStatus = 1;
        tl->ThunderTime = 2;
    }

    if (tl->ThunderStatus == 1)
    {
        tl->ThunderTime--;
        VibrateRequest1(0, 1);
        if (tl->ThunderTime == 0)
        {
            tl->ThunderTime = 2;
            tl->ThunderStatus = 2;
        }
    }
    else if (tl->ThunderStatus == 2)
    {
        if (tl->ThunderTime - 1 != 0)
        {
            tl->ThunderTime--;
            return;
        }
        tl->ThunderStatus = 3;
        tl->ThunderTime = 45;
    }
    else if (tl->ThunderStatus == 3)
    {
        tl->ThunderTime--;
        VibrateRequest1(0, 1);
        if (tl->ThunderTime == 0)
            tl->ThunderStatus = 0;
    }
}

// MapDrawSetRoomLight: while the lightning lights the room, its light
// becomes the room's light 1.
// FF3 0x00164818.

void EffThunderApplyLight(void)
{
    G3DLIGHT light = {};                // PC: zeroed; the ROM leaves the fields EffThunderSetLight skips as stack garbage

    if (ThunderLightCtrl.LightningFlg == 0)
        return;

    EffThunderSetLight(&light);
    gra3dSetLight(1, &light);
    gra3dLightEnable(1, 1);
    gra3dApplyLight();
}

// The lightning as a directional light: bluish white (0.8, 0.8, 1.0)
// along the strike's direction, normalised (w kept, 0). Only the diffuse,
// direction, type and falloff are written.
// FF3 0x00164868.

void EffThunderSetLight(G3DLIGHT *light)
{
    const float *dir = ThunderLightCtrl.LightningDirection;
    float q;

    light->vDiffuse[1] = 0.8f;          // rdata 0x36e144
    light->vDiffuse[0] = 0.8f;
    light->vDiffuse[3] = 1.0f;          // 0x36e148
    light->vDiffuse[2] = 1.0f;
    q = Vu0Rsqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);     // vrsqrt (PC: PS2 rsqrt)
    light->vDirection[0] = dir[0] * q;
    light->vDirection[1] = dir[1] * q;
    light->vDirection[2] = dir[2] * q;
    light->vDirection[3] = dir[3];
    light->Type = 0;
    light->fFalloff = 1.0f;
}

// char_model.c (0x1460b0, 0x1472e8): the room is lit by the lightning.
// FF3 0x001648d8.

int EffThunderIsLightning(void)
{
    return ThunderLightCtrl.LightningFlg;
}

// ──────────────────────────────────────────────────────────────────────
// Kind 0x1e: four glows circling a point
// ──────────────────────────────────────────────────────────────────────

static void EffKind1EDraw(float rate, float *pos, float **cnt);

// The photo objects' glow (PhotoObjSlotSet, photo_obj.c): a one-shot kind 0x1e record at
// a copy of pos (one static vector, so one glow at a time).
// FF3 0x001648e8.
// callcheck: fptodp (the (double) cast of the variadic argument)

void *EffKind1EReq(float rate, float *pos)
{
    sceVu0CopyVector(eff_kind1e_pos, pos);
    return SetEffects(0x1e, 2, eff_kind1e_pos, (double)rate);
}

// FF3 0x00164928.

int EffKind1ESetRate(float rate, void *eff)
{
    if (eff == nullptr)
        return 0;
    ((EFFECT_CONT *)eff)->dat.fl32[1] = rate;
    return 1;
}

// FF3 0x00164948.

int EffKind1ECut(void *eff)
{
    if (eff == nullptr)
        return 0;
    EffectRelease(eff);
    return 1;
}

// EffectZSort's kind 0x1e: draw with fl32[1] as the alpha rate and fw[0..3]
// as the parts' frame counters; a one-frame request ends.
// FF3 0x00164970.

void SetEffKind1E(EFFECT_CONT *ec)
{
    float *cnt[4];

    cnt[0] = &ec->fw[0];
    cnt[1] = &ec->fw[1];
    cnt[2] = &ec->fw[2];
    cnt[3] = &ec->fw[3];
    EffKind1EDraw(ec->dat.fl32[1], ec->Pos, cnt);
    if (ec->dat.uc8[1] & 1)
        ResetEffects(ec);
}

// FF3 0x001649e0.

static EFF_KIND1E_PART *EffKind1EGetPart(int no)
{
    return &eff_kind1e_part[no];
}

// PhotoObjSlotEffStep (photo_obj.c): row no of the deform parameters.
// FF3 0x00164a00.

EFF_PDEFORM_PRM *EffPDeformPrmGet(int no)
{
    return &eff_pdeform_prm[no];
}

// FF3 0x00164a20.

void EffKind1EMatrix(float *out, sceVu0FMATRIX mat, float *pos)
{
    sceVu0FMATRIX unit;
    sceVu0FVECTOR v = { 0.0f, -1.0f, -10.0f, 1.0f };    // rdata 0x36e14c..0x36e154
    sceVu0FVECTOR rot;

    GetCamFaceRot(rot, pos);
    sceVu0UnitMatrix(unit);             // the ROM starts from the unit matrix at data 0x3293c0
    sceVu0RotMatrixX(mat, unit, NormalizeAngle(NormalizeAngle(rot[0])));
    sceVu0RotMatrixY(mat, mat, NormalizeAngle(NormalizeAngle(rot[1])));
    sceVu0RotMatrixZ(mat, mat, NormalizeAngle(NormalizeAngle(rot[2])));
    sceVu0ApplyMatrix(out, mat, v);
    out[0] += pos[0];
    out[1] += pos[1];
    out[2] += pos[2];
}

static void EffKind1EDrawParts(float rate, sceVu0FMATRIX mat, float *pos, float **cnt);

// FF3 0x00164b38.
// callcheck: SetGsRegsCached (in EffSetAddDrawEnv)

static void EffKind1EDraw(float rate, float *pos, float **cnt)
{
    sceVu0FVECTOR vec;
    sceVu0FMATRIX mat;

    EffKind1EMatrix(vec, mat, pos);
    EffSetAddDrawEnv();
    EffKind1EDrawParts(rate, mat, vec, cnt);
}

// The four parts: each on its circle at angle ang_scale * (t + ang_phase),
// radius, size and alpha on their own sine curves of t = cnt * 2pi / period
// - pi; a 3D sprite of lang_effdat 2 (parts 0, 1) or 4 (parts 2, 3), size x10.
// The counters run 0..period unless effects are frozen.
// FF3 0x002b1fe0.

static void EffKind1EDrawParts(float rate, sceVu0FMATRIX mat, float *pos, float **cnt)
{
    sceVu0FVECTOR v;
    sceVu0FVECTOR w;
    EFF_KIND1E_PART *part;
    float t;
    float alpha;
    float rad;
    float size;
    float ang;
    int tex;
    int i;

    LangTexSend(2);
    LangTexSend(4);

    for (i = 0; i < 4; i++)
    {
        part = EffKind1EGetPart(i);
        if (part->on != 0)
        {
            t = (*cnt[i] * 6.2831855f) / part->period - 3.1415927f;    // rdata 0x36e1f4 / 0x36e1f8
            alpha = part->alpha_amp * Vu0Sinf(NormalizeAngle(t + part->alpha_phase)) + part->alpha_base;
            rad = part->rad_amp * Vu0Sinf(NormalizeAngle(t + part->rad_phase)) + part->rad_base;
            size = part->size_amp * Vu0Sinf(NormalizeAngle(t + part->size_phase)) + part->size_base;
            ang = part->ang_scale * NormalizeAngle(t + part->ang_phase);
            v[0] = Vu0Cosf(ang) * rad + part->pos[0];
            v[1] = Vu0Sinf(ang) * rad + part->pos[1];
            v[2] = part->pos[2];
            v[3] = 1.0f;                // rdata 0x36e1fc
            sceVu0ApplyMatrix(w, mat, v);
            w[0] += pos[0];             // vadd.xyzw: w too
            w[1] += pos[1];
            w[2] += pos[2];
            w[3] += pos[3];
            tex = (i < 2) ? 2 : 4;
            EffCamYawSprite(0.0f, size * 10.0f, size * 10.0f, &lang_effdat[tex], w,   // rdata 0x36e200
                         (u_char)part->rgb[0], (u_char)part->rgb[1], (u_char)part->rgb[2],
                         (u_char)(int)(alpha * rate));
        }

        if (eff_wrk.stop_flg == 0)
        {
            *cnt[i] += 1.0f;
            if (part->period < *cnt[i])
                *cnt[i] = 0.0f;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The seal door burn
// ──────────────────────────────────────────────────────────────────────

static void EffSealDoorBurnSprDraw(u_char alpha, const SPRT_DAT *spr);
static void EffSealDoorBurnEdgeDraw(float t, int alpha, const SPRT_DAT *spr, const SPRT_DAT *mask);

// InitEffects (EffectObjOthInit's tail).
// FF3 0x00164c00; FF2 DoorSealDisappearInit (MioPan effect_oth.c:1447).

void EffSealDoorBurnInit(void)
{
    memset(&SealDoorBurnCtrl, 0, sizeof(SealDoorBurnCtrl));    // EE 0x120
    SealDoorBurnCtrl.Counter = 0;
    SealDoorBurnCtrl.LoadId = -1;
    SealDoorBurnCtrl.Status = 0;
    SealDoorBurnCtrl.AlphaRate = 0.0f;
    SealDoorBurnCtrl.BurnRate = 0.0f;
    SealDoorBurnCtrl.pBuf = nullptr;
}

// Load the burn texture pack of row no (FUUIN_TEX_PK4 / _SP1 / _SP2) into
// the effect heap, once until Release.
// FF3 0x00164c50.
// callcheck: EFFECT_MALLOC (read as a macro)

void EffSealDoorBurnLoad(int no)
{
    const int *file;

    if (SealDoorBurnCtrl.Status != 0)
        return;

    file = &seal_door_burn_file[no];
    if (GetFileSize(*file) != (u_int)-1 && GetFileSize(*file) != 0)
    {
        SealDoorBurnCtrl.pBuf = EFFECT_MALLOC((int)GetFileSize(*file));
        SealDoorBurnCtrl.LoadId = LoadReq(*file, SealDoorBurnCtrl.pBuf);
    }
    SealDoorBurnCtrl.BurnRate = 0.0f;
    SealDoorBurnCtrl.Counter = 0;
    SealDoorBurnCtrl.AlphaRate = 0.0f;
    SealDoorBurnCtrl.Status = 1;
}

// FF3 0x00164cf0.

int EffSealDoorBurnIsEnd(void)
{
    return SealDoorBurnCtrl.Status == 0;
}

// FF3 0x00164d00.

void EffSealDoorBurnRelease(void)
{
    if (SealDoorBurnCtrl.Status == 0)
        return;

    if (SealDoorBurnCtrl.pBuf != nullptr)
    {
        heapCtrlFree(GetSystemHeapWrkP(), SealDoorBurnCtrl.pBuf, 0, 0, 0);
        SealDoorBurnCtrl.pBuf = nullptr;
    }
    SealDoorBurnCtrl.BurnRate = 0.0f;
    SealDoorBurnCtrl.Status = 0;
    SealDoorBurnCtrl.Counter = 0;
    SealDoorBurnCtrl.AlphaRate = 0.0f;
    SealDoorBurnCtrl.LoadId = -1;
}

// The burn's clock, once a frame (InitEffectObjEF's tail). FF2's seven
// stages (a hold before the cue, two holds) became five, one per frame
// through a switch: 1 waits for the pack, 2 fades the seal in (10 frames),
// 3 the deform (30, then the burn sound), 4 runs the burn to frame 129,
// 5 fades both out (10) and releases. The ROM leaves Counter at 30 when
// stage 3 ends, so the burn starts 30 / 129 of the way in.
// FF3 0x00164d78; FF2 DoorSealDisappearMain (MioPan effect_oth.c:1512).

void EffSealDoorBurnStep(void)
{
    SEAL_DOOR_BURN_CTRL *pCtrl = &SealDoorBurnCtrl;

    switch (pCtrl->Status)
    {
    case 1:
        if (IsLoadEnd(pCtrl->LoadId) != 0)
        {
            pCtrl->BurnRate = 0.0f;
            pCtrl->Status = 2;
            pCtrl->Counter = 0;
            pCtrl->AlphaRate = 0.0f;
            pCtrl->DeformAlphaRate = 0.0f;
        }
        break;

    case 2:
        if (pCtrl->Counter >= 10)
        {
            pCtrl->BurnRate = 0.0f;
            pCtrl->Status = 3;
            pCtrl->AlphaRate = 1.0f;        // rdata 0x36e158
            pCtrl->Counter = 0;
        }
        else
        {
            pCtrl->AlphaRate = (float)pCtrl->Counter / 10.0f;  // 0x36e15c
            pCtrl->Counter++;
        }
        break;

    case 3:
        if (pCtrl->Counter >= 30)
        {
            pCtrl->BurnRate = 0.0f;
            pCtrl->Status = 4;
            pCtrl->DeformAlphaRate = 1.0f;
            SndSePlay(0x1a9b000d, 100, 0x1000, 0, nullptr, 0);
        }
        else
        {
            pCtrl->DeformAlphaRate = (float)pCtrl->Counter / 30.0f;    // 0x36e160
            pCtrl->Counter++;
        }
        break;

    case 4:
        if (pCtrl->Counter >= 129)
        {
            pCtrl->Status = 5;
            pCtrl->BurnRate = 1.0f;
            pCtrl->Counter = 0;
        }
        else
        {
            pCtrl->BurnRate = (float)pCtrl->Counter / 129.0f;  // 0x36e164
            pCtrl->Counter++;
        }
        break;

    case 5:
        if (pCtrl->Counter >= 10)
        {
            EffSealDoorBurnRelease();
        }
        else
        {
            pCtrl->DeformAlphaRate = 1.0f - (float)pCtrl->Counter / 10.0f;
            pCtrl->AlphaRate = pCtrl->DeformAlphaRate;
            pCtrl->Counter++;
        }
        break;
    }
}

// The screen dither under the deform, at the deform's strength.
// FF3 0x00164f80.

static void EffSealDoorBurnDither(float rate)
{
    SubDither3(5, rate * 95.0f, 24.0f, 0x40, 0x40);     // rdata 0x36e16c / 0x36e168
}

// The deform record (kind 0x16, kernel 0x18): two wave terms, the speed
// and rate through the caller's floats, a slightly green grey.
// FF3 0x00164fc0; FF2 DoorSealDisappearPDeformParamSet (MioPan effect_oth.c:5444).

static void EffSealDoorBurnPDeformParamSet(EFFECT_CONT *ec, float *pPos, int AlphaRate,
                                           float *pSpeed, float *pWaveRate)
{
    ec->max = AlphaRate;
    ec->dat.uc8[0] = 0x16;
    ec->dat.uc8[1] = 1;
    ec->pnt[0] = pPos;
    ec->dat.uc8[2] = 0x18;
    ec->fw[0] = 0.0f;
    ec->cnt = 0;
    ec->dat.uc8[4] = 0xff;
    ec->in = 0;
    ec->keep = 0;
    ec->out = 0;
    ec->pnt[1] = nullptr;
    ec->dat.fl32[2] = 18.47f;           // rdata 0x36e170
    ec->dat.fl32[3] = 16.539999f;       // 0x36e174
    ec->fw[1] = 1.0f;                   // 0x36e178
    *pSpeed = 1.89f;                    // 0x36e17c
    ec->pnt[2] = pSpeed;
    *pWaveRate = 4.0f;                  // 0x36e180
    ec->dat.uc8[5] = 1;
    ec->r = 0x7b;
    ec->pnt[4] = pWaveRate;
    ec->g = 0x83;
    ec->b = 0x81;
    ec->pnt[5] = nullptr;
}

// The blur record over the same deform (kernel 0x27), neutral grey.
// FF3 0x00165080; FF2 DoorSealDisappearPDeformBlurParamSet (MioPan effect_oth.c:5406).

static void EffSealDoorBurnPDeformBlurParamSet(EFFECT_CONT *ec, float *pPos, int AlphaRate)
{
    ec->max = AlphaRate;
    ec->dat.uc8[0] = 0x16;
    ec->pnt[0] = pPos;
    ec->dat.uc8[5] = 1;
    ec->dat.uc8[4] = 0xff;
    ec->dat.uc8[2] = 0x27;
    ec->b = 0x80;
    ec->dat.uc8[1] = 1;
    ec->fw[0] = 0.0f;
    ec->dat.fl32[2] = 16.05f;           // rdata 0x36e184
    ec->cnt = 0;
    ec->in = 0;
    ec->dat.fl32[3] = 13.429999f;       // 0x36e188
    ec->keep = 0;
    ec->out = 0;
    ec->fw[1] = 1.0f;                   // 0x36e18c
    ec->pnt[1] = nullptr;
    ec->pnt[2] = nullptr;
    ec->pnt[4] = nullptr;
    ec->pnt[5] = nullptr;
    ec->r = 0x80;
    ec->g = 0x80;
}

// The deform under a borrowed camera: 500 units back along -z looking at
// the origin, no roll, 60 degrees; blur first (rate * 53), then the deform
// (rate * 31); the room camera put back.
// FF3 0x00165118; FF2 DoorSealDisappearDraw's second half (MioPan effect_oth.c:1663).

static void EffSealDoorBurnDeformDraw(SEAL_DOOR_BURN_CTRL *pCtrl)
{
    static const sceVu0FVECTOR origin = { 0.0f, 0.0f, 0.0f, 1.0f };    // rdata 0x329500
    sceVu0FVECTOR pos, tgt;
    float fov, roll;
    int   alpha, blur;

    sceVu0CopyVector(pos, gra3dcamGetPosition());
    sceVu0CopyVector(tgt, gra3dcamGetTarget());
    fov = gra3dcamGetFov();
    roll = gra3dcamGetRoll();
    gra3dcamSetPosition(0.0f, 0.0f, -500.0f);           // rdata 0x36e190
    gra3dcamSetTarget(0.0f, 0.0f, 0.0f, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(1.0471975f);                         // 0x36e194
    gra3dApplyCamera(nullptr, 0, 1);

    alpha = (int)(pCtrl->DeformAlphaRate * 31.0f);      // 0x36e198
    blur = (int)(pCtrl->DeformAlphaRate * 53.0f);       // 0x36e19c
    sceVu0CopyVector(pCtrl->BasePos, (float *)origin);
    EffSealDoorBurnPDeformBlurParamSet(&pCtrl->EffectContBlur, pCtrl->BasePos, blur);
    SetPartsDeform(&pCtrl->EffectContBlur);
    EffSealDoorBurnPDeformParamSet(&pCtrl->EffectCont, pCtrl->BasePos, alpha,
                                   &pCtrl->DeformSpeed, &pCtrl->DeformRate);
    SetPartsDeform(&pCtrl->EffectCont);

    gra3dcamSetPosition(pos);
    gra3dcamSetTarget(tgt, 1);
    gra3dcamSetRoll(roll);
    gra3dcamSetFov(fov);
    gra3dApplyCamera(nullptr, 0, 1);
}

// EffectDrawMain's last call: the burn over the finished frame. The screen
// darkens (AlphaRate * 64 of black), the pack goes to VRAM, the seal sprite
// (stages 2 / 3) or its burn (4) is drawn, then the dither and the deform
// under the effect GS state (additive, depth test always).
// FF3 0x001652a0; FF2 DoorSealDisappearDraw (MioPan effect_oth.c:1663).

void EffSealDoorBurnDraw(void)
{
    SEAL_DOOR_BURN_CTRL *pCtrl = &SealDoorBurnCtrl;
    GS_REG_SET regs[5];
    int alpha;

    if (pCtrl->Status == 0 || pCtrl->AlphaRate <= 0.0f)
        return;

    DispSqrRect(0.0f, 0.0f, 640.0f, 448.0f, 0, 0, 0, 0,        // rdata 0x36e1a4 / 0x36e1a8
                (u_char)(int)(pCtrl->AlphaRate * 64.0f));       // 0x36e1a0
    PK4SendVram(pCtrl->pBuf, 0, -1, -1);

    alpha = (int)(pCtrl->AlphaRate * 132.0f);                   // 0x36e1ac
    switch (pCtrl->Status)
    {
    case 2:
    case 3:
        EffSealDoorBurnSprDraw((u_char)alpha, &seal_door_burn_spr[0]);
        break;
    case 4:
        EffSealDoorBurnEdgeDraw(pCtrl->BurnRate, alpha, &seal_door_burn_spr[0], &seal_door_burn_spr[1]);
        break;
    }

    regs[0].data = 0x48;            regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x3000d;         regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);
    EffSealDoorBurnDither(pCtrl->DeformAlphaRate);
    EffSealDoorBurnDeformDraw(pCtrl);
}

// The seal, centred and scaled 1.1 about the screen centre.
// FF3 0x002b22a0 (out of line).

static void EffSealDoorBurnSprDraw(u_char alpha, const SPRT_DAT *spr)
{
    DISP_SPRT ds;
    float x = 320.0f - (float)spr->w * 0.5f;           // rdata 0x36e208 / 0x36e204
    float y = 224.0f - (float)spr->h * 0.5f;           // 0x36e20c

    CopySprDToSpr(&ds, spr);
    ds.alpha = alpha;
    ds.alphar = 0x44;                                   // rdata 0x354ae0
    ds.x = x;
    ds.y = y;
    ds.sch = 1.0999999f;                                // 0x36e210
    ds.csx = 320.0f;
    ds.csy = 224.0f;
    ds.scw = 1.0999999f;
    ds.pri = 0xa0;
    ds.z = 0xfff5f;
    ds.b = 0x80;
    ds.r = 0x80;
    ds.g = 0x80;
    DispSprD(&ds);
}

// The seal burning away (t: the burn's progress, alpha: the seal's).
//  - the seal itself, fading as alpha * (1 - 2t);
//  - the burning band: the mask drawn into the work frame 0x3020 twice
//    under an alpha test, added where its alpha is above t * 255 / 2 + 4 and
//    subtracted where above that + 22t, leaving the band between;
//  - the mask over the screen with only the pixels above t * 255 / 2 (what
//    is left of the seal) at alpha;
//  - the band as a blue glow (0x18 0x5a 0xdc, additive), then 22 copies
//    stretched upward from its bottom edge, fainter and taller each.
// The y of both sprites uses the width, as the ROM does (the pictures are
// square).
// FF3 0x002b23a0 (out of line).

static void EffSealDoorBurnEdgeDraw(float t, int alpha, const SPRT_DAT *spr, const SPRT_DAT *mask)
{
    DISP_SPRT  ds;
    SPRT_DAT2  sd;
    DISP_SPRT2 ds2;
    int   w = spr->w;
    int   h = spr->h;
    float fw = (float)w;
    float x = 320.0f - fw * 0.5f;                      // rdata 0x36e224 / 0x36e228
    float y = 224.0f - fw * 0.5f;                      // 0x36e22c
    int   glow = (int)(t * 255.0f);                     // 0x36e220
    int   fade = (int)((float)alpha * (1.0f - (t + t)));
    int   aref = glow / 2;
    int   aref0, aref1;
    int   rest, a, i;

    if (fade > 0)
    {
        CopySprDToSpr(&ds, spr);
        ds.alpha = (u_char)fade;
        ds.csx = 320.0f;
        ds.scw = 1.0999999f;                            // 0x36e234
        ds.sch = 1.0999999f;
        ds.alphar = 0x44;                               // rdata 0x354ae0
        ds.z = 0xfff5f;
        ds.b = 0x80;
        ds.pri = 0xa0;
        ds.csy = 224.0f;
        ds.r = 0x80;
        ds.g = 0x80;
        ds.x = x;
        ds.y = y;
        DispSprD(&ds);
    }

    aref0 = aref + 4;
    aref1 = aref + (int)(t * 22.0f) + 4;                // 0x36e238
    if (aref0 < 0)
        aref0 = 0;
    else if (aref0 > 0xff)
        aref0 = 0xff;
    if (aref1 < 0)
        aref1 = 0;
    else if (aref1 > 0xff)
        aref1 = 0xff;

    // The band into the work frame: Cs * 0x80 + Cd above aref0, Cd - Cs
    // above aref1 (TEST: ATE, GREATER, depth always).
    SetGsRegCached(0x8000000044ULL, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x30003ULL, SCE_GS_TEST_1, 1);
    SetGsRegCached(0x10a000118ULL, SCE_GS_ZBUF_1, 1);
    // PC: the work frame on the host, a scratch image in its own 320 x 448
    // pixels (the band is read back from its top-left corner below), and the
    // GS alpha test on the 2D draws -- the band is made of it -- through the
    // remaining seal.
    ReiPan_EffHostWorkBegin(0x3020, 320, 448, 1);
    ReiPan_RendererSet2dAlphaTest(1);
    EffDrawFrameFillRect(0x3020, SCE_GS_PSMCT24, 0, 0, w, h, 0, 0, 0, 0x80);
    CopySprDToSpr(&ds, mask);
    ds.pri = 0xa0;
    ds.z = 0xfff5f;
    ds.r = 0x80;
    ds.test = ((u_long)aref0 << 4) | 0x3000d;
    ds.g = 0x80;
    ds.b = 0x80;
    ds.alpha = 0x80;
    ds.tex1 = 0x161;
    ds.x = 0.0f;
    ds.alphar = 0x8000000068ULL;
    ds.zbuf = 0x10a000118ULL;
    ds.y = 0.0f;
    ds.scw = 1.0f;                                      // 0x36e230
    ds.sch = 1.0f;
    ds.csx = 0.0f;
    ds.csy = 0.0f;
    DispSprD(&ds);
    ds.test = ((u_long)aref1 << 4) | 0x3000d;
    ds.alphar = 0x8000000062ULL;
    DispSprD(&ds);
    EffResetDrawFrame();
    ReiPan_EffHostWorkEnd();            // PC

    // What is left of the seal.
    CopySprDToSpr(&ds, mask);
    ds.z = 0xfff5f;
    ds.alphar = 0x44;
    ds.test = ((u_long)aref << 4) | 0x3000d;
    ds.pri = 0xa0;
    ds.alpha = (u_char)alpha;
    ds.r = 0x80;
    ds.g = 0x80;
    ds.b = 0x80;
    ds.x = x;
    ds.y = y;
    ds.scw = 1.0999999f;
    ds.sch = 1.0999999f;
    ds.csx = 320.0f;
    ds.csy = 224.0f;
    ds.tex1 = 0x161;
    DispSprD(&ds);
    ReiPan_RendererSet2dAlphaTest(0);   // PC

    if (glow <= 0)
        return;

    // The band from the work frame (TEX0: 0x3020, TBW 5, PSMCT24, 512 x
    // 256), blue, added.
    sd.tex0 = 0;
    sd.u1 = 0.0f;
    sd.v1 = 0.0f;
    sd.u2 = fw;
    sd.v2 = (float)h;
    sd.w = fw;
    sd.h = (float)h;
    sd.x = 0.0f;
    sd.y = 0.0f;
    sd.pri = 0;
    sd.alpha = 0x80;
    CopySprDToSpr2(&ds2, &sd);
    ds2.tex1 = 0x161;
    ds2.pri = 0xa0;
    ds2.alpreg = 0x48;                                  // rdata 0x354ae8
    ds2.alp = 0x80;
    ds2.tex0 = 0x2000000224117020ULL;                   // rdata 0x36e218
    ds2.z = 0xfff00;
    ds2.zbuf = 0x10a000118ULL;
    ds2.sch = 1.0999999f;
    ds2.r = 0x18;
    ds2.g = 0x5a;
    ds2.b = 0xdc;
    ds2.x = x;
    ds2.y = y;
    ds2.scw = 1.0999999f;
    ds2.csx = 320.0f;
    ds2.csy = 224.0f;
    EffDispSprD2Direct(&ds2);

    rest = 0xff - glow;
    if (rest <= 0)
        return;
    for (i = 1; i < 23; i++)
    {
        a = rest / (23 - i);
        if ((a & 0xff) == 0)
            continue;
        ds2.alp = (u_char)a;
        ds2.csx = 320.0f;
        ds2.csy = fw * 0.5f + 224.0f;
        ds2.scw = 1.0999999f;
        ds2.sch = ((float)(glow * 23) / (float)(0x4eb * i) + 1.0f) * 1.0999999f;
        EffDispSprD2Direct(&ds2);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The room glow (kind 0x10)
// ──────────────────────────────────────────────────────────────────────

// FF3 0x00165428.

void EffRoomGlowSetFixed(int fixed)
{
    room_glow.fixed = fixed;
}

// FF3 0x00165438.

void EffRoomGlowClear(void)
{
    memset(&room_glow, 0, 0x10);
    room_glow.boost = 0;
}

// The fixed block (used while EffRoomGlowSetFixed is on).
// FF3 0x00165470.

void EffRoomGlowSet(u_char r, u_char g, u_char b, u_char alpha, u_char boost)
{
    room_glow.col.r = r;
    room_glow.col.g = g;
    room_glow.col.b = b;
    room_glow.boost = boost;
    room_glow.col.alpha = alpha;
}

// The colour block in force: the fixed one, else the player's area's pair,
// its second half while the camera is up (PLYR_FLG_800000) or in a
// CameraSetFromRotPair view (bit 0 of +0xe0).
// FF3 0x001654a8.

static ROOM_GLOW_COL *EffRoomGlowGetCol(void)
{
    ROOM_GLOW_COL *col;
    PLYR_WRK *plyr;

    if (room_glow.fixed != 0)
        return &room_glow.col;

    col = RoomGlowDatGet(MapLoadGetBuffID(room_glow.area));
    if (PlyrGetCurWrk() != nullptr)
    {
        plyr = ingame_wrk.plyr_wrk;
        // PC: FF3 steps past a null pair too (col + 0x10).
        if (col != nullptr && ((plyr->flags & PLYR_FLG_800000) != 0 || (plyr->_0e0 & 1) != 0))
            return col + 1;
    }
    return col;
}

// The brightness added to the shrunken frame before the colour is
// subtracted: 150 in every case of the ROM's six-entry table (the dream side
// world 4 / 2, +1 with the camera up), or the fixed block's boost.
// FF3 0x002b28e0.

static int EffRoomGlowGetBoost(void)
{
    int tbl[6] = { 150, 150, 150, 150, 150, 150 };
    PLYR_WRK *plyr;
    int i;

    if (room_glow.fixed != 0)
        return room_glow.boost;

    i = 0;
    if (ingame_wrk.b03 == 1)
    {
        i = 4;
        if (MapAreaSideGet() != 1)
            i = 2;
    }
    if (PlyrGetCurWrk() != nullptr)
    {
        plyr = ingame_wrk.plyr_wrk;
        if ((plyr->flags & PLYR_FLG_800000) != 0)
            i++;
        else if ((plyr->_0e0 & 1) != 0)
            i++;
    }
    return tbl[i];
}

// EffectControl's kind 0x10 (slot 16): the room glow, a bloom over the
// frame. The frame is drawn shrunk to (320 - 2hw) x (224 - 2hh) at (hw, hh)
// into work buffer 0 (cleared to opaque black), brightened by the boost
// (add), the colour subtracted (the bright pass), blurred three times
// between the two work buffers, then stretched back over the whole frame
// additively. The area is taken outside phase 0x22's event cameras; the
// strength grows with the haze; the movie phases use wider margins.
// FF3 0x00165578.
// callcheck: GetSysWrk (FF3 reads the shared inline cache sdata 0x372890 first)

void SetRoomGlow(EFFECT_CONT *ec)
{
    DISP_SPRT2 ds;
    SPRT_DAT2  sd;
    ROOM_GLOW_COL *col;
    float hw, hh;                       // margins of the shrunken copy
    float w, h;                         // its size
    float rad, d;
    int   alpha, r, g, b, boost;
    int   ix, iy, iw, ih;
    int   cur, last, i;

    if (GetGPhaseCtrl()->leaf->id != 0x22 || MapCamGetEventCameraFlg() == 0)
        room_glow.area = GetPlyrAreaNo();

    col = EffRoomGlowGetCol();
    if (col == nullptr)
        return;
    alpha = col->alpha;
    if (alpha <= 0)
        return;

    r = col->r;
    g = col->g;
    b = col->b;
    if (eff_wrk.monochro_mode != 0)
    {
        r = (r + g + b) / 3;
        g = r;
        b = r;
    }

    hw = 89.6f;                         // rdata 0x36e250 / 0x36e254
    hh = 62.72f;
    if (room_glow.fixed == 0)
        alpha = (int)((float)alpha * (EffectHazeLevelMax() * 0.4f + 1.0f));
    if (IsMoviePhase() != 0)
    {
        hw = 102.4f;
        hh = 71.68f;
    }
    w = 320.0f - (hw + hw);
    h = 224.0f - (hh + hh);

    // PC: the host replaces everything from here to the stretch below with
    // one renderer pass (ReiPan_RendererDrawRoomGlow at the end), so its 2D
    // bridges are muted throughout and its two work frames are not marked:
    // marked, they would hide the frame copies other effects keep at 0x2bc0.
    ReiPan_EffHostMuteBegin();

    // The frame (TEX0: the draw buffer, 0 or 0x1180; TBW 10, PSMCT24,
    // 1024 x 512) shrunk into buffer 0.
    SetGsRegCached(0xff00000064ULL, SCE_GS_ALPHA_1, 1);
    EffDrawFrameFillRect(room_glow_buf[0], SCE_GS_PSMCT24, 0, 0, 320, 224, 0, 0, 0, 0xff);
    sd.tex0  = 0;
    sd.u1    = 0.0f;
    sd.v1    = 0.0f;
    sd.u2    = 640.0f;
    sd.v2    = 448.0f;
    sd.w     = w;
    sd.h     = h;
    sd.x     = hw;
    sd.y     = hh;
    sd.pri   = 0xa0;
    sd.alpha = 0x80;
    CopySprDToSpr2(&ds, &sd);
    ds.tex0   = (u_long)(u_int)((GetSysWrk()->count & 1) * 0x1180 | 0x28128000) | 0x2000000240000000ULL;
    ds.tex1   = 0x161;
    ds.alpreg = 0x44;
    ds.z      = 0xfff00;
    EffDispSprD2Direct(&ds);

    ix = (int)hw;
    iy = (int)hh;
    iw = (int)(w + 0.5f);
    ih = (int)(h + 0.5f);

    // The bright pass: + boost (Cs * FIX + Cd), - colour ((Cd - Cs) * FIX).
    boost = EffRoomGlowGetBoost();
    if (boost > 0)
    {
        SetGsRegCached(0x8000000068ULL, SCE_GS_ALPHA_1, 1);
        b += boost;
        r += boost;
        EffDrawFrameFillRect(room_glow_buf[0], SCE_GS_PSMCT24, ix, iy, iw, ih,
                             (u_char)boost, (u_char)boost, (u_char)boost, 0x80);
        g += boost;
        if (r > 0xff)
            r = 0xff;
        if (g > 0xff)
            g = 0xff;
        if (b > 0xff)
            b = 0xff;
    }
    SetGsRegCached(0x80000000a1ULL, SCE_GS_ALPHA_1, 1);
    EffDrawFrameFillRect(room_glow_buf[0], SCE_GS_PSMCT24, ix, iy, iw, ih,
                         (u_char)r, (u_char)g, (u_char)b, 0x80);

    // Three blurs: each clears one buffer and draws the other into it eight
    // times at the strength (TEX0: TBW 5, PSMCT24, 512 x 256; ALPHA 0x48
    // adds Cs * As).
    sd.tex0  = 0;
    sd.u1    = hw;
    sd.v1    = hh;
    sd.u2    = 320.0f - hw;
    sd.v2    = 224.0f - hh;
    sd.w     = w;
    sd.h     = h;
    sd.x     = 0.0f;
    sd.y     = 0.0f;
    sd.pri   = 0xa0;
    sd.alpha = 0x80;
    CopySprDToSpr2(&ds, &sd);
    ds.alpreg = 0x48;
    ds.tex1   = 0x61;
    ds.z      = 0xfff00;

    cur  = 1;
    last = 1;
    for (i = 0; i < 3; i++)
    {
        rad = room_glow_blur[i];
        d   = rad * 1.4f;
        SetGsRegCached(0xff00000064ULL, SCE_GS_ALPHA_1, 1);
        EffDrawFrameFillRect(room_glow_buf[cur], SCE_GS_PSMCT24, (int)(hw - d), (int)(hh - d),
                             (int)((320.0f - (hw + hw)) + d + 0.5f),
                             (int)((224.0f - (hh + hh)) + d + 0.5f), 0, 0, 0, 0xff);
        last = cur;
        cur ^= 1;
        ds.alp  = (u_char)alpha;
        ds.tex0 = (u_long)(room_glow_buf[cur] | 0x24114000) | 0x2000000200000000ULL;

        ds.x = hw - rad;  ds.y = hh - rad;  EffDispSprD2Direct(&ds);
        ds.x = hw + rad;  ds.y = hh - rad;  EffDispSprD2Direct(&ds);
        ds.x = hw - rad;  ds.y = hh + rad;  EffDispSprD2Direct(&ds);
        ds.x = hw + rad;  ds.y = hh + rad;  EffDispSprD2Direct(&ds);
        ds.x = hw - d;    ds.y = hh;        EffDispSprD2Direct(&ds);
        ds.x = hw + d;    ds.y = hh;        EffDispSprD2Direct(&ds);
        ds.x = hw;        ds.y = hh - d;    EffDispSprD2Direct(&ds);
        ds.x = hw;        ds.y = hh + d;    EffDispSprD2Direct(&ds);
    }

    // The last blur target stretched over the frame, added.
    EffResetDrawFrame();
    sd.tex0  = 0;
    sd.u1    = hw + 0.5f;
    sd.v1    = hh + 0.5f;
    sd.u2    = 319.5f - hw;
    sd.v2    = 223.5f - hh;
    sd.w     = 640.0f;
    sd.h     = 448.0f;
    sd.x     = 0.0f;
    sd.y     = 0.0f;
    sd.pri   = 0xa0;
    sd.alpha = 0x80;
    CopySprDToSpr2(&ds, &sd);
    ds.y      = 0.5f;
    ds.x      = 0.5f;
    ds.tex0   = (u_long)(room_glow_buf[last] | 0x24114000) | 0x2000000200000000ULL;
    ds.tex1   = 0x61;
    ds.alpreg = 0x48;
    ds.z      = 0xfff00;
    EffDispSprD2Direct(&ds);
    ReiPan_EffHostMuteEnd();            // PC

    // PC: the passes above drew nothing on the host (muted). The renderer
    // runs the same glow as its own passes, at the output's resolution, from
    // what this function settled: r / g / b are the subtract (the boost
    // already added and clamped), alp the blur passes' u_char tap alpha.
    {
        ReiPanRoomGlow glow;

        glow.subtract[0] = (float)r;
        glow.subtract[1] = (float)g;
        glow.subtract[2] = (float)b;
        glow.boost       = (float)(boost > 0 ? boost : 0);
        glow.alpha       = (float)(u_char)alpha;
        glow.margin[0]   = hw;
        glow.margin[1]   = hh;
        for (i = 0; i < 3; i++)
            glow.blur[i] = room_glow_blur[i];
        ReiPan_RendererDrawRoomGlow(&glow);
    }

    if ((ec->dat.uc8[1] & 1) != 0)
        ResetEffects(ec);
}

// InitEffects.
// FF3 0x00165530.

void EffRoomGlowInit(void)
{
    memset(&room_glow, 0, sizeof(ROOM_GLOW_WRK));
    EffRoomGlowClear();
    room_glow.area = -1;
    EffRoomGlowSetFixed(0);
}

// FF3 0x00165cf8 (EffObjGPhaseCtrlStaticDtor) is compiler output: the
// atexit thunk of the GPhaseCtrl singleton this file's inline getters
// instantiate, GPhaseCtrlTerm(0x41e468); implicit for the C++ static.

// SET_LANTERNEFFECT_ENABLE.
// FF3 0x00165d18.

void EffLanternEffectEnable(int on)
{
    lantern_effect_on = on;
}

// EffectsReleaseAll: every kind 600 map effect.
// FF3 0x00167db8.

void EffKind600AllCut(void)
{
    EffLanternCut(-1);
}

// ──────────────────────────────────────────────────────────────────────
// The floating lanterns (map effect kind 600)
//
// FF3 C++: each object below has a constructor and destructor that GCC
// emitted twice; one body each here. The unreferenced twins, covered by
// those bodies (hazards.md):
//   FF3 0x00165dd8 LanternFlowInit2    = LanternFlowInit
//   FF3 0x00166438 LanternLipInit2     = LanternLipInit
//   FF3 0x00166a30 LanternInit2        = LanternInit
//   FF3 0x001672c0 LanternCtrlInit2    = LanternCtrlInit
//   FF3 0x00167630 LanternCtrlRelease2 = LanternCtrlRelease
// ──────────────────────────────────────────────────────────────────────

static const sceVu0FVECTOR lantern_vec_zero = { 0.0f, 0.0f, 0.0f, 0.0f };   // data 0x329530
static const sceVu0FVECTOR lantern_vec_org  = { 0.0f, 0.0f, 0.0f, 1.0f };   // data 0x329500

// InitEffects: the lantern model loaded once for the whole game part.
// FF3 0x00165d20.
// callcheck: operator_new, GameModelCtor (new GameModel)

void EffLanternMdlLoad(void)
{
    GameModel *mdl;

    mdl = new (operator_new(sizeof(GameModel))) GameModel;
    mdl->Load(LANTERN_MDL_PK4);
    lantern_mdl = mdl;
}

// EffectsReleaseAll. FF3: delete through the vtable (+0x0c).
// FF3 0x00165d60.

void EffLanternMdlRelease(void)
{
    if (lantern_mdl != nullptr)
    {
        lantern_mdl->~GameModel();
        operator_delete(lantern_mdl);
    }
    lantern_mdl = nullptr;
}

// CheckSetEffectsOK. FF3: the vtable's +0x10.
// FF3 0x00165d98.

int EffLanternMdlIsLoaded(void)
{
    if (lantern_mdl != nullptr && lantern_mdl->IsLoaded() != 0)
        return 1;
    return 0;
}

// ---- the flow map ----------------------------------------------------

static void LanternFlowMakeDefault(LANTERN_FLOW_DAT *dat);

// The flow map file into the system heap; a file too short for a 30 x 30
// map gets a made-up random one instead. wfp (RS50x_WFP) is passed but not
// used.
// FF3 0x00165ef0 (twin 0x00165dd8).

LANTERN_FLOW::LANTERN_FLOW(int wfm, int wfp)
{
    (void)wfp;
    load_id = -1;
    memcpy(zero, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    memcpy(flow, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    memcpy(offset, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    if (GetFileSize(wfm) == (u_int)-1 || GetFileSize(wfm) == 0 || GetFileSize(wfm) < sizeof(LANTERN_FLOW_DAT))
    {
        dat = (LANTERN_FLOW_DAT *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(LANTERN_FLOW_DAT), 0, 0, 0);
        LanternFlowMakeDefault(dat);
        status = 0;
    }
    else
    {
        dat = (LANTERN_FLOW_DAT *)heapCtrlMalloc(GetSystemHeapWrkP(), GetFileSize(wfm), 0, 0, 0);
        load_id = LoadReq(wfm, dat);
        status = 0;
    }
}

// FF3 0x00166060.

LANTERN_FLOW::~LANTERN_FLOW()
{
    if (dat != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), dat, 0, 0, 0);
}

// FF3 0x001660b8. The made-up map keeps load id -1 (IsLoadEnd's answer for
// it decides).

int LANTERN_FLOW::IsReady()
{
    if (status == 0)
    {
        if (IsLoadEnd(load_id) == 0)
            return 0;
        status = 1;
    }
    return 1;
}

// The flow at pos (x / z, room space): the cell's, x blended toward the next
// cell in x, z toward the next row; the zero vector outside the grid or
// while loading.
// FF3 0x00166100.

float *LANTERN_FLOW::Get(float *pos)
{
    LANTERN_FLOW_DAT *dat;
    float *cell;
    float fx, fz, vx, vz;
    int   ix, iz;

    if (IsReady() == 0 || (dat = this->dat) == nullptr)
        return zero;

    fx = (pos[0] - dat->x0) - offset[0];
    fz = (pos[2] - dat->z0) - offset[2];
    ix = (int)(fx / dat->cell_w + 0.5f);
    iz = (int)(fz / dat->cell_d + 0.5f);
    if (ix < 0 || ix >= dat->nx || iz < 0 || iz >= dat->nz)
        return zero;

    cell = dat->cell[iz][ix];
    vx = cell[0];
    if (ix < dat->nx - 1)
    {
        fx -= (float)ix * dat->cell_w;
        vx = (vx * (dat->cell_w - fx)) / dat->cell_w + (cell[2] * fx) / dat->cell_w;
    }
    vz = cell[1];
    if (iz < dat->nz - 1)
    {
        fz -= (float)iz * dat->cell_d;
        vz = (vz * (dat->cell_d - fz)) / dat->cell_d + (cell[30 * 2 + 1] * fz) / dat->cell_d;
    }
    flow[0] = vx;
    flow[2] = vz;
    flow[1] = 0.0f;
    flow[3] = 0.0f;
    return flow;
}

// 30 x 30 cells of 30 units from the room origin, random flows of up to
// 0.05 per axis.
// FF3 0x002b29c8.
// callcheck: rand (EffRand01: ReiPan_Rand, the ROM's 31-bit range)

static void LanternFlowMakeDefault(LANTERN_FLOW_DAT *dat)
{
    int x, z;

    if (dat == nullptr)
        return;

    dat->x0     = 0.0f;
    dat->z0     = 0.0f;
    dat->cell_d = 30.0f;
    dat->nz     = 30;
    dat->nx     = 30;
    dat->cell_w = 30.0f;
    for (z = 0; z < 30; z++)
    {
        for (x = 0; x < 30; x++)
        {
            dat->cell[z][x][0] = (EffRand01() - 0.5f) * 0.1f;
            dat->cell[z][x][1] = (EffRand01() - 0.5f) * 0.1f;
        }
    }
}

// ---- the spawn and sink points ---------------------------------------

// FF3 0x001662e8.

LANTERN_POINT::LANTERN_POINT(float x, float y, float z, float r)
{
    pos[3] = 0.0f;
    pos[0] = x;
    pos[1] = y;
    pos[2] = z;
    memcpy(rand_pos, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    this->r  = r;
    r2 = r * r;
}

// The destructor: nothing.
// FF3 0x00166328.

LANTERN_POINT::~LANTERN_POINT()
{
    (void)this;
}

// A random position in the point's square: pos +- r / 2 in x and z.
// FF3 0x00166330.
// callcheck: rand (EffRand01: ReiPan_Rand, the ROM's 31-bit range)

float *LANTERN_POINT::GetRandPos()
{
    rand_pos[1] = pos[1];
    rand_pos[0] = pos[0] + (EffRand01() - 0.5f) * r;
    rand_pos[3] = pos[3];
    rand_pos[2] = pos[2] + (EffRand01() - 0.5f) * r;
    return rand_pos;
}

// FF3 0x001663f0.

int LANTERN_POINT::IsIn(float *pos)
{
    return (pos[2] - this->pos[2]) * (pos[2] - this->pos[2])
         + (pos[0] - this->pos[0]) * (pos[0] - this->pos[0]) < r2;
}

static void LanternLipMakeDefault(LANTERN_LIP_DAT *dat);

// The point file into the system heap, or the area's built-in points when
// it is too short.
// FF3 0x00166538 (twin 0x00166438).
// callcheck: LanternPointRelease, operator_delete (the constructor's exception cleanup of the points)

LANTERN_LIP::LANTERN_LIP(int file)
{
    memcpy(offset, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    spawn = 0;
    load_id = -1;
    if (GetFileSize(file) == (u_int)-1 || GetFileSize(file) == 0 || GetFileSize(file) < sizeof(LANTERN_LIP_DAT))
    {
        dat = (LANTERN_LIP_DAT *)heapCtrlMalloc(GetSystemHeapWrkP(), sizeof(LANTERN_LIP_DAT), 0, 0, 0);
        LanternLipMakeDefault(dat);
        status = 0;
    }
    else
    {
        dat = (LANTERN_LIP_DAT *)heapCtrlMalloc(GetSystemHeapWrkP(), GetFileSize(file), 0, 0, 0);
        load_id = LoadReq(file, dat);
        status = 0;
    }
}

// The built-in points of areas 0x29 / 0x2b / 0x2c (rdata 0x36e2fc..). The
// start rows are left as the heap had them.
// FF3 0x002b2ae8.

static void LanternLipMakeDefault(LANTERN_LIP_DAT *dat)
{
    if (dat == nullptr)
        return;

    dat->num     = LANTERN_MAX;
    dat->in_num  = 2;
    dat->out_num = 3;
    switch (MapAreaNoGet())
    {
    case 0x29:
        dat->in[0].x  = -210.171097f;   dat->in[0].z  = -24.9191589f;   dat->in[0].r  = 4.0f;
        dat->in[1].x  = 195.408798f;    dat->in[1].z  = -41.5031586f;   dat->in[1].r  = 4.0f;
        dat->out[0].x = -509.896729f;   dat->out[0].z = 832.502869f;    dat->out[0].r = 80.0f;
        dat->out[1].x = 86.0697021f;    dat->out[1].z = 812.231812f;    dat->out[1].r = 80.0f;
        dat->out[2].x = -200.0f;        dat->out[2].z = 812.231812f;    dat->out[2].r = 80.0f;
        break;
    case 0x2b:
        dat->in[0].x  = -215.171097f;   dat->in[0].z  = -22.9191589f;   dat->in[0].r  = 3.0f;
        dat->in[1].x  = 190.408798f;    dat->in[1].z  = -41.5031586f;   dat->in[1].r  = 3.0f;
        dat->out[0].x = -508.29657f;    dat->out[0].z = 828.202698f;    dat->out[0].r = 80.0f;
        dat->out[1].x = 83.3696899f;    dat->out[1].z = 810.031921f;    dat->out[1].r = 80.0f;
        dat->out[2].x = -200.0f;        dat->out[2].z = 810.031921f;    dat->out[2].r = 80.0f;
        break;
    case 0x2c:
        dat->in[0].x  = -244.408798f;   dat->in[0].z  = 38.6968384f;    dat->in[0].r  = 6.0f;
        dat->in[1].x  = 239.171097f;    dat->in[1].z  = 40.3808441f;    dat->in[1].r  = 6.0f;
        dat->out[0].x = -150.069702f;   dat->out[0].z = 798.231689f;    dat->out[0].r = 80.0f;
        dat->out[1].x = 148.896698f;    dat->out[1].z = 798.40271f;     dat->out[1].r = 80.0f;
        dat->out[2].x = 0.0f;           dat->out[2].z = 798.40271f;     dat->out[2].r = 100.0f;
        break;
    }
}

// FF3 0x00166718.
// callcheck: operator_delete, LanternPointRelease (delete of the points)

LANTERN_LIP::~LANTERN_LIP()
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (in[i] != nullptr)
        {
            in[i]->~LANTERN_POINT();
            operator_delete(in[i]);
        }
    }
    for (i = 0; i < 3; i++)
    {
        if (out[i] != nullptr)
        {
            out[i]->~LANTERN_POINT();
            operator_delete(out[i]);
        }
    }
    if (dat != nullptr)
        heapCtrlFree(GetSystemHeapWrkP(), dat, 0, 0, 0);
}

// Once the file is in: the two spawn and three sink points at y 16.4,
// moved by the room offset.
// FF3 0x001667f8.
// callcheck: operator_new, LanternPointInit (new of the points)

int LANTERN_LIP::IsReady()
{
    LANTERN_POINT *pt;
    int i;

    if (status == 0)
    {
        if (IsLoadEnd(load_id) == 0)
            return 0;

        for (i = 0; i < 2; i++)
        {
            pt = new (operator_new(sizeof(LANTERN_POINT))) LANTERN_POINT(dat->in[i].x, 16.4f, dat->in[i].z, dat->in[i].r);
            in[i] = pt;
            pt->pos[0] += offset[0];
            pt->pos[1] += offset[1];
            pt->pos[2] += offset[2];
        }
        for (i = 0; i < 3; i++)
        {
            pt = new (operator_new(sizeof(LANTERN_POINT))) LANTERN_POINT(dat->out[i].x, 16.4f, dat->out[i].z, dat->out[i].r);
            out[i] = pt;
            pt->pos[0] += offset[0];
            pt->pos[1] += offset[1];
            pt->pos[2] += offset[2];
        }
        status = 1;
    }
    return 1;
}

// A spawn position, alternating between the two spawn points; (0, 0, 0, 1)
// before the file is in.
// FF3 0x00166938.

float *LANTERN_LIP::GetSpawnPos()
{
    if (IsReady() == 0)
        return (float *)lantern_vec_org;

    spawn = (spawn + 1) % 2;
    return in[spawn]->GetRandPos();
}

// FF3 0x001669a8.

int LANTERN_LIP::IsInSink(float *pos)
{
    int i;

    if (IsReady() == 0)
        return 0;

    for (i = 0; i < 3; i++)
    {
        if (out[i]->IsIn(pos))
            return 1;
    }
    return 0;
}

// ---- one lantern -----------------------------------------------------

// FF3 0x00166bc0.

void LANTERN::ClearMove()
{
    memcpy(pos, lantern_vec_org, sizeof(sceVu0FVECTOR));
    memcpy(pos_old, lantern_vec_org, sizeof(sceVu0FVECTOR));
    memcpy(vel, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    memcpy(drift, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    wander  = 0;
    ang     = 0.0f;
    ang_vel = 0.0f;
}

// FF3 0x00166c10.

void LANTERN::ClearState()
{
    alpha = 0;
    state = 0;
}

// FF3 0x00166c20.

void LANTERN::SetFlameCnt()
{
    flame_cnt = (short)GetRandValIByF(150);
}

// A lantern with its own LANTERN_MDL_PK4 GameModel.
// FF3 0x00166ac8 (twin 0x00166a30).
// callcheck: operator_new, GameModelCtor (new GameModel)

LANTERN::LANTERN()
{
    GameModel *mdl;

    ClearMove();
    ClearState();
    SetFlameCnt();
    mdl = new (operator_new(sizeof(GameModel))) GameModel;
    mdl->Load(LANTERN_MDL_PK4);
    this->mdl = mdl;
    mdl_ready = (mdl != nullptr && mdl->IsLoaded() != 0) ? 1 : 0;
}

// FF3 0x00166b90. FF3: delete through the vtable (+0x0c).

LANTERN::~LANTERN()
{
    if (mdl != nullptr)
    {
        mdl->~GameModel();
        operator_delete(mdl);
    }
}

// One frame of motion: every 5..14 frames the wander direction turns by up
// to +-0.75 rad; the flow feeds it, it feeds the velocity, both damped; the
// spin is clamped (PAL: x1.2) and damped; the matrix is RotY(ang) of the
// 1.2 scale (rdata 0x3536f0) at pos; the screen position and the depth fade
// follow.
// FF3 0x002b2d70.
// callcheck: rand (EffRand01: ReiPan_Rand, the ROM's 31-bit range)

void LANTERN::Move()
{
    static sceVu0FMATRIX scale = {                  // rdata 0x3536f0
        { 1.2f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.2f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.2f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    };
    float lo, hi;
    float a;
    int   w;
    int   i;

    if (wander < 1)
    {
        wander = (short)(GetRandValIByF(10) + 5);
        RotVectorY((EffRand01() - 0.5f) * 1.5f, drift, drift);
    }
    else
    {
        wander--;
    }

    for (i = 0; i < 3; i++)
        flow[i] *= 0.01f;
    for (i = 0; i < 3; i++)
        drift[i] += flow[i];
    drift[3] = 0.0f;
    drift[1] = 0.0f;
    for (i = 0; i < 3; i++)
        drift[i] *= 0.94f;
    for (i = 0; i < 3; i++)
        vel[i] += drift[i];
    vel[1] = 0.0f;
    vel[3] = 0.0f;
    for (i = 0; i < 3; i++)
        vel[i] *= 0.89f;
    for (i = 0; i < 3; i++)
        pos[i] += vel[i];
    pos_old[0] = pos[0];
    pos_old[1] = pos[1];
    pos_old[2] = pos[2];

    lo = (GetPALMode() != 0) ? -0.125663698f : -0.104719758f;
    hi = (GetPALMode() != 0) ? 0.125663698f : 0.104719758f;
    if (ang_vel < lo)
        ang_vel = lo;
    if (hi < ang_vel)
        ang_vel = hi;
    if (fabsf(ang_vel) > 0.01f)
        ang_vel *= 0.96f;

    a = ang + ang_vel;
    ang = a;
    if (a < -3.14159274f)
        ang = a + 6.28318548f;
    else if (a > 3.14159274f)
        ang = a - 6.28318548f;

    sceVu0RotMatrixY(mat, scale, NormalizeAngle(ang));
    mat[3][0] += pos[0];
    mat[3][1] += pos[1];
    mat[3][2] += pos[2];
    pos[3] = 1.0f;

    sceVu0RotTransPers(scr, gra3dGetCamera()->matWorldScreen, pos, 0);
    w = scr[3];
    if (w <= 199)
        far_rate = 0.0f;
    else if (w < 10001)
        far_rate = (float)(w - 200) / 9800.0f;
    else
        far_rate = 1.0f;
}

// State 0 fades in by 3 to 0x80, state 2 out to 0; both then go to 1.
// FF3 0x002b3110.

void LANTERN::Fade()
{
    short a;

    if (state == 0)
    {
        a = alpha;
        if (a < 0x80)
            alpha = a = (short)(a + 3);
        if (a > 0x7f)
        {
            state = 1;
            alpha = 0x80;
        }
    }
    else if (state == 2)
    {
        a = alpha;
        if (a > 0)
            alpha = a = (short)(a - 3);
        if (a < 1)
        {
            state = 1;
            alpha = 0;
        }
    }
}

// FF3 0x00166c50.

void LANTERN::Step()
{
    Move();
    Fade();
    flame_cnt = (short)((flame_cnt + 1) % 150);
}

// Drawn when the model is (or was) ready, it is not transparent and its
// screen position lies inside the GS window (0x4000..0xc000 in fixed 4,
// z 1..0xffffff).
// FF3 0x00166ca0.

int LANTERN::IsVisible()
{
    if (mdl == nullptr || mdl_ready == 0)
        return 0;
    if (alpha <= 0)
        return 0;
    if ((u_int)(scr[0] - 0x4000) > 0x8000)
        return 0;
    if ((u_int)(scr[1] - 0x4000) > 0x8000)
        return 0;
    return (u_int)(scr[2] - 1) <= 0xfffffe;
}

// The model (alpha fading out from far_rate 0.4, gone at 0.8), then its
// candle flame and, close up, the flame's glow 1.8 below the matrix origin,
// additive without z writes.
// FF3 0x00166d18.

void LANTERN::Draw()
{
    static sceVu0FVECTOR flame_ofs = { 0.0f, -1.8f, 0.0f, 1.0f };   // rdata 0x353740
    sceVu0FVECTOR flame;
    u_long alpha_reg, zbuf_reg;
    float far_rate, scale;
    int   alpha, a;

    far_rate = this->far_rate;
    if (far_rate < 0.4f)
        alpha = this->alpha;
    else
        alpha = (int)(((float)this->alpha * (1.0f - far_rate)) / 0.6f);

    if (mdl != nullptr && alpha != 0 && far_rate < 0.8f)
    {
        if (mdl_ready == 0)
        {
            if (mdl->IsLoaded() == 0)
                return;
            mdl_ready = 1;
        }
        mdl->SetWorldMatrix(mat);
        mdl->SetAlpha((u_char)alpha);
        mdl->DrawMdl();
    }

    alpha_reg = *GetGsRegCached(SCE_GS_ALPHA_1);
    zbuf_reg  = *GetGsRegCached(SCE_GS_ZBUF_1);
    SetGsRegCached(0x48, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x10a000118ULL, SCE_GS_ZBUF_1, 1);

    sceVu0ApplyMatrix(flame, mat, flame_ofs);
    far_rate = this->far_rate;
    scale = far_rate * 1.4f + 1.4f;
    a = ((int)(far_rate + far_rate) + 0x10 + GetRandValIByF(8)) * this->alpha;
    EffCandleFreaDraw(scale, 4.5f, flame, -1, 0x80, 0x80, 0x80, a / 128);
    if (this->far_rate < 0.1f)
        EffCandleFlameOneDraw(0.4f, flame, 0x80, 0x80, 0x80, (this->alpha * 0x40) / 128, flame_cnt);

    SetGsRegCached(alpha_reg, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(zbuf_reg, SCE_GS_ZBUF_1, 1);
}

// The player (pos, this frame's move) pushes the lantern out of r + 5.8:
// the push goes into pos and 0.4 of it into vel, and the move along the
// circle's tangent spins it.
// FF3 0x00166f90.

int LANTERN::PushByPlayer(float r, float *ppos, float *pmove)
{
    sceVu0FVECTOR d, t;
    float dist, push, dot;
    int   i;

    r += 5.8f;
    d[0] = pos[0] - ppos[0];
    d[1] = 0.0f;
    d[2] = pos[2] - ppos[2];
    d[3] = 0.0f;
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dist < r))
        return 0;

    for (i = 0; i < 3; i++)
        d[i] *= Ps2Divf(1.0f, dist);            // PC: PS2 division
    push = r - dist;
    RotVectorY(-1.57079637f, t, d);
    for (i = 0; i < 3; i++)
        d[i] *= push;
    for (i = 0; i < 3; i++)
        pos[i] += d[i];
    for (i = 0; i < 3; i++)
        d[i] *= 0.4f;
    for (i = 0; i < 3; i++)
        vel[i] += d[i];
    dot = t[0] * pmove[0] + t[2] * pmove[2];
    ang_vel = NormalizeAngle(ang_vel + (dot * 0.1f) / 5.8f);
    return 1;
}

// Two lanterns closer than 11.6 are pushed apart, half the overlap each,
// and trade a tenth of their spins.
// FF3 0x00167128.

int LANTERN::Collide(LANTERN *b)
{
    sceVu0FVECTOR d;
    float dist, push, t;
    int   i;

    d[0] = pos[0] - b->pos[0];
    d[1] = 0.0f;
    d[2] = pos[2] - b->pos[2];
    d[3] = 0.0f;
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(dist < 11.6f))
        return 0;

    push = (11.6f - dist) * 0.5f;
    for (i = 0; i < 3; i++)
        d[i] = d[i] * Ps2Divf(1.0f, dist) * push;   // PC: PS2 division
    for (i = 0; i < 3; i++)
    {
        pos[i] += d[i];
        b->pos[i] -= d[i];
    }
    t = ang_vel * 0.1f;
    ang_vel = b->ang_vel * 0.1f;
    b->ang_vel = t;
    return 1;
}

// The MapPut callback of each lantern (flags bit 4: not this pass).
// FF3 0x00167238.

static void LanternPutDraw(MAP_PUT *put, u_int flags)
{
    static sceVu0FVECTOR ambient = { 0.6f, 0.6f, 0.6f, 0.0f };     // rdata 0x353730
    LANTERN *l;

    if (put == nullptr || (flags & 0x10) != 0 || (eff_wrk.disp_flg & 0xff) == 0)
        return;

    l = (LANTERN *)MapPutGetWork(put);
    if (l->IsVisible() == 0)
        return;

    MapLightSetPlayerOnly();
    gra3dSetAmbient(ambient);
    gra3dApplyLight();
    l->Draw();
}

// ---- one room's lanterns ---------------------------------------------

// num lanterns, the flow map and the points of the area's files, and a
// draw callback per lantern in MapPut buffer area + 4000.
// FF3 0x00167478 (twin 0x001672c0).
// callcheck: operator_new, LanternFlowInit, LanternInit, LanternLipInit (new LANTERN / LANTERN_FLOW / LANTERN_LIP)

LANTERN_CTRL::LANTERN_CTRL(int area, int wfm, int lip_file, int wfp, int push, int num)
{
    LANTERN *l;
    MAP_PUT *put;
    int i;

    this->push = push;
    this->num  = num;
    for (i = 0; i < LANTERN_MAX; i++)
        lantern[i] = nullptr;
    for (i = 0; i < this->num; i++)
    {
        l = new (operator_new(sizeof(LANTERN))) LANTERN;
        lantern[i] = l;
    }

    MapLoadGetOffsetVector(offset, MapLoadGetBuffID(area));

    flow = new (operator_new(sizeof(LANTERN_FLOW))) LANTERN_FLOW(wfm, wfp);
    memcpy(flow->offset, offset, sizeof(sceVu0FVECTOR));

    lip = new (operator_new(sizeof(LANTERN_LIP))) LANTERN_LIP(lip_file);
    memcpy(lip->offset, offset, sizeof(sceVu0FVECTOR));

    state    = 0;
    put_buff = area + 4000;
    for (i = 0; i < this->num; i++)
    {
        if (lantern[i] == nullptr)
            continue;
        put = MapPutSetFunc(put_buff, LanternPutDraw, 2);
        if (put != nullptr)
        {
            MapPutSetMatrixPtr(put, (float *)lantern[i]->mat);
            MapPutSetWork(put, lantern[i]);
        }
    }
}

// FF3 0x001676d8 (twin 0x00167630).
// callcheck: operator_delete, LanternFlowRelease, LanternLipRelease, LanternRelease (delete of the parts)

LANTERN_CTRL::~LANTERN_CTRL()
{
    int i;

    for (i = 0; i < LANTERN_MAX; i++)
    {
        if (lantern[i] != nullptr)
        {
            lantern[i]->~LANTERN();
            operator_delete(lantern[i]);
        }
    }
    if (flow != nullptr)
    {
        flow->~LANTERN_FLOW();
        operator_delete(flow);
    }
    if (lip != nullptr)
    {
        lip->~LANTERN_LIP();
        operator_delete(lip);
    }
    MapPutDelete((short)put_buff);
}

// Back to a spawn point, still, at a random angle (+-pi / 2) and spin
// (+-pi / 10), fading in.
// FF3 0x00167780.
// callcheck: rand (EffRand01: ReiPan_Rand, the ROM's 31-bit range)

void LANTERN_CTRL::Respawn(LANTERN *l)
{
    float *pos;

    if (l == nullptr)
        return;

    if (lip != nullptr)
    {
        pos = lip->GetSpawnPos();
        memcpy(l->pos, pos, sizeof(sceVu0FVECTOR));
        memcpy(l->pos_old, l->pos, sizeof(sceVu0FVECTOR));
    }
    memcpy(l->vel, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    memcpy(l->drift, lantern_vec_zero, sizeof(sceVu0FVECTOR));
    l->ang = (EffRand01() - 0.5f) * 3.14159274f;
    l->state = 0;
    l->alpha = 0;
    l->ang_vel = (EffRand01() - 0.5f) * 3.14159274f * 0.2f;
}

// The first running frame: every lantern respawned, then the first num
// placed from the point file's start rows (y 16.4, w 1, + the offset).
// FF3 0x00167890.

void LANTERN_CTRL::Start()
{
    LANTERN_LIP_ROW *row;
    LANTERN *l;
    int i;

    for (i = 0; i < num; i++)
        Respawn(lantern[i]);

    for (i = 0; i < LANTERN_MAX; i++)
    {
        row = nullptr;
        if (lip->IsReady() != 0 && lip->dat != nullptr)
            row = &lip->dat->row[i];

        if (i < num && (l = lantern[i]) != nullptr && row != nullptr)
        {
            l->pos[0] = row->x + offset[0];
            l->pos[1] = 16.4f + offset[1];
            l->pos[2] = row->z + offset[2];
            l->pos[3] = 1.0f;
            memcpy(l->pos_old, l->pos, sizeof(sceVu0FVECTOR));
            l->vel[0] = row->vx;
            l->vel[2] = row->vz;
            l->vel[1] = 0.0f;
            l->vel[3] = 0.0f;
            l->drift[0] = row->dx;
            l->drift[2] = row->dz;
            l->drift[1] = 0.0f;
            l->drift[3] = 0.0f;
            l->ang     = row->ang;
            l->ang_vel = row->ang_vel;
        }
    }
}

// One running frame: flow and motion, the player's push (push rooms), the
// pairwise bumps, the room's walls (stuck ones respawn), finished fades
// respawn, sinks start the fade out.
// FF3 0x00167ac8.

void LANTERN_CTRL::Step()
{
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    sceVu0FVECTOR out;
    float *flow;
    void  *hit;
    LANTERN *l;
    int   i, j, ret;

    for (i = 0; i < num; i++)
    {
        l = lantern[i];
        flow = this->flow->Get(l->pos);
        memcpy(l->flow, flow, sizeof(sceVu0FVECTOR));
        lantern[i]->Step();
    }
    if (push != 0)
    {
        for (i = 0; i < num; i++)
            lantern[i]->PushByPlayer(4.8f, plyr->pos, plyr->move_last);
    }
    for (i = 0; i < num; i++)
    {
        for (j = i + 1; j < num; j++)
            lantern[j]->Collide(lantern[i]);
    }

    hit = MapLoadGetRoomData(MapLoadGetCurRoomAddr(), 3);
    for (i = 0; i < num; i++)
    {
        l = lantern[i];
        ret = MapHitTourouPushOut(5.8f, out, l->pos, l->pos_old, hit, plyr->mh_no);
        if (ret != 0)
        {
            memcpy(l->pos, out, sizeof(sceVu0FVECTOR));
            memcpy(l->pos_old, l->pos, sizeof(sceVu0FVECTOR));
            if (ret == 2)
                Respawn(l);
        }
    }
    for (i = 0; i < num; i++)
    {
        l = lantern[i];
        if (l->alpha == 0)
            Respawn(l);
        if (l->state != 2 && lip->IsInSink(l->pos) != 0)
            l->state = 2;
    }
}

// Waits for both files, then runs while the lantern effect is enabled.
// FF3 0x00167a38.

void LANTERN_CTRL::Exec()
{
    if (state == 0)
    {
        if (flow->IsReady() != 0 && lip->IsReady() != 0)
        {
            state = 1;
            Start();
        }
    }
    else if (state == 1 && lantern_effect_on != 0)
    {
        Step();
    }
}

// ---- the front end (InitEffects, EffectInPhoto, MapEff) --------------

// FF3 0x00167d50.

void EffLanternInit(void)
{
    int i;

    for (i = 0; i < 2; i++)
        memset(&lantern_slot[i], 0, sizeof(LANTERN_SLOT));
    g3dutilSetLightDefault(&lantern_light, 1);
    EffLanternEffectEnable(1);
}

// FF3 0x00167dd0.

void EffLanternExec(void)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (lantern_slot[i].ctrl != nullptr)
            lantern_slot[i].ctrl->Exec();
    }
}

// MapEffCreate's kind 600: a free slot gets the area's lanterns (RS504 /
// RS506: 30, RS507: 40 pushed by the player); other areas make nothing.
// FF3 0x00167e28.
// callcheck: operator_new, LanternCtrlInit (new LANTERN_CTRL)

int EffLanternReq(int no, int area)
{
    LANTERN_CTRL *ctrl;
    int i;

    if (lantern_effect_on == 0)
        return 0;

    for (i = 0; i < 2; i++)
    {
        if (lantern_slot[i].ctrl != nullptr)
            continue;

        ctrl = nullptr;
        if (area == 0x2b)
        {
            ctrl = new (operator_new(sizeof(LANTERN_CTRL))) LANTERN_CTRL(0x2b, RS506_WFM, RS506_LIP, RS506_WFP, 0, 30);
        }
        else if (area == 0x29)
        {
            ctrl = new (operator_new(sizeof(LANTERN_CTRL))) LANTERN_CTRL(0x29, RS504_WFM, RS504_LIP, RS504_WFP, 0, 30);
        }
        else if (area == 0x2c)
        {
            ctrl = new (operator_new(sizeof(LANTERN_CTRL))) LANTERN_CTRL(0x2c, RS507_WFM, RS507_LIP, RS507_WFP, 1, 40);
        }
        if (ctrl != nullptr)
        {
            lantern_slot[i].no   = no;
            lantern_slot[i].ctrl = ctrl;
            lantern_slot[i].area = area;
            return 1;
        }
    }
    return 0;
}

// MapEffDelete: the slots of no (-1: every slot).
// FF3 0x00167fa8.
// callcheck: operator_delete, LanternCtrlRelease (delete LANTERN_CTRL)

void EffLanternCut(int no)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        if (lantern_slot[i].ctrl != nullptr && (no < 0 || lantern_slot[i].no == no))
        {
            lantern_slot[i].ctrl->~LANTERN_CTRL();
            operator_delete(lantern_slot[i].ctrl);
            memset(&lantern_slot[i], 0, sizeof(LANTERN_SLOT));
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Front ends of the parts deform, the haze and kind 0x1d
// ──────────────────────────────────────────────────────────────────────

// No caller in the ROM. SetEffects(0x16 PDEFORM, 4, type, max alp,
// scales, pos, in / keep / out, no drive pointers, grey 0x80): FF2
// CallPartsDeform3_2's arguments under CallPartsDeform3's name.
// FF3 0x00168168; FF2 CallPartsDeform3 (MioPan effect_obj.c:333).
// callcheck: fptodp (the double arguments)

void *CallPartsDeform3(int type, float sclx, float scly, void *pos,
                       u_int in, u_int keep, u_int out, int alp)
{
    return SetEffects(0x16, 4, type, alp, (double)sclx, (double)scly, pos,
                      in, keep, out, (void *)nullptr, (void *)nullptr, (void *)nullptr, (void *)nullptr,
                      0x80, 0x80, 0x80);
}

// No caller in the ROM. SetEffects(0x16, 2, type, max 0x80, scales, pos,
// no envelope, drive alpha, no wave parameters, grey): CallPartsDeform5_2
// without prm0 / prm1.
// FF3 0x00168238; FF2 CallPartsDeform3_2 (other arguments there).
// callcheck: fptodp (the double arguments)

void *CallPartsDeform3_2(int type, float sclx, float scly, void *pos, float *alpha)
{
    return SetEffects(0x16, 2, type, 0x80, (double)sclx, (double)scly, pos,
                      0, 0, 0, (void *)alpha, (void *)nullptr, (void *)nullptr, (void *)nullptr,
                      0x80, 0x80, 0x80);
}

// No caller in the ROM. FF2's: one scale for both axes, speed and rate
// pointers, colour r / g / b.
// FF3 0x001683a0; FF2 CallPartsDeform6 (MioPan effect_obj.c:377).
// callcheck: fptodp (the double arguments)

void *CallPartsDeform6(int type, float scale, void *pos, u_int in, u_int keep, u_int out, int alp,
                       float *pSpd, float *pRate, int r, int g, int b)
{
    return SetEffects(0x16, 4, type, alp, (double)scale, (double)scale, pos,
                      in, keep, out, (void *)nullptr, (void *)pSpd, (void *)pRate, (void *)nullptr,
                      r, g, b);
}

// EneDeformInit: SetEffects(0x16 PDEFORM, 2, type no, max 0x80, scales,
// pWrk pos, in / keep / out 0, drive alpha, speed prm0, wave prm1, no xyz,
// grey 0x80).
// FF3 0x001682e0; FF2 CallPartsDeform5_2 (other arguments there).
// callcheck: fptodp (the double arguments)

void *CallPartsDeform5_2(float scale0, float scale1, u_char no, float *pos, float *alpha, float *prm0, float *prm1)
{
    return SetEffects(0x16, 2, (int)no, 0x80, (double)scale0, (double)scale1, (void *)pos,
                      0, 0, 0, (void *)alpha, (void *)prm0, (void *)prm1, (void *)nullptr,
                      0x80, 0x80, 0x80);
}

// A haze (kind 0x1b) at the player's joint 0x13, starting in phase 0.
// FF3 0x0016b738.

static void EffPlyrHazePos(float *pos, float *rot);

void EffPlyrHazeReq(void)
{
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;

    EffPlyrHazePos(pos, rot);
    SetEffects(0x1b, 2, (float *)pos, (float *)rot, 1);
}

// Joint 0x13's position and the haze's rotation: the player's rot turned
// into a matrix (X, Y, Z from the unit matrix) moved to the joint; both
// outputs are that matrix applied to (0, 0, 0, 1), i.e. the joint position.
// FF3 0x0016b928.

static void EffPlyrHazePos(float *pos, float *rot)
{
    sceVu0FMATRIX m;
    sceVu0FVECTOR org = { 0.0f, 0.0f, 0.0f, 1.0f };     // rdata 0x36e568
    sceVu0FVECTOR dir = { 0.0f, 0.0f, 0.0f, 1.0f };
    PLYR_WRK *plyr;

    PlyrGetJointPos(0x13, pos, nullptr);
    plyr = ingame_wrk.plyr_wrk;
    sceVu0UnitMatrix(m);                // the ROM starts from the unit matrix at data 0x3293c0
    sceVu0RotMatrixX(m, m, NormalizeAngle(NormalizeAngle(plyr->rot[0])));
    sceVu0RotMatrixY(m, m, NormalizeAngle(NormalizeAngle(plyr->rot[1])));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(NormalizeAngle(plyr->rot[2])));
    m[3][0] += pos[0];
    m[3][1] += pos[1];
    m[3][2] += pos[2];
    sceVu0ApplyMatrix(pos, m, org);
    sceVu0ApplyMatrix(rot, m, dir);
}

// InitEffectsEF's call into the kind 0x1d module: nothing.
// FF3 0x0016f938.

void EffKind1DInitEF(void)
{
}

// CH036_04_TM2 through the shared file cache.
// FF3 0x0016fa60.

static void EffKind1DTexLoad(void)
{
    kind1d_tex = file_cache.Req(CH036_04_TM2);
}

// FF3 0x0016fae0.

void EffKind1DTexRelease(void)
{
    file_cache.Release(CH036_04_TM2);
    kind1d_tex = nullptr;
}

// EneObjEffInit: kind 0x1d at pos / rot, record no + 6, alpha pointer;
// flag loads its texture first.
// FF3 0x0016fb08.

void *EffKind1DReq(float *pos, float *rot, float *alpha, int no, int flag)
{
    if (flag != 0)
        EffKind1DTexLoad();
    return SetEffects(0x1d, 2, pos, (void *)rot, 0, no + 6, (void *)alpha);
}

// EneObjEffRelease: end it; flag releases the texture.
// FF3 0x0016fb78.

void EffKind1DCut(void *eff, int flag)
{
    EffKind1DRelease(eff);
    if (flag != 0)
        EffKind1DTexRelease();
}

// EneObjEffSetRate: v into the record's fw[0].
// FF3 0x0016fbb0.

void EffKind1DSetRate(float v, void *eff)
{
    if (eff != nullptr)
        ((EFFECT_CONT *)eff)->fw[0] = v;
}

// FF3 0x0016fbd8.

static void EffKind1DListFree(SINGLE_LINK_LIST *list)
{
    SingleLinkListFree(list);
}

// Free the record's list (pnt[4]) and its work (pnt[1]), then the record.
// FF3 0x0016fed0.
// callcheck: EFFECT_FREE (read as a macro)

void EffKind1DRelease(void *eff)
{
    EFFECT_CONT *ec = (EFFECT_CONT *)eff;

    if (ec == nullptr)
        return;

    if (ec->pnt[4] != nullptr)
    {
        EffKind1DListFree((SINGLE_LINK_LIST *)ec->pnt[4]);
        ec->pnt[4] = nullptr;
    }
    if (ec->pnt[1] != nullptr)
        EFFECT_FREE(ec->pnt[1]);
    ResetEffects(ec);
}

// ──────────────────────────────────────────────────────────────────────
// Init / release of the FF2 effect_obj half (deform slots, model lists)
// ──────────────────────────────────────────────────────────────────────

static EFFINFO2 efi[PDEFORM_SLOT_NUM];          // data 0x3a7920 (FF2 efi, 8 there)
static EFFINFO2 pefi_once[5];                   // bss 0x3c3d50: pass 2's shared tables (FF2 SubPartsDeform2's static)
static int init_pdef2;                          // sdata gp-0x5a0: 1 re-seeds pefi_once on the next pass-2 draw

static SINGLE_LINK_LIST TourouFreaList;         // bss 0x3c3d00, TOUROU_FREA_DATA
static SINGLE_LINK_LIST TourouBaseList;         // bss 0x3c3d10, TOUROU_BASE_DATA
static SINGLE_LINK_LIST LightComeInList;        // bss 0x3c3d20, LIGHT_COME_IN_DATA (FF2 LightComeInCtrl.LightList)
static SINGLE_LINK_LIST WaterFlowList;          // bss 0x3c3d30, WATER_FLOW_DATA
static SINGLE_LINK_LIST ModelAlphaChangeList;   // bss 0x3c3d40, MODEL_ALPHA_CAHNGE_DATA

// FF3 0x0016d418; FF2 EffectLightComeInInit (MioPan effect_obj.c:2666).

static void EffectLightComeInInit(void)
{
    SingleLinkListInit(&LightComeInList, sizeof(LIGHT_COME_IN_DATA));   // EE 0x18 (PC: the host record, it holds a pointer)
}

// FF3 0x0016e430; FF2 EffectWaterFlowInit (0x1c there).

static void EffectWaterFlowInit(void)
{
    SingleLinkListInit(&WaterFlowList, sizeof(WATER_FLOW_DATA));   // EE 0x20 (PC: the host record, it holds a pointer)
}

// FF3 0x0016e7f8; FF2 EffectModelAlphaChangeInit.

static void EffectModelAlphaChangeInit(void)
{
    SingleLinkListInit(&ModelAlphaChangeList, sizeof(MODEL_ALPHA_CAHNGE_DATA));   // EE 0x18 (PC: the host record, it holds a pointer)
}

// FF3 0x0016eb68; FF2 EffectTourouFreaInit.

static void EffectTourouFreaInit(void)
{
    SingleLinkListInit(&TourouFreaList, sizeof(TOUROU_FREA_DATA));   // EE 0x20 (PC: the host record, it holds a pointer)
}

// FF3 0x0016f020; FF2 EffectTourouBaseInit.

static void EffectTourouBaseInit(void)
{
    SingleLinkListInit(&TourouBaseList, sizeof(TOUROU_BASE_DATA));   // EE 0x20 (PC: the host record, it holds a pointer)
}

// FF2 InitEffectObj's body (FF3's InitEffectObj, 0x162318, is a release):
// every deform slot free, pass 2's shared tables re-seeded on next use, the
// five lists emptied (initialised, not freed). FF2's parts-blur block is gone.
// FF3 0x00168078; FF2 InitEffectObj (MioPan effect_obj.c:205).

void EffectObjInit(void)
{
    int i;

    for (i = 0; i < PDEFORM_SLOT_NUM; i++)
    {
        efi[i].use = 0;
        efi[i].pass = 0;
    }
    init_pdef2 = 1;
    EffectLightComeInInit();
    EffectWaterFlowInit();
    EffectModelAlphaChangeInit();
    EffectTourouFreaInit();
    EffectTourouBaseInit();
}

// A slot claimed but not drawn last frame is freed; pass cleared for this one.
// FF3 0x001680e0; FF2 InitEffectObjEF (MioPan effect_obj.c:241).

void EffectObjInitEF(void)
{
    int i;

    for (i = 0; i < PDEFORM_SLOT_NUM; i++)
    {
        if (efi[i].use == 1 && efi[i].pass == 0)
            efi[i].use = 0;
        efi[i].pass = 0;
    }
}

// FF3 0x00168130.

void EffectObjReleaseAll(void)
{
    EffectLightComeInDeleteMapBuffId(-1);
    EffectWaterFlowDelete(-1);
    EffectModelAlphaChangeDelete(-1);
    EffectTourouFreaDelete(-1);
    EffectTourouBaseDelete(-1);
}

// The light-in surfaces' vertex colours: the three types set the same 36
// preset vertices, all black but the middle block's four groups. Written
// once (PC): FF3 has three straight-line copies.
static void EffectLightComeInSetVertexColorRows(SGDFILEHEADER *pSgd, const float (*mid)[3])
{
    static const signed char rows[3][4][6] = {
        { { 0x00, 0x16, -1 }, { 0x02, 0x17, 0x21, -1 }, { 0x04, 0x0a, 0x22, 0x23, -1 }, { 0x08, 0x25, -1 } },
        { { 0x01, 0x0f, 0x18, -1 }, { 0x03, 0x0c, 0x11, 0x19, 0x20, -1 }, { 0x05, 0x0b, 0x13, 0x1d, -1 }, { 0x06, 0x09, 0x1b, 0x24, -1 } },
        { { 0x0e, 0x10, 0x1a, -1 }, { 0x0d, 0x12, 0x1f, -1 }, { 0x14, 0x1e, -1 }, { 0x07, 0x15, 0x1c, -1 } },
    };
    float col[3];
    int   blk, grp, k;

    for (blk = 0; blk < 3; blk++)
    {
        for (grp = 0; grp < 4; grp++)
        {
            col[0] = blk == 1 ? mid[grp][0] : 0.0f;
            col[1] = blk == 1 ? mid[grp][1] : 0.0f;
            col[2] = blk == 1 ? mid[grp][2] : 0.0f;
            for (k = 0; rows[blk][grp][k] >= 0; k++)
                gra3dSetVertexColorPreset(pSgd, rows[blk][grp][k], col);
        }
    }
}

// Type 0 (FF2's colours were 0 / 66 46 18 / 64 48 18 / 0).
// FF3 0x0016d660; FF2 EffectLightComeInSetVertexColorNormal (MioPan effect_obj.c:3336).
// callcheck: gra3dSetVertexColorPreset (through EffectLightComeInSetVertexColorRows)

static void EffectLightComeInSetVertexColorNormal(void *pSgdTop)
{
    static const float mid[4][3] = {
        { 12.0f, 11.0f, 10.0f }, { 56.0f, 52.0f, 50.0f },  // rdata 0x36e698 / 0x36e6a4
        { 56.0f, 52.0f, 50.0f }, { 12.0f, 11.0f, 10.0f },
    };
    EffectLightComeInSetVertexColorRows((SGDFILEHEADER *)pSgdTop, mid);
}

// Type 1, furniture 607.
// FF3 0x0016d9d8; FF2 EffectLightComeInSetVertexColorF607 (MioPan effect_obj.c:3407).
// callcheck: gra3dSetVertexColorPreset (through EffectLightComeInSetVertexColorRows)

static void EffectLightComeInSetVertexColorF607(void *pSgdTop)
{
    static const float mid[4][3] = {
        { 0.0f, 0.0f, 0.0f },    { 55.0f, 55.0f, 56.0f },  // rdata 0x36e6b0 / 0x36e6b4
        { 50.0f, 53.0f, 56.0f }, { 0.0f, 0.0f, 0.0f },     // 0x36e6b8 / 0x36e6bc
    };
    EffectLightComeInSetVertexColorRows((SGDFILEHEADER *)pSgdTop, mid);
}

// Type 2, furniture 609.
// FF3 0x0016dd38; FF2 EffectLightComeInSetVertexColorF609 (MioPan effect_obj.c:3478).
// callcheck: gra3dSetVertexColorPreset (through EffectLightComeInSetVertexColorRows)

static void EffectLightComeInSetVertexColorF609(void *pSgdTop)
{
    static const float mid[4][3] = {
        { 0.0f, 0.0f, 0.0f },    { 30.0f, 35.0f, 37.0f },  // rdata 0x36e6c0..0x36e6c8
        { 30.0f, 35.0f, 37.0f }, { 0.0f, 0.0f, 0.0f },
    };
    EffectLightComeInSetVertexColorRows((SGDFILEHEADER *)pSgdTop, mid);
}

// FF3 0x0016e090; FF2 EffectLightComeInSetVertexColor (MioPan effect_obj.c:2903).

static void EffectLightComeInSetVertexColor(void *pSgdTop, int Type)
{
    if (Type == 1)
    {
        EffectLightComeInSetVertexColorF607(pSgdTop);
        return;
    }
    if (Type == 2)
    {
        EffectLightComeInSetVertexColorF609(pSgdTop);
        return;
    }
    EffectLightComeInSetVertexColorNormal(pSgdTop);
}

// A light-in surface (MapDrawRoomObjRegist): listed with its alpha ceiling
// (FF2's EffectLightComeInGetAlphaMax, inlined: type 1 0xa0, 2 100, else
// 0x69) and its vertex colours set.
// FF3 0x0016e0c8; FF2 EffectLightComeInRegist (MioPan effect_obj.c:2675).

void EffectLightComeInRegist(void *pSgdTop, int MapBuffId, int Type)
{
    LIGHT_COME_IN_DATA LightData;

    if (pSgdTop == nullptr)
        return;

    LightData.MapBuffId = MapBuffId;
    LightData.pSgdTop = pSgdTop;
    LightData.TotalS = 0.0f;
    LightData.TotalT = 0.0f;
    if (Type == 1)
        LightData.AlphaMax = 0xa0;
    else if (Type == 2)
        LightData.AlphaMax = 100;
    else
        LightData.AlphaMax = 0x69;
    LightData.Type = Type;
    SingleLinkListAddEnd(&LightComeInList, &LightData);
    EffectLightComeInSetVertexColor(pSgdTop, Type);
}

static int EffectWaterFlowGetAlpha(WATER_FLOW_DATA *pData);

// A water surface (MapDrawRoomObjRegist): listed with a random alpha phase
// and its first alpha set. FF2's river cues are gone; the alpha comes from
// the stack copy, so the listed entry keeps the unstepped phase (the ROM's).
// FF3 0x0016e478; FF2 EffectWaterFlowRegist (MioPan effect_obj.c:3011).

void EffectWaterFlowRegist(void *pSgdTop, int Id, int MoveType)
{
    WATER_FLOW_DATA WaterFlowData;

    if (pSgdTop == nullptr)
        return;

    WaterFlowData.MoveType = MoveType;
    WaterFlowData.Id = Id;
    WaterFlowData.pSgdTop = pSgdTop;
    WaterFlowData.TotalS = 0.0f;
    WaterFlowData.TotalT = 0.0f;
    WaterFlowData.Count = 0;
    WaterFlowData.MoveDataNo = 0;
    WaterFlowData.AlphaPhase = GetRandValF(6.2831855f) - 3.1415927f;   // rdata 0x36e6d8 / 0x36e6dc
    SingleLinkListAddEnd(&WaterFlowList, &WaterFlowData);
    gra3dSetSGDMaterialAlpha((SGDFILEHEADER *)pSgdTop, (u_char)EffectWaterFlowGetAlpha(&WaterFlowData));
}

// The five deletes share one shape: walk the list, drop the entries whose id
// matches, every entry for a negative id (FF3; FF2 matched only, and its water
// delete also stopped the river cues).
// FF3 0x0016e150; FF2 EffectLightComeInDeleteMapBuffId (MioPan effect_obj.c:3169).

void EffectLightComeInDeleteMapBuffId(int MapBuffId)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    LIGHT_COME_IN_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&LightComeInList); pCell != nullptr; pCell = pNext)
    {
        pData = (LIGHT_COME_IN_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (MapBuffId < 0 || pData->MapBuffId == MapBuffId)
            SingleLinkListRemove(&LightComeInList, pCell);
    }
}

// The angle rate: 1 with the camera looking along or against the model's
// z axis, 0 across it (yaw only). FF2's EffectLightComeInMatrix2Rot is
// inlined: the axis is rows 0..2 of the first coordinate's local-world
// matrix times (0, 0, 1), w 1.
// FF3 0x0016d438; FF2 EffectLightComeInCalcAlphaRateCamDir (MioPan effect_obj.c:3288).
// callcheck: sceVu0ApplyMatrix (the ROM multiplies inline)

float EffectLightComeInCalcAlphaRateCamDir(void *pSgdTop)
{
    static const sceVu0FVECTOR z_axis = { 0.0f, 0.0f, 1.0f, 1.0f };   // data 0x329490
    SGDCOORDINATE *pCoord;
    sceVu0FMATRIX m;
    sceVu0FVECTOR axis;
    float *cam_dir;
    float cam_rx, cam_ry;
    float lit_rx, lit_ry;
    float rate = 0.0f;
    int i;

    if (pSgdTop == nullptr)
        return rate;
    cam_dir = gra3dcamGetDirection();
    pCoord = SgdPtr<SGDCOORDINATE>(((SGDFILEHEADER *)pSgdTop)->pCoord);
    Vector2Rot(cam_dir, &cam_rx, &cam_ry);
    for (i = 0; i < 3; i++)
    {
        m[i][0] = pCoord->matLocalWorld[i][0];
        m[i][1] = pCoord->matLocalWorld[i][1];
        m[i][2] = pCoord->matLocalWorld[i][2];
        m[i][3] = pCoord->matLocalWorld[i][3];
    }
    m[3][0] = 0.0f;
    m[3][1] = 0.0f;
    m[3][2] = 0.0f;
    m[3][3] = 1.0f;                                             // rdata 0x36e678
    sceVu0ApplyMatrix(axis, m, (float *)z_axis);
    Vector2Rot(axis, &lit_rx, &lit_ry);

    while (cam_ry < 0.0f)
        cam_ry += 6.28318548f;                                  // rdata 0x36e67c
    while (lit_ry < 0.0f)
        lit_ry += 6.28318548f;
    rate = fabsf(cam_ry - lit_ry);
    if (3.14159274f < rate)                                     // rdata 0x36e680
        rate -= 3.14159274f;
    rate = fabsf(rate - 1.57079637f) / 1.57079637f;             // rdata 0x36e684
    return rate;
}

// The distance rate: 0 nearer than 20 to the model's origin, rising to 1
// at 100 (FF2: 100 .. 500).
// FF3 0x0016d5b0; FF2 EffectLightComeInCalcAlphaRateCamDist (MioPan effect_obj.c:2826).

float EffectLightComeInCalcAlphaRateCamDist(void *pSgdTop)
{
    SGDCOORDINATE *pCoord;
    float dist;

    if (pSgdTop == nullptr)
        return 0.0f;
    pCoord = SgdPtr<SGDCOORDINATE>(((SGDFILEHEADER *)pSgdTop)->pCoord);
    dist = Get2PLength(gra3dcamGetPosition(), pCoord->matLocalWorld[3]);
    if (dist < 20.0f)                                           // rdata 0x36e68c
        return 0.0f;
    if (dist < 100.0f)                                          // rdata 0x36e690
        return (dist - 20.0f) / 80.0f;                          // rdata 0x36e694
    return 1.0f;                                                // rdata 0x36e688
}

// The record of pSgdTop's model, nullptr if it has none.
// FF3 0x0016e3b8; FF2 EffectLightComeInGetDataPtr (MioPan effect_obj.c:2715).

LIGHT_COME_IN_DATA *EffectLightComeInGetDataPtr(void *pSgdTop)
{
    SLL_CELL *pCell;
    LIGHT_COME_IN_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&LightComeInList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (LIGHT_COME_IN_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pData->pSgdTop == pSgdTop)
            return pData;
    }
    return nullptr;
}

// One frame of a light-in surface: its ST scrolled by the type's step (FF2
// EffectLightComeInGetScrollU / V inlined: U 12 / 11 / 30, V 13 / 8 / 10 for
// types 1 / 2 / else, in 1 / 10000), each total kept inside -1 .. 1 (FF2
// AddSTScrollCtrl inlined); the material alpha is AlphaMax times the camera
// angle rate, or the smaller of it and the distance rate in mode 1; then the
// type's vertex colours.
// FF3 0x0016e1f8; FF2 EffectLightComeInExecOne (MioPan effect_obj.c:2860).

void EffectLightComeInExecOne(LIGHT_COME_IN_DATA *pData, int AlphaCalcMode)
{
    float AddS, AddT;
    float AlphaRate, DistRate;
    int u, v;

    if (pData == nullptr)
        return;

    if (pData->Type == 1)
        u = 0x270c;
    else if (pData->Type == 2)
        u = 0x270b;
    else
        u = 0x271e;
    AddS = (float)(u - 10000) / 10000.0f;                       // rdata 0x36e6cc
    if (pData->Type == 1)
        v = 0x270d;
    else if (pData->Type == 2)
        v = 0x2708;
    else
        v = 0x270a;
    AddT = (float)(v - 10000) / 10000.0f;
    if (AddS + pData->TotalS > 1.0f)                            // rdata 0x36e6d0
        AddS -= 1.0f;
    else if (AddS + pData->TotalS < -1.0f)                      // rdata 0x36e6d4
        AddS += 1.0f;
    if (AddT + pData->TotalT > 1.0f)
        AddT -= 1.0f;
    else if (AddT + pData->TotalT < -1.0f)
        AddT += 1.0f;
    pData->TotalS += AddS;
    pData->TotalT += AddT;
    gra3dChangeST((SGDFILEHEADER *)pData->pSgdTop, AddS, AddT);

    AlphaRate = EffectLightComeInCalcAlphaRateCamDir(pData->pSgdTop);
    if (AlphaCalcMode == 1)
    {
        DistRate = EffectLightComeInCalcAlphaRateCamDist(pData->pSgdTop);
        if (DistRate < AlphaRate)
            AlphaRate = DistRate;
    }
    gra3dSetSGDMaterialAlpha((SGDFILEHEADER *)pData->pSgdTop, (u_char)(int)((float)pData->AlphaMax * AlphaRate));
    EffectLightComeInSetVertexColor(pData->pSgdTop, pData->Type);
}

// FF3 0x0016e500; FF2 EffectWaterFlowDelete (MioPan effect_obj.c:3056).

void EffectWaterFlowDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    WATER_FLOW_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&WaterFlowList); pCell != nullptr; pCell = pNext)
    {
        pData = (WATER_FLOW_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (Id < 0 || pData->Id == Id)
            SingleLinkListRemove(&WaterFlowList, pCell);
    }
}

// The water flow move types (data 0x2f8918): rivers scroll one way, lakes
// drift through three legs; the alpha pulses between min and max.
static const WATER_FLOW_MOVE_DAT water_flow_move_dat[12] = {
    { { { 10000,  9970 }, { 10000,  9965 }, { 10000,  9969 } }, 128,  63, 118, 2, { 363, 90, 279 } },
    { { { 10000,  9975 }, { 10000, 10000 }, {  9980, 10000 } }, 170, 150,  64, 2, { 363, 90, 279 } },
    { { {  9994, 10000 }, { 10004, 10000 }, { 10003, 10000 } }, 128,   0,   0, 1, { 363, 90, 279 } },
    { { {  9984, 10000 }, {  9983, 10000 }, {  9984, 10000 } }, 105,  69,  63, 2, { 363, 90, 279 } },
    { { { 10004, 10000 }, { 10003, 10000 }, { 10003, 10000 } },  70,  30,  72, 2, { 363, 90, 279 } },
    { { {  9994, 10000 }, { 10004, 10000 }, { 10003, 10000 } }, 128,   0,   0, 1, { 363, 90, 279 } },
    { { { 10004, 10000 }, {  9998,  9997 }, {  9996, 10000 } },  70,  50,  65, 2, { 363, 90, 279 } },
    { { { 10003,  9998 }, { 10000, 10004 }, {  9997, 10004 } },  80,  62,  51, 2, { 363, 90, 279 } },
    { { { 10000, 10000 }, { 10000, 10000 }, { 10000, 10000 } },   0,   0,   0, 1, { 363, 90, 279 } },
    { { { 10000, 10000 }, { 10000, 10000 }, { 10000, 10000 } },   0,   0,   0, 1, { 363, 90, 279 } },
    { { { 10000, 10000 }, { 10000, 10000 }, { 10000, 10000 } },   0,   0,   0, 1, { 363, 90, 279 } },
    { { { 10000, 10000 }, { 10000, 10000 }, { 10000, 10000 } },   0,   0,   0, 1, { 363, 90, 279 } },
};

// A move type's row; out of range (unsigned > 11) reads row 0.
// FF3 0x0016e450 (FF2's slot held EffectLightComeInGetScrollU).

const WATER_FLOW_MOVE_DAT *EffectWaterFlowGetMoveDat(int MoveType)
{
    if ((u_int)MoveType > 11)
        MoveType = 0;
    return &water_flow_move_dat[MoveType];
}

// The scroll of one leg: hold A, A -> B over 60 frames, hold B, B -> C, hold
// C, C -> A; legs past 5 give 0. axis 0 S, 1 T.
static float EffectWaterFlowGetAdd(int MoveType, int MoveDataNo, int Count, int axis)
{
    const WATER_FLOW_MOVE_DAT *pDat = EffectWaterFlowGetMoveDat(MoveType);
    float c = (float)(pDat->AddST[2][axis] - 10000) / 10000.0f;     // rdata 0x36e790 / 0x36e798
    float a = (float)(pDat->AddST[0][axis] - 10000) / 10000.0f;
    float b = (float)(pDat->AddST[1][axis] - 10000) / 10000.0f;

    switch (MoveDataNo)
    {
    case 0: return a;
    case 1: return (b - a) * (float)Count / 60.0f + a;              // 0x36e794 / 0x36e79c
    case 2: return b;
    case 3: return (c - b) * (float)Count / 60.0f + b;
    case 4: return c;
    case 5: return (a - c) * (float)Count / 60.0f + c;
    }
    return 0.0f;
}

// FF3 0x002b37e8 (out of line); FF2 EffectWaterFlowGetAddS (a switch per type there).
// callcheck: EffectWaterFlowGetMoveDat (through EffectWaterFlowGetAdd)

static float EffectWaterFlowGetAddS(int MoveType, int MoveDataNo, int Count)
{
    return EffectWaterFlowGetAdd(MoveType, MoveDataNo, Count, 0);
}

// FF3 0x002b3948 (out of line); FF2 EffectWaterFlowGetAddT.
// callcheck: EffectWaterFlowGetMoveDat (through EffectWaterFlowGetAdd)

static float EffectWaterFlowGetAddT(int MoveType, int MoveDataNo, int Count)
{
    return EffectWaterFlowGetAdd(MoveType, MoveDataNo, Count, 1);
}

// The hold length of a leg (the move table's frames), 60 for a transition.
// FF3 0x0016e610; FF2 EffectWaterFlowGetChangeFrame (fixed 363 / 90 / 279 there).

static int EffectWaterFlowGetChangeFrame(int MoveType, int MoveDataNo)
{
    const WATER_FLOW_MOVE_DAT *pDat = EffectWaterFlowGetMoveDat(MoveType);

    switch (MoveDataNo)
    {
    case 0: return pDat->Frame[0];
    case 1:
    case 3:
    case 5: return 60;
    case 2: return pDat->Frame[1];
    case 4: return pDat->Frame[2];
    }
    return 0;
}

// Next leg once the current one has run its frames; every move type cycles
// (FF2: only the lakes).
// FF3 0x0016e680; FF2 EffectWaterFlowChangeCtrl.

static void EffectWaterFlowChangeCtrl(WATER_FLOW_DATA *pData)
{
    if (pData->Count < EffectWaterFlowGetChangeFrame(pData->MoveType, pData->MoveDataNo))
    {
        pData->Count++;
    }
    else
    {
        pData->Count = 0;
        pData->MoveDataNo++;
        if (pData->MoveDataNo > 5)
            pData->MoveDataNo = 0;
    }
}

// The surface's alpha this frame: its phase steps pi / AlphaPeriod (wrapped
// to -pi..pi) and the alpha swings sinusoidally between AlphaMin and
// AlphaMax. FF3-only (FF2's alpha was fixed per type at registration).
// FF3 0x002b36e8 (out of line).

static int EffectWaterFlowGetAlpha(WATER_FLOW_DATA *pData)
{
    const WATER_FLOW_MOVE_DAT *pDat = EffectWaterFlowGetMoveDat(pData->MoveType);
    int   a0 = pDat->AlphaMin;
    int   a1 = pDat->AlphaMax;
    float ph;

    if (pDat->AlphaPeriod < 1)
        return a0;

    ph = pData->AlphaPhase + 3.1415927f / (float)pDat->AlphaPeriod;  // rdata 0x36e780
    pData->AlphaPhase = ph;
    if (ph < -3.1415927f)                                           // 0x36e784
        pData->AlphaPhase = ph + 6.2831855f;                        // 0x36e788
    else if (ph > 3.1415927f)
        pData->AlphaPhase = ph - 6.2831855f;

    return (int)((float)(a1 - a0) * 0.5f * Vu0Sinf(pData->AlphaPhase)   // 0x36e78c
               + (float)(a1 + a0) * 0.5f);
}

// One surface: this leg's scroll, wrapped so the running total stays within
// -1..1 (FF2's EffectLightComeInAddSTScrollCtrl, inlined), handed to the
// model as a delta; then the leg step and the alpha.
// FF3 0x0016e6e0; FF2 EffectWaterFlowExecOne.

static void EffectWaterFlowExecOne(WATER_FLOW_DATA *pData)
{
    float AddS, AddT;

    if (pData == nullptr)
        return;

    AddS = EffectWaterFlowGetAddS(pData->MoveType, pData->MoveDataNo, pData->Count);
    AddT = EffectWaterFlowGetAddT(pData->MoveType, pData->MoveDataNo, pData->Count);

    if (AddS + pData->TotalS > 1.0f)                                // rdata 0x36e6e0
        AddS -= 1.0f;
    else if (AddS + pData->TotalS < -1.0f)                          // 0x36e6e4
        AddS += 1.0f;
    if (AddT + pData->TotalT > 1.0f)
        AddT -= 1.0f;
    else if (AddT + pData->TotalT < -1.0f)
        AddT += 1.0f;

    pData->TotalS += AddS;
    pData->TotalT += AddT;
    gra3dChangeST((SGDFILEHEADER *)pData->pSgdTop, AddS, AddT);
    EffectWaterFlowChangeCtrl(pData);
    gra3dSetSGDMaterialAlpha((SGDFILEHEADER *)pData->pSgdTop, (u_char)EffectWaterFlowGetAlpha(pData));
}

// Every registered surface, once a frame (EffectInPhoto).
// FF3 0x0016e5a8; FF2 EffectWaterFlowExec.

void EffectWaterFlowExec(void)
{
    SLL_CELL *pCell;

    for (pCell = SingleLinkListBeginCell(&WaterFlowList); pCell != nullptr; pCell = SingleLinkListNextCell(pCell))
        EffectWaterFlowExecOne((WATER_FLOW_DATA *)SingleLinkListCellBodyPtr(pCell));
}

// FF3 0x0016e860; FF2 EffectModelAlphaChangeDelete (MioPan effect_obj.c:4045).

void EffectModelAlphaChangeDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); pCell != nullptr; pCell = pNext)
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (Id < 0 || pData->Id == Id)
            SingleLinkListRemove(&ModelAlphaChangeList, pCell);
    }
}

// FF3 0x0016ec70; FF2 EffectTourouFreaDelete (MioPan effect_obj.c:4190).

void EffectTourouFreaDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    TOUROU_FREA_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouFreaList); pCell != nullptr; pCell = pNext)
    {
        pData = (TOUROU_FREA_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (Id < 0 || pData->Id == Id)
            SingleLinkListRemove(&TourouFreaList, pCell);
    }
}

// FF3 0x0016f110; FF2 EffectTourouBaseDelete.

void EffectTourouBaseDelete(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    TOUROU_BASE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouBaseList); pCell != nullptr; pCell = pNext)
    {
        pData = (TOUROU_BASE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (Id < 0 || pData->Id == Id)
            SingleLinkListRemove(&TourouBaseList, pCell);
    }
}

// A model joins the alpha ramps, not ramping yet.
// FF3 0x0016e818; FF2 EffectModelAlphaChangeRegist (MioPan effect_obj.c:4027).

void EffectModelAlphaChangeRegist(void *pSgdTop, int Id)
{
    MODEL_ALPHA_CAHNGE_DATA AlphaChangeData;

    if (pSgdTop == nullptr)
        return;

    AlphaChangeData.pSgdTop = pSgdTop;
    AlphaChangeData.AlphaStart = 0;
    AlphaChangeData.AlphaEnd = 0;
    AlphaChangeData.Time = 0;
    AlphaChangeData.Counter = 0;
    AlphaChangeData.Id = Id;
    SingleLinkListAddEnd(&ModelAlphaChangeList, &AlphaChangeData);
}

// Every entry of group Id (Id / 1000).
// FF3 0x0016e908; FF2 EffectModelAlphaChangeDeleteGroup (MioPan effect_obj.c:4067).

void EffectModelAlphaChangeDeleteGroup(int Id)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); pCell != nullptr; pCell = pNext)
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        pNext = SingleLinkListNextCell(pCell);
        if (pData->Id / 1000 == Id)
            SingleLinkListRemove(&ModelAlphaChangeList, pCell);
    }
}

// Arms the ramp of every entry Id: AlphaStart -> AlphaEnd over Time frames,
// AlphaStart applied at once.
// FF3 0x0016eab0; FF2 EffectModelAlphaChangeReq (MioPan effect_obj.c:4125);
// FF3 sets the SGD's material alpha (FF2 ManmdlSetAlpha).

void EffectModelAlphaChangeReq(int Id, int AlphaStart, int AlphaEnd, int Time)
{
    SLL_CELL *pCell;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pData->Id == Id)
        {
            pData->AlphaStart = AlphaStart;
            pData->AlphaEnd = AlphaEnd;
            pData->Time = Time;
            pData->Counter = 0;
            gra3dSetSGDMaterialAlpha((SGDFILEHEADER *)pData->pSgdTop, (u_char)pData->AlphaStart);
        }
    }
}

// The lantern flame's two scales and alpha over its two half cycles of
// AllTime frames: X 0.95 -> 0.61 -> 0.95, Y 1.16 -> 1.27 -> 1.16, alpha
// 68 -> 86 -> 68; AllTime 0 gives 1.0 / 1.0 / 0x80.
// FF3 0x0016ed18; FF2 EffectTourouFreaGetScaleX (MioPan effect_obj.c:4214).

static float EffectTourouFreaGetScaleX(int Counter, int AllTime)
{
    if (AllTime == 0)
        return 1.0f;                                            // rdata 0x36e6f4
    if (Counter < AllTime)
        return ((float)Counter / (float)AllTime) * -0.34f + 0.95f;              // rdata 0x36e6f8 / 0x36e6fc
    return ((float)(Counter - AllTime) / (float)AllTime) * 0.34f + 0.61f;       // rdata 0x36e700 / 0x36e704
}

// FF3 0x0016edb8; FF2 EffectTourouFreaGetScaleY (MioPan effect_obj.c:4229).

static float EffectTourouFreaGetScaleY(int Counter, int AllTime)
{
    if (AllTime == 0)
        return 1.0f;                                            // rdata 0x36e708
    if (Counter < AllTime)
        return ((float)Counter / (float)AllTime) * 0.11f + 1.16f;               // rdata 0x36e70c / 0x36e710
    return ((float)(Counter - AllTime) / (float)AllTime) * -0.11f + 1.27f;      // rdata 0x36e714 / 0x36e718
}

// FF3 0x0016ee58; FF2 EffectTourouFreaGetAlpha (MioPan effect_obj.c:4246).

static int EffectTourouFreaGetAlpha(int Counter, int AllTime)
{
    if (AllTime == 0)
        return 0x80;
    if (Counter < AllTime)
        return (int)(((float)Counter / (float)AllTime) * 18.0f + 68.0f);        // rdata 0x36e71c / 0x36e720
    return (int)(((float)(Counter - AllTime) / (float)AllTime) * -18.0f + 86.0f);   // rdata 0x36e724 / 0x36e728
}

// A lantern flame: a 34-frame half cycle and the 150-frame flame counter,
// both at a random phase.
// FF3 0x0016eb88; FF2 EffectTourouFreaRegist (MioPan effect_obj.c:4165).
// callcheck: rand (EffRand01: ReiPan_Rand; FF2 EffectGetRandom inlined)

void EffectTourouFreaRegist(void *pHandle, int Id)
{
    TOUROU_FREA_DATA TourouFreaData;

    if (pHandle == nullptr)
        return;

    TourouFreaData.pHandle = pHandle;
    TourouFreaData.Time = 0x22;
    TourouFreaData.Id = Id;
    TourouFreaData.Counter = (int)(EffRand01() * 34.0f);       // rdata 0x36e6e8 / 0x36e6ec
    TourouFreaData.FlameCounter = (int)(EffRand01() * 149.0f); // rdata 0x36e6f0
    TourouFreaData.ScaleX = EffectTourouFreaGetScaleX(TourouFreaData.Counter, TourouFreaData.Time);
    TourouFreaData.ScaleY = EffectTourouFreaGetScaleY(TourouFreaData.Counter, TourouFreaData.Time);
    TourouFreaData.Alpha = EffectTourouFreaGetAlpha(TourouFreaData.Counter, TourouFreaData.Time);
    SingleLinkListAddEnd(&TourouFreaList, &TourouFreaData);
}

// One frame of a flame: Counter wraps at 2 * Time, FlameCounter at 150.
// FF3 0x0016ef00; FF2 EffectTourouFreaExecOne (MioPan effect_obj.c:4262).

void EffectTourouFreaExecOne(TOUROU_FREA_DATA *pData)
{
    if (pData == nullptr)
        return;

    pData->Counter++;
    if (pData->Counter >= pData->Time * 2)
        pData->Counter = 0;
    pData->FlameCounter++;
    if (pData->FlameCounter > 0x95)
        pData->FlameCounter = 0;
    pData->ScaleX = EffectTourouFreaGetScaleX(pData->Counter, pData->Time);
    pData->ScaleY = EffectTourouFreaGetScaleY(pData->Counter, pData->Time);
    pData->Alpha = EffectTourouFreaGetAlpha(pData->Counter, pData->Time);
}

// The flame of model handle pHandle, nullptr if none.
// FF3 0x0016ef90; FF2 EffectTourouFreaGetDataPtr (MioPan effect_obj.c:4287).

TOUROU_FREA_DATA *EffectTourouFreaGetDataPtr(void *pHandle)
{
    SLL_CELL *pCell;
    TOUROU_FREA_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouFreaList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (TOUROU_FREA_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pData->pHandle == pHandle)
            return pData;
    }
    return nullptr;
}

// The lantern base's colour over its two half cycles: (70, 23, 28) up to
// (97, 43, 40) and back; Color[3] untouched.
// FF3 0x0016f1b8; FF2 EffectTourouBaseGetColor (MioPan effect_obj.c:4372).

static void EffectTourouBaseGetColor(int *Color, int Counter, int AllTime)
{
    float t;

    if (Counter < AllTime)
    {
        t = (float)Counter / (float)AllTime;
        Color[2] = (int)(t * 12.0f + 28.0f);                    // rdata 0x36e738 / 0x36e744
        Color[0] = (int)(t * 27.0f + 70.0f);                    // rdata 0x36e73c / 0x36e748
        Color[1] = (int)(t * 20.0f + 23.0f);                    // rdata 0x36e740 / 0x36e74c
    }
    else
    {
        t = (float)(Counter - AllTime) / (float)AllTime;
        Color[2] = (int)(t * -12.0f + 40.0f);                   // rdata 0x36e750 / 0x36e75c
        Color[0] = (int)(t * -27.0f + 97.0f);                   // rdata 0x36e754 / 0x36e760
        Color[1] = (int)(t * -20.0f + 43.0f);                   // rdata 0x36e758 / 0x36e764
    }
}

// A lantern base: a random half cycle of 3 .. 37 frames at a random phase,
// alpha 0xba.
// FF3 0x0016f040; FF2 EffectTourouBaseRegist (MioPan effect_obj.c:4323).
// callcheck: rand (EffRand01: ReiPan_Rand; FF2 EffectGetRandom inlined)

void EffectTourouBaseRegist(void *pSgdTop, int Id)
{
    TOUROU_BASE_DATA TourouBaseData;

    if (pSgdTop == nullptr)
        return;

    TourouBaseData.pSgdTop = pSgdTop;
    TourouBaseData.Id = Id;
    TourouBaseData.Time = (int)(EffRand01() * 34.0f + 3.0f);   // rdata 0x36e72c / 0x36e730 / 0x36e734
    TourouBaseData.Counter = (int)(EffRand01() * (float)TourouBaseData.Time);
    EffectTourouBaseGetColor(TourouBaseData.Color, TourouBaseData.Counter, TourouBaseData.Time);
    TourouBaseData.Color[3] = 0xba;
    SingleLinkListAddEnd(&TourouBaseList, &TourouBaseData);
}

// One frame of a base; each new cycle draws a new length.
// FF3 0x0016f320; FF2 EffectTourouBaseExecOne (MioPan effect_obj.c:4405).
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffectTourouBaseExecOne(TOUROU_BASE_DATA *pData)
{
    if (pData == nullptr)
        return;

    pData->Counter++;
    if (pData->Counter >= pData->Time * 2)
    {
        pData->Counter = 0;
        pData->Time = (int)(EffRand01() * 34.0f + 3.0f);       // rdata 0x36e768 / 0x36e76c / 0x36e770
    }
    EffectTourouBaseGetColor(pData->Color, pData->Counter, pData->Time);
}

// The base of model pSgdTop, nullptr if none.
// FF3 0x0016f3d8; FF2 EffectTourouBaseGetDataPtr (MioPan effect_obj.c:4422).

TOUROU_BASE_DATA *EffectTourouBaseGetDataPtr(void *pSgdTop)
{
    SLL_CELL *pCell;
    TOUROU_BASE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&TourouBaseList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (TOUROU_BASE_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pData->pSgdTop == pSgdTop)
            return pData;
    }
    return nullptr;
}

// The three lists EffKind1DInit sets up (named for it; none is kind 0x1d's
// own): B, the falling particles (EffFallPart*); A, the map lamps
// (EffLamp*); C, the particle bursts (EffParticle*).
static SINGLE_LINK_LIST EffKind1DListB;         // bss 0x3d2180, 0x60-byte entries
static SINGLE_LINK_LIST EffKind1DListA;         // bss 0x3d2190, 0x40-byte entries
static SINGLE_LINK_LIST EffKind1DListC;         // bss 0x3d45a0, 0x70-byte entries

// FF3 0x00172560.

static void EffKind1DListAInit(void)
{
    SingleLinkListInit(&EffKind1DListA, 0x40);
}

// FF3 0x001716f8.

static void EffKind1DListBInit(void)
{
    SingleLinkListInit(&EffKind1DListB, 0x60);
}

// FF3 0x00172800.

static void EffKind1DListCInit(void)
{
    SingleLinkListInit(&EffKind1DListC, 0x70);
}

// FF3 0x001726a0.

static void EffKind1DListAFree(void)
{
    SingleLinkListAllCellFree(&EffKind1DListA);
}

// FF3 0x0016f910.

void EffKind1DInit(void)
{
    EffKind1DListAInit();
    EffKind1DListBInit();
    EffKind1DListCInit();
}

// List A emptied, every entry of B and C deleted.
// FF3 0x0016f940.

void EffKind1DReleaseAll(void)
{
    EffKind1DListAFree();
    EffFallPartCut(-1);
    EffParticleCut(-1, -1);
}

// ──────────────────────────────────────────────────────────────────────
// The room SCL buffers (data 0x2f9210)
// ──────────────────────────────────────────────────────────────────────

// Five rooms' records and the default one past them (FF3-only). Prm is the
// particle parameter block effect_oth.c's FUN_0017de50 / EffMapPartReq read
// (not decoded yet, kept as the ROM's words).
static EFF_OBJ_BUF eff_obj_buf_tbl[EFF_OBJ_BUF_NUM + 1] = {
    {   // data 0x2f9210
        6,
        {
            0x00000000, 0x00000000, 0x00000000, 0x43960000, 0x41f00000, 0x43960000, 0x00000000, 0x3f800000,
            0x00000258, 0x00000000, 0x00000000, 0x3e8f5c28, 0x3dcccccc, 0x3e8f5c28, 0x00000000, 0x3f4ccccc,
            0x3f7851eb, 0x00000000, 0x00000000, 0x3eff7036, 0x3dccaa32, 0xbdcccccc, 0x00000000, 0x3effffde,
            0x3dcccccc, 0x3dcccc46, 0x00000000, 0x3eb33333, 0x3e1999dc, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x00000028, 0x00000014, 0x00000000, 0x00000000, 0x433e0000,
            0x41a00000, 0x433e0000, 0x00000000, 0x41200000, 0x000000f0, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbca3d70a,
            0x3ecccccc, 0xbca3d70a, 0x00000000, 0x3ca3d70a, 0x3f000000, 0x3ca3d70a, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3dcccccc, 0x3ca3d70a, 0x00000080, 0x00000080, 0x00000080, 0x00000012,
            0x00000012, 0x00000000, 0x00000000,
        },
        nullptr,
    },
    {   // data 0x2f9350
        9,
        {
            0x00000000, 0x00000000, 0x00000000, 0x432a0000, 0x42200000, 0x42dc0000, 0x00000000, 0x41200000,
            0x00000258, 0x00000000, 0x00000000, 0x3e8f5c28, 0x3dcccccc, 0x3e8f5c28, 0x00000000, 0x3f4ccccc,
            0x3f7851eb, 0x00000000, 0x00000000, 0x3eff7036, 0x3dccaa32, 0xbdcccccc, 0x00000000, 0x3effffde,
            0x3dcccccc, 0x3dcccc46, 0x00000000, 0x3e999999, 0x3e4ccd0f, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x430c0000,
            0x41200000, 0x42a00000, 0x00000000, 0x41200000, 0x000000fa, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbca3d70a,
            0x3ecccccc, 0xbca3d70a, 0x00000000, 0x3ca3d70a, 0x3f000000, 0x3ca3d70a, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3dcccccc, 0x3ca3d70a, 0x00000080, 0x00000080, 0x00000080, 0x00000012,
            0x00000012, 0x00000000, 0x00000000,
        },
        nullptr,
    },
    {   // data 0x2f9490
        10,
        {
            0x00000000, 0x00000000, 0x00000000, 0x40800000, 0x41200000, 0x41200000, 0x00000000, 0x41f80000,
            0x00000078, 0x00000000, 0x00000000, 0x3e4ccccc, 0x3dcccccc, 0x3e4ccccc, 0x00000000, 0x3f4ccccc,
            0x3f7851eb, 0x00000000, 0x00000000, 0x3eb33333, 0x3f266666, 0xbe4ccccc, 0x00000000, 0x3f000000,
            0x3f599999, 0x3e4ccccc, 0x00000000, 0x3e6147ae, 0x3dcccccc, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x0000001e, 0x00000028, 0x00000000, 0x00000000, 0x420c0000,
            0x41200000, 0x43480000, 0x00000000, 0x41200000, 0x00000096, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbca3d70a,
            0x3ecccccc, 0xbca3d70a, 0x00000000, 0x3ca3d70a, 0x3f000000, 0x3ca3d70a, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3dcccccc, 0x3ca3d70a, 0x00000080, 0x00000080, 0x00000080, 0x00000032,
            0x00000010, 0x00000000, 0x00000000,
        },
        nullptr,
    },
    {   // data 0x2f95d0
        34,
        {
            0x00000000, 0x00000000, 0x00000000, 0x42c80000, 0x40800000, 0x420c0000, 0x00000000, 0x42340000,
            0x00000190, 0x00000000, 0x00000000, 0x3e8f5c28, 0x3dcccccc, 0x3e8f5c28, 0x00000000, 0x3f4ccccc,
            0x3f7851eb, 0x00000000, 0x00000000, 0x3dca8da7, 0x3dccaa32, 0x3f000000, 0x00000000, 0x3e999978,
            0x3dcccccc, 0x3effffde, 0x00000000, 0x3e999999, 0x3e4ccd0f, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x427c0000,
            0x41200000, 0x43660000, 0x00000000, 0x41980000, 0x00000064, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbe4ccccc,
            0xbe4ccccc, 0xbe4ccccc, 0x00000000, 0x3e4ccccc, 0x3e4ccccc, 0x3e4ccccc, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3df5c28f, 0x3ca3d70a, 0x000000ff, 0x00000080, 0x00000080, 0x00000012,
            0x00000012, 0x00000000, 0x00000000,
        },
        nullptr,
    },
    {   // data 0x2f9710
        35,
        {
            0x00000000, 0x00000000, 0x00000000, 0x42200000, 0x40800000, 0x425c0000, 0x00000000, 0x41f80000,
            0x00000190, 0x00000000, 0x00000000, 0x3e8f5c28, 0x3dcccccc, 0x3e8f5c28, 0x00000000, 0x3f4ccccc,
            0x3f7851eb, 0x00000000, 0x00000000, 0x3eff7036, 0x3dccaa32, 0xbdcccccc, 0x00000000, 0x3effffde,
            0x3dcccccc, 0x3dcccc46, 0x00000000, 0x3e999999, 0x3e4ccd0f, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x427c0000,
            0x41200000, 0x43660000, 0x00000000, 0x41980000, 0x00000064, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbe4ccccc,
            0xbe4ccccc, 0xbe4ccccc, 0x00000000, 0x3e4ccccc, 0x3e4ccccc, 0x3e4ccccc, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3df5c28f, 0x3ca3d70a, 0x000000ff, 0x00000080, 0x00000080, 0x00000012,
            0x00000012, 0x00000000, 0x00000000,
        },
        nullptr,
    },
    {   // data 0x2f9850 (the default, past the five)
        -1,
        {
            0x00000000, 0x00000000, 0x00000000, 0x42b00000, 0x40800000, 0x41000000, 0x00000000, 0x42500000,
            0x00000190, 0x00000000, 0x00000000, 0x3e8f5c28, 0x3dcccccc, 0x3e8f5c28, 0x00000000, 0x3f4ccccc,
            0x3f733333, 0x00000000, 0x00000000, 0x3eff7036, 0x3dccaa32, 0xbdcccccc, 0x00000000, 0x3effffde,
            0x3dcccccc, 0x3dcccc46, 0x00000000, 0x3e999999, 0x3e4ccd0f, 0x3f800000, 0x3dcccccc, 0x3ca3d70a,
            0x000000ff, 0x000000ff, 0x000000ff, 0x00000020, 0x00000020, 0x00000000, 0x00000000, 0x427c0000,
            0x41200000, 0x43660000, 0x00000000, 0x41980000, 0x00000064, 0x00000000, 0x00000000, 0x3e4ccccc,
            0x3d23d70a, 0x3e4ccccc, 0x00000000, 0x3f199999, 0x3f7851eb, 0x00000000, 0x00000000, 0xbe4ccccc,
            0xbe4ccccc, 0xbe4ccccc, 0x00000000, 0x3e4ccccc, 0x3e4ccccc, 0x3e4ccccc, 0x00000000, 0x411ffd98,
            0x408fff82, 0x3f7fffef, 0x3df5c28f, 0x3ca3d70a, 0x000000ff, 0x00000080, 0x00000080, 0x00000012,
            0x00000012, 0x00000000, 0x00000000,
        },
        nullptr,
    },
};

// The room -> file pairs (data 0x2f9990; a zero pair ends it).
static const int eff_obj_buf_file[EFF_OBJ_BUF_NUM][2] = {
    { 6, RS106_SCL }, { 9, RS203_SCL }, { 10, RS204_SCL }, { 0x22, RS407_SCL }, { 0x23, RS408_SCL },
};

static int eff_obj_buf_cur;                     // sdata 0x372f64 (gp-0x58c): EffObjBufTblSelect's record

static void EffObjBufTblLoad(void);
static void EffObjBufTblFree(void);

// InitEffects (first): no record current, every room's SCL file loaded.
// FF3 0x00176450.

void EffObjBufTblInit(void)
{
    eff_obj_buf_cur = -1;
    EffObjBufTblLoad();
}

// FF3 0x00176470.

void EffObjBufTblRelease(void)
{
    EffObjBufTblFree();
}

// FF3 0x00176488.

int EffObjIsLoadEnd(void)
{
    return IsLoadEndAll();
}

// The record of room id, -1 when none.
// FF3 0x001764a0.

int EffObjBufTblFind(int id)
{
    int i;

    for (i = 0; i < EFF_OBJ_BUF_NUM; i++)
    {
        if (eff_obj_buf_tbl[i].Id == id)
            return i;
    }
    return -1;
}

// FF3 0x001764d8 (FF2's matcher took it for global.constructors.keyed.to.efi).

void EffObjBufTblSelect(int id)
{
    eff_obj_buf_cur = EffObjBufTblFind(id);
}

// The current record; the default one when none is selected.
// FF3 0x001764f8.

EFF_OBJ_BUF *EffObjBufTblGet(void)
{
    if ((u_int)eff_obj_buf_cur < EFF_OBJ_BUF_NUM)
        return &eff_obj_buf_tbl[eff_obj_buf_cur];
    return &eff_obj_buf_tbl[EFF_OBJ_BUF_NUM];
}

// Each record's file (by the room pairs) into the system heap.
// FF3 0x00176548.

static void EffObjBufTblLoad(void)
{
    EFF_OBJ_BUF *rec;
    int i, file;

    for (rec = &eff_obj_buf_tbl[0]; rec < &eff_obj_buf_tbl[EFF_OBJ_BUF_NUM]; rec++)
    {
        rec->pBuf = nullptr;
        for (i = 0; i < EFF_OBJ_BUF_NUM; i++)
        {
            if (rec->Id != eff_obj_buf_file[i][0])
                continue;
            file = eff_obj_buf_file[i][1];
            if (GetFileSize(file) != (u_int)-1 && GetFileSize(file) != 0)
            {
                rec->pBuf = heapCtrlMalloc(GetSystemHeapWrkP(), GetFileSize(file), 0, 0, 0);
                if (rec->pBuf != nullptr)
                    LoadReq(file, rec->pBuf);
            }
            break;
        }
    }
}

// FF3 0x00176658.

static void EffObjBufTblFree(void)
{
    EFF_OBJ_BUF *rec;

    for (rec = &eff_obj_buf_tbl[0]; rec < &eff_obj_buf_tbl[EFF_OBJ_BUF_NUM]; rec++)
    {
        if (rec->pBuf != nullptr)
        {
            heapCtrlFree(GetSystemHeapWrkP(), rec->pBuf, 0, 0, 0);
            rec->pBuf = nullptr;
        }
    }
}

// A float's bits as the GS packet words take them (the ROM moves them raw).
static inline u_int FloatBits(float f)
{
    u_int u;

    memcpy(&u, &f, sizeof(u));
    return u;
}

// ──────────────────────────────────────────────────────────────────────
// Parts deform (kinds 0x16 / 0x1f; FF2 effect_obj.c)
// ──────────────────────────────────────────────────────────────────────

static u_char SubPartsDeform1(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate);
static u_char SubPartsDeform2(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate);

// The edge fade of the grid (rdata 0x3538f0; FF2's pdeform_alpha1, same bytes).
static const u_char pdeform_alpha1[PDEFORM_VERTEX_MAX] = {
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   1,  16,  32,  64,  64,  64,  64,  32,  32,  16,   0,   0,   0,
      0,   0,   0,   1,  32,  64,  96, 128, 128, 128,  96,  96,  64,  32,   8,   0,   0,
      0,   0,   1,  32,  64, 128, 128, 128, 128, 128, 128, 128,  96,  64,  32,  16,   0,
      0,   1,  32,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  64,  32,   0,
      0,  16,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  32,   0,
      1,  32,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  64,   1,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      8,  64, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,   8,
      1,  64,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  96,  32,   1,
      0,  32,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,  16,   0,
      0,  32,  64,  96, 128, 128, 128, 128, 128, 128, 128, 128, 128,  64,  32,   1,   0,
      0,  16,  32,  64,  96, 128, 128, 128, 128, 128, 128, 128,  64,  32,   1,   0,   0,
      0,   0,   8,  32,  64,  96,  96, 128, 128, 128,  96,  64,  32,   1,   0,   0,   0,
      0,   0,   0,  16,  32,  32,  64,  64,  64,  64,  32,  16,   1,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   1,   0,   0,   0,   0,
};

// The frame copy the deform samples: TEX0 of GS 0x2bc0 (LocalCopyLtoL's
// target), rdata 0x36e4b0.
#define PDEFORM_TEX0    0x2000000264116bc0ULL

// One deform record per frame: the level (in / keep / out envelope, or
// *pnt[1], or max) / 100, the speed / rate / trate from pnt[2] / pnt[4] /
// pnt[5], then pass 2 (uc8[2] / 10 == 2) or pass 1 by the sub-type
// uc8[2] % 10 (x5 of pass 2 only ends it). uc8[2] 25 runs at level 0. The
// slot number lives in uc8[4] between frames.
// FF3 0x00168490; FF2 SetPartsDeform (MioPan effect_obj.c:407).
// callcheck: __floatunsisf (the u_int products)

void SetPartsDeform(EFFECT_CONT *ec)
{
    int   ef = 0;
    int   n0;
    float sp, rt, vol, tr;

    n0 = ec->dat.uc8[2];
    if (n0 != 25)
    {
        if ((ec->dat.uc8[1] & 4) != 0)
        {
            if (ec->flow == 0)
            {
                if (ec->in != 0)
                    ef = (int)((float)(ec->cnt * ec->max) / (float)ec->in);
            }
            else if (ec->flow == 1)
            {
                ef = (int)ec->max;
            }
            else if (ec->flow == 2)
            {
                if (ec->out != 0)
                    ef = (int)ec->max - (int)((float)(ec->cnt * ec->max) / (float)ec->out);
            }
            else if (ec->flow == 3)
            {
                ResetEffects(ec);
                return;
            }
            EffInKeepOutFlowCtrl(ec);
        }
        else if (ec->pnt[1] == nullptr)
        {
            ef = (int)ec->max;
        }
        else
        {
            ef = (int)*(float *)ec->pnt[1];
        }
    }

    sp  = (ec->pnt[2] != nullptr) ? *(float *)ec->pnt[2] : 1.0f;          // rdata 0x36e3a8
    rt  = (ec->pnt[4] != nullptr) ? *(float *)ec->pnt[4] : 1.0f;
    tr  = (ec->pnt[5] != nullptr) ? 2.0f - *(float *)ec->pnt[5] : 1.0f;   // 0x36e3ac
    vol = (float)ef / 100.0f;                                               // 0x36e3b0

    if (n0 / 10 == 2)
    {
        if (n0 % 10 == 5)
            ResetEffects(ec);
        else
            ec->dat.uc8[4] = SubPartsDeform2(ec, ec->dat.uc8[4], 2, n0 % 10,
                                             ec->dat.fl32[2], ec->dat.fl32[3], vol, 1, sp, rt, tr);
    }
    else
    {
        ec->dat.uc8[4] = SubPartsDeform1(ec, ec->dat.uc8[4], n0 / 10, n0 % 10,
                                         ec->dat.fl32[2], ec->dat.fl32[3], vol, 1, sp, rt, tr);
    }

    if ((ec->dat.uc8[1] & 1) != 0)
        ResetEffects(ec);
}

// 1 for a deform record of pass 1 (uc8[2] / 10 == 1): it waits for the blur
// pass. FF3 checks kinds 0x16 / 0x1f (FF2 0x18 / 0x23).
// FF3 0x00168770; FF2 EffectObjPartsDeformBlurCheck (MioPan effect_obj.c:495).

int EffectObjPartsDeformBlurCheck(EFFECT_CONT *ec)
{
    if (ec->dat.uc8[0] == 0x16 || ec->dat.uc8[0] == 0x1f)
        return (ec->dat.uc8[2] / 10) == 1;
    return 0;
}

// vclipw.xyz v, v: the six flags of one clip test (+x, -x, +y, -y, +z, -z
// beyond |w|), the CLIP register's newest bits.
static u_int PartsDeformClipFlags(const float *v)
{
    float w = fabsf(v[3]);
    u_int flags = 0;

    if (v[0] > w)  flags |= 0x01;
    if (v[0] < -w) flags |= 0x02;
    if (v[1] > w)  flags |= 0x04;
    if (v[1] < -w) flags |= 0x08;
    if (v[2] > w)  flags |= 0x10;
    if (v[2] < -w) flags |= 0x20;
    return flags;
}

// The vertex's own w is ignored (vmaddw against vf0: always (x, y, z, 1)); x
// and y go out in 12.4 fixed (vftoi4), z as an integer (vftoi0), and the
// fourth word keeps the screen w as float bits (sqc2 writes all four).
// FF3 0x001687b8; FF2 CalcPartsDeformXYZ (MioPan effect_obj.c:542).
// PC: the matrices FF3 leaves in vf4..vf11 are arguments (see the header);
// pNdcZ, when given, receives the clip-space z / w the GS tested through
// XYZF -- MioPan's addition, for the host draw's depth test.

int CalcPartsDeformXYZ(int *vi, const float *vf,
                       sceVu0FMATRIX matLocalScreen, sceVu0FMATRIX matLocalClip,
                       float *pNdcZ)
{
    float vScreen[4], vClip[4];
    float q;
    int   c;

    for (c = 0; c < 4; c++)
    {
        vScreen[c] = matLocalScreen[0][c] * vf[0] + matLocalScreen[1][c] * vf[1]
                   + matLocalScreen[2][c] * vf[2] + matLocalScreen[3][c];
        vClip[c]   = matLocalClip[0][c] * vf[0] + matLocalClip[1][c] * vf[1]
                   + matLocalClip[2][c] * vf[2] + matLocalClip[3][c];
    }

    q = 1.0f / vScreen[3];
    vi[0] = (int)(vScreen[0] * q * 16.0f);
    vi[1] = (int)(vScreen[1] * q * 16.0f);
    vi[2] = (int)(vScreen[2] * q);
    memcpy(&vi[3], &vScreen[3], sizeof(float));
    // PC: every vertex a caller draws passed the +-w test, so w is positive
    // there; the guard is for the ones that did not.
    if (pNdcZ != nullptr)
        *pNdcZ = (vClip[3] != 0.0f) ? vClip[2] / vClip[3] : 0.0f;
    return (int)PartsDeformClipFlags(vClip);
}

// Is the grid's origin off screen? Its projection must land with x / y in
// 0x300..0x300 + 64000 and z in 1..0xfffff; non-zero clips.
// FF3 0x002b3678 (out of line); FF2 PartsDeformClipCheck (MioPan effect_obj.c:907).

static int PartsDeformClipCheck(sceVu0FMATRIX slm)
{
    static sceVu0FVECTOR origin = { 0.0f, 0.0f, 0.0f, 1.0f };  // rdata 0x329500
    sceVu0IVECTOR ivec;

    sceVu0RotTransPers(ivec, slm, origin, 0);
    if ((u_int)(ivec[0] - 0x300) > 64000)
        return 1;
    if ((u_int)(ivec[1] - 0x300) > 64000)
        return 1;
    return (u_int)(ivec[2] - 1) > 0xffffe;
}

// The grid as one strip per patch row: TEXFLUSH, TEX0, PRIM 0x5c (strip,
// gouraud, textured, blended), then per step a REGLIST of ST / RGBAQ / XYZF
// for the vertex on this row and the one below. A first column restarts the
// strip (both XYZF3); elsewhere each triangle kicks (XYZF2) only when none of
// its three corners left the guard band. Alpha is the edge fade times aprate.
// DrawEnvType non-zero tests z ALWAYS (the door seal) instead of GEQUAL.
// FF3 0x002b31b8 (out of line); FF2 MakePartsDeformPacket (MioPan effect_obj.c:610).
// PC: nothing walks the DIRECT chain, so the same grid also goes to the
// renderer as a triangle list (MioPan's bridge, below the packet); the GS
// 0x2bc0 / stash copies it samples are capture slots (g2d_draw.c).

static void MakePartsDeformPacket(int pnumw, int pnumh, sceVu0FVECTOR *vt, sceVu0FMATRIX wlm,
                                  sceVu0FVECTOR *stq, const u_char *use_alpha, float aprate,
                                  u_long tex0, int Red, int Green, int Blue, int DrawEnvType)
{
    static int clip[PDEFORM_VERTEX_MAX];            // PC: off the stack
    static sceVu0IVECTOR vtiw[PDEFORM_VERTEX_MAX];
    static float ndcz[PDEFORM_VERTEX_MAX];          // PC: each vertex's z / w, for the host draw
    GRA3DCAMERA *pCam;
    sceVu0FMATRIX matLocalScreen;
    sceVu0FMATRIX matLocalClip;
    GS_REG_SET regs[5];
    u_long *p;
    u_long rgb;
    int i, j, k, l;

    pCam = gra3dGetCamera();
    sceVu0MulMatrix(matLocalScreen, pCam->matWorldScreen, wlm);
    sceVu0MulMatrix(matLocalClip, pCam->matWorldClipPolygon, wlm);
    for (i = 0; i < (pnumw + 1) * (pnumh + 1); i++)
        clip[i] = CalcPartsDeformXYZ(vtiw[i], vt[i], matLocalScreen, matLocalClip, &ndcz[i]) & 0x3f;

    regs[0].data = 0x44;            regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 5;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = (DrawEnvType == 0) ? 0x5000d : 0x30003;    regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    p = (u_long *)StartDmaDirectTrans();
    p[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);     // 0x1000000000008002
    p[1] = SCE_GIF_PACKED_AD;
    p[2] = 0;
    p[3] = SCE_GS_TEXFLUSH;
    p[4] = tex0;
    p[5] = SCE_GS_TEX0_1;
    p[6] = 0x2400400000008001ULL;   // PACKED, 2 regs: PRIM, NOP
    p[7] = 0xf0;
    p[8] = 0x5c;
    p[9] = 0;
    p += 10;

    rgb = (u_long)(u_int)Red | ((u_long)(u_int)Green << 8) | ((u_long)(u_int)Blue << 16);
    for (i = 0; i < pnumh * (pnumw + 1); i++)
    {
        j = i + pnumw + 1;
        if (i % (pnumw + 1) == 0)
        {
            p[1] = 0xc12c12;                // ST RGBAQ XYZF3 x2
        }
        else
        {
            k = clip[i - 1] + clip[i + pnumw] + clip[i];
            l = clip[i + pnumw] + clip[i] + clip[j];
            if (k + l == 0)
                p[1] = 0x412412;            // both kick
            else if (k != 0)
                p[1] = (l != 0) ? 0xc12c12 : 0x412c12;
            else if (l != 0)
                p[1] = 0xc12412;
            else
                continue;                   // unreachable (k + l != 0), the ROM's
        }
        p[0] = 0x6400400000008001ULL;       // REGLIST, 6 regs (rdata 0x36e778)

        memcpy(&p[2], &stq[i][0], 8);
        p[3] = rgb | ((u_long)((u_int)(int)((float)use_alpha[i] * aprate) & 0xff) << 24)
             | ((u_long)FloatBits(stq[i][2]) << 32);
        p[4] = (u_long)(u_int)vtiw[i][0] | ((u_long)(u_int)vtiw[i][1] << 16)
             | ((u_long)(u_int)vtiw[i][2] << 32);
        memcpy(&p[5], &stq[j][0], 8);
        p[6] = rgb | ((u_long)((u_int)(int)((float)use_alpha[j] * aprate) & 0xff) << 24)
             | ((u_long)FloatBits(stq[j][2]) << 32);
        p[7] = (u_long)(u_int)vtiw[j][0] | ((u_long)(u_int)vtiw[j][1] << 16)
             | ((u_long)(u_int)vtiw[j][2] << 32);
        p += 8;
    }
    EndDmaDirectTrans((qword *)p);

    // PC: the strip as a triangle list (MioPan's bridge). The cell left of
    // column i is bounded by i - 1, i, i + pnumw and i + pnumw + 1; its two
    // triangles are the ones the strip kicks at i and at j, each drawn only
    // when none of its corners left the guard band, as above. vtiw is GS
    // window 12.4, so the 3D env's XYOFFSET (1728, 1824) comes back off; ST
    // is normalised and divided by Q. The draw env's ZTST (GEQUAL, or ALWAYS
    // for the door seal) is the depth test; ZMSK is set, so nothing writes.
    {
        static float  xy[PDEFORM_VERTEX_MAX * 6 * 2];
        static float  uv[PDEFORM_VERTEX_MAX * 6 * 2];
        static float  zz[PDEFORM_VERTEX_MAX * 6];
        static u_char rgba[PDEFORM_VERTEX_MAX * 6 * 4];
        int n = 0;
        int t, c;

        for (i = 0; i < pnumh * (pnumw + 1); i++)
        {
            int tri[2][3];
            int ntri = 0;

            if (i % (pnumw + 1) == 0)
                continue;                   // a first column only restarts the strip
            j = i + pnumw + 1;
            if (clip[i - 1] + clip[i + pnumw] + clip[i] == 0)
            {
                tri[ntri][0] = i - 1;
                tri[ntri][1] = i + pnumw;
                tri[ntri][2] = i;
                ntri++;
            }
            if (clip[i + pnumw] + clip[i] + clip[j] == 0)
            {
                tri[ntri][0] = i + pnumw;
                tri[ntri][1] = i;
                tri[ntri][2] = j;
                ntri++;
            }

            for (t = 0; t < ntri; t++)
            {
                for (c = 0; c < 3; c++)
                {
                    int   v = tri[t][c];
                    float q = (stq[v][2] != 0.0f) ? stq[v][2] : 1.0f;

                    xy[n * 2 + 0]   = (float)vtiw[v][0] / 16.0f - 1728.0f;
                    xy[n * 2 + 1]   = (float)vtiw[v][1] / 16.0f - 1824.0f;
                    uv[n * 2 + 0]   = stq[v][0] / q;
                    uv[n * 2 + 1]   = stq[v][1] / q;
                    zz[n]           = ndcz[v];
                    rgba[n * 4 + 0] = (u_char)Red;
                    rgba[n * 4 + 1] = (u_char)Green;
                    rgba[n * 4 + 2] = (u_char)Blue;
                    rgba[n * 4 + 3] = (u_char)((u_int)(int)((float)use_alpha[v] * aprate) & 0xff);
                    n++;
                }
            }
        }
        if (n != 0)
            ReiPan_RendererDrawTexturedTriangles2D((const sceGsTex0 *)&tex0, xy, uv, rgba, zz, n,
                                                   1, 1, DrawEnvType == 0);
    }
}

// The STQ parameters (FF2 effect_obj.h CalcStqXYZ's vf8..vf10): the GS
// offset, the vertex scale (trate in pass 1, 1 in pass 2), the 640 x 448
// clamp, the 1 / 1024 x 1 / 512 texture scale and the 0.0625 Q scale.
// rdata 0x36e3e4.. (pass 1) / 0x36e4d4.. (pass 2), the same values.
// PC: the clamp is the window (the view bounds, still in 640 x 448 units):
// the frame copy holds the whole window, so a vertex past the original
// frame samples what lies under it instead of the frame's edge column.
// 0..639 x 0..447 on a 640:448 window. The haze shares it.
static void PartsDeformSetStqParam(float stqparam[3][4], float scale)
{
    float x0, y0, x1, y1;

    ReiPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);  // PC
    stqparam[0][0] = 1727.5f;
    stqparam[0][1] = 1823.5f;
    stqparam[0][2] = scale;
    stqparam[0][3] = 0.0f;                              // not read
    stqparam[1][0] = x0;                                // PC: 0, 639, 0, 447 in the ROM
    stqparam[1][1] = x1 - 1.0f;
    stqparam[1][2] = y0;
    stqparam[1][3] = y1 - 1.0f;
    stqparam[2][0] = 0.0009765625f;
    stqparam[2][1] = 0.001953125f;
    stqparam[2][2] = 1.0f;
    stqparam[2][3] = 0.0625f;
}

// CalcStqXYZ (inlined in both passes): the frame position of the vertex (its
// x / y scaled by stqparam[0][2]), minus the GS offset, clamped to 0..639 x
// 0..447, scaled to the 1024 x 512 texture, with Q; so the strip samples the
// frame copy under itself.
static void PartsDeformCalcStq(float *stq, const float *vt, sceVu0FMATRIX slm,
                               float stqparam[3][4])
{
    float v[3], s[4], q;
    int c;

    v[0] = vt[0] * stqparam[0][2];                      // vmulz.xy by stqparam[0].z
    v[1] = vt[1] * stqparam[0][2];
    v[2] = vt[2];
    for (c = 0; c < 4; c++)
        s[c] = slm[0][c] * v[0] + slm[1][c] * v[1] + slm[2][c] * v[2] + slm[3][c];
    q = 1.0f / s[3];
    s[0] = s[0] * q - stqparam[0][0];
    s[1] = s[1] * q - stqparam[0][1];
    if (s[0] < stqparam[1][0]) s[0] = stqparam[1][0];
    if (s[0] > stqparam[1][1]) s[0] = stqparam[1][1];
    if (s[1] < stqparam[1][2]) s[1] = stqparam[1][2];
    if (s[1] > stqparam[1][3]) s[1] = stqparam[1][3];
    stq[0] = s[0] * (stqparam[2][0] * q) * stqparam[2][3];
    stq[1] = s[1] * (stqparam[2][1] * q) * stqparam[2][3];
    stq[2] = stqparam[2][2] * q * stqparam[2][3];
    stq[3] = s[3];                                      // sqc2 keeps vf13.w (not read)
}

// Pass 1's second edge fade, for the pages that do not sample the shown frame
// (rdata 0x353a18; FF3's own values, FF2's pdeform_alpha2 differs).
static const u_char pdeform_alpha2[PDEFORM_VERTEX_MAX] = {
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   4,  16,  24,  48,  48,  32,  16,   8,   0,   0,   0,   0,
      0,   0,   0,   1,  16,  48,  64,  88,  88,  88,  64,  48,  32,  24,   8,   0,   0,
      0,   0,   1,  16,  48,  64,  88, 104, 104, 104,  88,  88,  64,  32,  24,   8,   0,
      0,   1,  16,  48,  64,  88, 104, 104, 104, 104, 104, 104,  88,  64,  32,  16,   0,
      0,  16,  48,  64,  88, 104, 104, 104, 104, 104, 104, 104, 104,  88,  48,  24,   0,
      1,  24,  64,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  64,  32,   1,
      8,  32,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  32,   8,
      8,  48,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  64,  32,   8,
      8,  48,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  48,  24,   8,
      1,  32,  64,  88, 104, 104, 104, 104, 104, 104, 104, 104, 104,  88,  32,  16,   1,
      0,  24,  48,  88, 104, 104, 104, 104, 104, 104, 104, 104,  88,  64,  24,   8,   0,
      0,  16,  32,  64,  88, 104, 104, 104, 104, 104, 104,  88,  64,  32,   8,   1,   0,
      0,   8,  16,  32,  64,  88,  88, 104, 104, 104,  88,  64,  32,   8,   1,   0,   0,
      0,   0,   8,  16,  32,  48,  64,  88,  88,  88,  48,  32,   8,   1,   0,   0,   0,
      0,   0,   0,   8,  16,  16,  24,  32,  32,  24,  16,   8,   1,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   1,   8,   8,   8,   1,   0,   1,   0,   0,   0,   0,
};

// Page 1's sample: the 320 x 224 picture of EE buffer 0x117b000 restored to
// GS 0x2bc0 (TBW 5, 512 x 256), rdata 0x36e3c8.
#define PDEFORM_TEX0_HALF   0x2000000224116bc0ULL

// The phase step of pass 1: + add, wrapped past 360; frozen effects
// (eff_wrk.stop_flg) hold it.
// FF3 0x00168828; FF2 PartsDeformAddIso.

static float PartsDeformAddIso(float r, float add)
{
    if (eff_wrk.stop_flg == 0)
    {
        r += add;
        if (r > 360.0f)                                 // rdata 0x36e3b4
            r -= 360.0f;
    }
    return r;
}

// The eight pass-1 kernels walk the 17 x 17 grid as one run of vertices; the
// radial ones measure each vertex's distance from the centre (cntw, cnth)
// against maxl, the corner's, for a falloff 1 - len / maxl.

// y sways by row: sin(base + row * 30 degrees) * rate * 0.8.
// FF3 0x00169258; FF2 PartsDeformCalcWaveY (MioPan effect_obj.c:1103).

static void PartsDeformCalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh, float base_iso, float rate)
{
    float vtrate = rate * 0.8f;                         // rdata 0x36e414
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
        vt[i][1] += Vu0Sinf((base_iso + (float)((i / vnumw) * 30)) * 3.1415927f / 180.0f) * vtrate;
}

// z replaced by a ripple running out from the centre (phase base + len * 30),
// faded to the rim, amplitude rate * 3.
// FF3 0x00169350; FF2 PartsDeformCalcWaveZ (MioPan effect_obj.c:1138).

static void PartsDeformCalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                 float tsw, float tsh, float cntw, float cnth,
                                 float base_iso, float rate)
{
    float maxl = sqrtf(cnth * cnth + cntw * cntw);      // vsqrt
    float vtrate = rate * 3.0f;                         // rdata 0x36e420
    float wfw, wfh, len;
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        wfw = (float)(i % vnumw) * tsw - cntw;
        wfh = (float)(i / vnumw) * tsh - cnth;
        len = sqrtf(wfh * wfh + wfw * wfw);
        vt[i][2] = Vu0Sinf((base_iso + len * 30.0f) * 3.1415927f / 180.0f)
                 * (1.0f - len / maxl) * vtrate;
    }
}

// z replaced by rings: the base angle sets one frequency (sin(base) * 60),
// the radius drives the phase.
// FF3 0x00169520; FF2 PartsDeformCalcWaveIsoZ (MioPan effect_obj.c:1180).

static void PartsDeformCalcWaveIsoZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, float rate)
{
    float maxl = sqrtf(cnth * cnth + cntw * cntw);
    float rr = Vu0Sinf(base_iso * 3.1415927f / 180.0f) * 60.0f;     // rdata 0x36e43c
    float wfw, wfh, len;
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        wfw = (float)(i % vnumw) * tsw - cntw;
        wfh = (float)(i / vnumw) * tsh - cnth;
        len = sqrtf(wfh * wfh + wfw * wfw);
        vt[i][2] = Vu0Sinf(rr * len * 10.0f * 3.1415927f / 180.0f)  // 0x36e440
                 * (1.0f - len / maxl) * rate;
    }
}

// The inner vertices turn about the centre by sin(base) * 60 * ep[i].lng
// degrees (lng seeded by SubPartsDeform1 for sub-types 4 / 5); the border only
// re-centres. Both tests use vnumw - 1 (the ROM's; the grid is square).
// FF3 0x00169720; FF2 PartsDeformCalcWaveRot (MioPan effect_obj.c:1222).

static void PartsDeformCalcWaveRot(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float tsw, float tsh, float cntw, float cnth,
                                   float base_iso, const EFFINFO2 *pefi)
{
    int   pnumw = vnumw - 1;
    float rr = Vu0Sinf(base_iso * 3.1415927f / 180.0f) * 60.0f;     // rdata 0x36e454
    float f1 = (float)pnumw * 0.5f * tsw;                           // 0x36e450
    float f2 = (float)(vnumh - 1) * 0.5f * tsh;
    float wfw, wfh, ang, fs, fc;
    int   i, col, row;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        row = i / vnumw;
        col = i % vnumw;
        wfw = (float)col * tsw - cntw;
        wfh = (float)row * tsh - cnth;
        if (col == 0 || col == pnumw || row == 0 || row == pnumw)
        {
            vt[i][0] = (float)col * tsw - f1;
            vt[i][1] = (float)row * tsh - f2;
        }
        else
        {
            ang = rr * pefi->ep[i].lng * 3.1415927f / 180.0f;
            fs = Vu0Sinf(ang);
            fc = Vu0Cosf(ang);
            vt[i][0] = wfw * fc - wfh * fs + cntw - f1;
            vt[i][1] = wfh * fc + wfw * fs + cnth - f2;
        }
    }
}

// A byte-for-byte copy of WaveRot (its own constants at rdata 0x36e458..);
// it writes x / y too, despite the name.
// FF3 0x00169960; FF2 PartsDeformCalcWaveRotZ (MioPan effect_obj.c:1279).
// callcheck: Vu0Sinf, Vu0Cosf (through PartsDeformCalcWaveRot, the ROM's copy)

static void PartsDeformCalcWaveRotZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth,
                                    float base_iso, const EFFINFO2 *pefi)
{
    PartsDeformCalcWaveRot(vt, vnumw, vnumh, tsw, tsh, cntw, cnth, base_iso, pefi);
}

// In the grid plane: every vertex pushed out from the centre by
// cos(base) * renz * (1 - len / maxl), re-centred on the origin.
// FF3 0x00169ba0; FF2 PartsDeformCalcWaveBound (MioPan effect_obj.c:1335).

static void PartsDeformCalcWaveBound(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth,
                                     float base_iso, float renz)
{
    float maxl = sqrtf(cnth * cnth + cntw * cntw);
    float fx, fy, wfw, wfh, len, fw;
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        fx = (float)(i % vnumw) * tsw;
        fy = (float)(i / vnumw) * tsh;
        wfw = fx - cntw;
        wfh = fy - cnth;
        len = sqrtf(wfh * wfh + wfw * wfw);
        fw = (1.0f - len / maxl) * Vu0Cosf(base_iso * 3.1415927f / 180.0f) * renz;   // rdata 0x36e468
        vt[i][0] = fw * wfw + fx - tsw * 8.0f;          // 0x36e46c
        vt[i][1] = fw * wfh + fy - tsh * 8.0f;
    }
}

// The lens pull towards the centre; renz scales the displacement.
// FF3 0x00169d88; FF2 PartsDeformCalcWaveRenz1 (MioPan effect_obj.c:1373).

static void PartsDeformCalcWaveRenz1(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth, float renz)
{
    float maxl = sqrtf(cnth * cnth + cntw * cntw);
    float wfw, wfh, fall;
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        wfw = (float)(i % vnumw) * tsw - cntw;
        wfh = (float)(i / vnumw) * tsh - cnth;
        fall = 1.0f - sqrtf(wfh * wfh + wfw * wfw) / maxl;
        vt[i][0] = fall * wfw * renz + cntw - tsw * 8.0f;   // rdata 0x36e470
        vt[i][1] = fall * wfh * renz + cnth - tsh * 8.0f;
    }
}

// The same pull, renz scaling the reach (the radius term) instead.
// FF3 0x00169e90; FF2 PartsDeformCalcWaveRenz2 (MioPan effect_obj.c:1411).

static void PartsDeformCalcWaveRenz2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                     float tsw, float tsh, float cntw, float cnth, float renz)
{
    float maxl = sqrtf(cnth * cnth + cntw * cntw);
    float wfw, wfh, fall;
    int   i;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        wfw = (float)(i % vnumw) * tsw - cntw;
        wfh = (float)(i / vnumw) * tsh - cnth;
        fall = 1.0f - sqrtf(wfh * wfh + wfw * wfw) / maxl * renz;
        vt[i][0] = fall * wfw + cntw - tsw * 8.0f;          // rdata 0x36e478
        vt[i][1] = fall * wfh + cnth - tsh * 8.0f;
    }
}

static float pdeform1_renz = 1.0f;          // sdata 0x36e3bc (gp-0x5134): read by kinds 7 / 8, written by nothing
static int   pdeform1_passflg[10];          // data 0x2f8b88: the frame count each sub-type last stepped
static float pdeform1_passcnt[10];          // data 0x2f8bb0: the shared phase of the one-frame records

// Pass 1: the 17 x 17 grid of cells 2 * scl, facing the camera's yaw at
// pnt[0], samples a copy of the frame under itself (STQ computed before the
// kernel moves the vertices, so the picture refracts). page picks the copy:
// 0 the shown frame, 1 the half-size EE buffer 0x117b000 (FF3; FF2 sampled the
// blur page 0x7aa0 without a copy), else the other frame; pages other than 0
// use the second edge fade. sbj picks the kernel; its phase is ec->fw[0], or
// for a one-frame record (uc8[1] bit 0) a per-sub-type shared one stepped once
// a frame. trate scales the sampled window. A new slot for sub-types 4 / 5
// seeds ep[].lng = (corner distance - distance)^2 * 0.006 * rate. Returns
// the slot (0xff: none free). fl is not read. FF3 against FF2: the alpha
// fades in from 4 to 8 units (FF2 100 / 200), the world matrix is a unit one
// (FF2 x25) and the yaw normalised, no Reserve2DPacket.
// FF3 0x00168860; FF2 SubPartsDeform1 (MioPan effect_obj.c:805).
// callcheck: GetSysWrk (FF3 reads the shared inline cache sdata gp-0xc60 first)

static u_char SubPartsDeform1(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate)
{
    static sceVu0FVECTOR stq[PDEFORM_VERTEX_MAX];   // PC: off the stack (the ROM's frame is 0x25a0)
    static sceVu0FVECTOR vt[PDEFORM_VERTEX_MAX];
    EFFINFO2 *pefi;
    GRA3DCAMERA *pCam;
    float *cam_dir;
    float *cam_pos;
    sceVu0FVECTOR vpos;
    sceVu0FMATRIX wlm;
    sceVu0FMATRIX slm;
    float stqparam[3][4];
    float tsw, tsh, cntw, cnth;
    float rot_x, rot_y;
    float l, aprate, r, f3;
    float wfw, wfh, len;
    float dx, dy, dz;
    float xx, yy;
    u_long tex0;
    u_char ret_num;
    int i, j, k, n;

    (void)fl;

    tsw = sclx + sclx;
    tsh = scly + scly;
    pCam = gra3dGetCamera();
    cam_dir = gra3dcamGetDirection();
    cntw = tsw * 8.0f;                                  // rdata 0x36e3d0
    cnth = tsh * 8.0f;

    if (num == 0xff)
    {
        // GetFreePartsDeformCtrlNo + GetPartsDeformCtrlWrk, inlined in FF3.
        for (i = 0; i < PDEFORM_SLOT_NUM; i++)
        {
            if (efi[i].use == 0)
            {
                efi[i].use = 1;
                break;
            }
        }
        if (i >= PDEFORM_SLOT_NUM)
            return 0xff;                    // FF2 printed "PartsDeform Buffer is Full!!"
        ret_num = (u_char)i;
        pefi = &efi[ret_num];

        // FF2's static r2 (sdata 0x36e3b8) is cleared here and read by nothing.
        if ((u_int)(sbj - 4) < 2)
        {
            r = 0.0f;
            for (i = 0; i < PDEFORM_VERTEX_MAX; i++)
            {
                wfw = (float)(i % PDEFORM_VERTEX_W) * tsw - cntw;
                wfh = (float)(i / PDEFORM_VERTEX_W) * tsh - cnth;
                len = sqrtf(wfh * wfh + wfw * wfw);     // vsqrt
                if (i == 0)
                    r = len;
                l = r - len;
                pefi->ep[i].lng = l * l * 0.006f * rate;    // rdata 0x36e3d4
            }
        }
    }
    else
    {
        if (num >= PDEFORM_SLOT_NUM)
            return num;
        ret_num = num;
        pefi = &efi[num];
    }
    pefi->pass = 1;

    Vector2Rot(cam_dir, &rot_x, &rot_y);
    memcpy(vpos, ec->pnt[0], sizeof(vpos));

    // GetDistancePosToCamera, inlined (vsqrt).
    cam_pos = gra3dcamGetPosition();
    dx = vpos[0] - cam_pos[0];
    dy = vpos[1] - cam_pos[1];
    dz = vpos[2] - cam_pos[2];
    l = sqrtf(dx * dx + dy * dy + dz * dz);

    if (l > 8.0f)
        aprate = vol;
    else if (l < 4.0f)                                  // rdata 0x36e3d8
        aprate = 0.0f;
    else
        aprate = (l - 4.0f) * vol * 0.25f;              // 0x36e3dc

    sceVu0UnitMatrix(wlm);                              // FF3 builds it inline, diagonal 1.0 (rdata 0x36e3e0)
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += vpos[0];                               // vadd.xyz: sceVu0TransMatrix
    wlm[3][1] += vpos[1];
    wlm[3][2] += vpos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    if (PartsDeformClipCheck(slm) != 0)
        return ret_num;

    if (page == 0)
    {
        LocalCopyLtoL(3, (int)(GetSysWrk()->count & 1) * 0x1180, 0x2bc0);
        tex0 = PDEFORM_TEX0;                            // rdata 0x36e3c0
    }
    else if (page == 1)
    {
        LocalCopyBtoLAdrs(1, 0x117b000, 0x2bc0);
        tex0 = PDEFORM_TEX0_HALF;
    }
    else
    {
        LocalCopyLtoL(3, (int)((GetSysWrk()->count + 1) & 1) * 0x1180, 0x2bc0);
        tex0 = PDEFORM_TEX0;
    }

    PartsDeformSetStqParam(stqparam, trate);

    k = 0;
    yy = -(tsh * 8.0f);
    for (j = 0; j < PDEFORM_VERTEX_H; j++)
    {
        xx = -(tsw * 8.0f);
        for (i = 0; i < PDEFORM_VERTEX_W; i++)
        {
            vt[k][0] = xx;
            vt[k][1] = yy;
            vt[k][2] = 0.0f;
            vt[k][3] = 1.0f;
            xx += tsw;
            k++;
        }
        yy += tsh;
    }
    for (i = 0; i < PDEFORM_VERTEX_MAX; i++)
        PartsDeformCalcStq(stq[i], vt[i], slm, stqparam);

    if ((ec->dat.uc8[1] & 1) != 0)
    {
        r = pdeform1_passcnt[sbj];
        n = pdeform1_passflg[sbj] != (int)GetSysWrk()->count;
    }
    else
    {
        r = ec->fw[0];
        n = 1;
    }

    switch (sbj)                                        // jump table rdata 0x353750
    {
    case 0:
        return ret_num;
    case 1:
        PartsDeformCalcWaveY(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, r, rate);
        if (n != 0)
            r = PartsDeformAddIso(r, spd + spd);
        break;
    case 2:
        PartsDeformCalcWaveZ(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, r, rate);
        if (n != 0)
            r = PartsDeformAddIso(r, spd * 4.0f);       // rdata 0x36e3d8
        break;
    case 3:
        PartsDeformCalcWaveIsoZ(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, r, rate);
        if (n != 0)
            r = PartsDeformAddIso(r, spd + spd);
        break;
    case 4:
        PartsDeformCalcWaveRot(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, r, pefi);
        if (n != 0)
            r = PartsDeformAddIso(r, spd + spd);
        break;
    case 5:
        PartsDeformCalcWaveRotZ(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, r, pefi);
        if (n != 0)
            r = PartsDeformAddIso(r, spd + spd);
        break;
    case 6:
        // A bounce: loses a fifth of its height (fw[1]) whenever the phase
        // crosses a half period, snapped to rest at 0.02.
        PartsDeformCalcWaveBound(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, r, ec->fw[1]);
        f3 = Vu0Cosf(r * 3.1415927f / 180.0f) * ec->fw[1];         // rdata 0x36e400 / 0x36e404
        if (n != 0)
            r = PartsDeformAddIso(r, 6.0f);                         // 0x36e408
        if (Vu0Cosf(r * 3.1415927f / 180.0f) * f3 < 0.0f)
        {
            if (ec->fw[1] <= 0.02f)                                 // 0x36e40c
                ec->fw[1] = 0.0f;
            else
                ec->fw[1] = ec->fw[1] * 0.8f;                       // 0x36e410
        }
        break;
    case 7:
        PartsDeformCalcWaveRenz1(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, pdeform1_renz);
        break;
    case 8:
        PartsDeformCalcWaveRenz2(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, tsw, tsh, cntw, cnth, pdeform1_renz);
        break;
    default:                                // 9: an undisplaced grid
        break;
    }

    pdeform1_passflg[sbj] = (int)GetSysWrk()->count;
    if ((ec->dat.uc8[1] & 1) != 0)
        pdeform1_passcnt[sbj] = r;
    else
        ec->fw[0] = r;

    if (sclx != 0.0f && scly != 0.0f)
    {
        MakePartsDeformPacket(16, 16, vt, wlm, stq, (page == 0) ? pdeform_alpha1 : pdeform_alpha2,
                              aprate, tex0, ec->r, ec->g, ec->b, ec->dat.uc8[5]);
    }
    return ret_num;
}

// Seed a slot's per-vertex phases for a kernel: 1 random phase / small
// amplitude / step (WaveY), 2 the same larger plus the distance from the
// centre (WaveZ), 3 amplitudes inherited from the left / upper neighbour
// (WaveY2); 0, 4 and 5 seed nothing. r (the grid phase) always cleared.
// FF3 0x00169f90; FF2 PartsDeformEffInf02Init (MioPan effect_obj.c:1716).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void PartsDeformEffInf02Init(EFFINFO2 *pEffInfo, int type, int vnumw, int vnumh,
                                    float tsw, float tsh, float cntw, float cnth, float spd,
                                    PDEFORM_PARA *pPara)
{
    float wfw, wfh;
    int   i;

    pEffInfo->r = 0.0f;
    switch (type)
    {
    case 1:
        for (i = 0; i < vnumw * vnumh; i++)
        {
            pEffInfo->ep[i].r   = EffRand01() * 360.0f;                 // rdata 0x36e484
            pEffInfo->ep[i].h   = EffRand01() * 0.4f + 0.1f;            // 0x36e488 / 0x36e48c
            pEffInfo->ep[i].add = (EffRand01() * 1.5f + 1.0f) * spd;    // 0x36e490 / 0x36e494
        }
        break;

    case 2:
        for (i = 0; i < vnumw * vnumh; i++)
        {
            wfw = (float)(i % vnumw) * tsw - cntw;
            wfh = (float)(i / vnumw) * tsh - cnth;
            pEffInfo->ep[i].lng = sqrtf(wfw * wfw + wfh * wfh) * 150.0f;   // vsqrt; 0x36e498
            pEffInfo->ep[i].r   = EffRand01() * 360.0f;
            pEffInfo->ep[i].h   = EffRand01() * 4.0f + 1.0f;            // 0x36e49c
            pEffInfo->ep[i].add = (EffRand01() * 3.0f + 2.0f) * spd;    // 0x36e4a0 / 0x36e4a4
        }
        break;

    case 3:
        for (i = 0; i < vnumw * vnumh; i++)
        {
            if (i % vnumw == 0)
                pEffInfo->ep[i].h = pPara->pr11 * EffRand01() + pPara->pr12;
            else
                pEffInfo->ep[i].h = pEffInfo->ep[i - 1].h + pPara->pr21 * EffRand01() - pPara->pr22;

            if (i / vnumw == 0)
                pEffInfo->ep[i].r = pPara->pr11 * EffRand01() + pPara->pr12;
            else
                pEffInfo->ep[i].r = pEffInfo->ep[i - vnumw].r + pPara->pr21 * EffRand01() - pPara->pr22;
        }
        break;

    default:                                // 0, 4, 5 (the jump table's break entries)
        break;
    }
}

// The phase step the per-vertex kernels share: back by add, wrapped below
// -360 (r + 360 - add).
static inline float PartsDeformStepPhase(const EFFPOS *ep)
{
    float r = ep->r - ep->add;

    if (r < -360.0f)
        r = ep->r + 360.0f - ep->add;
    return r;
}

// Inner vertices sway in x and y on their own phase (rows / columns offset by
// 50 degrees); a vertex whose x crosses zero upward gets a new amplitude and
// step. Frozen effects (eff_wrk.stop_flg) hold their phase.
// FF3 0x0016ab80; FF2 PartsDeform2CalcWaveY (MioPan effect_obj.c:2028).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void PartsDeform2CalcWaveY(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd, EFFINFO2 *pefi)
{
    EFFPOS *ep;
    int i, col, row;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        ep = &pefi->ep[i];
        row = i / vnumw;
        col = i % vnumw;
        if (col != 0 && col != vnumw - 1 && row != 0 && row != vnumh - 1)
        {
            vt[i][0] += Vu0Sinf((ep->r + (float)row * 50.0f) * 3.1415927f / 180.0f) * ep->h * comp * rate;
            vt[i][1] += Vu0Sinf((ep->r + (float)col * 50.0f) * 3.1415927f / 180.0f) * ep->h * comp * rate;
            if (eff_wrk.stop_flg == 0 && vt[i][0] >= 0.0f && ep->ox < 0.0f)
            {
                ep->h   = EffRand01() * 0.4f + 0.1f;                    // rdata 0x36e504 / 0x36e508
                ep->add = (EffRand01() * 1.5f + 1.0f) * spd;            // 0x36e50c / 0x36e510
            }
        }
        if (eff_wrk.stop_flg == 0)
        {
            ep->r = PartsDeformStepPhase(ep);
            ep->ox = vt[i][0];
        }
    }
}

// z only, faded from the centre to the rim by lng (ep[0].lng, a corner, is
// the longest).
// FF3 0x0016ae60; FF2 PartsDeform2CalcWaveZ (MioPan effect_obj.c:2073).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void PartsDeform2CalcWaveZ(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                  float comp, float rate, float spd, EFFINFO2 *pefi)
{
    EFFPOS *ep;
    float ml = pefi->ep[0].lng;
    float l;
    int i, col, row;

    for (i = 0; i < vnumw * vnumh; i++)
    {
        ep = &pefi->ep[i];
        l = ep->lng;
        row = i / vnumw;
        col = i % vnumw;
        if (col != 0 && col != vnumw - 1 && row != 0 && row != vnumh - 1)
        {
            vt[i][2] = Vu0Sinf((ep->r + l) * 3.1415927f / 180.0f) * (1.0f - l / ml) * ep->h * comp * rate;
            if (eff_wrk.stop_flg == 0 && vt[i][2] >= 0.0f && ep->ox < 0.0f)
            {
                ep->h   = EffRand01() * 4.0f + 1.0f;                    // rdata 0x36e52c
                ep->add = EffRand01() * 3.0f * spd + spd + spd;         // 0x36e530
            }
        }
        if (eff_wrk.stop_flg == 0)
        {
            ep->r = PartsDeformStepPhase(ep);
            ep->ox = vt[i][2];
        }
    }
}

// One grid-wide phase (pefi->r, + 2 * spd a frame, wrapped at 360); each
// vertex sways by its own x amplitude (ep.r) and y amplitude (ep.h), which
// drift from the neighbours' whenever the sway crosses zero upward. Frozen:
// the last displacement is re-applied.
// FF3 0x0016b0d8; FF2 PartsDeform2CalcWaveY2 (MioPan effect_obj.c:2123).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void PartsDeform2CalcWaveY2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                   float comp, float rate, float spd,
                                   PDEFORM_PARA *pPara, EFFINFO2 *pefi)
{
    EFFPOS *ep;
    float add = spd + spd;
    float r;
    int i;

    if (eff_wrk.stop_flg == 0)
    {
        for (i = 0; i < vnumw * vnumh; i++)
        {
            ep = &pefi->ep[i];
            ep->x = Vu0Sinf((pefi->r + (float)(i / vnumw) * 32.0f) * 3.1415927f / 180.0f) * ep->r * comp * rate;
            ep->y = Vu0Sinf((pefi->r + (float)(i % vnumw) * 32.0f) * 3.1415927f / 180.0f) * ep->h * comp * rate;

            if (ep->y >= 0.0f && ep->oy < 0.0f)
            {
                if (i % vnumw == 0)
                    ep->h = pPara->pr11 * EffRand01() + pPara->pr12;
                else
                    ep->h = pefi->ep[i - 1].h + pPara->pr21 * EffRand01() - pPara->pr22;
            }
            if (ep->x >= 0.0f && ep->ox < 0.0f)
            {
                if (i / vnumw == 0)
                    ep->r = pPara->pr11 * EffRand01() + pPara->pr12;
                else
                    ep->r = pefi->ep[i - vnumw].r + pPara->pr21 * EffRand01() - pPara->pr22;
            }
            ep->ox = ep->x;
            ep->oy = ep->y;
            vt[i][0] += ep->x;
            vt[i][1] += ep->y;
        }
        r = pefi->r + add;
        pefi->r = r;
        if (r > 360.0f)                                                 // rdata 0x36e54c
            pefi->r = r - 360.0f;
    }
    else
    {
        for (i = 0; i < vnumw * vnumh; i++)
        {
            vt[i][0] += pefi->ep[i].x;
            vt[i][1] += pefi->ep[i].y;
        }
    }
}

// One separable ripple: a 17-entry sine table (phase + n * 50 degrees, times
// rate; FF2 rate / 10) added to x by row and to y by column, inner vertices
// only; the phase always advances (2 * spd, wrapped at 360).
// FF3 0x0016b458; FF2 PartsDeform2CalcWaveType2 (MioPan effect_obj.c:2202).

static void PartsDeform2CalcWaveType2(sceVu0FVECTOR *vt, int vnumw, int vnumh,
                                      float comp, float rate, float spd,
                                      PDEFORM_PARA *pPara, EFFINFO2 *pefi)
{
    float SinCalcBuf[17];
    float ll;
    float r;
    int i, j;

    (void)comp;
    (void)pPara;

    for (i = 0; i < 17; i++)
        SinCalcBuf[i] = Vu0Sinf((pefi->r + (float)i * 50.0f) * 3.1415927f / 180.0f) * rate;

    for (j = 1; j < vnumh - 1; j++)
    {
        for (i = 1; i < vnumw - 1; i++)
        {
            ll = (j < 17) ? SinCalcBuf[j]
                          : Vu0Sinf((pefi->r + (float)j * 50.0f) * 3.1415927f / 180.0f) * rate;
            vt[j * vnumw + i][0] += ll;
            ll = (i < 17) ? SinCalcBuf[i]
                          : Vu0Sinf((pefi->r + (float)i * 50.0f) * 3.1415927f / 180.0f) * rate;
            vt[j * vnumw + i][1] += ll;
        }
    }

    r = pefi->r + spd + spd;
    if (r > 360.0f)                                                     // rdata 0x36e55c
        r -= 360.0f;
    pefi->r = r;
}

// Pass 2: the shared-shape deform. A 17 x 17 grid of cells 2 * scl, facing
// the camera's yaw at pnt[0], gets screen STQ per vertex (the frame position
// clamped to 640 x 448, so the strip samples the frame copy under itself),
// one kernel by sbj, and is drawn over a copy of the displayed frame (GS
// 0x2bc0). The phases come from a claimed slot (num 0xff: claim one, seeded
// for sbj), or for a one-frame record (uc8[1] bit 0) from the shared table of
// its sub-type (uc8[5] non-zero: table 4). comp grows the sway with distance
// (d / 52 past 30, else 30 / 52); the alpha fades in from 4 to 8 units
// (FF2 used 100 / 200 and 750 / 1300: FF3's world is 25 times smaller, and
// the grid's world matrix lost FF2's x25 scale). Returns the slot number
// (0xff: none free). page, fl and trate are not read.
// FF3 0x0016a438; FF2 SubPartsDeform2 (MioPan effect_obj.c:1819).
// callcheck: GetSysWrk (FF3 reads the shared inline cache sdata gp-0xc60 first)

static u_char SubPartsDeform2(EFFECT_CONT *ec, u_char num, int page, int sbj,
                              float sclx, float scly, float vol, int fl,
                              float spd, float rate, float trate)
{
    static sceVu0FVECTOR stq[PDEFORM_VERTEX_MAX];   // PC: off the stack (the ROM's frame is 0x25a0)
    static sceVu0FVECTOR vt[PDEFORM_VERTEX_MAX];
    PDEFORM_PARA para = { 1.0f, -0.3f, 0.4f, 0.2f };   // rdata 0x353790
    EFFINFO2 *pefi;
    GRA3DCAMERA *pCam;
    float *cam_dir;
    float *cam_pos;
    sceVu0FVECTOR vpos;
    sceVu0FMATRIX wlm;
    sceVu0FMATRIX slm;
    float stqparam[3][4];
    float tsw, tsh, cntw, cnth;
    float rot_x, rot_y;
    float l, comp, aprate;
    float dx, dy, dz;
    float xx, yy;
    u_char ret_num = 0;
    int i, j, k;

    (void)page;
    (void)fl;
    (void)trate;

    tsw = sclx + sclx;
    tsh = scly + scly;
    pCam = gra3dGetCamera();
    cam_dir = gra3dcamGetDirection();
    cntw = tsw * 8.0f;                                  // rdata 0x36e4b8
    cnth = tsh * 8.0f;

    if (init_pdef2 != 0)
    {
        for (i = 0; i < 5; i++)
            PartsDeformEffInf02Init(&pefi_once[i], i + 1, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H,
                                    tsw, tsh, cntw, cnth, spd, &para);
        init_pdef2 = 0;
    }

    if ((ec->dat.uc8[1] & 1) != 0)
    {
        pefi = (ec->dat.uc8[5] == 0) ? &pefi_once[sbj - 1] : &pefi_once[4];
    }
    else
    {
        if (num == 0xff)
        {
            // GetFreePartsDeformCtrlNo + GetPartsDeformCtrlWrk, inlined in FF3.
            for (i = 0; i < PDEFORM_SLOT_NUM; i++)
            {
                if (efi[i].use == 0)
                {
                    efi[i].use = 1;
                    break;
                }
            }
            if (i >= PDEFORM_SLOT_NUM)
                return 0xff;                // FF2 printed "PartsDeform Buffer is Full!!"
            ret_num = (u_char)i;
            pefi = &efi[ret_num];
            PartsDeformEffInf02Init(pefi, sbj, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H,
                                    tsw, tsh, cntw, cnth, spd, &para);
        }
        else
        {
            if (num >= PDEFORM_SLOT_NUM)
                return num;
            pefi = &efi[num];
            ret_num = num;
        }
        pefi->pass = 1;
    }

    Vector2Rot(cam_dir, &rot_x, &rot_y);
    memcpy(vpos, ec->pnt[0], sizeof(vpos));

    // GetDistancePosToCamera, inlined (vsqrt).
    cam_pos = gra3dcamGetPosition();
    dx = vpos[0] - cam_pos[0];
    dy = vpos[1] - cam_pos[1];
    dz = vpos[2] - cam_pos[2];
    l = sqrtf(dx * dx + dy * dy + dz * dz);

    comp = (l > 30.0f) ? l / 52.0f : 0.5769231f;      // rdata 0x36e4bc / 0x36e4c0 / 0x36e4c4 (30 / 52)
    if (l > 8.0f)
        aprate = vol;
    else if (l < 4.0f)                                  // 0x36e4c8
        aprate = 0.0f;
    else
        aprate = (l - 4.0f) * vol * 0.25f;              // 0x36e4cc

    sceVu0UnitMatrix(wlm);                              // FF3 builds it inline, diagonal 1.0 (rdata 0x36e4d0)
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += vpos[0];                               // vadd.xyz: sceVu0TransMatrix
    wlm[3][1] += vpos[1];
    wlm[3][2] += vpos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    if (PartsDeformClipCheck(slm) != 0)
        return ret_num;

    PartsDeformSetStqParam(stqparam, 1.0f);

    k = 0;
    yy = tsh * -8.0f;                                   // rdata 0x36e4f0
    for (j = 0; j < PDEFORM_VERTEX_H; j++)
    {
        xx = tsw * -8.0f;
        for (i = 0; i < PDEFORM_VERTEX_W; i++)
        {
            vt[k][0] = xx;
            vt[k][1] = yy;
            vt[k][2] = 0.0f;
            vt[k][3] = 1.0f;
            PartsDeformCalcStq(stq[k], vt[k], slm, stqparam);
            xx += tsw;
            k++;
        }
        yy += tsh;
    }

    switch (sbj)
    {
    case 0:
        return ret_num;
    case 1:
        PartsDeform2CalcWaveY(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, comp, rate, spd, pefi);
        break;
    case 2:
        PartsDeform2CalcWaveZ(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, comp, rate, spd, pefi);
        break;
    case 3:
        PartsDeform2CalcWaveY2(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, comp, rate, spd, &para, pefi);
        break;
    case 4:
        PartsDeform2CalcWaveType2(vt, PDEFORM_VERTEX_W, PDEFORM_VERTEX_H, comp, rate, spd, &para, pefi);
        break;
    }

    if (sclx != 0.0f && scly != 0.0f)
    {
        LocalCopyLtoL(3, (int)(GetSysWrk()->count & 1) * 0x1180, 0x2bc0);
        MakePartsDeformPacket(16, 16, vt, wlm, stq, pdeform_alpha1, aprate, PDEFORM_TEX0,
                              ec->r, ec->g, ec->b, ec->dat.uc8[5]);
    }
    return ret_num;
}

// ──────────────────────────────────────────────────────────────────────
// The haze (kind 0x1b; FF2 SetHaze's slot, FF3's own effect)
// ──────────────────────────────────────────────────────────────────────

// The haze magnifies a still of the frame: on its first frame SetHaze stores
// the shown picture (GS 0x38e0, 320 x 224) at EE 0x11c1000; every frame it is
// restored to GS 0x2bc0 and drawn through an 18-vertex fan that samples it
// at one size and is drawn at a larger one, centre 70 alpha fading out over
// ten frames, rim clear. A grey camera-facing sprite (effdat 4) blooms with
// it for eight frames.
#define HAZE_EE_STASH   0x11c1000
#define HAZE_FAN_NUM    18

// The snapshot as a texture: TBP 0x2bc0, TBW 5, PSMCT24, 512 x 256 (rdata
// 0x36e590).
#define HAZE_TEX0       0x2000000224116bc0ULL

static sceVu0FVECTOR haze_vtx[HAZE_FAN_NUM];    // bss 0x3d1f40
static sceVu0FVECTOR haze_stq[HAZE_FAN_NUM];    // bss 0x3d2060

// The fan's centre (data 0x329500).
static const sceVu0FVECTOR haze_center = { 0.0f, 0.0f, 0.0f, 1.0f };

// A haze record's level: 1 at cnt 0 down to 0 at 10, 0 outside.
// FF3 0x0016bc00.

float EffHazeLevel(int cnt)
{
    float lvl = 0.0f;

    if (cnt < 11)
    {
        lvl = 1.0f;                     // rdata 0x36e578
        if (cnt > -1)
            lvl = 1.0f - (float)cnt / 10.0f;    // rdata 0x36e57c
    }
    return lvl;
}

// A ring of num vertices for a triangle fan: out[0] the centre, out[1] ..
// out[num - 2] r away around z from -pi, out[num - 1] closing on out[1].
// The adds are full vectors (w = 2 on the ring; nothing reads it).
// FF3 0x0016ba88.

static void EffMakeFanVertex(float r, sceVu0FVECTOR *out, const float *center, int num)
{
    sceVu0FMATRIX m;
    sceVu0FMATRIX unit;
    sceVu0FVECTOR v = { 0.0f, 0.0f, 0.0f, 1.0f };   // rdata 0x36e56c
    int i;

    if (num - 2 <= 0)
        return;

    v[0] = r;
    out[0][0] = center[0];
    out[0][1] = center[1];
    out[0][2] = center[2];
    out[0][3] = center[3];
    sceVu0UnitMatrix(unit);             // the ROM rotates the unit matrix at data 0x3293c0
    for (i = 1; i < num - 1; i++)
    {
        sceVu0RotMatrixZ(m, unit,
                         NormalizeAngle(((float)(i - 1) * 6.2831855f) / (float)(num - 2) - 3.1415927f));  // rdata 0x36e570 / 0x36e574
        sceVu0ApplyMatrix(out[i], m, v);
        out[i][0] += center[0];
        out[i][1] += center[1];
        out[i][2] += center[2];
        out[i][3] += center[3];
    }
    out[num - 1][0] = out[1][0];
    out[num - 1][1] = out[1][1];
    out[num - 1][2] = out[1][2];
    out[num - 1][3] = out[1][3];
}

// The fan at pos, facing the camera: its STQ is the frame position of each
// vertex at scale (the parts deform's CalcStq, vertex scale 1), it is drawn
// grown by grow, so the snapshot under it shows magnified. CLAMP 5 (clamp
// both ways), TEST 0x3000d, no z write.
// FF3 0x0016be08.
// callcheck: SetGsRegsCached (the five registers)

static void EffHazeFanDraw(float scale, float grow, u_long tex0, float *pos,
                           u_char *rgba_c, u_char *rgba_o, u_long alpha)
{
    GRA3DCAMERA *pCam;
    sceVu0FMATRIX wlm;
    sceVu0FMATRIX slm;
    sceVu0FMATRIX gm;
    float stqparam[3][4];
    GS_REG_SET regs[5];
    float rot_x;
    float rot_y;
    int i;

    pCam = gra3dGetCamera();
    Vector2Rot(gra3dcamGetDirection(), &rot_x, &rot_y);
    sceVu0UnitMatrix(wlm);
    wlm[0][0] = scale;
    wlm[1][1] = scale;
    wlm[2][2] = scale;
    sceVu0RotMatrixX(wlm, wlm, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(wlm, wlm, NormalizeAngle(rot_y));
    wlm[3][0] += pos[0];                // vadd.xyz
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    EffMakeFanVertex(10.0f, haze_vtx, haze_center, HAZE_FAN_NUM);    // rdata 0x36e5ac
    PartsDeformSetStqParam(stqparam, 1.0f);                         // rdata 0x36e5b0..
    for (i = 0; i < HAZE_FAN_NUM; i++)
        PartsDeformCalcStq(haze_stq[i], haze_vtx[i], slm, stqparam);

    sceVu0UnitMatrix(gm);
    gm[0][0] = grow;
    gm[1][1] = grow;
    gm[2][2] = grow;
    sceVu0MulMatrix(gm, wlm, gm);

    regs[0].data = alpha;           regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 5;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x3000d;         regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);
    EneDmgLargeHitMakePacket(haze_vtx, HAZE_FAN_NUM, gm, rgba_c, rgba_o, haze_stq, tex0);
}

// The magnifying fan for cnt 0..10 (scale 2.22, grown 1.02 + 0.044 cnt).
// In the camera / crawl / fixed views the first frame also flashes the
// screen. 1 while drawn.
// FF3 0x0016bc58.

static int EffHazeDistortDraw(float *pos, int cnt)
{
    u_char rgba_c[4] = { 0xff, 0xff, 0xff, 0 };     // rdata 0x36e580
    u_char rgba_o[4] = { 0xff, 0xff, 0xff, 0 };     // rdata 0x36e588
    PLYR_WRK *plyr;
    float grow;
    float lvl;
    int ret = 0;

    if (cnt < 11)
    {
        grow = ((float)cnt * 0.44f) / 10.0f + 1.02f;    // rdata 0x36e598 / 0x36e59c / 0x36e5a0
        lvl = EffHazeLevel(cnt);
        rgba_c[3] = (u_char)(int)(lvl * 70.0f);         // rdata 0x36e5a4
        rgba_o[3] = (u_char)(int)(lvl * 0.0f);
        LocalCopyBtoLAdrs(1, HAZE_EE_STASH, 0x2bc0);
        plyr = ingame_wrk.plyr_wrk;
        if ((plyr->flags & (PLYR_FLG_800000 | PLYR_FLG_8000000)) == 0 && (plyr->_0e0 & 1) == 0)
        {
            EffHazeFanDraw(2.22f, grow, HAZE_TEX0, pos, rgba_c, rgba_o, 0x48);     // rdata 0x36e5a8
            ret = 1;
        }
        else
        {
            EffHazeFanDraw(2.22f, grow, HAZE_TEX0, pos, rgba_c, rgba_o, 0x48);
            ret = 1;
            if (cnt == 0)
                ScreenFlashReq(0x30, 4, 0xf0, 0xf0, 0xf0, 1);
        }
    }
    return ret;
}

// A grey (0x8c) camera-facing sprite of effdat[4 + mono] at pos for cnt 0..8:
// (3 + 0.75 cnt) * 100 across, alpha 0 at cnt 0, 8 for 1..4, then down by 2
// a frame. 1 while drawn.
// FF3 0x0016c218.

static int EffHazeFlashDraw(float *pos, int cnt)
{
    u_long regs[6] = { 0x161, 0x48, 0x10a000118ULL, 0x3000d, 0, 0x302a400000008004ULL };
    sceVu0FMATRIX m;
    float *dir;
    float size;
    float rot_x;
    float rot_y;
    u_char alpha;

    dir = gra3dcamGetDirection();
    alpha = (u_char)(cnt << 3);
    if (cnt >= 9)
        return 0;

    size = (float)cnt * 6.0f * 0.125f + 3.0f;     // rdata 0x36e5d4 / 0x36e5d8 / 0x36e5dc
    if (cnt > 0)
    {
        alpha = 8;
        if (cnt > 3)
            alpha = (u_char)(int)((1.0f - (float)(cnt - 4) * 0.25f) * 8.0f);    // rdata 0x36e5e4 / 0x36e5e0 / 0x36e5e8
    }
    Vector2Rot(dir, &rot_x, &rot_y);
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    m[3][0] += pos[0];                  // vadd.xyz
    m[3][1] += pos[1];
    m[3][2] += pos[2];
    size *= 100.0f;                     // rdata 0x36e5ec
    Set3DPosTexure(size, size, m, regs, 4, 0x8c, 0x8c, 0x8c, alpha);     // colour rdata 0x36e5d0
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// Kind 8: the lens flare (FF2 SetRenzFlare, split in three)
// ──────────────────────────────────────────────────────────────────────

// The flare's flicker, redrawn every frame effects run (sdata 0x36e5f0).
static float EffRenzFlareFlicker;

// The nine ghosts (data 0x3537e8; FF2 kept the same values in a local
// copy, with effdat numbers 2 higher).
static const EFRENZ efrenz[9] = {
    { 0, { 0xa0, 0xc8, 0xff, 0x08 }, -1.2f, 4.5f },
    { 2, { 0xa0, 0xff, 0xa0, 0x0a }, -0.7f, 1.5f },
    { 2, { 0xc8, 0xa0, 0xff, 0x0e }, -0.5f, 2.5f },
    { 0, { 0xff, 0xa0, 0xa0, 0x14 },  0.0f, 3.5f },
    { 2, { 0xa0, 0xa0, 0xff, 0x0a },  0.2f, 2.0f },
    { 2, { 0xa0, 0xa0, 0xff, 0x0e },  0.3f, 1.5f },
    { 2, { 0xe0, 0xa0, 0xff, 0x0e },  0.6f, 2.5f },
    { 0, { 0xa0, 0xff, 0xa0, 0x0a },  0.9f, 5.0f },
    { 0, { 0xa0, 0xc8, 0xff, 0x08 },  1.2f, 8.0f },
};

// The flare body. The camera direction's and rot's x / y angles (degrees,
// brought within 180 of each other) give two closeness rates, 1 when equal,
// 0 from 60 degrees apart; their product, the flicker (new each frame
// effects run: 0.25 .. 0.26) and strength scale every alpha. The ghosts lie
// on the line from the light (pos on screen) through the screen centre;
// the fourth also gets the corona (effdat 0xe: 80, 24 and 12 pixels) and
// num star rays turned by the light's screen angle. Returns the flicker.
// FF3 differences from FF2: the strength factor, single-precision fabs,
// WorldToScreenPos for the screen position, the camera angles without the
// 180-degree offset, no 25x scale, corona texture 0xe (FF2 0xc), no
// "Illegal Camera Position" messages.
// FF3 0x0016c528; FF2 SetRenzFlare (MioPan effect_obj.c:2328).
// callcheck: sceVu0UnitMatrix, sceVu0MulMatrix, sceVu0ApplyMatrix (the ROM builds and applies them inline)
// callcheck: rand (EffRand01: ReiPan_Rand)

static float EffRenzFlareMain(float strength, float flicker, float *pos, float *rot, int num)
{
    static const sceVu0FVECTOR wppos = { 0.0f, 0.0f, 0.0f, 1.0f };     // data 0x329500
    sceVu0FMATRIX wlm, slm;
    sceVu0FVECTOR pos1;
    float vpos[4];
    GRA3DCAMERA *pCam;
    float *cam_dir;
    float t1rot_x, t1rot_y, t2rot_x, t2rot_y;
    float f1, f2;
    float rx, ry;
    float mx, my;
    float ang, f, inv;
    const EFRENZ *e;
    int i;

    if (!(0.0f < strength))
        return flicker;

    t2rot_y = 0.0f;
    cam_dir = gra3dcamGetDirection();
    pCam = gra3dGetCamera();
    Vector2Rot(cam_dir, &f1, &f2);
    t2rot_x = 0.0f;
    t1rot_y = (rot[1] * 180.0f) / 3.14159274f;                  // rdata 0x36e5f4 / 0x36e5f8
    t1rot_x = (rot[0] * 180.0f) / 3.14159274f;
    if (-31.4159260f < f1 && f1 < 31.4159260f)                  // rdata 0x36e5fc / 0x36e600
        t2rot_x = (f1 * 180.0f) / 3.14159274f;
    if (-31.4159260f < f2 && f2 < 31.4159260f)
        t2rot_y = (f2 * 180.0f) / 3.14159274f;

    while (t1rot_x < 0.0f)
        t1rot_x += 360.0f;                                      // rdata 0x36e604
    while (t2rot_x < 0.0f)
        t2rot_x += 360.0f;
    while (t1rot_x + 180.0f < t2rot_x)
        t1rot_x += 360.0f;
    while (t2rot_x + 180.0f < t1rot_x)
        t2rot_x += 360.0f;
    while (t1rot_y < 0.0f)
        t1rot_y += 360.0f;
    while (t2rot_y < 0.0f)
        t2rot_y += 360.0f;
    while (t1rot_y + 180.0f < t2rot_y)
        t1rot_y += 360.0f;
    while (t2rot_y + 180.0f < t1rot_y)
        t2rot_y += 360.0f;

    ry = 0.0f;
    rx = 0.0f;
    if (fabsf(t1rot_x - t2rot_x) < 60.0f)                       // rdata 0x36e608
        rx = (60.0f - fabsf(t1rot_x - t2rot_x)) / 60.0f;
    if (fabsf(t1rot_y - t2rot_y) < 60.0f)
        ry = (60.0f - fabsf(t1rot_y - t2rot_y)) / 60.0f;

    WorldToScreenPos(pos, &f1, &f2);
    mx = f1 - 320.0f;                                           // rdata 0x36e60c
    my = f2 - 224.0f;                                           // rdata 0x36e610
    ang = (g3dAtan2f(mx, my) * 180.0f) / 3.14159274f + 45.0f;   // rdata 0x36e614

    sceVu0UnitMatrix(wlm);              // built from vf0; the ROM writes 1.0 (FF2: 25.0) on the diagonal
    wlm[3][0] += pos[0];                // vadd.xyz
    wlm[3][1] += pos[1];
    wlm[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);
    sceVu0ApplyMatrix(pos1, slm, (float *)wppos);
    inv = 1.0f / pos1[3];                                       // rdata 0x36e618
    for (i = 0; i < 4; i++)             // vmulx.xyzw
        pos1[i] *= inv;

    if (eff_wrk.stop_flg == 0)
        flicker = EffRand01() * 0.00999999046f + 0.25f;            // rdata 0x36e61c / 0x36e620 / 0x36e624

    f = rx * ry * flicker * strength;
    if (0.0f < f)
    {
        GS_REG_SET regs[5];

        regs[0].data = 0x48;            regs[0].reg = SCE_GS_ALPHA_1;   // additive
        regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
        regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
        regs[3].data = 0x5000d;         regs[3].reg = SCE_GS_TEST_1;
        regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
        SetGsRegsCached(regs, 5, 1);

        for (i = 1; i < 10; i++)
        {
            e = &efrenz[i - 1];
            vpos[0] = pos1[0] + mx * e->lscl;
            vpos[1] = pos1[1] + my * e->lscl;
            vpos[2] = pos1[2];
            vpos[3] = 1.0f;
            // PC: the ghost sits at (1 + lscl) times the light's offset from
            // the screen centre, so under frame smoothing's in-between
            // cameras it moves that many times as far as the light does.
            ReiPan_Graph3dBeginWorldAnchor(pos, 1.0f + e->lscl);
            if (i == 4)
            {
                SetEffSQTex(0xe, vpos, 0, 80.0f, 80.0f, 0xff, 0x80, 0x80,
                            (u_char)((int)(f * 16.0f) & 0xff));        // rdata 0x36e628 / 0x36e634
                if (0 < num)
                    SetStarRay(vpos, 0, f, num, -ang);
                SetEffSQTex(0xe, vpos, 0, 24.0f, 24.0f, 0xff, 0xc0, 0xd0,
                            (u_char)((int)(f * 32.0f) & 0xff));        // rdata 0x36e62c / 0x36e638
                SetEffSQTex(0xe, vpos, 0, 12.0f, 12.0f, 0xff, 0xc0, 0xd0,
                            (u_char)((int)(f * 48.0f) & 0xff));        // rdata 0x36e630 / 0x36e63c
            }
            SetEffSQTex(e->type, vpos, 0, e->tscl * 10.0f, e->tscl * 10.0f,   // rdata 0x36e640
                        e->rgba[0], e->rgba[1], e->rgba[2],
                        (u_char)((int)((float)e->rgba[3] * f) & 0xff));
            ReiPan_RendererEndAnchor();     // PC
        }
    }
    return flicker;
}

// The flare's star: num spikes evenly round bpos (screen, GS pixels),
// all turned by aang degrees, each an untextured Gouraud strip (PRIM 0x4c)
// of four points (table data 0x353878: base +-4, the bright point, the tip
// 100 out) whose z is the alpha (0, 28 sc, 0, 0); flat pink, grey in mono
// mode. FF3 differences from FF2: no interlace halving of y, ZBUF fixed
// (tp unused), only the depth is clipped (FF2 also dropped spikes outside
// the guard band), the angle through pi / 180.
// FF3 0x0016cd70; FF2 SetStarRay (MioPan effect_obj.c:2513).
// callcheck: __fixunssfsi (the u_int conversion)
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

void SetStarRay(float *bpos, int tp, float sc, int num, float aang)
{
    static const float pos[4][3] = {    // data 0x353878
        { 0.0f, 4.0f, 0.0f },
        { 0.0f, 0.0f, 28.0f },
        { -100.0f, 0.0f, 0.0f },
        { 0.0f, -4.0f, 0.0f },
    };
    float pos2[4][2];
    int x[4], y[4];
    float bx, by, ang, f, rad, ss, cc;
    u_int z, rr, gg, bb;
    u_long *pk;
    u_int *q;
    int i;

    (void)tp;
    bx = bpos[0];
    by = bpos[1];
    z = (u_int)(bpos[2] * 16.0f);                               // rdata 0x36e644
    ang = 360.0f / (float)num;                                  // rdata 0x36e648
    if (eff_wrk.monochro_mode == 0)
    {
        rr = 0xff;
        gg = 0xc0;
        bb = 0xd0;
    }
    else
    {
        rr = 0xda;
        gg = 0xda;
        bb = 0xda;
    }
    SetGsRegCached(0x44, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x5000d, SCE_GS_TEST_1, 1);
    SetGsRegCached(0x10a000118ULL, SCE_GS_ZBUF_1, 1);

    for (f = 0.0f; f < 360.0f; f += ang)
    {
        rad = ((f + aang) * 3.14159274f) / 180.0f;              // rdata 0x36e64c / 0x36e650
        ss = Vu0Sinf(rad);
        cc = Vu0Cosf(rad);
        for (i = 0; i < 4; i++)
        {
            pos2[i][0] = pos[i][0] * ss - pos[i][1] * cc;
            pos2[i][1] = pos[i][1] * ss + pos[i][0] * cc;
            x[i] = (int)((pos2[i][0] + bx) * 16.0f);
            y[i] = (int)((pos2[i][1] + by) * 16.0f);
        }
        if ((u_int)(z - 0xff) >= 0xfffff01)
            continue;

        pk = (u_long *)StartDmaDirectTrans();
        pk[0] = 0x2026400000008004ULL;  // 4 loops, EOP, PRIM 0x4c (strip, Gouraud, blended), 2 regs
        pk[1] = 0x41;                   // RGBAQ, XYZF2
        q = (u_int *)(pk + 2);
        for (i = 0; i < 4; i++)
        {
            q[0] = rr;
            q[1] = gg;
            q[2] = bb;
            q[3] = (u_int)(int)(pos[i][2] * sc);
            q[4] = (u_int)x[i];
            q[5] = (u_int)y[i];
            q[6] = z;
            q[7] = (1 < i) ? 0 : 0x8000;
            q += 8;
        }
        EndDmaDirectTrans((qword *)(pk + 0x12));
    }
}

// The flare's second part: 26 camera-facing sprites (effdat 0x48, 0x49 in
// mono with the mean grey) from pos along rot (x negated, then y; the light's
// facing), each 0.389 bigger and 0.615 fainter than the last (from 0.29 and
// 18, clamped to 255; alpha x rate), spaced 0.09 of the next one's size.
// From the fourth on, once the march passes the room limit they dim by 0.92
// a sprite, and the trail ends when the alpha drops below 0. The limit:
// 12.519 outside a room; inside, 0 when the room's rcb (data member 3) cuts
// the line from pos to the player, else where it cuts the 12.519 ray, 15.419
// when nothing does.
// FF3 0x0016d070.
// callcheck: SetGsRegsCached (EffSetAddDrawEnv: the same five registers)

void EffRenzFlareGhost(float rate, float *pos, float *rot)
{
    static const sceVu0FVECTOR z_axis = { 0.0f, 0.0f, 1.0f, 0.0f };   // data 0x329460
    sceVu0FVECTOR dir;
    sceVu0FVECTOR cur;
    sceVu0FVECTOR hit;
    sceVu0FVECTOR end;
    MAP_ROOM_DAT *room;
    PLYR_WRK *plyr;
    void *rcb;
    float size, alpha, limit, dist, a, step;
    int r, g, b, tex;
    short mh;
    int i;

    if (rate <= 0.0f)
        return;

    room = MapLoadGetCurRoomAddr();
    r = 0xc0;
    g = 0xb0;
    b = 0xb1;
    tex = 0x48;
    if (eff_wrk.monochro_mode != 0)
    {
        r = g = b = (0xc0 + 0xb0 + 0xb1) / 3;   // folded to 0xb5 in the ROM
        tex = 0x49;
    }
    EffSetAddDrawEnv();                         // ALPHA 0x48, TEX1 0x161, CLAMP 0, TEST 0x5000d, ZBUF 0x10a000118
    size = 0.29f;                               // rdata 0x36e654
    alpha = 18.0f;                              // rdata 0x36e658
    RotVectorX(-rot[0], dir, (float *)z_axis);
    RotVectorY(rot[1], dir, dir);
    memcpy(cur, pos, sizeof(cur));
    limit = 12.519f;                            // rdata 0x36e65c
    for (i = 0; i < 3; i++)                     // vmulx.xyz: w keeps dir's
        hit[i] = dir[i] * limit;
    hit[3] = dir[3];
    for (i = 0; i < 3; i++)                     // vadd.xyz
        end[i] = pos[i] + hit[i];
    end[3] = pos[3];

    if (room != nullptr)
    {
        limit = 0.0f;
        rcb = MapLoadGetRoomData(room, 3);
        plyr = ingame_wrk.plyr_wrk;
        mh = (short)plyr->mh_no;                // lh
        if (RcbLineHit(rcb, hit, pos, plyr->pos, mh, 0, 0) == 0)
        {
            limit = 15.419f;                    // rdata 0x36e660
            if (RcbLineHit(rcb, hit, pos, end, mh, 0, 0) != 0)
            {
                float dx = hit[0] - pos[0];
                float dy = hit[1] - pos[1];
                float dz = hit[2] - pos[2];

                limit = sqrtf(dx * dx + dy * dy + dz * dz);
            }
        }
    }

    dist = 0.0f;
    for (i = 0; i < 0x1a; i++)
    {
        if (2 < i && limit < dist + size)
            alpha *= 0.919999f;                 // rdata 0x36e664
        if (alpha < 0.0f)
            return;
        if (255.0f < alpha)                     // rdata 0x36e674
            alpha = 255.0f;
        a = alpha * rate;
        Eff3DFaceSprite(0.0f, size, size, &effdat[tex], cur, (u_char)r, (u_char)g, (u_char)b, (u_char)(int)a);
        alpha += -0.615385f;                    // rdata 0x36e66c
        size += 0.389229f;                      // rdata 0x36e668
        step = size * 0.09f;                    // rdata 0x36e670
        cur[0] += dir[0] * step;                // vmulx.xyz, vadd.xyz
        cur[1] += dir[1] * step;
        cur[2] += dir[2] * step;
        dist += step;
    }
}

// flare > 0: the flare body (it keeps the flicker); other >= 0: the second
// part. Nothing unless one of them is above 0.
// FF3 0x0016c480.

void EffRenzFlareDraw(float flare, float other, float *pos, float *rot, int num)
{
    if (flare <= 0.0f && other <= 0.0f)
        return;
    if (0.0f <= flare)
        EffRenzFlareFlicker = EffRenzFlareMain(flare, EffRenzFlareFlicker, pos, rot, num);
    if (0.0f <= other)
        EffRenzFlareGhost(other, pos, rot);
}

// Kind 8: fw[0] the flare's strength, fw[1] the second part's, pnt[0] the
// light, pnt[1] its rotation, uc8[2] the star rays; one-frame requests end
// here.
// FF3 0x0016c420; FF2 SetRenzFlare's head.

void SetRenzFlare(EFFECT_CONT *ec)
{
    EffRenzFlareDraw(ec->fw[0], ec->fw[1], (float *)ec->pnt[0], (float *)ec->pnt[1], ec->dat.uc8[2]);
    if ((ec->dat.uc8[1] & 1) != 0)
        ResetEffects(ec);
}

// Kind 0x1b: the haze at pnt[0] (fw a second position, the player's joint
// for EffPlyrHazeReq). In the camera / crawl / fixed views it is put 0.034 of
// the way from the camera to its target, 3.8 lower, instead. A record that
// started in phase 0 makes a follow-up haze (phase 1) on its second frame;
// the record ends when neither part draws.
// FF3 0x0016b780. FF2's SetHaze (a HAZE_CTRL particle cloud) is unrelated.

void SetHaze(EFFECT_CONT *ec)
{
    sceVu0FVECTOR rpos;
    sceVu0FVECTOR cpos;
    GRA3DCAMERA *pCam;
    PLYR_WRK *plyr;
    float *campos;
    float *pos;
    int fan;
    int flash;
    int i;

    if (ec->cnt == 0)
        LocalCopyLtoBAdrs(1, HAZE_EE_STASH, 0x38e0);

    rpos[0] = ec->fw[0];
    rpos[1] = ec->fw[1];
    rpos[2] = ec->fw[2];
    rpos[3] = 1.0f;                     // rdata 0x36e560
    plyr = ingame_wrk.plyr_wrk;
    if ((plyr->flags & (PLYR_FLG_800000 | PLYR_FLG_8000000)) == 0 && (plyr->_0e0 & 1) == 0)
    {
        fan = EffHazeDistortDraw((float *)ec->pnt[0], (int)ec->cnt);
        pos = rpos;
    }
    else
    {
        pCam = gra3dGetCamera();
        campos = gra3dcamGetPosition();
        for (i = 0; i < 4; i++)
            cpos[i] = (pCam->vTarget[i] - campos[i]) * 0.034f;
        campos = gra3dcamGetPosition();
        for (i = 0; i < 4; i++)
            cpos[i] = campos[i] + cpos[i];
        cpos[1] -= 3.8f;                // rdata 0x36e564
        cpos[3] = 1.0f;
        fan = EffHazeDistortDraw(cpos, (int)ec->cnt);
        pos = cpos;
    }
    flash = EffHazeFlashDraw(pos, (int)ec->cnt);

    if (ec->flow == 0 && ec->cnt == 1)
    {
        SetEffects(0x1b, 2, (float *)ec->pnt[0], (float *)rpos, 0);
        ec->flow = 1;
    }
    if (fan == 0 && flash == 0)
        ResetEffects(ec);
    else
        ec->cnt++;
}

// ──────────────────────────────────────────────────────────────────────
// The model alpha fades (FF2 effect_obj.c)
// ──────────────────────────────────────────────────────────────────────

// Every running record: alpha lerped AlphaStart -> AlphaEnd over Time
// frames, then the record stops (Time 0).
// FF3 0x0016e9c8; FF2 EffectModelAlphaChangeExec (MioPan effect_obj.c:4092);
// FF3 sets the SGD's material alpha (FF2 ManmdlSetAlpha).

void EffectModelAlphaChangeExec(void)
{
    SLL_CELL *pCell;
    MODEL_ALPHA_CAHNGE_DATA *pData;

    for (pCell = SingleLinkListBeginCell(&ModelAlphaChangeList); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        pData = (MODEL_ALPHA_CAHNGE_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pData->Time != 0)
        {
            gra3dSetSGDMaterialAlpha((SGDFILEHEADER *)pData->pSgdTop,
                (u_char)(int)((float)(pData->AlphaEnd - pData->AlphaStart)
                                  * ((float)pData->Counter / (float)pData->Time)
                              + (float)pData->AlphaStart));
            if (pData->Counter < pData->Time)
            {
                pData->Counter++;
            }
            else
            {
                pData->Counter = 0;
                pData->Time = 0;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Kind 0x1d: the particle clouds (FF3-only)
// ──────────────────────────────────────────────────────────────────────

// A record runs one cloud of up to 0x80 camera-facing quads from one of nine
// parameter sets: spawned on a disc around the owner, drifting with their own
// velocity plus a wind that cycles through three vectors, spinning, scaled
// and faded over their life. Records 6 / 7 follow their owner with a lagging
// position trail, record 8 draws relative to it; 6..8 use CH036_04_TM2's
// picture, the others effdat 82. Record 5 never draws (see EffKind1DCtrl).

// The nine parameter sets (data 0x2f8bd8).
static const KIND1D_PARAM kind1d_param[9] = {
    { 26, 1023, 998, 15,
      { { 1100, 920 }, { 1111, 917 }, { 1046, 956 } },
      { { 1000, 992, 1141 }, { 1000, 955, 916 }, { 1003, 960, 1140 } }, { 95, 95, 95 },
      28, 120, 240, 120, { 240, 272 }, { 27, 29, 28 }, 36, 30, 239,
      0, 1, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
    { 200, 1040, 970, 20,
      { { 1100, 920 }, { 1111, 917 }, { 1046, 956 } },
      { { 1000, 992, 1141 }, { 1000, 955, 916 }, { 1003, 960, 1140 } }, { 95, 95, 95 },
      30, 150, 625, 185, { 530, 640 }, { 27, 29, 29 }, 36, 30, 239,
      0, 1, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
    { 200, 1010, 960, 20,
      { { 1100, 920 }, { 1111, 917 }, { 1046, 956 } },
      { { 1000, 992, 1141 }, { 1000, 955, 916 }, { 1003, 960, 1140 } }, { 95, 95, 95 },
      14, 124, 576, 133, { 510, 556 }, { 96, 96, 48 }, 36, 30, 239,
      2, 0, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
    { 31, 1021, 970, 42,
      { { 1254, 757 }, { 1000, 850 }, { 1050, 950 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 100, 100, 100 },
      103, 9, 0, 111, { 239, 480 }, { 47, 44, 43 }, 36, 30, 239,
      0, 2, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
    { 40, 1000, 965, 42,
      { { 1254, 757 }, { 1000, 850 }, { 1050, 950 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 100, 100, 100 },
      103, 9, 0, 111, { 239, 480 }, { 10, 255, 255 }, 36, 30, 239,
      1, 0, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
    { 80, 1020, 980, 282,
      { { 1120, 880 }, { 1030, 990 }, { 1120, 880 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 100, 100, 100 },
      68, 32, 32, 16, { 410, 480 }, { 50, 45, 40 }, 36, 30, 239,
      0, 1, 0, 1, 0, { 10000, 9990, 10000 }, 0, 0 },
    { 0, 1020, 990, 27,
      { { 1500, 500 }, { 1056, 794 }, { 1230, 704 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 95, 95, 95 },
      51, 20, 100, 180, { 36, 271 }, { 128, 128, 128 }, 12, 6, 102,
      0, 1, 0, 1, 0, { 10000, 10000, 10000 }, 20, 10 },
    { 0, 1020, 990, 14,
      { { 1410, 689 }, { 1092, 744 }, { 1534, 851 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 95, 95, 95 },
      30, 20, 100, 180, { 40, 243 }, { 128, 128, 128 }, 12, 8, 98,
      0, 1, 0, 1, 0, { 10000, 10000, 10000 }, 20, 10 },
    { 0, 1000, 1000, 32,
      { { 1150, 850 }, { 1150, 850 }, { 1150, 850 } },
      { { 1000, 1000, 1000 }, { 1000, 1000, 1000 }, { 1000, 1000, 1000 } }, { 95, 95, 95 },
      96, 10, 10, 60, { 100, 300 }, { 255, 255, 255 }, 0, 0, 0,
      0, 1, 0, 1, 0, { 10000, 10000, 10000 }, 0, 0 },
};

static sceVu0IVECTOR kind1d_scr[KIND1D_PARTICLE_NUM][4];   // bss 0x3d21a0: this frame's quads
static int kind1d_order[KIND1D_PARTICLE_NUM];              // bss 0x3d41a0: draw order (far first)
static int kind1d_part[KIND1D_PARTICLE_NUM];               // bss 0x3d43a0: the particle of each quad

// The CH036_04 picture as MakeTim2Direct puts it (rdata 0x36e7d0).
#define KIND1D_MOVIE_TEX0   0x2005980621312bc0ULL

// The quad's corners (built on the stack) and their ST (data 0x353c98).
static const sceVu0FVECTOR kind1d_quad[4] = {
    { -5.0f, -5.0f, 0.0f, 1.0f },
    { -5.0f,  5.0f, 0.0f, 1.0f },
    {  5.0f, -5.0f, 0.0f, 1.0f },
    {  5.0f,  5.0f, 0.0f, 1.0f },
};
static const float kind1d_quad_st[4][2] = {
    { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
};

// MapEffCreate's kinds 500..502: a cloud of record no at a copy of pos.
// FF3 0x0016f970.

void *EffKind1DMapReq(float *pos, int no)
{
    return SetEffects(0x1d, 2, pos, (void *)nullptr, 0, no, (void *)nullptr);
}

// Kind 0x1d record no at pos, tagged id for EffKind1DCutId.
// FF3 0x0016f9a0 (no caller in the ROM).

void *EffKind1DIdReq(float *pos, int id, int no)
{
    return SetEffects(0x1d, 2, pos, (void *)nullptr, id, no, (void *)nullptr);
}

// FF3 0x0016f9d0 (no caller in the ROM).

void *EffKind1DReq3(float *pos, float *rot, float *alpha)
{
    return SetEffects(0x1d, 2, pos, (void *)rot, 0x3e6, 3, (void *)alpha);
}

// SceneEffectHazeRegist's cloud.
// FF3 0x0016fa00.

void *EffKind1DReq4(float *pos, float *rot, float *alpha)
{
    return SetEffects(0x1d, 2, pos, (void *)rot, 0x3e7, 4, (void *)alpha);
}

// FF3 0x0016fa30 (no caller in the ROM).

void *EffKind1DReq5(float *pos, float *rot, float *alpha)
{
    return SetEffects(0x1d, 2, pos, (void *)rot, 0x3e7, 5, (void *)alpha);
}

// Ends every live kind 0x1d record whose work carries id.
// FF3 0x0016ff38.

void EffKind1DCutId(int id)
{
    EFFECT_CONT *ec = EffectGetModelCont(0);
    KIND1D_WRK *wrk;
    int i;

    for (i = 0; i < EFFECT_MODEL_NUM; i++, ec++)    // the ROM walks 0x1c00 bytes of 0x70-byte records
    {
        wrk = (KIND1D_WRK *)ec->pnt[1];
        if (ec->dat.uc8[0] == 0x1d && wrk != nullptr && wrk->Id == id)
            EffKind1DRelease(ec);
    }
}

// ──────────────────────────────────────────────────────────────────────
// The file screen's flipping panel (menu_file.c; FF3-only)
// ──────────────────────────────────────────────────────────────────────

// Cleared, scales 1, and the game camera saved.
// FF3 0x0016f508.

void EffFlipPanelInit(EFF_FLIP_PANEL *fp)
{
    fp->Ang = 0.0f;
    fp->AngStart = 0.0f;
    fp->AngEnd = 0.0f;
    fp->ScaleY = 1.0f;                          // rdata 0x36e7a4
    fp->Cnt = 0;
    fp->Start = 0;
    fp->End = 0;
    fp->Life = 0;
    fp->ScaleX = 1.0f;
    fp->_1c = 0;
    fp->ZOff = 0.0f;
    fp->Active = 0;
    memcpy(fp->CamPos, gra3dcamGetPosition(), sizeof(fp->CamPos));
    memcpy(fp->CamTarget, gra3dcamGetTarget(), sizeof(fp->CamTarget));
    fp->CamRoll = gra3dcamGetRoll();
    fp->CamFov = gra3dcamGetFov();
}

// The ROM's unreferenced copy of EffFlipPanelInit (its 1.0 is rdata
// 0x36e7a0).
// FF3 0x0016f470.
// callcheck: gra3dcamGetPosition, gra3dcamGetTarget, gra3dcamGetRoll, gra3dcamGetFov (through EffFlipPanelInit)

void EffFlipPanelInit2(EFF_FLIP_PANEL *fp)
{
    EffFlipPanelInit(fp);
}

// The saved camera back (target with fix-up). Not applied here.
// FF3 0x0016f5e0.

void EffFlipPanelCamRestore(EFF_FLIP_PANEL *fp)
{
    gra3dcamSetPosition(fp->CamPos);
    gra3dcamSetTarget(fp->CamTarget, 1);
    gra3dcamSetRoll(fp->CamRoll);
    gra3dcamSetFov(fp->CamFov);
}

// The ROM's unreferenced copy of EffFlipPanelCamRestore.
// FF3 0x0016f5a0.
// callcheck: gra3dcamSetPosition, gra3dcamSetTarget, gra3dcamSetRoll (through EffFlipPanelCamRestore)

void EffFlipPanelCamRestore2(EFF_FLIP_PANEL *fp)
{
    EffFlipPanelCamRestore(fp);
}

// A flip from 0 to pi (dir -1) or pi to 0, turning between frames start
// and end, over after frame life.
// FF3 0x0016f620.

void EffFlipPanelStart(EFF_FLIP_PANEL *fp, int dir, int start, int end, int life)
{
    if (dir == -1)
    {
        fp->AngStart = 0.0f;
        fp->AngEnd = 3.1415927f;                // rdata 0x36e7a8
    }
    else
    {
        fp->AngEnd = 0.0f;
        fp->AngStart = 3.1415927f;
    }
    fp->Start = start;
    fp->End = end;
    fp->Life = life;
    fp->Cnt = 0;
    fp->Active = 1;
    fp->Ang = fp->AngStart;
}

// The panel's camera: 525 above the origin (0.05 towards -z so the up
// vector holds), looking down, roll 0, fov 11.68; applied.
// FF3 0x002b3aa8 (out-of-line static).

static void EffFlipPanelCamSet(void)
{
    gra3dcamSetPosition(0.0f, 525.0f, -0.05f);  // rdata 0x36e7ac / 0x36e7b0
    gra3dcamSetTarget(0.0f, 0.0f, 0.0f, 1);
    gra3dcamSetRoll(0.0f);
    gra3dcamSetFov(11.68f);                     // rdata 0x36e7b4
    gra3dApplyCamera(nullptr, 1, 1);
}

// Sets the panel camera, then one frame of the flip: AngStart before Start,
// lerped to AngEnd at End, AngEnd after; inactive once the frame passes
// Life. 0 when inactive (the camera is set anyway).
// FF3 0x0016f678.

int EffFlipPanelStep(EFF_FLIP_PANEL *fp)
{
    int cnt;

    EffFlipPanelCamSet();
    if (fp->Active == 0)
        return 0;

    cnt = fp->Cnt;
    if (cnt < fp->Start)
        fp->Ang = fp->AngStart;
    else if (cnt <= fp->End)
        fp->Ang = ((float)(cnt - fp->Start) / (float)(fp->End - fp->Start)) * (fp->AngEnd - fp->AngStart)
                  + fp->AngStart;
    else
        fp->Ang = fp->AngEnd;
    fp->Cnt = cnt + 1;
    if (fp->Life < cnt + 1)
        fp->Active = 0;
    return 1;
}

// The panel as a quad in the XZ plane, ScaleX x the uv width wide from x 0,
// ScaleY x the uv height tall around z ZOff, turned about z by Ang (seen
// from EffFlipPanelCamSet's camera: a card flipping over), drawn with ALPHA
// 0x44 and ZBUF 0x10a000118, the cached ALPHA_1 / ZBUF_1 put back after.
// FF3 0x0016f730.

void EffFlipPanelDraw(EFF_FLIP_PANEL *fp, u_long tex0, int *uv, int *col)
{
    sceVu0FMATRIX m;
    sceVu0FVECTOR v[4];
    u_long alpha_reg, zbuf_reg;
    float w, h;

    if (fp->Active == 0)
        return;

    w = fp->ScaleX * ((float)uv[2] - (float)uv[0]);
    h = fp->ScaleY * ((float)uv[3] - (float)uv[1]) * 0.5f;     // rdata 0x36e7bc
    v[0][0] = 0.0f; v[0][1] = 0.0f; v[0][2] = fp->ZOff + h; v[0][3] = 1.0f;     // rdata 0x36e7b8
    v[1][0] = w;    v[1][1] = 0.0f; v[1][2] = fp->ZOff + h; v[1][3] = 1.0f;
    v[2][0] = 0.0f; v[2][1] = 0.0f; v[2][2] = fp->ZOff - h; v[2][3] = 1.0f;
    v[3][0] = w;    v[3][1] = 0.0f; v[3][2] = fp->ZOff - h; v[3][3] = 1.0f;

    alpha_reg = *GetGsRegCached(SCE_GS_ALPHA_1);
    zbuf_reg = *GetGsRegCached(SCE_GS_ZBUF_1);
    SetGsRegCached(0x44, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x10a000118ULL, SCE_GS_ZBUF_1, 1);
    sceVu0UnitMatrix(m);                        // the ROM rotates the unit matrix at data 0x3293c0
    sceVu0RotMatrixZ(m, m, NormalizeAngle(fp->Ang));
    // The ROM adds data 0x329500's xyz (0, 0, 0) to the translation row.
    EffMatTexQuad(tex0, m, v, uv, col);
    SetGsRegCached(alpha_reg, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(zbuf_reg, SCE_GS_ZBUF_1, 1);
}

// The parameter set no (jump table 0x353bb0), nullptr past 8.
// FF3 0x00171648.

static const KIND1D_PARAM *EffKind1DGetParam(u_int no)
{
    if (no < 9)
        return &kind1d_param[no];
    return nullptr;
}

// CH036_04_TM2 loaded: its picture and CLUT go to their own GS addresses.
// FF3 0x0016fa88.

static int EffKind1DTexIsReady(void)
{
    int loaded = file_cache.IsLoaded(CH036_04_TM2);

    if (loaded)
    {
        MakeTim2Direct(kind1d_tex, 0, -1);
        MakeClutDirect(kind1d_tex, 0, -1);
    }
    return loaded != 0;
}

// FF3 0x0016fbc0.

static SINGLE_LINK_LIST *EffKind1DTrailAlloc(void)
{
    return SingleLinkListAlloc(sizeof(sceVu0FVECTOR));
}

// FF3 0x0016fbf0.

static void EffKind1DTrailClear(SINGLE_LINK_LIST *list)
{
    SingleLinkListAllCellFree(list);
}

// Record the owner's position: out = the oldest of the last TrailLen + 1
// positions (pos itself without a trail).
// FF3 0x0016fc08.

static void EffKind1DTrailPush(int no, float *pos, float *out, SINGLE_LINK_LIST *list)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)no);

    if (prm->TrailLen == 0)
    {
        out[0] = pos[0];
        out[1] = pos[1];
        out[2] = pos[2];
        out[3] = pos[3];
        return;
    }
    if ((u_int)prm->TrailLen < SingleLinkListRegCount(list))
        SingleLinkListRemoveBegin(list);
    SingleLinkListAddEnd(list, pos);
    memcpy(out, SingleLinkListCellBodyPtr(SingleLinkListBeginCell(list)), sizeof(sceVu0FVECTOR));
}

// Where a particle of the given life hangs: at the owner's pos until it is
// TrailLag frames old, then easing toward the trail position (1 - t^2) over
// the rest of its life.
// FF3 0x0016fcc8.

static void EffKind1DTrailOffset(int no, int life, float *pos, float *trail, float *out)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)no);
    int all;
    int age;
    float t;
    int i;

    if (prm->TrailLag != 0)
    {
        all = prm->In + prm->Keep + prm->Out;
        age = all - life - 1;
        if (prm->TrailLag <= age)
        {
            t = (float)(age - prm->TrailLag) / (float)(all - prm->TrailLag - 1);
            for (i = 0; i < 4; i++)
                out[i] = trail[i] + (pos[i] - trail[i]) * (1.0f - t * t);  // rdata 0x36e7c4
            return;
        }
    }
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2];
    out[3] = pos[3];
}

// FF3 0x0016ffc0.

static void EffKind1DWrkInit(KIND1D_WRK *wrk, float *pos, float *rot, int v, int no, float *alpha)
{
    KIND1D_PARTICLE *p;
    int i;

    if (wrk == nullptr)
        return;

    for (i = 0, p = wrk->Part; i < KIND1D_PARTICLE_NUM; i++, p++)
    {
        p->Pos[0] = 0.0f;
        p->Pos[1] = 0.0f;
        p->Pos[2] = 0.0f;
        p->Pos[3] = 1.0f;               // rdata 0x36e7c8
        p->Alpha = 0;
        p->Life = 0;
        p->Scale = 0.0f;
        p->Rot = 0.0f;
    }
    wrk->No = (short)no;
    wrk->pPos = pos;
    wrk->pRot = rot;
    wrk->pAlpha = alpha;
    wrk->Id = v;
    wrk->WindCnt = 0;
    wrk->WindPhase = 0;
    wrk->Num = 0;
}

// Kind 0x1d: the work (and for records 6 / 7 the trail) on the first frame,
// then the cloud; anything failing ends the record.
// FF3 0x0016fdc0.
// callcheck: EFFECT_MALLOC (read as a macro)

static KIND1D_WRK *EffKind1DCtrl(float rate, KIND1D_WRK *wrk, SINGLE_LINK_LIST *list);

void SetEffKind1D(EFFECT_CONT *ec)
{
    KIND1D_WRK *wrk;
    int no;
    int v;

    if (ec->pnt[1] == nullptr)
    {
        no = ec->dat.i32[2];
        ec->pnt[4] = nullptr;
        v = ec->dat.i32[1];
        if ((u_int)(no - 6) < 2)
            ec->pnt[4] = EffKind1DTrailAlloc();
        wrk = (KIND1D_WRK *)EFFECT_MALLOC(sizeof(KIND1D_WRK));     // 0x2820 on the EE
        ec->pnt[1] = wrk;
        if (wrk == nullptr)
        {
            if (ec->pnt[4] != nullptr)
            {
                EffKind1DListFree((SINGLE_LINK_LIST *)ec->pnt[4]);
                ec->pnt[4] = nullptr;
            }
            ResetEffects(ec);           // and once more below: the ROM falls through
        }
        else
        {
            EffKind1DWrkInit(wrk, (float *)ec->pnt[0], (float *)ec->pnt[2], v, no, (float *)ec->pnt[3]);
        }
    }

    ec->pnt[1] = EffKind1DCtrl(ec->fw[0], (KIND1D_WRK *)ec->pnt[1], (SINGLE_LINK_LIST *)ec->pnt[4]);
    if (ec->pnt[1] == nullptr)
    {
        if (ec->pnt[4] != nullptr)
        {
            EffKind1DListFree((SINGLE_LINK_LIST *)ec->pnt[4]);
            ec->pnt[4] = nullptr;
        }
        ResetEffects(ec);
    }
}

// One quad: a 4-vertex textured strip (PRIM 0x5c), colour r g b with the
// corner alphas, the first two vertices primers (ADC).
// FF3 0x002b3b20 (out of line).

static void EffKind1DQuadPacket(sceVu0IVECTOR *v, int r, int g, int b, const int *alpha)
{
    u_long *p = (u_long *)StartDmaDirectTrans();
    u_int  *q;
    float   tq;
    int     i;

    p[0] = 0x302e400000008004ULL;       // 4 loops, PRIM 0x5c (strip, gouraud, textured, blended), 3 regs
    p[1] = 0x412;                       // ST, RGBAQ, XYZF2

    q = (u_int *)(p + 2);
    for (i = 0; i < 4; i++)
    {
        float *f = (float *)q;

        tq = 1.0f / (float)v[i][3];     // rdata 0x36ea64
        f[3] = 0.0f;
        f[2] = tq;
        f[0] = kind1d_quad_st[i][0] * tq;
        f[1] = kind1d_quad_st[i][1] * tq;
        q[4] = (u_int)r;
        q[5] = (u_int)g;
        q[6] = (u_int)b;
        q[7] = (u_int)alpha[i];
        q[8] = (u_int)v[i][0];
        q[9] = (u_int)v[i][1];
        q[10] = (u_int)v[i][2];
        q[11] = (i < 2) ? 0x8000 : 0;
        q += 12;
    }
    EndDmaDirectTrans((qword *)(p + 0x1a));
}

// A new particle in p: on a disc of the set's radius around the owner (the
// owner's position left out for 6..8, which draw relative to it) at a random
// height, plus offset; random velocity per axis, full life, start scale,
// random spin of random sign.
// FF3 0x00170b98.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffKind1DParticleInit(KIND1D_PARTICLE *p, float *pos, float *offset, int no)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)no);
    sceVu0FMATRIX m;
    sceVu0FMATRIX unit;
    sceVu0FVECTOR v;
    float ang;
    float spin_hi;
    float spin_lo;
    float spin_div;
    float y_lo;
    float vx_hi, vx_lo, vy_hi, vy_lo, vz_hi, vz_lo;
    float rv;
    int i;

    ang = EffRand01() * 6.2831855f;                         // rdata 0x36e7f4
    v[0] = 0.0f;
    v[1] = 0.0f;
    v[2] = EffRand01() * (float)prm->Radius;
    v[3] = 1.0f;                                            // rdata 0x36e804
    spin_div = (float)prm->SpinDiv;
    spin_hi = ((float)prm->SpinMax * 3.1415927f) / 180.0f;  // rdata 0x36e7f0 / 0x36e7fc
    spin_lo = ((float)prm->SpinMin * 3.1415927f) / 180.0f;
    vz_lo = (float)(prm->Vel[2][1] - 1000) / 2500.0f;       // rdata 0x36e7f8
    vx_hi = (float)(prm->Vel[0][0] - 1000) / 2500.0f;
    vx_lo = (float)(prm->Vel[0][1] - 1000) / 2500.0f;
    vy_hi = (float)(prm->Vel[1][0] - 1000) / 2500.0f;
    vy_lo = (float)(prm->Vel[1][1] - 1000) / 2500.0f;
    vz_hi = (float)(prm->Vel[2][0] - 1000) / 2500.0f;
    y_lo = (float)(prm->YMin - 1000);

    sceVu0UnitMatrix(unit);             // data 0x3293c0
    sceVu0RotMatrixY(m, unit, NormalizeAngle(ang - 3.1415927f));
    sceVu0ApplyMatrix(p->Pos, m, v);
    p->Pos[1] = ((float)(prm->YMax - 1000) - y_lo) * EffRand01() + y_lo;
    if ((u_int)(no - 6) > 2)
    {
        for (i = 0; i < 4; i++)
            p->Pos[i] += pos[i];
    }
    for (i = 0; i < 4; i++)
        p->Pos[i] += offset[i];

    p->Vel[0] = (vx_hi - vx_lo) * EffRand01() + vx_lo;
    p->Vel[1] = (vy_hi - vy_lo) * EffRand01() + vy_lo;
    rv = EffRand01();
    p->Vel[3] = 0.0f;
    p->Life = prm->In + prm->Keep + prm->Out;
    p->Scale = (float)prm->Scale[0] / 100.0f;               // rdata 0x36e800
    p->Alpha = 0;
    p->Rot = 0.0f;
    p->Vel[2] = (vz_hi - vz_lo) * rv + vz_lo;
    if (spin_div == 0.0f)
    {
        p->Spin = 0.0f;
    }
    else
    {
        p->Spin = ((spin_hi - spin_lo) * EffRand01() + spin_lo) / spin_div;
        if (EffRand01() < 0.5f)                             // rdata 0x36e808
            p->Spin = -p->Spin;
    }
}

// Spawn this frame's particles: Rate / 100, plus one with the chance of the
// remainder, each into the first free slot while fewer than 0x80 live. The
// set's offset turns with the owner's rot[1] (none without a rot).
// FF3 0x001709b0.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffKind1DEmit(KIND1D_WRK *wrk)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)wrk->No);
    KIND1D_PARTICLE *p;
    sceVu0FMATRIX m;
    sceVu0FMATRIX unit;
    sceVu0FVECTOR ofs;
    sceVu0FVECTOR v;
    int num;
    int i;
    int k;

    num = (int)((float)prm->Rate / 100.0f);                 // rdata 0x36e7e0
    if (EffRand01() * 100.0f <= (float)prm->Rate - (float)num * 100.0f)
        num++;

    if (wrk->pRot == nullptr)
    {
        ofs[0] = ofs[1] = ofs[2] = ofs[3] = 0.0f;
    }
    else
    {
        v[0] = (float)(prm->Offset[0] - 10000);
        v[1] = (float)(prm->Offset[1] - 10000);
        v[2] = (float)(prm->Offset[2] - 10000);
        v[3] = 1.0f;                                        // rdata 0x36e7e8
        sceVu0UnitMatrix(unit);
        sceVu0RotMatrixY(m, unit, NormalizeAngle(wrk->pRot[1]));
        sceVu0ApplyMatrix(ofs, m, v);
    }

    for (k = 0; k < num; k++)
    {
        if (wrk->Num >= KIND1D_PARTICLE_NUM)
            return;
        p = wrk->Part;
        for (i = 0; p->Life != 0; )
        {
            i++;
            if (i > KIND1D_PARTICLE_NUM - 1)
            {
                p = nullptr;
                break;
            }
            p++;
        }
        if (p != nullptr)
        {
            EffKind1DParticleInit(p, wrk->pPos, ofs, wrk->No);
            wrk->Num++;
        }
    }
}

// The wind of the current phase: vector 0, 1 or 2 held, or the 60-frame
// move between two of them (x, y, z; w 0).
// FF3 0x00170fe0.

static void EffKind1DGetWind(float *out, KIND1D_WRK *wrk)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)wrk->No);
    float w[3][3];
    float t;
    int a = -1;
    int b = -1;
    int i;
    int c;

    t = (float)wrk->WindCnt / 60.0f;                        // rdata 0x36e810
    for (i = 0; i < 3; i++)
        for (c = 0; c < 3; c++)
            w[i][c] = (float)(prm->Wind[i][c] - 1000) / 2500.0f;    // rdata 0x36e80c

    switch (wrk->WindPhase)
    {
    case 0: a = 0;          break;
    case 1: a = 0; b = 1;   break;
    case 2: a = 1;          break;
    case 3: a = 1; b = 2;   break;
    case 4: a = 2;          break;
    case 5: a = 2; b = 0;   break;
    }
    for (c = 0; c < 3; c++)
    {
        if (a < 0)
            out[c] = 0.0f;
        else if (b < 0)
            out[c] = w[a][c];
        else
            out[c] = (w[b][c] - w[a][c]) * t + w[a][c];
    }
    out[3] = 0.0f;
}

// Advance the wind phase: holds of WindTime[0..2] frames, moves of 60.
// FF3 0x00171228.

static void EffKind1DWindStep(KIND1D_WRK *wrk)
{
    const KIND1D_PARAM *prm = EffKind1DGetParam((u_int)wrk->No);

    wrk->WindCnt++;
    switch (wrk->WindPhase)
    {
    case 0:
    case 2:
    case 4:
        if (wrk->WindCnt < prm->WindTime[wrk->WindPhase / 2])
            return;
        wrk->WindCnt = 0;
        wrk->WindPhase++;
        break;
    case 1:
    case 3:
        if (wrk->WindCnt < 60)
            return;
        wrk->WindCnt = 0;
        wrk->WindPhase++;
        break;
    case 5:
        if (wrk->WindCnt > 59)
        {
            wrk->WindPhase = 0;
            wrk->WindCnt = 0;
        }
        break;
    }
}

// Age every live particle: scale from start to end over the life, alpha
// rising over In, held, falling over Out; moved by its velocity and the wind
// (both turned by the owner's rot[1] for 6 / 7), spun (kept in -pi..pi), one
// frame of life less. A dead slot's alpha is 0. Returns the live count.
// FF3 0x00171338.

static int EffKind1DParticleStep(KIND1D_WRK *wrk, float *wind, int no)
{
    const KIND1D_PARAM *prm;
    KIND1D_PARTICLE *p;
    sceVu0FMATRIX m;
    sceVu0FMATRIX unit;
    sceVu0FVECTOR v;
    sceVu0FVECTOR w;
    float s0;
    float s1;
    float fade;
    float t;
    int in_keep;
    int all;
    int age;
    int life;
    int turn;
    int num = 0;
    int i;
    int c;

    if (wrk == nullptr)
        return 0;

    prm = EffKind1DGetParam((u_int)no);
    in_keep = prm->In + prm->Keep;
    all = in_keep + prm->Out;
    s0 = (float)prm->Scale[0] / 100.0f;                     // rdata 0x36e814
    s1 = (float)prm->Scale[1] / 100.0f;
    turn = (u_int)(no - 6) < 2;
    if (turn)
    {
        sceVu0UnitMatrix(unit);
        sceVu0RotMatrixY(m, unit, NormalizeAngle(wrk->pRot[1]));
    }

    for (i = 0, p = wrk->Part; i < KIND1D_PARTICLE_NUM; i++, p++)
    {
        if (all == 0)
        {
            fade = 0.0f;
            life = p->Life;
            t = 1.0f;                                       // rdata 0x36e824
        }
        else
        {
            life = p->Life;
            age = all - life;
            t = 1.0f - (float)life / (float)all;
            fade = 1.0f;
            if (age < prm->In)
            {
                if (prm->In != 0)
                    fade = (float)age / (float)prm->In;
            }
            else if (in_keep <= age)
            {
                fade = (prm->Out == 0) ? 0.0f : 1.0f - (float)(age - in_keep) / (float)prm->Out;
            }
        }

        if (life == 0)
        {
            p->Alpha = 0;
            continue;
        }

        p->Scale = (s1 - s0) * t + s0;
        p->Alpha = (int)((float)prm->Alpha * fade);
        for (c = 0; c < 4; c++)
        {
            v[c] = p->Vel[c];
            w[c] = wind[c];
        }
        if (turn)
        {
            sceVu0ApplyMatrix(v, m, v);
            sceVu0ApplyMatrix(w, m, w);
        }
        for (c = 0; c < 4; c++)
            p->Pos[c] += v[c];
        for (c = 0; c < 4; c++)
            p->Pos[c] += w[c];

        p->Rot += p->Spin;
        if (p->Rot > 3.1415927f)                            // rdata 0x36e81c
            p->Rot -= 6.2831855f;                           // rdata 0x36e818
        if (p->Rot < -3.1415927f)                           // rdata 0x36e820
            p->Rot += 6.2831855f;
        num++;
        p->Life--;
    }
    return num;
}

// The cloud: GS state from the set (its ALPHA, bilinear, z test without z
// write), every live particle's quad facing the camera, turned by its Rot and
// scaled, guard-band clipped (x / y 0x4000..0xc000, z 1..0xffffff), drawn far
// to near; then, unless effects are frozen, spawn, wind and age. rate (fw[0]
// times *pAlpha) scales the alphas; 0 kills every particle and the trail.
// Returns wrk (nullptr in, nullptr out).
// FF3 0x00170030.

static KIND1D_WRK *EffKind1DCtrl(float rate, KIND1D_WRK *wrk, SINGLE_LINK_LIST *list)
{
    const KIND1D_PARAM *prm;
    GRA3DCAMERA *pCam;
    KIND1D_PARTICLE *p;
    sceVu0FMATRIX bill;
    sceVu0FMATRIX m;
    sceVu0FMATRIX slm;
    sceVu0FVECTOR base;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR off;
    sceVu0FVECTOR wind;
    GS_REG_SET regs[5];
    u_long *pk;
    u_long tex0;
    float *campos;
    float rot_x;
    float rot_y;
    int mode;
    int r, g, b;
    int num;
    int out;
    int a;
    int i;
    int k;
    int c;

    if (wrk == nullptr)
        return nullptr;

    mode = 0;
    if ((u_int)(wrk->No - 6) < 2)
    {
        if (EffKind1DTexIsReady() == 0)
            return wrk;
        mode = 1;
    }
    else if (wrk->No == 8)
    {
        if (EffKind1DTexIsReady() == 0)
            return wrk;
        mode = 2;
    }

    pCam = gra3dGetCamera();
    campos = gra3dcamGetPosition();
    prm = EffKind1DGetParam((u_int)wrk->No);
    if (eff_wrk.monochro_mode == 0 || wrk->No == 4)
    {
        r = prm->Rgb[0];
        g = prm->Rgb[1];
        b = prm->Rgb[2];
    }
    else
    {
        r = g = b = (prm->Rgb[0] + prm->Rgb[1] + prm->Rgb[2]) / 3;
    }

    regs[0].data = (u_long)prm->AlphaA | (u_long)prm->AlphaFix << 32
                 | (u_long)prm->AlphaC << 4 | (u_long)prm->AlphaB << 2
                 | (u_long)prm->AlphaD << 6;
    regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x5000d;         regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    if (wrk->pAlpha != nullptr)
    {
        rate *= *wrk->pAlpha;
        if (rate == 0.0f)
        {
            for (i = 0; i < KIND1D_PARTICLE_NUM; i++)
                wrk->Part[i].Life = 0;
        }
    }
    if (mode == 1)
    {
        if (rate == 0.0f)
            EffKind1DTrailClear(list);
        EffKind1DTrailPush(wrk->No, wrk->pPos, base, list);
    }
    else if (mode == 2)
    {
        for (c = 0; c < 4; c++)
            base[c] = wrk->pPos[c];
    }

    Get2PosRot(campos, pCam->vTarget, &rot_x, &rot_y);
    sceVu0UnitMatrix(bill);
    sceVu0RotMatrixX(bill, bill, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(bill, bill, NormalizeAngle(rot_y));

    num = 0;
    for (i = 0; i < KIND1D_PARTICLE_NUM; i++)
    {
        p = &wrk->Part[i];
        if (p->Life == 0)
            continue;

        for (c = 0; c < 4; c++)
            pos[c] = p->Pos[c];
        if (mode == 1)
        {
            if (rate == 0.0f)
                EffKind1DTrailClear(list);
            EffKind1DTrailOffset(wrk->No, p->Life, wrk->pPos, base, off);
            for (c = 0; c < 4; c++)
                pos[c] += off[c];
        }
        else if (mode == 2)
        {
            for (c = 0; c < 4; c++)
                pos[c] += base[c];
        }

        sceVu0UnitMatrix(m);
        m[0][0] = p->Scale;
        m[1][1] = p->Scale;
        m[2][2] = p->Scale;
        sceVu0RotMatrixZ(m, m, NormalizeAngle(p->Rot));
        sceVu0MulMatrix(m, bill, m);
        m[3][0] += pos[0];              // vadd.xyz
        m[3][1] += pos[1];
        m[3][2] += pos[2];
        sceVu0MulMatrix(slm, pCam->matWorldScreen, m);

        out = 0;
        for (k = 0; k < 4; k++)
        {
            sceVu0RotTransPers(kind1d_scr[num][k], slm, (float *)kind1d_quad[k], 0);
            if ((u_int)(kind1d_scr[num][k][0] - 0x4000) > 0x8000
                || (u_int)(kind1d_scr[num][k][1] - 0x4000) > 0x8000
                || (u_int)(kind1d_scr[num][k][2] - 1) > 0xfffffe)
            {
                out = 1;
                break;
            }
        }
        if (out != 0)
            continue;

        a = (int)((float)p->Alpha * rate);
        if (wrk->No == 5)
        {
            // FF3 tests a float that is never set (0) against 0.01 (rdata
            // 0x36e7dc) and skips: set 5 never draws. Past the test the ROM
            // would give its corners alpha a * 0; that code is dead.
            continue;
        }
        for (k = 0; k < 4; k++)
            p->VtxAlpha[k] = a;
        kind1d_part[num] = i;
        num++;
    }

    for (i = 0; i < num; i++)
        kind1d_order[i] = i;
    for (i = 0; i < num - 1; i++)
    {
        for (k = i + 1; k < num; k++)
        {
            int ok = kind1d_order[k];
            int oi = kind1d_order[i];

            if (kind1d_scr[ok][0][2] < kind1d_scr[oi][0][2])
            {
                kind1d_order[k] = oi;
                kind1d_order[i] = ok;
            }
        }
    }

    if (num > 0)
    {
        tex0 = KIND1D_MOVIE_TEX0;
        if (mode == 0)
            tex0 = effdat[82 + eff_wrk.monochro_mode].tex0;     // data 0x352540
        pk = (u_long *)StartDmaDirectTrans();
        pk[4] = tex0;
        pk[2] = 0;
        pk[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);    // 0x1000000000008002
        pk[1] = SCE_GIF_PACKED_AD;
        pk[3] = SCE_GS_TEXFLUSH;
        pk[5] = SCE_GS_TEX0_1;
        EndDmaDirectTrans((qword *)(pk + 6));
        for (i = 0; i < num; i++)
        {
            k = kind1d_order[i];
            EffKind1DQuadPacket(kind1d_scr[k], r, g, b, wrk->Part[kind1d_part[k]].VtxAlpha);
        }
    }

    if (eff_wrk.stop_flg == 0)
    {
        EffKind1DEmit(wrk);
        EffKind1DGetWind(wind, wrk);
        EffKind1DWindStep(wrk);
        wrk->Num = (short)EffKind1DParticleStep(wrk, wind, wrk->No);
    }
    return wrk;
}

// ──────────────────────────────────────────────────────────────────────
// List B: falling particles (FF3-only). Nothing calls EffFallPartReq, so
// the list stays empty in the retail game; EffKind1DListBExec walks it.
// ──────────────────────────────────────────────────────────────────────

// A particle back to the top: a random spot in the spread x spread square
// over the origin (offsets truncated to whole units), height above it, new
// sway targets and fall speed, the emitter's colour, counters and alpha 0.
// FF3 0x00171898.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffFallPartReset(EFF_FALL_PART *p, float *origin, int spread, float *speed, short height,
                             short *col)
{
    float s = (float)spread;
    float half = s * 0.5f;                                  // rdata 0x36e83c

    p->Pos[0] = origin[0] + (float)(int)(s * EffRand01() - half);
    p->Pos[2] = origin[2] + (float)(int)(s * EffRand01() - half);
    p->Pos[3] = 1.0f;                                       // rdata 0x36e840
    p->Pos[1] = origin[1] - (float)height;
    p->Tgt[0] = (speed[3] + speed[3]) * EffRand01() - speed[3];
    p->Tgt[2] = (speed[3] + speed[3]) * EffRand01() - speed[3];
    p->Acc[0] = 0.0f;
    p->Acc[2] = 0.0f;
    p->Rot[0] = 0.0f;
    p->Rot[1] = 0.0f;
    p->Rot[2] = 0.0f;
    p->Acc[1] = speed[1] - (EffRand01() * 3.0f + 1.0f) / 500.0f;   // rdata 0x36e844 / 0x36e848
    p->Col[0] = col[0];
    p->Col[1] = col[1];
    p->Col[2] = col[2];
    p->FadeInCnt = 0;
    p->Alpha = 0;
    p->LandCnt = 0;
}

// A new emitter in ctrl: num particles, each placed by EffFallPartReset. A
// particle takes height / speed[1] frames to fall and fade frames to fade,
// so Interval spaces num of them over one cycle. 1 when the buffer came.
// FF3 0x00171ab8.
// callcheck: EFFECT_MALLOC (read as a macro)

static int EffFallPartCtrlInit(EFF_FALL_CTRL *ctrl, float *pos, int spread, int num, float *speed,
                               short height, int fade, short r, short g, short b, short alpha)
{
    float t;
    int i;

    ctrl->pPart = (EFF_FALL_PART *)EFFECT_MALLOC(num * (int)sizeof(EFF_FALL_PART));
    if (ctrl->pPart == nullptr)
        return 0;

    for (i = 0; i < 4; i++)
        ctrl->Origin[i] = pos[i];
    ctrl->Max = num;
    ctrl->Active = num;
    for (i = 0; i < 4; i++)
        ctrl->Speed[i] = speed[i];
    ctrl->Col[0] = r;
    ctrl->Col[1] = g;
    ctrl->Col[2] = b;
    ctrl->Height = height;
    ctrl->Alpha = alpha;
    ctrl->FadeTime = fade;
    ctrl->Spread = (short)spread;
    t = ((float)height / ctrl->Speed[1] + (float)fade) / (float)ctrl->Max;
    ctrl->Timer = t;
    ctrl->Interval = t;
    for (i = 0; i < num; i++)
        EffFallPartReset(&ctrl->pPart[i], ctrl->Origin, spread, ctrl->Speed, ctrl->Height, ctrl->Col);
    return 1;
}

// Spreads a new emitter's particles over the fall: each starts a random
// part of the height lower, less the fade time's worth of fall.
// FF3 0x00171c30.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffFallPartScatter(EFF_FALL_CTRL *ctrl)
{
    int i;

    for (i = 0; i < ctrl->Max; i++)
        ctrl->pPart[i].Pos[1] += (float)(int)((float)ctrl->Height * EffRand01())    // rdata 0x36e84c
                               - (float)ctrl->FadeTime * ctrl->Speed[1];
}

// Unreferenced in the ROM: a list B emitter at pos (30 bluish particles
// falling 300 units through a 400-unit square, 117-frame landing fade).
// FF3 0x00171718.
// callcheck: EFFECT_FREE (read as a macro)

void EffFallPartReq(float *pos, int id)
{
    EFF_FALL_CTRL ctrl;
    float speed[4] = { 0.03f, 0.73999995f, 0.038f, 4.0059996f };    // rdata 0x36e828..0x36e834

    if (EffFallPartCtrlInit(&ctrl, pos, 400, 30, speed, 300, 117, 0x40, 0x55, 0xa7, 0xa1) == 0)
        return;
    EffFallPartScatter(&ctrl);
    ctrl.Id = id;
    if (SingleLinkListAddEnd(&EffKind1DListB, &ctrl) == nullptr)
        EFFECT_FREE(ctrl.pPart);
}

// Every list B emitter with Id id (id < 0: all): its particles freed, the
// entry removed.
// FF3 0x001717f8.
// callcheck: EFFECT_FREE (read as a macro)

void EffFallPartCut(int id)
{
    SLL_CELL *pCell, *pNext;
    EFF_FALL_CTRL *ctrl;

    for (pCell = SingleLinkListBeginCell(&EffKind1DListB); pCell != nullptr; pCell = pNext)
    {
        pNext = pCell->pNext;
        ctrl = (EFF_FALL_CTRL *)SingleLinkListCellBodyPtr(pCell);
        if (id < 0 || ctrl->Id == id)
        {
            EFFECT_FREE(ctrl->pPart);
            SingleLinkListRemove(&EffKind1DListB, pCell);
        }
    }
}

// The x / z swing: Acc walks toward the target by the speed step and pos
// moves by it (by target - Acc past half way, so the swing eases back);
// a reached target is replaced by a new random one within +- speed[3].
// y jitters by +-0.002 each frame.
// FF3 0x00172018.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffFallPartSway(float *pos, float *acc, float *tgt, float *speed)
{
    float d;
    int reached;

    if (acc[0] < tgt[0])
    {
        acc[0] += speed[0];
        d = acc[0];
        if (tgt[0] * 0.5f <= d)                             // rdata 0x36e854
            d = tgt[0] - d;
        pos[0] += d;
        reached = tgt[0] <= acc[0];
    }
    else
    {
        acc[0] -= speed[0];
        d = acc[0];
        if (d <= tgt[0] * 0.5f)
            d = tgt[0] - d;
        pos[0] += d;
        reached = acc[0] <= tgt[0];
    }
    if (reached)
    {
        tgt[0] = (float)(int)((speed[3] + speed[3]) * EffRand01() - speed[3]);     // rdata 0x36e858
        acc[0] = 0.0f;
    }

    if ((int)(EffRand01() * 10.0f) + 1 < 6)                 // rdata 0x36e85c
        pos[1] += 0.0019999999f;                            // rdata 0x36e860
    else
        pos[1] -= 0.0019999999f;

    if (acc[2] < tgt[2])
    {
        acc[2] += speed[2];
        d = acc[2];
        if (tgt[2] * 0.5f <= d)
            d = tgt[2] - d;
        pos[2] += d;
        reached = tgt[2] <= acc[2];
    }
    else
    {
        acc[2] -= speed[2];
        d = acc[2];
        if (d <= tgt[2] * 0.5f)
            d = tgt[2] - d;
        pos[2] += d;
        reached = acc[2] <= tgt[2];
    }
    if (reached)
    {
        tgt[2] = (float)(int)((speed[3] + speed[3]) * EffRand01() - speed[3]);
        acc[2] = 0.0f;
    }
}

// The tilt that follows the swing: rot z from the x swing, rot x from the
// z swing, up to +-pi/2 at the swing's turn. A target within +-1 sets rot y
// (not the axis's own angle) to 0 instead; the z half then returns early.
// FF3 0x001722c0.

static void EffFallPartTilt(float *rot, float *acc, float *tgt)
{
    const float half_pi = 1.5707964f;                       // rdata 0x36e870 (negated) / 0x36e874
    float a, t;

    a = acc[0];
    t = tgt[0];
    if ((t <= a) || (-1.0f <= t && t <= 1.0f))              // rdata 0x36e864 / 0x36e868
    {
        if ((t <= a) && (t < -1.0f || 1.0f < t))
            rot[2] = (((t * 0.5f < a) ? a : (t - a)) * half_pi) / (t * 0.5f);     // rdata 0x36e86c
        else
            rot[1] = 0.0f;
    }
    else
    {
        rot[2] = (((t * 0.5f <= a) ? (t - a) : a) * -half_pi) / (t * 0.5f);
    }

    a = acc[2];
    t = tgt[2];
    if ((t <= a) || (-1.0f <= t && t <= 1.0f))
    {
        if ((a < t) || (-1.0f <= t && t <= 1.0f))
        {
            rot[1] = 0.0f;
            return;
        }
        rot[0] = (((t * 0.5f < a) ? a : (t - a)) * -half_pi) / (t * 0.5f);
    }
    else
    {
        rot[0] = (((a < t * 0.5f) ? a : (t - a)) * half_pi) / (t * 0.5f);
    }
}

// The drawn colour: the emitter's inside the player's light cone (1200
// units), half of it outside.
// FF3 0x001724b8.

static void EffFallPartLightCol(float *pos, short *out, short *col)
{
    float off = 0.0f;
    float depth = 0.0f;

    if (EffPlyrLightConeCheck(1200.0f, pos, &off, &depth) == 0)    // rdata 0x36e878
    {
        out[0] = col[0] / 2;
        out[1] = col[1] / 2;
        out[2] = col[2] / 2;
    }
    else
    {
        out[0] = col[0];
        out[1] = col[1];
        out[2] = col[2];
    }
}

// One particle: a 2.4-unit square turned by its tilt (x, then y, then z)
// at its position, texture effdat[n] mapped whole (UV), flat colour (grey,
// the mean of r, g, b, in mono mode) at its alpha. The first corner
// outside the guard band drops it. The ROM passes the position, tilt and
// colour as pointers into the particle.
// FF3 0x002b3c98.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

static void EffFallPartDraw(EFF_FALL_PART *p, int n)
{
    sceVu0FVECTOR vtx[4] = {                                // stack constants (+-1.2)
        { -1.2f, 1.2f, 0.0f, 1.0f }, { 1.2f, 1.2f, 0.0f, 1.0f },
        { -1.2f, -1.2f, 0.0f, 1.0f }, { 1.2f, -1.2f, 0.0f, 1.0f },
    };
    sceVu0FMATRIX m;
    sceVu0FMATRIX slm;
    sceVu0IVECTOR iv[4];
    GRA3DCAMERA *pCam;
    u_int r, g, b;
    u_int w, h;
    u_long *pk;
    u_int *q;
    int i;

    pCam = gra3dGetCamera();
    if (eff_wrk.monochro_mode == 0)
    {
        r = (u_char)p->Col[0];
        g = (u_char)p->Col[1];
        b = (u_char)p->Col[2];
    }
    else
    {
        r = (u_int)((p->Col[0] + p->Col[1] + p->Col[2]) / 3) & 0xff;
        g = r;
        b = r;
    }

    sceVu0UnitMatrix(m);                // the ROM rotates the unit matrix at data 0x3293c0
    sceVu0RotMatrixX(m, m, NormalizeAngle(p->Rot[0]));
    sceVu0RotMatrixY(m, m, NormalizeAngle(p->Rot[1]));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(p->Rot[2]));
    m[3][0] += p->Pos[0];               // vadd.xyz
    m[3][1] += p->Pos[1];
    m[3][2] += p->Pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, m);
    sceVu0RotTransPersN(iv, slm, vtx, 4, 0);
    for (i = 0; i < 4; i++)
    {
        if ((u_int)(iv[i][0] - 0x4000) > 0x8000 || (u_int)(iv[i][1] - 0x4000) > 0x8000
            || (u_int)(iv[i][2] - 1) > 0xfffffe)
            return;
    }

    w = effdat[n].w;
    h = effdat[n].h;
    pk = (u_long *)StartDmaDirectTrans();
    pk[4] = effdat[n].tex0;
    pk[2] = 0;
    pk[0] = SCE_GIF_SET_TAG(2, 0, 0, 0, SCE_GIF_PACKED, 1);    // 0x1000000000000002 (no EOP)
    pk[6] = 0x30aa400000008004ULL;      // 4 loops, PRIM 0x154 (strip, flat, textured, blended, UV), 3 regs
    pk[1] = SCE_GIF_PACKED_AD;
    pk[3] = SCE_GS_TEXFLUSH;
    pk[5] = SCE_GS_TEX0_1;
    pk[7] = 0x431;                      // RGBAQ, UV, XYZF2

    q = (u_int *)(pk + 8);
    for (i = 0; i < 4; i++)
    {
        q[0] = r;
        q[1] = g;
        q[2] = b;
        q[3] = (u_int)(int)p->Alpha;
        q[4] = (i & 1) ? w << 4 : 0;
        q[5] = (i / 2 != 0) ? h << 4 : 0;
        q[6] = 0;
        q[7] = 0;
        q[8] = (u_int)iv[i][0];
        q[9] = (u_int)iv[i][1];
        q[10] = (u_int)iv[i][2];
        q[11] = (i < 2) ? 0x8000 : 0;
        q += 12;
    }
    EndDmaDirectTrans((qword *)(pk + 0x20));
}

// One emitter per frame: the emit timer (only while Active < Max, which
// EffFallPartCtrlInit never leaves), the GS state, then each moving
// particle: landed (at or below the origin's y) it fades out over FadeTime
// frames and starts again; falling it fades in over 60 frames, falls,
// sways and tilts. Every particle is lit and drawn (effdat 0xc + mono).
// FF3 0x00171d70.

static void EffFallPartCtrlExec(EFF_FALL_CTRL *ctrl)
{
    EFF_FALL_PART *pPart = ctrl->pPart;
    EFF_FALL_PART *p;
    GS_REG_SET regs[5];
    float t;
    int lim;
    int i;

    if (ctrl->Active < ctrl->Max)
    {
        t = ctrl->Timer + 1.0f;                             // rdata 0x36e850
        ctrl->Timer = t;
        while (ctrl->Interval <= t)
        {
            ctrl->Active++;
            t = ctrl->Timer - ctrl->Interval;
            ctrl->Timer = t;
        }
    }

    regs[0].data = 0x44;            regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = 0x5000d;         regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    for (i = 0; i < ctrl->Active; i++)
    {
        p = &pPart[i];
        if (ctrl->Origin[1] <= p->Pos[1])
        {
            p->LandCnt++;
            lim = 0;
            if (ctrl->FadeTime != 0)
            {
                p->Alpha = (short)(ctrl->Alpha - (ctrl->Alpha * p->LandCnt) / ctrl->FadeTime);
                lim = ctrl->FadeTime;
            }
            if (lim <= p->LandCnt)
                EffFallPartReset(p, ctrl->Origin, ctrl->Spread, ctrl->Speed, ctrl->Height, ctrl->Col);
        }
        else
        {
            if (p->FadeInCnt < 60)
            {
                p->Alpha = (short)((ctrl->Alpha * p->FadeInCnt) / 60);
                p->FadeInCnt++;
            }
            else
            {
                p->Alpha = ctrl->Alpha;
            }
            p->Pos[1] += p->Acc[1];
            EffFallPartSway(p->Pos, p->Acc, p->Tgt, ctrl->Speed);
            EffFallPartTilt(p->Rot, p->Acc, p->Tgt);
        }
        EffFallPartLightCol(p->Pos, p->Col, ctrl->Col);
        EffFallPartDraw(p, eff_wrk.monochro_mode + 0xc);
    }
}

// EffectInPhoto: every entry of list B.
// FF3 0x00171d08.

void EffKind1DListBExec(void)
{
    SLL_CELL *pCell;

    for (pCell = SingleLinkListBeginCell(&EffKind1DListB); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
        EffFallPartCtrlExec((EFF_FALL_CTRL *)SingleLinkListCellBodyPtr(pCell));
}

// ──────────────────────────────────────────────────────────────────────
// List A: the lamps of map effect kind 4 (MapEff.c)
// ──────────────────────────────────────────────────────────────────────

// A lamp record at pos for put id; flag picks the additive draw.
// PC: the whole record is cleared (the ROM leaves +0x34.. as stack bytes;
// nothing reads them before the draws, which are not reconstructed).
// FF3 0x00172580.

void EffLampReq(float *pos, int id, int flag)
{
    EFF_LAMP lamp;
    int i;

    memset(&lamp, 0, sizeof(lamp));
    for (i = 0; i < 4; i++)
        lamp.Pos[i] = pos[i];
    lamp.Id = id;
    lamp.AddBlend = flag;
    SingleLinkListAddEnd(&EffKind1DListA, &lamp);
}

// Removes the lamps of put id (id < 0: all).
// FF3 0x00172600.

void EffLampCut(int id)
{
    SLL_CELL *pCell, *pNext;
    EFF_LAMP *lamp;

    for (pCell = SingleLinkListBeginCell(&EffKind1DListA); pCell != nullptr; pCell = pNext)
    {
        pNext = pCell->pNext;
        lamp = (EFF_LAMP *)SingleLinkListCellBodyPtr(pCell);
        if (id < 0 || lamp->Id == id)
            SingleLinkListRemove(&EffKind1DListA, pCell);
    }
}

// m = the unit matrix turned by the camera's facing angles (x, then y),
// moved to pos (vadd.xyz): the lamps' camera-facing billboard frame.
// PC: shared by the two lamp draws, which build it inline.

static void EffLampFaceMatrix(sceVu0FMATRIX m, float rot_x, float rot_y, const float *pos)
{
    sceVu0UnitMatrix(m);                // the ROM rotates the unit matrix at data 0x3293c0
    sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    m[3][0] += pos[0];
    m[3][1] += pos[1];
    m[3][2] += pos[2];
}

// The normal-blend lamp: a slow blink (dark for a wait, fade in, fade out)
// faster inside the player's light cone (1200 units), a spinning glow
// (effdat 0x4e) and a camera-facing halo (0x50) whose alpha follows the
// blink; inside the cone the glow is white and three crossed sparkles (0x50,
// tilted 45 degrees, tinted blue / green / red) replace the halo. The ROM
// also builds the halo frame when it does not draw it.
// FF3 0x002b3fe8.
// callcheck: sceVu0UnitMatrix (the ROM rotates data 0x3293c0)

static void EffLampDrawNormal(EFF_LAMP *lamp, u_long *regs)
{
    GRA3DCAMERA *pCam;
    float *cam_pos;
    sceVu0FVECTOR pos;
    sceVu0FMATRIX m_face;               // camera-facing: the halo
    sceVu0FMATRIX m_glow;               // spun by Rot, then camera-facing
    sceVu0FMATRIX m_spk[3];             // the sparkles
    float rot_x, rot_y;
    float wait_time, fade_time;
    float rot, cnt, new_cnt;
    float t = 0.0f;
    float spk_ang;
    int state, lit, i;
    u_int a;

    pCam = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();
    for (i = 0; i < 4; i++)
        pos[i] = lamp->Pos[i];
    state = lamp->State;
    cnt = lamp->Cnt;
    rot = lamp->Rot;

    lit = EffPlyrLightConeIn(1200.0f, pos);                     // rdata 0x36ea68
    if (lit == 0)
    {
        if (eff_wrk.stop_flg == 0)
        {
            rot += 0.0581776425f;                                 // rdata 0x36ea6c (pi / 54)
            if (3.14159274f < rot)
                rot -= 6.28318548f;
        }
        fade_time = 30.0f;                                      // rdata 0x36ea78
        wait_time = 60.0f;                                      // rdata 0x36ea7c
    }
    else
    {
        if (eff_wrk.stop_flg == 0)
        {
            rot += 0.08726646f;                                 // rdata 0x36ea80 (pi / 36)
            if (3.14159274f < rot)
                rot -= 6.28318548f;
        }
        fade_time = 20.0f;                                      // rdata 0x36ea84
        wait_time = 10.0f;                                      // rdata 0x36ea88
    }

    // The timer, clamped to the phase's length.
    if (state == 0)
    {
        if (wait_time < cnt)
            cnt = wait_time;
    }
    else if (state == 1 || state == 2)
    {
        if (fade_time < cnt)
            cnt = fade_time;
    }

    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);

    new_cnt = cnt;
    if (state == 0)
    {
        if (wait_time <= cnt)
        {
            state = 1;
            new_cnt = 0.0f;
        }
        else
        {
            new_cnt = (eff_wrk.stop_flg == 0) ? cnt + 1.0f : cnt;   // rdata 0x36ea8c
        }
    }
    else if (state == 1)
    {
        t = cnt / fade_time;
        if (fade_time <= cnt)
        {
            state = 2;
            new_cnt = 0.0f;
        }
        else
        {
            new_cnt = (eff_wrk.stop_flg == 0) ? cnt + 1.0f : cnt;
        }
    }
    else if (state == 2)
    {
        t = (fade_time - cnt) / fade_time;
        if (fade_time <= cnt)
        {
            state = 0;
            new_cnt = 0.0f;
        }
        else
        {
            new_cnt = (eff_wrk.stop_flg == 0) ? cnt + 1.0f : cnt;
        }
    }

    EffLampFaceMatrix(m_face, rot_x, rot_y, pos);
    sceVu0UnitMatrix(m_glow);
    sceVu0RotMatrixZ(m_glow, m_glow, NormalizeAngle(rot));
    sceVu0RotMatrixX(m_glow, m_glow, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m_glow, m_glow, NormalizeAngle(rot_y));
    m_glow[3][0] += pos[0];
    m_glow[3][1] += pos[1];
    m_glow[3][2] += pos[2];

    if (lit == 0)
    {
        Set3DPosTexure2(t * 4.79999971f, t * 4.79999971f, m_glow, regs, 0x4e, 0xb4, 0xb4, 0xb4, 0x50, 0);  // rdata 0x36ea90
        a = (u_int)(int)(t * 48.0f) & 0xff;                     // rdata 0x36ea94
        Set3DPosTexure2(2.15999985f, 2.15999985f, m_face, regs, 0x50, 0xb4, 0xb4, 0xb4, (u_char)a, 0);  // rdata 0x36ea98
    }
    else
    {
        spk_ang = 0.7853982f;                                   // rdata 0x36ea9c (pi / 4)
        sceVu0UnitMatrix(m_spk[0]);
        sceVu0RotMatrixX(m_spk[0], m_spk[0], NormalizeAngle(rot));
        sceVu0RotMatrixY(m_spk[0], m_spk[0], NormalizeAngle(spk_ang));
        sceVu0UnitMatrix(m_spk[1]);
        sceVu0RotMatrixY(m_spk[1], m_spk[1], NormalizeAngle(rot));
        sceVu0RotMatrixZ(m_spk[1], m_spk[1], NormalizeAngle(spk_ang));
        sceVu0UnitMatrix(m_spk[2]);
        sceVu0RotMatrixX(m_spk[2], m_spk[2], NormalizeAngle(spk_ang));
        sceVu0RotMatrixZ(m_spk[2], m_spk[2], NormalizeAngle(rot));
        for (i = 0; i < 3; i++)
        {
            m_spk[i][3][0] += pos[0];
            m_spk[i][3][1] += pos[1];
            m_spk[i][3][2] += pos[2];
        }
        Set3DPosTexure2(t * 6.0f, t * 6.0f, m_glow, regs, 0x4e, 0xff, 0xff, 0xff, 0x50, 0);  // rdata 0x36eaa0
        a = (u_int)(int)(t * 64.0f) & 0xff;                     // rdata 0x36eaa8
        Set3DPosTexure2(3.12f, 3.12f, m_spk[0], regs, 0x50, 0xf0, 0xf0, 0xff, (u_char)a, 0);  // rdata 0x36eaa4
        Set3DPosTexure2(3.12f, 3.12f, m_spk[1], regs, 0x50, 0xf0, 0xff, 0xf0, (u_char)a, 0);
        Set3DPosTexure2(3.12f, 3.12f, m_spk[2], regs, 0x50, 0xff, 0xf0, 0xf0, (u_char)a, 0);
    }

    lamp->Rot = rot;
    lamp->State = state;
    lamp->Cnt = new_cnt;
}

// A triangle wave over 2 * half frames: a at cnt 0, b at half, back to a.
// FF3 0x002b5900.

static int EffLampFlickerTri(int cnt, int half, int a, int b)
{
    float t = 1.0f;                     // rdata 0x36eb04 (half <= 0)

    if (cnt < half)
    {
        if (0 < half)
            t = (float)cnt / (float)half;
        return (int)((float)(b - a) * t + (float)a);
    }
    if (0 < half)
        t = (float)(cnt - half) / (float)half;
    return (int)((float)(a - b) * t + (float)b);
}

// The additive lamp: two camera-facing glows (effdat 6 and 8) whose sizes
// and alphas follow four free-running triangle waves of different periods
// (the first size wave runs from 22 to 22: constant), dimmer under the
// candle-out screen effects 5 / 6.
// FF3 0x002b46d0.
// callcheck: sceVu0UnitMatrix (the ROM rotates data 0x3293c0)
// callcheck: NormalizeAngle, sceVu0RotMatrixX, sceVu0RotMatrixY (in EffLampFaceMatrix)

static void EffLampDrawAdd(EFF_LAMP *lamp, u_long *regs)
{
    sceVu0FMATRIX m;
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float rot_x, rot_y;
    float size0, size1;
    u_char r0 = 0x36, g0 = 0x42, b0 = 0xb5;     // effdat 6
    u_char r1 = 100, g1 = 0x7b, b1 = 0xea;      // effdat 8
    u_char a0, a1;

    if (++lamp->Flk[0] > 0x9a)
        lamp->Flk[0] = 0;
    if (++lamp->Flk[1] > 0x2c)
        lamp->Flk[1] = 0;
    if (++lamp->Flk[2] > 0x8e)
        lamp->Flk[2] = 0;
    if (++lamp->Flk[3] > 0x4a)
        lamp->Flk[3] = 0;

    size0 = (float)EffLampFlickerTri(lamp->Flk[0], 0x4d, 0x16, 0x16) / 100.0f;   // rdata 0x36eaac
    size1 = (float)EffLampFlickerTri(lamp->Flk[1], 0x16, 0x3b, 0x50) / 100.0f;
    a0 = (u_char)EffLampFlickerTri(lamp->Flk[2], 0x47, 0x20, 0x44);
    a1 = (u_char)EffLampFlickerTri(lamp->Flk[3], 0x25, 0x19, 0x3b);

    cam_pos = gra3dcamGetPosition();
    pCam = gra3dGetCamera();
    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);
    EffLampFaceMatrix(m, rot_x, rot_y, lamp->Pos);

    if (EffectNowScreenEffectIs5Or6() != 0)
    {
        r0 = 0x0d; g0 = 0x10; b0 = 0x5a;
        r1 = 0x19; g1 = 0x1e; b1 = 0x75;
    }
    Set3DPosTexure2(size0 * 4.0f, size0 * 4.0f, m, regs, 6, r0, g0, b0, a0, 0);  // rdata 0x36eab0
    Set3DPosTexure2(size1 * 4.0f, size1 * 4.0f, m, regs, 8, r1, g1, b1, a1, 0);
}

// MapEffDrawLamp: the first lamp of put id, drawn by one of the two
// out-of-line draws with its GS values (TEX1, ALPHA 0x44 or additive 0x48,
// ZBUF, TEST, CLAMP and the strip's GIF tag), then the default GS state.
// Nothing while effects are hidden (disp_flg, read as a byte).
// FF3 0x001726c0.

void EffLampDraw(int id)
{
    SLL_CELL *pCell;
    EFF_LAMP *lamp = nullptr;
    EFF_LAMP *l;
    u_long regs[6];

    if ((u_char)eff_wrk.disp_flg == 0)
        return;
    for (pCell = SingleLinkListBeginCell(&EffKind1DListA); pCell != nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        l = (EFF_LAMP *)SingleLinkListCellBodyPtr(pCell);
        if (l->Id == id)
        {
            lamp = l;
            break;
        }
    }
    if (lamp == nullptr)
        return;

    regs[0] = 0x161;                    // TEX1_1
    regs[1] = (lamp->AddBlend == 0) ? 0x44 : 0x48;     // ALPHA_1
    regs[2] = 0x10a000118ULL;           // ZBUF_1
    regs[3] = 0x5000d;                  // TEST_1
    regs[4] = 0;                        // CLAMP_1
    regs[5] = 0x302a400000008004ULL;    // 4 loops, PRIM 0x54 (strip, textured, blended), 3 regs
    if (lamp->AddBlend == 0)
        EffLampDrawNormal(lamp, regs);
    else
        EffLampDrawAdd(lamp, regs);
    gra3dSetGsRegisterDefault();
}

// ──────────────────────────────────────────────────────────────────────
// List C: particle bursts (update_particles and FUN_001e9ae8 ask for them)
// ──────────────────────────────────────────────────────────────────────

// ---- list C per-type setups and particle inits (tables 0x353c38 / 0x353c68)

// Type 0: 256 white specks rising out of an 8 x 8 square, a few new per
// frame (EmitRate 0.2), never freed when empty.
// FF3 0x00173258.

int EffParticleType0Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[0] = 8.0f;                     // rdata 0x36e8a4
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[2] = 8.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[0] = 0.01f;                      // rdata 0x36e8a8
    ctrl->Kick[1] = 0.001f;                     // rdata 0x36e8b0
    ctrl->Kick[2] = 0.01f;
    ctrl->Kick[3] = 0.0f;
    ctrl->AlphaReg = 0x44;
    ctrl->TestReg = 0x5000b;
    ctrl->Num = 0x100;
    ctrl->EmitRate = 0.2f;                      // rdata 0x36e8ac
    ctrl->Drag = 0.98f;                         // rdata 0x36e8b4
    ctrl->FloorY = 0.0f;
    ctrl->FreeWhenEmpty = 0;
    return 0;
}

// FF3 0x001732e8.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType0Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Vel[0] = EffRand01() * 0.2f - 0.1f;      // rdata 0x36e8b8 / 0x36e8bc / 0x36e8c0
    p->Vel[1] = EffRand01() * -0.2f - 0.2f;     // rdata 0x36e8c4
    p->Vel[2] = EffRand01() * 0.2f - 0.1f;
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Acc[2] = 0.0f;
    p->Col[0] = 200;
    p->Col[1] = 200;
    p->Col[2] = 200;
    p->Col[3] = 0x80;
    p->Gravity = 0.0f;
    p->SizeX = 8.0f;                            // rdata 0x36e8c8
    p->SizeY = 8.0f;
    p->SizeAdd = 0.0f;
    p->Ang = 0.0f;
    p->AngVel = 0.0f;
    p->KickRate = 0.05f;                        // rdata 0x36e8cc
    p->Age = 0;
    p->Life = GetRandValIByF(300);
    p->Tex = -1;
    p->DrawMode = 1;
    p->Move = 1;
}

// Type 1: 32 grey puffs at once in a 4 x 4 square, freed when all are gone.
// FF3 0x00173450.

int EffParticleType1Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[0] = 4.0f;                     // rdata 0x36e8d0
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[2] = 4.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->AlphaReg = 0x44;
    ctrl->TestReg = 0x5000b;
    ctrl->Num = 0x20;
    ctrl->EmitRate = 0.0f;
    ctrl->Drag = 0.96f;                         // rdata 0x36e8d4
    ctrl->FloorY = 0.0f;
    ctrl->FreeWhenEmpty = 1;
    return 0x20;
}

// Spread around ctrl->Pos against the current player's last move, rising,
// growing and turning (effdat 0x12).
// FF3 0x001734c0.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType1Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    float a;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    a = ingame_wrk.plyr_wrk->move_last[0] * -EffRand01();             // rdata 0x36e8d8
    p->Vel[0] = a + EffRand01() - 0.5f;                                 // rdata 0x36e8dc
    p->Vel[1] = EffRand01() * -0.5f;                                    // rdata 0x36e8e4
    a = ingame_wrk.plyr_wrk->move_last[2] * -EffRand01();
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Gravity = 0.0f;
    p->Vel[2] = a + EffRand01() - 0.5f;
    a = EffRand01();
    p->SizeX = a + a + 1.0f;                    // rdata 0x36e8e0
    a = EffRand01();
    p->SizeY = a + a + 1.0f;
    p->SizeAdd = EffRand01() * 0.02f + 0.01f;   // rdata 0x36e8e8 / 0x36e8ec
    p->Ang = EffRand01() * 6.2831854f - 3.1415927f;     // rdata 0x36e8f0 / 0x36e8f4
    p->Col[3] = 0x20;
    p->Col[2] = 0x80;
    p->Col[0] = 0x80;
    p->Col[1] = 0x80;
    p->Age = 0;
    p->AngVel = EffRand01() * 1.5707964f - 0.7853982f;  // rdata 0x36e8f8 / 0x36e8fc
    p->Life = GetRandValIByF(0x28);
    p->DrawMode = 1;
    p->Tex = 0x12;
    p->Move = 1;
}

// Type 2: 16 bright puffs at once, freed when all are gone.
// FF3 0x00173760.

int EffParticleType2Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[3] = 0.0f;
    ctrl->EmitRate = 0.0f;
    ctrl->AlphaReg = 0x44;
    ctrl->Spread[2] = 1.5f;                     // rdata 0x36e900
    ctrl->Num = 0x10;
    ctrl->Spread[0] = 1.5f;
    ctrl->Kick[0] = 0.0f;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Spread[1] = 1.0f;                     // rdata 0x36e904
    ctrl->Kick[3] = 0.0f;
    ctrl->FloorY = 0.0f;
    ctrl->Drag = 0.96f;                         // rdata 0x36e908
    ctrl->FreeWhenEmpty = 1;
    ctrl->TestReg = 0x5000b;
    return 0x10;
}

// From the current player's listening point 1.5 ahead along the facing
// (z turned by rot y, then x), blown forward at 0.2 with a random spread.
// ctrl->Pos is not used.
// FF3 0x001737e0.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType2Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    sceVu0FVECTOR dir = { 0.0f, 0.0f, 1.0f, 0.0f };     // data 0x329460
    PLYR_WRK *plyr = ingame_wrk.plyr_wrk;
    int i;

    (void)ctrl;
    RotVectorY(plyr->rot[1], dir, dir);
    RotVectorX(plyr->rot[0], dir, dir);
    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = plyr->listen_pos[i] + off[i];
    for (i = 0; i < 3; i++)                     // vmulx.xyzw, vadd.xyz
        p->Pos[i] += dir[i] * 1.5f;
    for (i = 0; i < 4; i++)                     // vmulx.xyzw
        p->Vel[i] = dir[i] * 0.2f;              // rdata 0x36e90c
    p->Vel[0] += (EffRand01() - 0.5f) * 0.5f;   // rdata 0x36e910 / 0x36e914
    p->Vel[1] += (EffRand01() - 0.5f) * 0.2f;
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Gravity = 0.0f;
    p->Vel[2] += (EffRand01() - 0.5f) * 0.5f;
    p->SizeX = EffRand01() + 1.0f;              // rdata 0x36e918
    p->SizeY = EffRand01() + 1.0f;
    p->SizeAdd = EffRand01() * 0.01f + 0.01f;   // rdata 0x36e91c
    p->Ang = EffRand01() * 6.2831854f - 3.1415927f;     // rdata 0x36e920 / 0x36e924
    p->Col[3] = 0x18;
    p->Col[2] = 0xf0;
    p->Col[0] = 0xf0;
    p->Col[1] = 0xf0;
    p->Age = 0;
    p->AngVel = EffRand01() * 1.5707964f - 0.7853982f;  // rdata 0x36e928 / 0x36e92c
    p->Life = GetRandValIByF(0x28);
    p->DrawMode = 1;
    p->Tex = 0x12;
    p->Move = 1;
}

// Type 3: 22 splash drops at once in a 2 x 2 square, the floor 0.6 above
// the burst, freed when all are gone (the after-move EffParticleType3Floor
// leaves a mark where each lands).
// FF3 0x00173ab8.

int EffParticleType3Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[1] = 0.0f;
    ctrl->AlphaReg = 0x48;
    ctrl->Spread[2] = 2.0f;                     // rdata 0x36e930
    ctrl->Spread[0] = 2.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Num = 0x16;
    ctrl->EmitRate = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->FloorY = ctrl->Pos[1] + 0.600001f;    // rdata 0x36e934
    ctrl->Kick[1] = 0.0f;
    ctrl->Drag = 0.96f;                         // rdata 0x36e938
    ctrl->FreeWhenEmpty = 1;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->TestReg = 0x5000b;
    return 0x16;
}

// Started at floor height, thrown up and against the current player's last
// move (twice as high with flags2 bit 0), falling (gravity 0.098) and
// shrinking; reddish (effdat 0x3a).
// FF3 0x00173b40.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType3Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    PLYR_WRK *plyr;
    float up;
    float a;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Pos[1] = ctrl->FloorY;
    plyr = ingame_wrk.plyr_wrk;
    up = (plyr->flags2 & 1) != 0 ? 2.0f : 1.0f;                         // rdata 0x36e944 / 0x36e948
    a = plyr->move_last[0] * -EffRand01();
    p->Vel[0] = a + EffRand01() - 0.5f;                                 // rdata 0x36e940
    p->Vel[1] = EffRand01() * -up;
    plyr = ingame_wrk.plyr_wrk;
    a = plyr->move_last[2] * -EffRand01();
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Gravity = 0.098f;                        // rdata 0x36e94c
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Vel[2] = a + EffRand01() - 0.5f;
    p->Ang = 0.0f;
    p->Col[2] = 0x8b;
    p->SizeAdd = -0.01f;                        // rdata 0x36e950
    p->Col[0] = 200;
    p->AngVel = 0.0f;
    p->Col[1] = 0x7d;
    p->Age = 0;
    p->Col[3] = 10;
    a = EffRand01() * 0.05f + 0.1f;             // rdata 0x36e954 / 0x36e958
    p->SizeX = a;
    p->SizeY = a;
    p->Life = GetRandValIByF(0x28);
    p->DrawMode = 1;
    p->Tex = 0x3a;
    p->Move = 1;
}

// Type 4: three floor marks at once in a 2.2 square, the floor 0.6 above
// the burst, freed when all are gone.
// FF3 0x00173e38.

int EffParticleType4Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Spread[2] = 2.1999989f;               // rdata 0x36e968
    ctrl->TestReg = 0x5000b;
    ctrl->Spread[0] = 2.1999989f;
    ctrl->Num = 3;
    ctrl->EmitRate = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->FreeWhenEmpty = 1;
    ctrl->FloorY = ctrl->Pos[1] + 0.600001f;    // rdata 0x36e96c
    ctrl->Kick[1] = 0.0f;
    ctrl->AlphaReg = 0x48;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->Drag = 0.0f;
    return 3;
}

// A still grey-blue mark on the floor (effdat 0x5a), growing 0.1 a frame
// from 1.2 .. 1.8, for 65 .. 85 frames.
// FF3 0x00173eb0.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType4Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    float size;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Pos[1] = ctrl->FloorY;
    p->Vel[0] = 0.0f;
    p->Vel[1] = 0.0f;
    p->Vel[2] = 0.0f;
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Gravity = 0.0f;
    size = EffRand01() * 0.59999996f + 1.1999999f;     // rdata 0x36e978 / 0x36e97c
    p->Col[0] = 0x5a;
    p->Col[2] = 0x69;
    p->SizeAdd = 0.099998996f;                  // rdata 0x36e970
    p->Ang = 0.0f;
    p->AngVel = 0.0f;
    p->Col[1] = 0x5f;
    p->Age = 0;
    p->Col[3] = 10;
    p->SizeX = size;
    p->SizeY = size;
    p->Life = GetRandValIByF(0x15) + 0x41;
    p->Tex = 0x5a;
    p->DrawMode = 0;
    p->Move = 0;
}

// Type 5: 256 slots, 32 at once in a 0.4 square, then 0.92 new a frame,
// with random kicks; never freed when empty.
// FF3 0x00173fb8.

int EffParticleType5Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->Spread[2] = 0.4f;                     // rdata 0x36e980
    ctrl->TestReg = 0x5000b;
    ctrl->Spread[0] = 0.4f;
    ctrl->Kick[2] = 0.0015f;                    // rdata 0x36e984
    ctrl->Num = 0x100;
    ctrl->Kick[0] = 0.0015f;
    ctrl->FloorY = 0.0f;
    ctrl->FreeWhenEmpty = 0;
    ctrl->EmitRate = 0.92f;                     // rdata 0x36e988
    ctrl->Kick[1] = 0.0008f;                    // rdata 0x36e98c
    ctrl->Drag = 0.98f;                         // rdata 0x36e990
    ctrl->AlphaReg = 0x48;
    return 0x20;
}

// Blue motes (0x50, 0x50, 0xff) drifting up and shrinking (effdat 0x3c),
// up to 80 frames.
// FF3 0x00174048.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType5Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    float size;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Vel[0] = EffRand01() * 0.04f - 0.02f;    // rdata 0x36e994 / 0x36e998 / 0x36e99c
    p->Vel[1] = EffRand01() * 0.05f - 0.1f;     // rdata 0x36e9a4 / 0x36e9a0
    p->Acc[0] = 0.0f;
    p->Acc[2] = 0.0f;
    p->Acc[1] = -0.001f;                        // rdata 0x36e9a8
    p->Gravity = 0.0f;
    p->KickRate = 0.6f;                         // rdata 0x36e9ac
    p->Vel[2] = EffRand01() * 0.04f - 0.02f;
    size = EffRand01() * 0.4f + 0.1f;           // rdata 0x36e9b0
    p->SizeX = size;
    p->SizeY = size;
    p->Col[2] = 0xff;
    p->Col[1] = 0x50;
    p->Col[3] = 0x40;
    p->Col[0] = 0x50;
    p->Ang = 0.0f;
    p->AngVel = 0.0f;
    p->Age = 0;
    p->SizeAdd = EffRand01() * 0.02f - 0.01f;   // rdata 0x36e9b4
    p->Life = GetRandValIByF(0x50);
    p->DrawMode = 1;
    p->Tex = 0x3c;
    p->Move = 1;
}

// Type 6: 256 slots, none at once, 0.92 new a frame with random kicks,
// ALPHA 0x44; never freed when empty.
// FF3 0x00174220.

int EffParticleType6Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->Spread[2] = 0.4f;                     // rdata 0x36e9b8
    ctrl->TestReg = 0x5000b;
    ctrl->Spread[0] = 0.4f;
    ctrl->Kick[2] = 0.001f;                     // rdata 0x36e9bc
    ctrl->Num = 0x100;
    ctrl->Kick[0] = 0.001f;
    ctrl->FloorY = 0.0f;
    ctrl->FreeWhenEmpty = 0;
    ctrl->EmitRate = 0.92f;                     // rdata 0x36e9c0
    ctrl->Kick[1] = 0.0008f;                    // rdata 0x36e9c4
    ctrl->Drag = 0.98f;                         // rdata 0x36e9c8
    ctrl->AlphaReg = 0x44;
    return 0;
}

// Faint white motes drifting up (effdat 0x12), up to 80 frames.
// FF3 0x001742b0.
// callcheck: rand (EffRand01: ReiPan_Rand)

void EffParticleType6Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    float size;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Vel[0] = EffRand01() * 0.02f - 0.01f;    // rdata 0x36e9cc / 0x36e9d0 / 0x36e9d4
    p->Vel[1] = EffRand01() * 0.06f - 0.1f;     // rdata 0x36e9dc / 0x36e9d8
    p->Acc[0] = 0.0f;
    p->Acc[2] = 0.0f;
    p->Acc[1] = -0.001f;                        // rdata 0x36e9e0
    p->Gravity = 0.0f;
    p->KickRate = 0.6f;                         // rdata 0x36e9e4
    p->Vel[2] = EffRand01() * 0.02f - 0.01f;
    size = EffRand01() * 0.2f + 0.1f;           // rdata 0x36e9e8
    p->SizeX = size;
    p->SizeY = size;
    p->Col[2] = 0xff;
    p->Col[3] = 0xc;
    p->Col[0] = 0xff;
    p->Col[1] = 0xff;
    p->Ang = 0.0f;
    p->Age = 0;
    p->AngVel = 0.0f;
    p->SizeAdd = EffRand01() * 0.02f - 0.01f;
    p->Life = GetRandValIByF(0x50);
    p->DrawMode = 1;
    p->Tex = 0x12;
    p->Move = 1;
}

// Type 7: 10 .. 19 sparks at once in a 1 x 1 square, freed when all are
// gone.
// FF3 0x00174478.

int EffParticleType7Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->AlphaReg = 0x48;
    ctrl->Spread[2] = 1.0f;                     // rdata 0x36e9ec
    ctrl->EmitRate = 0.0f;
    ctrl->Spread[0] = 1.0f;
    ctrl->Drag = 0.96f;                         // rdata 0x36e9f0
    ctrl->FreeWhenEmpty = 1;
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->TestReg = 0x5000b;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->FloorY = 0.0f;
    ctrl->Num = GetRandValIByF(10) + 10;
    return ctrl->Num;
}

// Sparks thrown out sideways and up, falling (gravity 0.098) and
// shrinking, in EffSparkColGet's colour at half its alpha (effdat 0x3a),
// 20 .. 30 frames. size: 2 for type 7, 4 for type 8 (the ROM has two
// copies of this body).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffParticleSparkInit(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off, float size)
{
    int *col;
    float a;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Vel[0] = (EffRand01() - 0.5f) * 0.8f;    // rdata 0x36e9f8 / 0x36e9fc (type 8: 0x36ea1c / 0x36ea20)
    p->Vel[1] = -EffRand01() * 0.8f;
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Gravity = 0.098f;                        // rdata 0x36ea00 (0x36ea24)
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Vel[2] = (EffRand01() - 0.5f) * 0.8f;
    p->Ang = 0.0f;
    p->AngVel = 0.0f;
    p->SizeAdd = -0.001f;                       // rdata 0x36ea04 (0x36ea28)
    a = (EffRand01() * 0.02f + 0.1f) * size;    // rdata 0x36ea08 / 0x36ea0c (0x36ea2c / 0x36ea30 / 0x36ea34)
    p->SizeX = a;
    p->SizeY = a;
    col = EffSparkColGet();
    p->Col[0] = col[0];
    p->Col[1] = col[1];
    p->Col[2] = col[2];
    p->Age = 0;
    p->Col[3] = col[3] / 2;
    p->Life = GetRandValIByF(10) + 0x14;
    p->DrawMode = 1;
    p->Tex = 0x3a;
    p->Move = 1;
}

// FF3 0x00174508 (a + a for the size).
// callcheck: rand, GetRandValIByF, EffSparkColGet (through EffParticleSparkInit)

void EffParticleType7Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    EffParticleSparkInit(p, ctrl, off, 2.0f);
}

// Type 8: type 7's setup, its own copy in the ROM.
// FF3 0x001746b8.

int EffParticleType8Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->AlphaReg = 0x48;
    ctrl->Spread[2] = 1.0f;                     // rdata 0x36ea10
    ctrl->EmitRate = 0.0f;
    ctrl->Spread[0] = 1.0f;
    ctrl->Drag = 0.96f;                         // rdata 0x36ea14
    ctrl->FreeWhenEmpty = 1;
    ctrl->Spread[1] = 0.0f;
    ctrl->Spread[3] = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->TestReg = 0x5000b;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->FloorY = 0.0f;
    ctrl->Num = GetRandValIByF(10) + 10;
    return ctrl->Num;
}

// FF3 0x00174748 (times 4.0, rdata 0x36ea34).
// callcheck: rand, GetRandValIByF, EffSparkColGet (through EffParticleSparkInit)

void EffParticleType8Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    EffParticleSparkInit(p, ctrl, off, 4.0f);
}

// Type 9: 3 or 4 marks at once in a 0.8 square, freed when all are gone.
// FF3 0x00174908.

int EffParticleType9Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->TestReg = 0x5000b;
    ctrl->Spread[2] = 0.8f;                     // rdata 0x36ea38
    ctrl->FreeWhenEmpty = 1;
    ctrl->Spread[0] = 0.8f;
    ctrl->Spread[1] = 0.0f;
    ctrl->AlphaReg = 0x48;
    ctrl->Spread[3] = 0.0f;
    ctrl->EmitRate = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->Drag = 0.0f;
    ctrl->FloorY = 0.0f;
    ctrl->Num = GetRandValIByF(1) + 3;
    return ctrl->Num;
}

// A still grey mark (effdat 0x5a) growing 0.05 a frame, 80 .. 100 frames.
// size: 2 for type 9, 4 for type 10 (two copies in the ROM).
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffParticleMarkInit(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off, float size)
{
    float a;
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] = ctrl->Pos[i] + off[i];
    p->Vel[0] = 0.0f;
    p->Vel[1] = 0.0f;
    p->Vel[2] = 0.0f;
    p->Acc[0] = 0.0f;
    p->Acc[1] = 0.0f;
    p->Acc[2] = 0.0f;
    p->KickRate = 0.0f;
    p->Gravity = 0.0f;
    a = EffRand01();
    p->Ang = 0.0f;
    p->Col[2] = 100;
    p->Col[0] = 100;
    p->Col[1] = 100;
    p->AngVel = 0.0f;
    p->Age = 0;
    p->SizeAdd = 0.05f;                         // rdata 0x36ea40 (0x36ea54)
    p->Col[3] = 10;
    a = (a * 0.2f + 0.7f) * size;               // rdata 0x36ea44 / 0x36ea48 (0x36ea58 / 0x36ea5c / 0x36ea60)
    p->SizeX = a;
    p->SizeY = a;
    p->Life = GetRandValIByF(0x14) + 0x50;
    p->DrawMode = 0;
    p->Tex = 0x5a;
    p->Move = 0;
}

// FF3 0x00174990 (a + a for the size).
// callcheck: rand, GetRandValIByF (through EffParticleMarkInit)

void EffParticleType9Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    EffParticleMarkInit(p, ctrl, off, 2.0f);
}

// Type 10: type 9's setup, its own copy in the ROM.
// FF3 0x00174a88.

int EffParticleType10Setup(EFF_PARTICLE_CTRL *ctrl)
{
    ctrl->TestReg = 0x5000b;
    ctrl->Spread[2] = 0.8f;                     // rdata 0x36ea4c
    ctrl->FreeWhenEmpty = 1;
    ctrl->Spread[0] = 0.8f;
    ctrl->Spread[1] = 0.0f;
    ctrl->AlphaReg = 0x48;
    ctrl->Spread[3] = 0.0f;
    ctrl->EmitRate = 0.0f;
    ctrl->Kick[0] = 0.0f;
    ctrl->Kick[1] = 0.0f;
    ctrl->Kick[2] = 0.0f;
    ctrl->Kick[3] = 0.0f;
    ctrl->Drag = 0.0f;
    ctrl->FloorY = 0.0f;
    ctrl->Num = GetRandValIByF(1) + 3;
    return ctrl->Num;
}

// FF3 0x00174b10 (times 4.0, rdata 0x36ea60).
// callcheck: rand, GetRandValIByF (through EffParticleMarkInit)

void EffParticleType10Init(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl, float *off)
{
    EffParticleMarkInit(p, ctrl, off, 4.0f);
}

// Per type: the setup (fills the record, returns how many particles start
// at once) and the particle init (gets the random start offset).
static int (*const eff_particle_setup[EFF_PARTICLE_TYPE_NUM])(EFF_PARTICLE_CTRL *) = {   // data 0x353c38
    EffParticleType0Setup, EffParticleType1Setup, EffParticleType2Setup, EffParticleType3Setup,
    EffParticleType4Setup, EffParticleType5Setup, EffParticleType6Setup, EffParticleType7Setup,
    EffParticleType8Setup, EffParticleType9Setup, EffParticleType10Setup,
};
static void (*const eff_particle_init[EFF_PARTICLE_TYPE_NUM])(EFF_PARTICLE *, EFF_PARTICLE_CTRL *, float *) = {    // data 0x353c68
    EffParticleType0Init, EffParticleType1Init, EffParticleType2Init, EffParticleType3Init,
    EffParticleType4Init, EffParticleType5Init, EffParticleType6Init, EffParticleType7Init,
    EffParticleType8Init, EffParticleType9Init, EffParticleType10Init,
};

// Type 3's after-move, for a particle below the record's FloorY: put on the
// floor and stopped as a grey-blue mark (effdat 0x5a, 16 frames) growing
// 0.1 a frame from 0.2 .. 0.3.
// FF3 0x00173d78.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffParticleType3Floor(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl)
{
    float size;

    if (ctrl->FloorY < p->Pos[1])
        p->Pos[1] = ctrl->FloorY;
    size = EffRand01() * 0.1f + 0.2f;           // rdata 0x36e960 / 0x36e95c / 0x36e964
    p->DrawMode = 0;
    p->Col[2] = 0x69;
    p->SizeAdd = 0.1f;                          // rdata 0x36e95c
    p->Age = 0;
    p->Tex = 0x5a;
    p->Col[1] = 0x5f;
    p->Life = 0x10;
    p->Col[0] = 0x5a;
    p->Col[3] = 10;
    p->Move = 0;
    p->SizeX = size;
    p->SizeY = size;
}

// A particle from scratch: cleared, a random offset of +-Spread / 2 per
// axis, the type's init, alive.
// FF3 0x00172988.
// callcheck: rand, memset (rand: EffRand01, ReiPan_Rand)

static void EffParticleInit(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl)
{
    sceVu0FVECTOR off;
    int i;

    memset(p, 0, sizeof(*p));
    for (i = 0; i < 3; i++)
        off[i] = ctrl->Spread[i] * (EffRand01() - 0.5f);    // rdata 0x36e880 / 0x36e884
    off[3] = 0.0f;
    if (ctrl->Type < EFF_PARTICLE_TYPE_NUM)
        eff_particle_init[ctrl->Type](p, ctrl, off);
    p->Alive = 1;
}

// The first dead particle starts again; 1 when there was one.
// FF3 0x00172ad8.

static int EffParticleEmit(EFF_PARTICLE_CTRL *ctrl)
{
    EFF_PARTICLE *p = nullptr;
    int i;

    for (i = 0; i < ctrl->Num; i++)
    {
        if (ctrl->pPart[i].Alive == 0)
        {
            p = &ctrl->pPart[i];
            break;
        }
    }
    if (p != nullptr)
        EffParticleInit(p, ctrl);
    return p != nullptr;
}

// One frame of a live particle. Moving: a random kick of +-Kick / 2 per
// axis on Acc (chance KickRate), drag on Acc and Vel (xyz) unless 1.0,
// gravity, Pos += Vel, the spin wrapped to +-pi. Always: both sizes grow by
// SizeAdd, Age counts up to Life.
// FF3 0x00172b40.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffParticleStep(EFF_PARTICLE *p, EFF_PARTICLE_CTRL *ctrl)
{
    float a;
    int i;

    if (p->Alive == 0)
        return;

    if (p->Move != 0)
    {
        if (0.0f < p->KickRate && EffRand01() < p->KickRate)       // rdata 0x36e888
        {
            for (i = 0; i < 3; i++)
                p->Acc[i] += EffRand01() * ctrl->Kick[i] - ctrl->Kick[i] * 0.5f;   // rdata 0x36e88c
        }
        if (ctrl->Drag != 1.0f)                             // rdata 0x36e890
        {
            for (i = 0; i < 3; i++)                         // vmulx.xyz
                p->Acc[i] *= ctrl->Drag;
        }
        for (i = 0; i < 4; i++)                             // vadd.xyzw
            p->Vel[i] += p->Acc[i];
        if (ctrl->Drag != 1.0f)
        {
            for (i = 0; i < 3; i++)
                p->Vel[i] *= ctrl->Drag;
        }
        p->Vel[1] += p->Gravity;
        for (i = 0; i < 4; i++)
            p->Pos[i] += p->Vel[i];
        p->Pos[3] = 1.0f;
        if (p->AngVel != 0.0f)
        {
            a = p->Ang + p->AngVel;
            p->Ang = a;
            if (3.1415927f < a)                             // rdata 0x36e894
                p->Ang = a - 6.2831855f;                    // rdata 0x36e898
            else if (a < -3.1415927f)                       // rdata 0x36e89c
                p->Ang = a + 6.2831855f;
        }
    }

    p->SizeX += p->SizeAdd;
    p->SizeY += p->SizeAdd;
    p->Age++;
    if (p->Life <= p->Age)
        p->Alive = 0;
}

// ---- list C particle draws (out of line, 0x2b4968..0x2b5900) --------------

// local -> screen of a particle: the unit matrix turned by rot (x, y, then
// z; each angle normalised twice, as the ROM does), moved to pos
// (vadd.xyz), under the camera's world -> screen matrix.
// PC: shared by the four draws, which build it inline.

static void EffParticleScreenMatrix(sceVu0FMATRIX slm, GRA3DCAMERA *pCam, const float *rot, const float *pos)
{
    sceVu0FMATRIX m;

    sceVu0UnitMatrix(m);                // the ROM rotates the unit matrix at data 0x3293c0
    sceVu0RotMatrixX(m, m, NormalizeAngle(NormalizeAngle(rot[0])));
    sceVu0RotMatrixY(m, m, NormalizeAngle(NormalizeAngle(rot[1])));
    sceVu0RotMatrixZ(m, m, NormalizeAngle(NormalizeAngle(rot[2])));
    m[3][0] += pos[0];
    m[3][1] += pos[1];
    m[3][2] += pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, m);
}

// 1 when a projected vertex is outside the guard band or the depth range.
static inline int EffParticleOffScreen(const sceVu0IVECTOR v)
{
    return (u_int)(v[0] - 0x4000) > 0x8000 || (u_int)(v[1] - 0x4000) > 0x8000
        || (u_int)(v[2] - 1) > 0xfffffe;
}

// The particle's alpha: Col[3], faded by the life left (Life 0: never).
static inline u_int EffParticleAlpha(const EFF_PARTICLE *p, u_int a)
{
    if (p->Life != 0)
        a = (u_int)(((int)a * (p->Life - p->Age)) / p->Life) & 0xff;
    return a;
}

// DrawMode != 0: a SPRITE (PRIM 0x156) of effdat[Tex] between the
// projected corners (-SizeX, -SizeY) and (SizeX, SizeY) of a camera-facing
// frame, flat colour Col, alpha faded by age. GetCamFaceRot's z turn is
// used; the ROM stores Ang after it, where nothing reads it.
// FF3 0x002b4968.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)
// callcheck: NormalizeAngle, sceVu0RotMatrixX, sceVu0RotMatrixY, sceVu0RotMatrixZ (in EffParticleScreenMatrix)

static void EffParticleDrawSprite(EFF_PARTICLE *p)
{
    sceVu0FVECTOR vtx[2] = {            // the ROM's stack table holds two more it never projects
        { -p->SizeX, -p->SizeY, 0.0f, 1.0f },
        { p->SizeX, p->SizeY, 0.0f, 1.0f },                     // w: rdata 0x36eac0
    };
    sceVu0IVECTOR iv[2];
    sceVu0FMATRIX slm;
    GRA3DCAMERA *pCam;
    float rot[4];
    u_int a, w, h;
    u_long *pk;
    u_int *q;
    int n, i;

    pCam = gra3dGetCamera();
    n = p->Tex;
    a = (u_char)p->Col[3];
    GetCamFaceRot(rot, p->Pos);
    rot[3] = p->Ang;
    EffParticleScreenMatrix(slm, pCam, rot, p->Pos);
    for (i = 0; i < 2; i++)
    {
        sceVu0RotTransPers(iv[i], slm, vtx[i], 0);
        if (EffParticleOffScreen(iv[i]))
            return;
    }
    a = EffParticleAlpha(p, a);

    w = effdat[n].w;
    h = effdat[n].h;
    pk = (u_long *)StartDmaDirectTrans();
    pk[2] = 0;
    pk[1] = 0x43431ee;                  // A+D, A+D, RGBAQ, UV, XYZF2, UV, XYZF2
    pk[0] = 0x70ab400000008001ULL;      // rdata 0x36eab8: 1 loop, EOP, PRIM 0x156 (sprite, textured, blended, UV), 7 regs
    pk[3] = SCE_GS_TEXFLUSH;
    pk[5] = SCE_GS_TEX0_1;
    pk[4] = effdat[n].tex0;
    q = (u_int *)(pk + 6);
    q[0] = (u_int)p->Col[0];
    q[1] = (u_int)p->Col[1];
    q[2] = (u_int)p->Col[2];
    q[3] = a;
    q += 4;
    for (i = 0; i < 2; i++)
    {
        q[2] = 0;
        q[3] = 0;
        q[1] = (i & 1) ? h << 4 : 0;
        q[0] = (i & 1) ? w << 4 : 0;
        q[4] = (u_int)iv[i][0];
        q[5] = (u_int)iv[i][1];
        q[6] = (u_int)iv[i][2];
        q[7] = (0 < i) ? 0 : 0x8000;
        q += 8;
    }
    EndDmaDirectTrans((qword *)(pk + 0x10));
}

// DrawMode 0: a quad of effdat[Tex] laid flat (x turned by pi / 2, spun by
// Ang about y), a 4-vertex strip (PRIM 0x154), flat colour Col, alpha
// faded by age. The camera-facing branch can not run: its only caller
// draws DrawMode 0 here.
// FF3 0x002b4cf8.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)
// callcheck: NormalizeAngle, sceVu0RotMatrixX, sceVu0RotMatrixY, sceVu0RotMatrixZ (in EffParticleScreenMatrix)

static void EffParticleDrawQuad(EFF_PARTICLE *p)
{
    sceVu0FVECTOR vtx[4] = {            // the ROM's stack table holds four more it never projects
        { -p->SizeX, -p->SizeY, 0.0f, 1.0f },
        { p->SizeX, -p->SizeY, 0.0f, 1.0f },
        { -p->SizeX, p->SizeY, 0.0f, 1.0f },
        { p->SizeX, p->SizeY, 0.0f, 1.0f },                     // w: rdata 0x36ead8
    };
    sceVu0IVECTOR iv[4];
    sceVu0FMATRIX slm;
    GRA3DCAMERA *pCam;
    float rot[4];
    u_int a, w, h;
    u_long *pk;
    u_int *q;
    int n, i;

    pCam = gra3dGetCamera();
    n = p->Tex;
    a = (u_char)p->Col[3];
    if (p->DrawMode == 0)
    {
        rot[0] = 1.5707964f;            // rdata 0x36eadc
        rot[1] = p->Ang;
        rot[2] = 0.0f;                  // data 0x329538
        rot[3] = 0.0f;
    }
    else
    {
        GetCamFaceRot(rot, p->Pos);
        rot[3] = p->Ang;
    }
    EffParticleScreenMatrix(slm, pCam, rot, p->Pos);
    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(iv[i], slm, vtx[i], 0);
        if (EffParticleOffScreen(iv[i]))
            return;
    }
    a = EffParticleAlpha(p, a);

    w = effdat[n].w;
    h = effdat[n].h;
    pk = (u_long *)StartDmaDirectTrans();
    pk[2] = 0;
    pk[0] = 0xb0aa400000008001ULL;      // rdata 0x36eac8: 1 loop, EOP, PRIM 0x154 (strip, textured, blended, UV), 11 regs
    pk[1] = 0x00000434343431eeULL;      // A+D, A+D, RGBAQ, then UV / XYZF2 four times
    pk[3] = SCE_GS_TEXFLUSH;
    pk[5] = SCE_GS_TEX0_1;
    pk[4] = effdat[n].tex0;
    q = (u_int *)(pk + 6);
    q[0] = (u_int)p->Col[0];
    q[1] = (u_int)p->Col[1];
    q[2] = (u_int)p->Col[2];
    q[3] = a;
    q += 4;
    for (i = 0; i < 4; i++)
    {
        q[2] = 0;
        q[3] = 0;
        q[0] = (i & 1) ? w << 4 : 0;
        q[1] = (i / 2 != 0) ? h << 4 : 0;
        q[4] = (u_int)iv[i][0];
        q[5] = (u_int)iv[i][1];
        q[6] = (u_int)iv[i][2];
        q[7] = (1 < i) ? 0 : 0x8000;
        q += 8;
    }
    EndDmaDirectTrans((qword *)(pk + 0x18));
}

// Tex -1: a heat-haze ripple. A camera-facing cross (centre and the four
// arm ends at +-SizeX, +-SizeY) is projected; the fan (PRIM 0x15d) samples
// the half-size frame copy (TBP 0x2bc0, 512 x 256, the room glow's work
// frame) at the points' own screen positions, the arm ends jittered by
// rand, so the frame under the particle wobbles. Only the centre carries
// alpha. The whole cross must be inside the 640 x 448 screen. The ROM's
// sign flips test vertex 3 among the even arms and vertex 4 among the odd
// ones, so neither fires.
// FF3 0x002b50f0.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)
// callcheck: rand (EffRand01: ReiPan_Rand)
// callcheck: NormalizeAngle, sceVu0RotMatrixX, sceVu0RotMatrixY, sceVu0RotMatrixZ (in EffParticleScreenMatrix)

static void EffParticleDrawDistort(EFF_PARTICLE *p)
{
    sceVu0FVECTOR vtx[5] = {            // the ROM's stack table holds five more it never projects
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { -p->SizeX, 0.0f, 0.0f, 1.0f },
        { 0.0f, -p->SizeY, 0.0f, 1.0f },
        { p->SizeX, 0.0f, 0.0f, 1.0f },
        { 0.0f, p->SizeY, 0.0f, 1.0f },                         // w: rdata 0x36eae8
    };
    sceVu0IVECTOR iv[5];
    sceVu0FMATRIX slm;
    GRA3DCAMERA *pCam;
    float rot[4];
    u_int a;
    int du, dv, u, v;
    int k, idx, i;
    u_long *pk;
    u_int *q;

    pCam = gra3dGetCamera();
    a = (u_char)p->Col[3];
    GetCamFaceRot(rot, p->Pos);
    EffParticleScreenMatrix(slm, pCam, rot, p->Pos);
    for (i = 0; i < 5; i++)
    {
        sceVu0RotTransPers(iv[i], slm, vtx[i], 0);
        if ((u_int)(iv[i][0] - 0x6c01) > 0x27fe)    // x 1728 .. 2368 (GS pixels)
            return;
        if ((u_int)(iv[i][1] - 0x7201) > 0x1bfe)    // y 1824 .. 2272
            return;
        if ((u_int)(iv[i][2] - 1) > 0xfffffe)
            return;
    }
    a = EffParticleAlpha(p, a);

    pk = (u_long *)StartDmaDirectTrans();
    pk[2] = 0;
    pk[1] = SCE_GIF_PACKED_AD;
    pk[0] = SCE_GIF_SET_TAG(2, 0, 0, 0, SCE_GIF_PACKED, 1);    // 0x1000000000000002 (no EOP)
    pk[3] = SCE_GS_TEXFLUSH;
    pk[4] = 0x2000000224016bc0ULL;      // rdata 0x36eae0: TBP 0x2bc0, TBW 5, PSMCT32, 512 x 256
    pk[5] = SCE_GS_TEX0_1;
    pk[6] = 0x30aec00000008006ULL;      // 6 loops, EOP, PRIM 0x15d (fan, textured, blended, UV), 3 regs
    pk[7] = 0x431;                      // RGBAQ, UV, XYZF2
    q = (u_int *)(pk + 8);
    for (k = 0; k < 6; k++)
    {
        idx = (k == 5) ? 1 : k;         // the fan closes on the first arm
        du = 0;
        dv = 0;
        if (k != 0)
        {
            if ((k & 1) == 0)
            {
                du = (int)((EffRand01() * 2.0f + 0.5f) * 16.0f);            // rdata 0x36eaf0 / 0x36eaf4
                if (k == 3)
                    du = -du;
                dv = (int)((EffRand01() * 4.0f - 2.0f + 0.5f) * 16.0f);     // rdata 0x36eaf8 / 0x36eafc
            }
            else
            {
                du = (int)((EffRand01() * 4.0f - 2.0f + 0.5f) * 16.0f);
                dv = (int)((EffRand01() * 2.0f + 0.5f) * 16.0f);
                if (k == 4)
                    dv = -dv;
            }
        }
        u = ((iv[idx][0] >> 4) - 0x6c0) * 8 + du;  // screen pixel / 2, in 1/16
        if (u < 1)
            u = 1;
        v = ((iv[idx][1] >> 4) - 0x720) * 8 + dv;
        if (v < 1)
            v = 1;
        q[0] = (u_int)p->Col[0];
        q[1] = (u_int)p->Col[1];
        q[2] = (u_int)p->Col[2];
        q[3] = (0 < k) ? 0 : a;
        q[4] = (u_int)u;
        q[5] = (u_int)v;
        q[6] = 0;
        q[7] = 0;
        q[8] = (u_int)iv[idx][0];
        q[9] = (u_int)iv[idx][1];
        q[10] = (u_int)iv[idx][2];
        q[11] = (u_int)(k < 2) << 15;
        q += 12;
    }
    EndDmaDirectTrans((qword *)(pk + 0x2c));
}

// Tex -2: a streak. One Gouraud line (PRIM 0x49) from the particle back
// along its velocity (Pos to Pos - Vel), colour Col, alpha faded by age at
// both ends.
// FF3 0x002b5670.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

static void EffParticleDrawStreak(EFF_PARTICLE *p)
{
    sceVu0FVECTOR vtx[2];
    sceVu0IVECTOR iv[2];
    sceVu0FMATRIX m, slm;
    GRA3DCAMERA *pCam;
    u_int a;
    u_long *pk;
    u_int *q;
    int i;

    memset(vtx, 0, sizeof(vtx));
    vtx[0][3] = 1.0f;                   // rdata 0x36eb00
    vtx[1][3] = 1.0f;
    pCam = gra3dGetCamera();
    a = (u_char)p->Col[3];
    for (i = 0; i < 4; i++)             // vsub.xyzw
        vtx[1][i] -= p->Vel[i];
    sceVu0UnitMatrix(m);                // the ROM copies the unit matrix at data 0x3293c0
    m[3][0] += p->Pos[0];
    m[3][1] += p->Pos[1];
    m[3][2] += p->Pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, m);
    for (i = 0; i < 2; i++)
    {
        sceVu0RotTransPers(iv[i], slm, vtx[i], 0);
        if (EffParticleOffScreen(iv[i]))
            return;
    }
    a = EffParticleAlpha(p, a);

    pk = (u_long *)StartDmaDirectTrans();
    pk[0] = 0x2024c00000008002ULL;      // 2 loops, EOP, PRIM 0x49 (line, Gouraud, blended), 2 regs
    pk[1] = 0x41;                       // RGBAQ, XYZF2
    q = (u_int *)(pk + 2);
    for (i = 0; i < 2; i++)
    {
        q[0] = (u_int)p->Col[0];
        q[1] = (u_int)p->Col[1];
        q[2] = (u_int)p->Col[2];
        q[3] = a;
        q[4] = (u_int)iv[i][0];
        q[5] = (u_int)iv[i][1];
        q[6] = (u_int)iv[i][2];
        q[7] = (i != 0) ? 0 : 0x8000;
        q += 8;
    }
    EndDmaDirectTrans((qword *)(pk + 10));
}

// One burst per frame: maybe a new particle (EmitRate), unless effects are
// stopped every particle moves (type 3 also runs FUN_00173d78 on those
// below FloorY; the ROM builds this one-entry table on the stack), then
// the GS state from the record and each live particle's draw. Returns how
// many are alive.
// FF3 0x00172da8.
// callcheck: rand, memset (rand: EffRand01, ReiPan_Rand; memset: the stack table)

static int EffParticleCtrlExec(EFF_PARTICLE_CTRL *ctrl)
{
    static void (*const after_move[EFF_PARTICLE_TYPE_NUM])(EFF_PARTICLE *, EFF_PARTICLE_CTRL *) = {
        nullptr, nullptr, nullptr, EffParticleType3Floor,
    };
    EFF_PARTICLE *p;
    GS_REG_SET regs[5];
    int has_after;
    int n = 0;
    int i;

    if (0.0f < ctrl->EmitRate && EffRand01() < ctrl->EmitRate)     // rdata 0x36e8a0
        EffParticleEmit(ctrl);

    if (eff_wrk.stop_flg == 0)
    {
        has_after = ctrl->Type < EFF_PARTICLE_TYPE_NUM && after_move[ctrl->Type] != nullptr;
        for (i = 0; i < ctrl->Num; i++)
        {
            p = &ctrl->pPart[i];
            EffParticleStep(p, ctrl);
            if (has_after && ctrl->FloorY < p->Pos[1])
                after_move[ctrl->Type](p, ctrl);
        }
    }

    regs[0].data = ctrl->AlphaReg;  regs[0].reg = SCE_GS_ALPHA_1;
    regs[1].data = 0x161;           regs[1].reg = SCE_GS_TEX1_1;
    regs[2].data = 0;               regs[2].reg = SCE_GS_CLAMP_1;
    regs[3].data = ctrl->TestReg;   regs[3].reg = SCE_GS_TEST_1;
    regs[4].data = 0x10a000118ULL;  regs[4].reg = SCE_GS_ZBUF_1;
    SetGsRegsCached(regs, 5, 1);

    for (i = 0; i < ctrl->Num; i++)
    {
        p = &ctrl->pPart[i];
        if (p->Alive == 0)
            continue;
        if (p->Tex < 0)
        {
            if (p->Tex == -2)
                EffParticleDrawStreak(p);
            else if (p->Tex == -1)
                EffParticleDrawDistort(p);
        }
        else if (p->DrawMode == 0)
        {
            EffParticleDrawQuad(p);
        }
        else
        {
            EffParticleDrawSprite(p);
        }
        n++;
    }
    return n;
}

// A burst of type at pos into ctrl: the type's setup, Num particles, the
// setup's starting count emitted. 1 when made (Valid).
// FF3 0x00172820.
// callcheck: memset, EFFECT_MALLOC (EFFECT_MALLOC read as a macro)

static int EffParticleCtrlInit(EFF_PARTICLE_CTRL *ctrl, float *pos, u_int type)
{
    int n;
    int i;

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->Valid = 0;
    if (type >= EFF_PARTICLE_TYPE_NUM)
        return 0;

    ctrl->Type = type;
    for (i = 0; i < 4; i++)
        ctrl->Pos[i] = pos[i];
    n = eff_particle_setup[type](ctrl);
    if (ctrl->Num <= 0)
        return 0;
    ctrl->pPart = (EFF_PARTICLE *)EFFECT_MALLOC(ctrl->Num << 7);
    if (ctrl->pPart == nullptr)
        return 0;
    for (i = 0; i < ctrl->Num; i++)
        memset(&ctrl->pPart[i], 0, sizeof(EFF_PARTICLE));
    for (; n > 0; n--)
        EffParticleEmit(ctrl);
    ctrl->Valid = 1;
    return 1;
}

// A burst of type (0..10) at pos, Id id, appended to list C. 0 only when
// the setup failed; a failed append frees it and still answers 1.
// FF3 0x00172ff8.
// callcheck: EFFECT_FREE, memset (EFFECT_FREE read as a macro)

int EffParticleReq(u_int type, float *pos, int id)
{
    EFF_PARTICLE_CTRL ctrl;

    if (EffParticleCtrlInit(&ctrl, pos, type) == 0)
        return 0;
    ctrl.Id = id;
    if (SingleLinkListAddEnd(&EffKind1DListC, &ctrl) == nullptr)
    {
        if (ctrl.pPart != nullptr)
            EFFECT_FREE(ctrl.pPart);
        memset(&ctrl, 0, sizeof(ctrl));
    }
    return 1;
}

// Frees the bursts matching type and id (negative: any).
// FF3 0x00173078.
// callcheck: EFFECT_FREE, memset (EFFECT_FREE read as a macro)

void EffParticleCut(int type, int id)
{
    SLL_CELL *pCell, *pNext;
    EFF_PARTICLE_CTRL *ctrl;

    for (pCell = SingleLinkListBeginCell(&EffKind1DListC); pCell != nullptr; pCell = pNext)
    {
        pNext = pCell->pNext;
        ctrl = (EFF_PARTICLE_CTRL *)SingleLinkListCellBodyPtr(pCell);
        if (id >= 0 && ctrl->Id != id)
            continue;
        if (type < 0 || (int)ctrl->Type == type)
        {
            if (ctrl->pPart != nullptr)
                EFFECT_FREE(ctrl->pPart);
            memset(ctrl, 0, sizeof(*ctrl));
            SingleLinkListRemove(&EffKind1DListC, pCell);
        }
    }
}

// EffectInPhoto: copies the shown frame to GS 0x2bc0 (the particle draws
// sample it), runs each burst; a burst with FreeWhenEmpty and nothing
// alive is freed.
// FF3 0x00173160.
// callcheck: GetSysWrk, EFFECT_FREE, memset (GetSysWrk: FF3 reads the shared inline cache sdata gp-0xc60 first; EFFECT_FREE read as a macro)

void EffParticleExec(void)
{
    SLL_CELL *pCell, *pNext;
    EFF_PARTICLE_CTRL *ctrl;

    LocalCopyLtoL(1, (int)(GetSysWrk()->count & 1) * 0x1180, 0x2bc0);
    for (pCell = SingleLinkListBeginCell(&EffKind1DListC); pCell != nullptr; pCell = pNext)
    {
        pNext = pCell->pNext;
        ctrl = (EFF_PARTICLE_CTRL *)SingleLinkListCellBodyPtr(pCell);
        if (EffParticleCtrlExec(ctrl) == 0 && ctrl->FreeWhenEmpty != 0)
        {
            if (ctrl->pPart != nullptr)
                EFFECT_FREE(ctrl->pPart);
            memset(ctrl, 0, sizeof(*ctrl));
            SingleLinkListRemove(&EffKind1DListC, pCell);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The rain (FF3-only). Nothing calls EffRainReq, so EffRainExec never
// finds drops in the retail game.
// ──────────────────────────────────────────────────────────────────────

static EFF_RAIN_WRK eff_rain;                   // bss 0x3d45b0

// The roof the drops stop on, relative to the origin: [0] max, [1] min of
// an outer box and of the inner box cut out of it (data 0x3545f0 / 0x354610).
static const float rain_roof_outer[2][4] = {
    { 180.32686f, -29.132084f, 181.14174f, 0.0f },
    { -56.017456f, -198.3399f, -55.202557f, 0.0f },
};
static const float rain_roof_inner[2][4] = {
    { 97.10388f, -29.074406f, 100.40328f, 0.0f },
    { 27.431381f, -198.28223f, 27.116943f, 0.0f },
};

// InitEffects: the streak colours (head dim grey, tail white and faint),
// no drops.
// FF3 0x00174c20.

void EffRainInit(void)
{
    eff_rain.HeadCol[0] = 0x40;
    eff_rain.HeadCol[1] = 0x40;
    eff_rain.HeadCol[2] = 0x40;
    eff_rain.HeadCol[3] = 0x44;
    eff_rain.TailCol[0] = 0xff;
    eff_rain.TailCol[1] = 0xff;
    eff_rain.TailCol[2] = 0xff;
    eff_rain.TailCol[3] = 0x15;
    eff_rain.Origin[0] = 0.0f;
    eff_rain.Origin[1] = 0.0f;
    eff_rain.Origin[2] = 0.0f;
    eff_rain.Origin[3] = 0.0f;
    eff_rain.Num = 0;
    eff_rain.pDrop = nullptr;
}

// A drop at height y somewhere in the 136-unit square by the origin,
// falling 19..37 units a frame with a slight drift, 24 long.
// FF3 0x00174dd8.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffRainDropReset(float y, EFF_RAIN_DROP *d, float *origin)
{
    float x, z;
    float vx, vy;
    int i;

    x = EffRand01() * 136.34431f + -6.017456f;              // rdata 0x36eb14 / 0x36eb18 / 0x36eb1c
    z = EffRand01() * 136.34428f + -5.20256f;               // rdata 0x36eb24 / 0x36eb28
    d->Pos[0] = x;
    d->Pos[1] = y;
    d->Pos[3] = 1.0f;                                       // rdata 0x36eb20
    d->Pos[2] = z;
    for (i = 0; i < 4; i++)                                 // vadd.xyzw
        d->Pos[i] += origin[i];
    vx = (EffRand01() + 39.0f) - 40.0f;                     // rdata 0x36eb2c / 0x36eb30
    vy = (EffRand01() * 18.0f + 59.0f) - 40.0f;             // rdata 0x36eb34 / 0x36eb38
    d->Vel[0] = vx;
    d->Vel[1] = vy;
    d->Len = 24.0f;                                         // rdata 0x36eb3c
    d->Vel[3] = 0.0f;
    d->Vel[2] = (EffRand01() + 39.0f) - 40.0f;
    Vector2Rot(d->Vel, &d->RotX, &d->RotY);
}

// Unreferenced in the ROM: 306 drops around pos at random heights up to
// 8000 above it; once (a second request while drops exist does nothing).
// FF3 0x00174c78.
// callcheck: rand, EFFECT_MALLOC (rand: EffRand01, ReiPan_Rand; EFFECT_MALLOC read as a macro)

void EffRainReq(float *pos)
{
    int i;

    if (eff_rain.pDrop != nullptr)
        return;
    eff_rain.pDrop = (EFF_RAIN_DROP *)EFFECT_MALLOC(EFF_RAIN_DROP_NUM * (int)sizeof(EFF_RAIN_DROP));   // 0x3960
    if (eff_rain.pDrop == nullptr)
        return;
    eff_rain.Origin[0] = pos[0];
    eff_rain.Origin[1] = pos[1];
    eff_rain.Origin[2] = pos[2];
    eff_rain.Origin[3] = 0.0f;
    eff_rain.Num = EFF_RAIN_DROP_NUM;
    for (i = 0; i < EFF_RAIN_DROP_NUM; i++)
        EffRainDropReset(EffRand01() * 7970.8677f + -8000.0f, &eff_rain.pDrop[i], pos);    // rdata 0x36eb08..0x36eb10
}

// EffectsReleaseAll.
// FF3 0x00174da0.
// callcheck: EFFECT_FREE (read as a macro)

void EffRainRelease(void)
{
    if (eff_rain.pDrop == nullptr)
        return;
    EFFECT_FREE(eff_rain.pDrop);
    EffRainInit();
}

// A drop is done at the floor (y >= 0) or inside the roof: the outer box
// less the inner one, both placed at the origin.
// FF3 0x00175388.

static int EffRainDropIsHit(float *pos, float *origin)
{
    int i;
    int in;

    if (0.0f <= pos[1])
        return 1;

    in = 1;
    for (i = 0; i < 3; i++)
    {
        if (!(rain_roof_outer[1][i] + origin[i] <= pos[i] && pos[i] <= rain_roof_outer[0][i] + origin[i]))
            in = 0;
    }
    if (in == 0)
        return 0;

    for (i = 0; i < 3; i++)
    {
        if (!(rain_roof_inner[1][i] + origin[i] <= pos[i] && pos[i] <= rain_roof_inner[0][i] + origin[i]))
            return 1;
    }
    return 0;
}

// One drop: a line from +Len / 2 (head colour) to -Len / 2 (tail colour)
// along its direction at its position, gouraud and blended; dropped when
// an end leaves the guard band (x / y 0x300..0xfb00, z 0xf..0xfffff).
// FF3 0x00174f98.
// callcheck: StartDmaDirectTrans, EndDmaDirectTrans (the packet, inline)

static void EffRainDropDraw(EFF_RAIN_DROP *d, int *head, int *tail)
{
    sceVu0FVECTOR v[2];
    sceVu0FMATRIX m;
    sceVu0FMATRIX slm;
    sceVu0IVECTOR iv[2];
    GRA3DCAMERA *pCam;
    u_long *pk;
    u_int *q;
    int i;

    pCam = gra3dGetCamera();
    v[0][0] = 0.0f;
    v[0][1] = 0.0f;
    v[0][2] = d->Len * 0.5f;                                // rdata 0x36eb48
    v[0][3] = 1.0f;                                         // rdata 0x36eb4c
    v[1][0] = 0.0f;
    v[1][1] = 0.0f;
    v[1][2] = -d->Len * 0.5f;
    v[1][3] = 1.0f;

    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, NormalizeAngle(d->RotX));
    sceVu0RotMatrixY(m, m, NormalizeAngle(d->RotY));
    m[3][0] += d->Pos[0];               // vadd.xyz
    m[3][1] += d->Pos[1];
    m[3][2] += d->Pos[2];
    sceVu0MulMatrix(slm, pCam->matWorldScreen, m);
    sceVu0RotTransPersN(iv, slm, v, 2, 0);
    for (i = 0; i < 2; i++)
    {
        if ((u_int)(iv[i][0] - 0x300) > 64000 || (u_int)(iv[i][1] - 0x300) > 64000
            || (u_int)(iv[i][2] - 0xf) > 0xffff0)
            return;
    }

    pk = (u_long *)StartDmaDirectTrans();
    pk[1] = 0x4141;                     // RGBAQ, XYZF2, RGBAQ, XYZF2
    pk[0] = 0x4024c00000008001ULL;      // rdata 0x36eb40: 1 loop, PRIM 0x49 (line, gouraud, blended), 4 regs
    q = (u_int *)(pk + 2);
    q[0] = (u_int)head[0];
    q[1] = (u_int)head[1];
    q[2] = (u_int)head[2];
    q[3] = (u_int)head[3];
    q[4] = (u_int)iv[0][0];
    q[5] = (u_int)iv[0][1];
    q[7] = 0;
    q[6] = (u_int)iv[0][2];
    q[8] = (u_int)tail[0];
    q[9] = (u_int)tail[1];
    q[10] = (u_int)tail[2];
    q[11] = (u_int)tail[3];
    q[12] = (u_int)iv[1][0];
    q[13] = (u_int)iv[1][1];
    q[15] = 0;
    q[14] = (u_int)iv[1][2];
    EndDmaDirectTrans((qword *)(pk + 10));
}

// A drop falls one frame; a finished one starts again 8000 above.
// FF3 0x00175240.

static void EffRainDropStep(EFF_RAIN_DROP *d, float *origin)
{
    int i;

    for (i = 0; i < 4; i++)                                 // vadd.xyzw
        d->Pos[i] += d->Vel[i];
    if (EffRainDropIsHit(d->Pos, origin))
        EffRainDropReset(-8000.0f, d, origin);              // rdata 0x36eb50
}

// EffectInPhoto: the GS state (additive ALPHA 0x48, TEST, ZBUF), then each
// drop drawn and moved; the drops move even while effects are stopped.
// FF3 0x001752b0.

void EffRainExec(void)
{
    int i;

    if (eff_rain.pDrop == nullptr)
        return;
    SetGsRegCached(0x48, SCE_GS_ALPHA_1, 1);
    SetGsRegCached(0x5000d, SCE_GS_TEST_1, 1);
    SetGsRegCached(0x10a000118ULL, SCE_GS_ZBUF_1, 1);
    for (i = 0; i < eff_rain.Num; i++)
    {
        EffRainDropDraw(&eff_rain.pDrop[i], eff_rain.HeadCol, eff_rain.TailCol);
        EffRainDropStep(&eff_rain.pDrop[i], eff_rain.Origin);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Area particles (FF3-only; EffAreaPartReq has no caller in the ROM)
// ──────────────────────────────────────────────────────────────────────

// Two emitters at fixed points of one place: 0 grey sprites rising and
// growing, 1 small still sparks.
static const EFF_EMIT_AREA eff_emit_area0[5] = {     // data 0x353d30
    { {
        { 31.1388149f, -29.9530773f, 98.3051376f, 1.0f },
        { 31.1388149f, -29.9530773f, 31.1764774f, 1.0f },
        { 94.0585098f, -29.9530773f, 31.1764774f, 1.0f },
        { 94.0585098f, -29.9530773f, 98.3051376f, 1.0f },
    }, 2000 },
    { {
        { 30.7307739f, -205.541626f, 32.0020256f, 1.0f },
        { -5.8801899f, -243.674652f, -4.60894585f, 1.0f },
        { 130.464127f, -243.674652f, -4.60893059f, 1.0f },
        { 93.8531494f, -205.541626f, 32.0020294f, 1.0f },
    }, 2000 },
    { {
        { 93.8531494f, -205.541626f, 32.0020294f, 1.0f },
        { 130.464127f, -243.674652f, -4.60893059f, 1.0f },
        { 130.464127f, -243.674652f, 131.735367f, 1.0f },
        { 93.8531418f, -205.541626f, 95.1244049f, 1.0f },
    }, 2000 },
    { {
        { 93.8531418f, -205.541626f, 95.1244049f, 1.0f },
        { 130.464127f, -243.674652f, 131.735367f, 1.0f },
        { -5.88019753f, -243.674652f, 131.735367f, 1.0f },
        { 30.7307682f, -205.541626f, 95.1244049f, 1.0f },
    }, 2000 },
    { {
        { 30.7307682f, -205.541626f, 95.1244049f, 1.0f },
        { -5.88019753f, -243.674652f, 131.735367f, 1.0f },
        { -5.8801899f, -243.674652f, -4.60894585f, 1.0f },
        { 30.7307739f, -205.541626f, 32.0020256f, 1.0f },
    }, 2000 },
};
static const EFF_EMIT_AREA eff_emit_area1[23] = {     // data 0x353ec0
    { {
        { 31.3816624f, -51.9537849f, 31.0761585f, 1.0f },
        { 31.3816605f, -51.9537849f, 51.0392265f, 1.0f },
        { 30.0570202f, -51.982605f, 51.0765648f, 1.0f },
        { 30.0570202f, -51.982605f, 30.3511753f, 1.0f },
    }, 585 },
    { {
        { 30.0570202f, -51.982605f, 30.3511753f, 1.0f },
        { 50.3208809f, -51.982605f, 30.3511791f, 1.0f },
        { 50.3101654f, -51.9537849f, 31.0761585f, 1.0f },
        { 31.3816624f, -51.9537849f, 31.0761585f, 1.0f },
    }, 308 },
    { {
        { 73.2049026f, -51.982605f, 30.3511848f, 1.0f },
        { 94.7375488f, -51.982605f, 30.3511848f, 1.0f },
        { 93.0244598f, -51.9537849f, 31.0761585f, 1.0f },
        { 73.1894226f, -51.9537849f, 31.0761585f, 1.0f },
    }, 325 },
    { {
        { 94.7375488f, -51.982605f, 30.3511848f, 1.0f },
        { 94.7375488f, -51.982605f, 98.7435989f, 1.0f },
        { 93.0244598f, -51.9537849f, 97.6581955f, 1.0f },
        { 93.0244598f, -51.9537849f, 31.0761585f, 1.0f },
    }, 2510 },
    { {
        { 67.3553848f, -51.9537849f, 97.6581955f, 1.0f },
        { 93.0244598f, -51.9537849f, 97.6581955f, 1.0f },
        { 94.7375488f, -51.982605f, 98.7435989f, 1.0f },
        { 67.5738678f, -51.982605f, 98.7435837f, 1.0f },
    }, 622 },
    { {
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
        { 42.7967033f, -51.4934616f, 42.9678688f, 1.0f },
        { 52.1851006f, -51.4934616f, 42.9678688f, 1.0f },
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
    }, 478 },
    { {
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
        { 52.1851006f, -51.4934616f, 42.9678688f, 1.0f },
        { 52.1851006f, -51.4934616f, 52.3562622f, 1.0f },
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
    }, 478 },
    { {
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
        { 52.1851006f, -51.4934616f, 52.3562622f, 1.0f },
        { 42.7967033f, -51.4934616f, 52.3562622f, 1.0f },
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
    }, 478 },
    { {
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
        { 42.7967033f, -51.4934616f, 52.3562622f, 1.0f },
        { 42.7967033f, -51.4934616f, 42.9678688f, 1.0f },
        { 47.4909058f, -54.6585426f, 47.6620636f, 1.0f },
    }, 478 },
    { {
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
        { 77.4878235f, -51.4934616f, 81.1747818f, 1.0f },
        { 86.8762207f, -51.4934616f, 81.1747818f, 1.0f },
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
    }, 478 },
    { {
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
        { 86.8762207f, -51.4934616f, 81.1747818f, 1.0f },
        { 86.8762207f, -51.4934616f, 90.5631714f, 1.0f },
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
    }, 478 },
    { {
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
        { 86.8762207f, -51.4934616f, 90.5631714f, 1.0f },
        { 77.4878235f, -51.4934616f, 90.5631714f, 1.0f },
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
    }, 478 },
    { {
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
        { 77.4878235f, -51.4934616f, 90.5631714f, 1.0f },
        { 77.4878235f, -51.4934616f, 81.1747818f, 1.0f },
        { 82.1820297f, -54.6585426f, 85.8689728f, 1.0f },
    }, 478 },
    { {
        { 48.9597015f, -63.4836578f, 62.9300613f, 1.0f },
        { 50.3630524f, -63.9987679f, 62.9267693f, 1.0f },
        { 51.2905846f, -63.8456116f, 64.2338715f, 1.0f },
        { 48.8620834f, -63.729229f, 64.2953186f, 1.0f },
    }, 56 },
    { {
        { 50.3630524f, -63.9987679f, 62.9267693f, 1.0f },
        { 54.5835342f, -64.7225189f, 61.4389076f, 1.0f },
        { 55.0127792f, -64.6395798f, 62.8984756f, 1.0f },
        { 51.2905846f, -63.8456116f, 64.2338715f, 1.0f },
    }, 241 },
    { {
        { 54.5835342f, -64.7225189f, 61.4389076f, 1.0f },
        { 59.6354866f, -62.2075195f, 60.6173286f, 1.0f },
        { 59.9077759f, -62.4398079f, 61.9252815f, 1.0f },
        { 55.0127792f, -64.6395798f, 62.8984756f, 1.0f },
    }, 246 },
    { {
        { 59.6354866f, -62.2075195f, 60.6173286f, 1.0f },
        { 64.4929657f, -61.5563736f, 59.9147186f, 1.0f },
        { 64.7429504f, -62.0245285f, 60.998642f, 1.0f },
        { 59.9077759f, -62.4398079f, 61.9252815f, 1.0f },
    }, 211 },
    { {
        { 64.4929657f, -61.5563736f, 59.9147186f, 1.0f },
        { 69.2804337f, -60.0636024f, 59.4665375f, 1.0f },
        { 69.4200058f, -60.3517075f, 61.0944176f, 1.0f },
        { 64.7429504f, -62.0245285f, 60.998642f, 1.0f },
    }, 167 },
    { {
        { 69.2804337f, -60.0636024f, 59.4665375f, 1.0f },
        { 72.853241f, -61.4624786f, 62.1598282f, 1.0f },
        { 72.3026428f, -61.4336662f, 63.0727234f, 1.0f },
        { 69.4200058f, -60.3517075f, 61.0944176f, 1.0f },
    }, 252 },
    { {
        { 72.853241f, -61.4624786f, 62.1598282f, 1.0f },
        { 77.2462006f, -60.7931442f, 63.1938286f, 1.0f },
        { 77.2300186f, -60.8502769f, 64.0488586f, 1.0f },
        { 72.3026428f, -61.4336662f, 63.0727234f, 1.0f },
    }, 191 },
    { {
        { 77.2462006f, -60.7931442f, 63.1938286f, 1.0f },
        { 78.2210999f, -61.4724236f, 63.4112244f, 1.0f },
        { 78.2245636f, -61.2238083f, 64.2758865f, 1.0f },
        { 77.2300186f, -60.8502769f, 64.0488586f, 1.0f },
    }, 45 },
    { {
        { 78.2210999f, -61.4724236f, 63.4112244f, 1.0f },
        { 85.6973724f, -62.3321495f, 65.1245804f, 1.0f },
        { 85.5670166f, -62.3321495f, 65.6899185f, 1.0f },
        { 78.2245636f, -61.2238083f, 64.2758865f, 1.0f },
    }, 366 },
    { {
        { 85.6973724f, -62.3321495f, 65.1245804f, 1.0f },
        { 88.1025009f, -63.2954369f, 64.5253067f, 1.0f },
        { 88.0491257f, -63.2954369f, 65.0323868f, 1.0f },
        { 85.5670166f, -62.3321495f, 65.6899185f, 1.0f },
    }, 61 },
};
static EFF_AREA_PART_WRK eff_area_part[2];      // bss 0x3d45f0, 0x3d4650

static void EffAreaPartSet(EFF_AREA_PART_WRK *w, float *pos, int num, const EFF_EMIT_AREA *area, int area_num);
static void EffAreaPartRelease(EFF_AREA_PART_WRK *w);
static void EffAreaPartEmit(EFF_AREA_PART_WRK *w);
static void EffAreaPartExecOne(EFF_AREA_PART_WRK *w);
static void EffAreaPartDraw(float rot_x, float rot_y, float w, float h, float *pos,
                            u_char r, u_char g, u_char b, u_char a);
static void EffAreaPartFree(EFF_AREA_PART *p);
static void EffAreaPartStart(float size_start, float size_end, EFF_AREA_PART *p,
                             const EFF_EMIT_AREA *area, float *origin, float *vel, int life);

// InitEffects: the two emitters' constants, no buffers.
// FF3 0x001754e0.
// callcheck: memset (PC: one clear for the ROM's word stores)

void EffAreaPartInit(void)
{
    EFF_AREA_PART_WRK *w;

    memset(eff_area_part, 0, sizeof(eff_area_part));   // the ROM stores every word
    w = &eff_area_part[0];
    w->Col[0] = 0x48;
    w->Col[1] = 0x47;
    w->Col[2] = 0x48;
    w->Alpha = 0x14;
    w->VelYMin = -1.0f;                         // rdata 0x36eb58
    w->EmitMin = 2;
    w->EmitMax = 5;
    w->Life = 0x26;
    w->SizeStart = 0.04f;                       // rdata 0x36eb5c
    w->SizeEndMax = 0.4f;                       // rdata 0x36eb60
    w->SizeEndMin = 0.1f;                       // rdata 0x36eb64

    w = &eff_area_part[1];
    w->Col[0] = 0x3d;
    w->Col[1] = 0x3d;
    w->Col[2] = 0x3d;
    w->Alpha = 0x16;
    w->EmitMin = 1;
    w->EmitMax = 0xc;
    w->Life = 0xf;
    w->SizeStart = 0.0208f;                     // rdata 0x36eb54
    w->SizeEndMax = 0.0208f;
    w->SizeEndMin = 0.0208f;
}

// Both emitters at pos: 100 particles in the five areas of table 0, 52 in
// the 23 of table 1.
// FF3 0x00175620.

void EffAreaPartReq(float *pos)
{
    EffAreaPartSet(&eff_area_part[0], pos, 100, eff_emit_area0, 5);
    EffAreaPartSet(&eff_area_part[1], pos, 0x34, eff_emit_area1, 0x17);
}

// An emitter at pos with num particles (all free) and its areas, then the
// first particles; nothing while its buffer is held.
// FF3 0x00175680.
// callcheck: EFFECT_MALLOC (read as a macro)

static void EffAreaPartSet(EFF_AREA_PART_WRK *w, float *pos, int num, const EFF_EMIT_AREA *area, int area_num)
{
    int i;

    if (w->pPart != nullptr || area == nullptr || num <= 0 || area_num <= 0)
        return;
    w->pPart = (EFF_AREA_PART *)EFFECT_MALLOC(num * (int)sizeof(EFF_AREA_PART));
    if (w->pPart == nullptr)
        return;
    w->Pos[0] = pos[0];                         // lq / sq
    w->Pos[1] = pos[1];
    w->Pos[2] = pos[2];
    w->pArea = area;
    w->AreaNum = area_num;
    w->Pos[3] = 0.0f;
    w->Num = num;
    for (i = 0; i < num; i++)
        EffAreaPartFree(&w->pPart[i]);
    EffAreaPartEmit(w);
}

// EffectsReleaseAll.
// FF3 0x00175770.

void EffAreaPartReleaseAll(void)
{
    EffAreaPartRelease(&eff_area_part[0]);
    EffAreaPartRelease(&eff_area_part[1]);
}

// FF3 0x00175798.
// callcheck: EFFECT_FREE (read as a macro)

static void EffAreaPartRelease(EFF_AREA_PART_WRK *w)
{
    if (w->pPart != nullptr)
    {
        EFFECT_FREE(w->pPart);
        w->Num = 0;
        w->pPart = nullptr;
        w->pArea = nullptr;
        w->AreaNum = 0;
    }
}

// The first free particle, or nullptr.
// FF3 0x001757e0.

static EFF_AREA_PART *EffAreaPartFindFree(EFF_AREA_PART_WRK *w)
{
    int i;

    for (i = 0; i < w->Num; i++)
        if (w->pPart[i].Life == -1)
            return &w->pPart[i];
    return nullptr;
}

// EmitMin .. EmitMax new particles (fewer when the buffer is full), each
// in an area picked by weight, with a random end size and velocity.
// FF3 0x00175828.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffAreaPartEmit(EFF_AREA_PART_WRK *w)
{
    EFF_AREA_PART *p;
    sceVu0FVECTOR vel;
    float size;
    int n, i, k, idx, sum, r;

    n = (int)((float)(w->EmitMax - w->EmitMin) * EffRand01()) + w->EmitMin;   // rdata 0x36eb68
    for (i = 0; i < n; i++)
    {
        p = EffAreaPartFindFree(w);
        if (p == nullptr)
            return;
        idx = w->AreaNum - 1;
        sum = 0;
        r = (int)(EffRand01() * 10000.0f);      // rdata 0x36eb6c
        for (k = 0; k < w->AreaNum; k++)
        {
            sum += w->pArea[k].Weight;
            if (r <= sum)
            {
                idx = k;
                break;
            }
        }
        size = (w->SizeEndMax - w->SizeEndMin) * EffRand01() + w->SizeEndMin;
        vel[0] = (w->VelXMax - w->VelXMin) * EffRand01() + w->VelXMin;
        vel[1] = (w->VelYMax - w->VelYMin) * EffRand01() + w->VelYMin;
        vel[2] = (w->VelZMax - w->VelZMin) * EffRand01() + w->VelZMin;
        vel[3] = 0.0f;
        EffAreaPartStart(w->SizeStart, size, p, &w->pArea[idx], w->Pos, vel, w->Life);
    }
}

// One particle on: moved, a frame older (-1 after its last).
// FF3 0x00175ab0.

static void EffAreaPartStep(EFF_AREA_PART *p)
{
    int i;

    for (i = 0; i < 4; i++)                     // vadd.xyzw
        p->Pos[i] += p->Vel[i];
    p->Life = p->Life == 0 ? -1 : p->Life - 1;
}

// EffectInPhoto: both emitters.
// FF3 0x00175ae0.

void EffAreaPartExec(void)
{
    EffAreaPartExecOne(&eff_area_part[0]);
    EffAreaPartExecOne(&eff_area_part[1]);
}

// Each live particle drawn facing the camera (size and alpha by age) and
// moved, then the frame's new ones. Runs while effects are stopped too.
// FF3 0x00175b08.

static void EffAreaPartExecOne(EFF_AREA_PART_WRK *w)
{
    GRA3DCAMERA *pCam;
    EFF_AREA_PART *p;
    float rot_x, rot_y, t, size;
    int i;

    if (w->pPart == nullptr)
        return;
    pCam = gra3dGetCamera();
    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &rot_x, &rot_y);
    for (i = 0; i < w->Num; i++)
    {
        p = &w->pPart[i];
        if (p->Life == -1)
            continue;
        t = (float)(w->Life - p->Life) / (float)w->Life;
        size = (p->SizeEnd - p->SizeStart) * t + p->SizeStart;
        EffAreaPartDraw(rot_x, rot_y, size, size, p->Pos,
                        (u_char)w->Col[0], (u_char)w->Col[1], (u_char)w->Col[2],
                        (u_char)(int)((float)w->Alpha * (1.0f - t)));      // rdata 0x36eb70
        EffAreaPartStep(p);
    }
    EffAreaPartEmit(w);
}

// A w x h (x 100) sprite of effdat 0x16 at pos turned by rot_x / rot_y,
// additive (ALPHA 0x48).
// FF3 0x00175c40.
// callcheck: sceVu0UnitMatrix (the ROM builds it from vf0)

static void EffAreaPartDraw(float rot_x, float rot_y, float w, float h, float *pos,
                            u_char r, u_char g, u_char b, u_char a)
{
    u_long regs[6];
    sceVu0FMATRIX m;

    regs[0] = 0x161;                            // TEX1_1
    regs[1] = 0x48;                             // ALPHA_1: additive
    regs[2] = 0x10a000118ULL;                   // ZBUF_1
    regs[3] = 0x5000d;                          // TEST_1
    regs[4] = 0;
    regs[5] = 0x302a400000008004ULL;            // the strip's GIF tag
    sceVu0UnitMatrix(m);                        // built inline (vf0)
    sceVu0RotMatrixX(m, m, NormalizeAngle(rot_x));
    sceVu0RotMatrixY(m, m, NormalizeAngle(rot_y));
    sceVu0TransMatrix(m, m, pos);
    Set3DPosTexure(w * 100.0f, h * 100.0f, m, regs, 0x16, r, g, b, a);   // rdata 0x36eb74
}

// FF3 0x00175d98.

static void EffAreaPartFree(EFF_AREA_PART *p)
{
    p->Life = -1;
}

// A particle at a random point of area (between its two edges), offset by
// origin.
// FF3 0x00175da8.
// callcheck: rand (EffRand01: ReiPan_Rand)

static void EffAreaPartStart(float size_start, float size_end, EFF_AREA_PART *p,
                             const EFF_EMIT_AREA *area, float *origin, float *vel, int life)
{
    sceVu0FVECTOR a, b;
    float r;
    int i;

    if (p == nullptr)
        return;
    r = EffRand01();                            // rdata 0x36eb78
    for (i = 0; i < 4; i++)                     // vsub / vmulx / vadd .xyzw
    {
        a[i] = (area->V[1][i] - area->V[0][i]) * r + area->V[0][i];
        b[i] = (area->V[2][i] - area->V[3][i]) * r + area->V[3][i];
    }
    r = EffRand01();
    for (i = 0; i < 4; i++)
        p->Pos[i] = (b[i] - a[i]) * r + a[i] + origin[i];
    for (i = 0; i < 4; i++)
        p->Vel[i] = vel[i];
    p->SizeEnd = size_end;
    p->Life = life;
    p->SizeStart = size_start;
}

// ──────────────────────────────────────────────────────────────────────
// Drops (FF3-only; EffDropReq has no caller in the ROM)
// ──────────────────────────────────────────────────────────────────────

// The drops' start points, offsets from the source.
static const sceVu0FVECTOR eff_drop_pos[35] = {     // data 0x354630
    { 30.334137f, -78.5291824f, 29.2090359f, 1.0f },
    { 94.1574326f, -98.0412598f, 56.4145279f, 1.0f },
    { 51.8664246f, -51.3140259f, 43.3620644f, 1.0f },
    { 73.324379f, -56.9786644f, 57.7293625f, 1.0f },
    { 94.0830688f, -98.0902634f, 88.673996f, 1.0f },
    { 94.3965149f, -77.5980988f, 47.9261436f, 1.0f },
    { 74.3241119f, -143.423813f, 94.3011551f, 1.0f },
    { 65.5503006f, -158.219131f, 95.5125351f, 1.0f },
    { 56.7166138f, -164.513077f, 93.2820435f, 1.0f },
    { 43.0623016f, -146.146133f, 93.6916962f, 1.0f },
    { 76.1817932f, -155.236893f, 100.127647f, 1.0f },
    { 76.2484512f, -159.042145f, 93.7817078f, 1.0f },
    { 69.1403046f, -171.624786f, 93.9208832f, 1.0f },
    { 74.7095032f, -151.509903f, 96.3073273f, 1.0f },
    { 84.0004654f, -142.14032f, 90.621521f, 1.0f },
    { 85.266655f, -140.737946f, 93.770462f, 1.0f },
    { 86.8896408f, -139.996338f, 93.7796326f, 1.0f },
    { 84.707756f, -148.507278f, 96.0758743f, 1.0f },
    { 87.1493301f, -139.289093f, 98.3768997f, 1.0f },
    { 86.946701f, -128.908127f, 91.1117249f, 1.0f },
    { 83.2492371f, -124.122261f, 97.4387054f, 1.0f },
    { 53.711441f, -163.803391f, 33.1706543f, 1.0f },
    { 36.9289627f, -163.803391f, 34.0077972f, 1.0f },
    { 78.1188507f, -195.611084f, 95.921669f, 1.0f },
    { 86.8463516f, -195.611084f, 95.8348999f, 1.0f },
    { 88.9977493f, -196.539581f, 94.0799637f, 1.0f },
    { 90.8672409f, -195.559235f, 87.889328f, 1.0f },
    { 90.8672409f, -195.559235f, 80.8758621f, 1.0f },
    { 90.8672409f, -195.559235f, 73.2629547f, 1.0f },
    { 69.1970749f, -195.559235f, 95.8722687f, 1.0f },
    { 52.4696426f, -104.504974f, 84.7485886f, 1.0f },
    { 59.2043686f, -93.9864273f, 82.7974014f, 1.0f },
    { 81.5837326f, -163.439529f, 97.7192078f, 1.0f },
    { 93.8623734f, -172.042419f, 90.5787048f, 1.0f },
    { 98.4310989f, -192.984726f, 87.8357086f, 1.0f },
};
static EFF_DROP_WRK eff_drop;                   // bss 0x3d46b0

static int  EffDropStep(EFF_DROP *d);
static void EffDropSet(float w, float h, EFF_DROP_WRK *wrk, const sceVu0FVECTOR *tbl, float *pos,
                       int num, int r, int g, int b, int a);
static void EffDropEmit(EFF_DROP_WRK *wrk);
static void EffDropPartSet(float gravity, float floor_y, EFF_DROP *d, float *pos, float *vel);

// InitEffects.
// FF3 0x00175f38.

void EffDropInit(void)
{
    memset(&eff_drop, 0, sizeof(eff_drop));
}

// EffectInPhoto: each drop drawn (EffAreaPartDraw's sprite) and moved,
// removed at its floor, then maybe a new one. Runs while effects are
// stopped too.
// FF3 0x00175f90.

void EffDropExec(void)
{
    GRA3DCAMERA *pCam;
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    EFF_DROP *d;
    float rot_x, rot_y;

    pCam = gra3dGetCamera();
    float *cam_pos = gra3dcamGetPosition();
    if (eff_drop.pList == nullptr)
        return;
    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);
    for (pCell = SingleLinkListBeginCell(eff_drop.pList); pCell != nullptr; pCell = pNext)
    {
        pNext = SingleLinkListNextCell(pCell);
        d = (EFF_DROP *)SingleLinkListCellBodyPtr(pCell);
        EffAreaPartDraw(rot_x, rot_y, eff_drop.W, eff_drop.H, d->Pos,
                        (u_char)eff_drop.Col[0], (u_char)eff_drop.Col[1], (u_char)eff_drop.Col[2],
                        (u_char)eff_drop.Col[3]);
        if (EffDropStep(d) == 0)
            SingleLinkListRemove(eff_drop.pList, pCell);
    }
    EffDropEmit(&eff_drop);
}

// One drop on; 0 once it is at or below its floor (not moved then).
// FF3 0x00176078.

static int EffDropStep(EFF_DROP *d)
{
    int i;

    if (d->FloorY <= d->Pos[1])
        return 0;
    for (i = 0; i < 4; i++)                     // vadd.xyzw
        d->Pos[i] += d->Vel[i];
    d->Vel[1] += d->Gravity;
    return 1;
}

// The drop source at pos: 35 start points, 0.2 x 0.36 grey sprites.
// FF3 0x001760c0.

void EffDropReq(float *pos)
{
    EffDropSet(0.002f, 0.0036f, &eff_drop, eff_drop_pos, pos, 0x23,    // rdata 0x36eb7c / 0x36eb80
               0x4b, 0x4b, 0x4b, 0x3b);
}

// The source's list and constants; nothing while a list or table is held.
// FF3 0x00176118.

static void EffDropSet(float w, float h, EFF_DROP_WRK *wrk, const sceVu0FVECTOR *tbl, float *pos,
                       int num, int r, int g, int b, int a)
{
    SINGLE_LINK_LIST *pList;

    if (wrk == nullptr || tbl == nullptr || wrk->pList != nullptr || wrk->pTbl != nullptr)
        return;
    pList = SingleLinkListAlloc(sizeof(EFF_DROP));
    if (pList == nullptr)
        return;
    wrk->Pos[0] = pos[0];                       // lq / sq
    wrk->Pos[1] = pos[1];
    wrk->Pos[2] = pos[2];
    wrk->Col[3] = a;
    wrk->pList = pList;
    wrk->pTbl = tbl;
    wrk->TblNum = num;
    wrk->W = w;
    wrk->H = h;
    wrk->Col[0] = r;
    wrk->Col[1] = g;
    wrk->Col[2] = b;
    wrk->Pos[3] = 0.0f;
}

// EffectsReleaseAll.
// FF3 0x00176208.

void EffDropRelease(void)
{
    if (eff_drop.pList != nullptr)
    {
        SingleLinkListFree(eff_drop.pList);
        eff_drop.pTbl = nullptr;
        eff_drop.pList = nullptr;
    }
}

// A new drop at rest on 53 % of the frames, at a random start point.
// FF3 0x00176248.
// callcheck: rand, memset (rand: EffRand01, ReiPan_Rand; memset: PC clears the stack record)

static void EffDropEmit(EFF_DROP_WRK *wrk)
{
    EFF_DROP d;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR vel;
    int n, idx, i;

    n = EffRand01() * 100.0f <= 53.0f;          // rdata 0x36eb84 / 0x36eb88 / 0x36eb8c
    while (n-- != 0)
    {
        idx = (int)((float)wrk->TblNum * EffRand01());
        if (wrk->TblNum <= idx)
            idx = wrk->TblNum - 1;
        for (i = 0; i < 4; i++)                 // vadd.xyzw; the ROM reads the table, not pTbl
            pos[i] = eff_drop_pos[idx][i] + wrk->Pos[i];
        vel[0] = EffRand01() * 0.0f;            // the ROM draws three randoms for a zero range
        vel[1] = EffRand01() * 0.0f;
        vel[2] = EffRand01() * 0.0f;
        vel[3] = 0.0f;
        memset(&d, 0, sizeof(d));               // PC: the ROM leaves +0x28.. as stack bytes
        EffDropPartSet(0.088f, 160.0f, &d, pos, vel);   // rdata 0x36eb90 / 0x36eb94
        SingleLinkListAddEnd(wrk->pList, &d);
    }
}

// FF3 0x00176420.

static void EffDropPartSet(float gravity, float floor_y, EFF_DROP *d, float *pos, float *vel)
{
    int i;

    if (d == nullptr)
        return;
    for (i = 0; i < 4; i++)
    {
        d->Pos[i] = pos[i];
        d->Vel[i] = vel[i];
    }
    d->FloorY = floor_y;
    d->Gravity = gravity;
}
