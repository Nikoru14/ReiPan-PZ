#ifndef REIPAN_XBOX_MOTORS_H
#define REIPAN_XBOX_MOTORS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An Xbox pad's four motors -- the two in the grips and the two behind the
 * triggers -- through Windows.Gaming.Input.
 *
 * On Windows SDL reads these pads through XInput, which has no way to reach the
 * trigger motors.  SDL does have drivers that can, off by default, and either
 * changes how every Xbox pad is read: raw input gives it another GUID, which is
 * what the settings remember a pad by, and SDL's Windows.Gaming.Input driver
 * only gets a pad with XInput switched off.  So SDL keeps the input and only
 * the motors are driven from here.
 *
 * Everywhere but Windows all of this answers "no".
 */

typedef struct ReiPan_XboxMotors ReiPan_XboxMotors;

/* Buttons held on a pad, for telling two of the same model apart: the bits of
 * Windows.Gaming.Input's own GamepadButtons. */
#define REIPAN_XBOX_BTN_MENU   0x0001u
#define REIPAN_XBOX_BTN_VIEW   0x0002u
#define REIPAN_XBOX_BTN_A      0x0004u
#define REIPAN_XBOX_BTN_B      0x0008u
#define REIPAN_XBOX_BTN_X      0x0010u
#define REIPAN_XBOX_BTN_Y      0x0020u
#define REIPAN_XBOX_BTN_UP     0x0040u
#define REIPAN_XBOX_BTN_DOWN   0x0080u
#define REIPAN_XBOX_BTN_LEFT   0x0100u
#define REIPAN_XBOX_BTN_RIGHT  0x0200u
#define REIPAN_XBOX_BTN_LB     0x0400u
#define REIPAN_XBOX_BTN_RB     0x0800u
#define REIPAN_XBOX_BTN_LS     0x1000u
#define REIPAN_XBOX_BTN_RS     0x2000u

/* 1 when Windows.Gaming.Input is there at all. */
int ReiPan_XboxMotorsSupported(void);

/* The motors of the pad with these USB ids, or NULL when Windows does not list
 * it yet -- the list fills in a moment after it is first asked for -- or it
 * cannot be told from another one: several pads with the same ids, or none
 * (Windows gives some pads a stand-in id).  Then it is the one pad with exactly
 * `buttons` held, so ask again while some are.  One thread at a time. */
ReiPan_XboxMotors *ReiPan_XboxMotorsFind(unsigned vendor, unsigned product,
                                         unsigned buttons);

/* All four at once, 0..1 each: the big motor in the left grip, the small one in
 * the right, the triggers.  They keep turning until told otherwise.  0 when the
 * pad is gone.  Returns at once; any thread. */
int ReiPan_XboxMotorsSet(ReiPan_XboxMotors *motors, float low, float high,
                         float left_trigger, float right_trigger);

void ReiPan_XboxMotorsRelease(ReiPan_XboxMotors *motors);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_XBOX_MOTORS_H */
