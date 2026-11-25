#ifndef REIPAN_HAPTIC_CUES_H
#define REIPAN_HAPTIC_CUES_H

#include "reipan/io/reipan_haptics.h"           // REIPAN_CUE_*

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What the game's rumble is for (io/reipan_haptics.h): called beside the
 * requests it makes of the two motors, which stay as they are.  Nothing with
 * the game's Vibration option off, as they do nothing then.
 *
 *   Heartbeat     player.c PlyrVibCheck, vib_pulse.c (peeping)
 *   Hit           player.c PlyrVibCtrl
 *   Held          player.c PlyrDamageCtrl, the ticks of a ghost's hold
 *   Stricken      player.c PlyrStatusCtrl, status 1
 *   Shot          effect_ene.c EffEneHitVibrate
 *   Thunder       effect_obj.c EffectThunderLightExec
 *   Scene         scene_effect.c SceneEffectVibrate
 *
 * and, the original having no rumble there, only with the setting on:
 *
 *   Shutter       plyr_shot.c PlyrShotJudge
 *   Charge        plyr_shot.c PlyrShotChargeCtrl
 *   Chance        plyr_shot.c PlyrShotChargeCtrl
 */
void ReiPan_HapticCue(int cue, float strength);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_HAPTIC_CUES_H */
