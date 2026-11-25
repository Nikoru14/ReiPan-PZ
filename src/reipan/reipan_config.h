#ifndef REIPAN_CONFIG_H
#define REIPAN_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Persisted host settings.
 *
 * These are the port's own knobs -- window, resolution, aspect, shading -- and
 * they deliberately live OUTSIDE the game's save data.  system/mc/dat/save_data.c
 * is a manifest walk whose callback order is literally the byte layout of a save
 * file, so adding a field there would invalidate every existing save; and none
 * of this belongs to the ROM's option screen in any case.
 *
 * The file is `reipan.ini` in the port's user directory (see io/reipan_paths.h),
 * next to `memcard/`, and it is plain text meant to be hand-editable: enumerated
 * settings are written as names ("borderless", "16:9", "native") and read back
 * either way, so a numeric value from an older file still loads.
 *
 * Precedence is defaults < file < environment.  The REIPAN_* environment
 * variables still win, so a one-off test run never has to disturb -- or
 * accidentally persist into -- the saved settings.
 */

typedef struct ReiPan_ConfigRenderer
{
    int   window_mode;       /* REIPAN_WINDOW_MODE_*   */
    int   window_width;      /* windowed size only; fullscreen is never stored */
    int   window_height;
    int   backend;           /* REIPAN_BACKEND_*; read once, at startup */
    int   render_mode;       /* REIPAN_RENDER_RES_*    */
    float render_scale;
    int   upscale_filter;    /* REIPAN_RENDER_FILTER_* */
    int   hd_fonts;          /* optional PNG font replacements */
    int   smooth_2d;         /* bilinear font and HUD/menu sprites */
    int   msaa_samples;      /* 1 (off), 2, 4 or 8     */
    int   anisotropy;        /* 1 (off), 2, 4, 8 or 16 */
    int   aspect_mode;       /* REIPAN_ASPECT_*        */
    float aspect_ratio;      /* used when aspect_mode is CUSTOM */
    int   lighting_mode;     /* REIPAN_LIGHTING_*      */
    int   shadow_filter;     /* REIPAN_SHADOW_FILTER_*  */
    int   extra_presents;    /* frame smoothing: 0..3 additional presents */
    int   interpolate_presents;
    int   interpolate_geometry; /* blend the world too, not just the camera */

    /* Output transform.  See the block comment above
     * ReiPan_RendererSetHdrMode() in rendering/reipan_renderer.h. */
    int   hdr_mode;              /* REIPAN_HDR_*                            */
    float hdr_paper_white;       /* nits; 0 follows the OS's SDR white level */
    float hdr_peak;              /* nits; 0 follows the display's headroom   */
    float hdr_expansion;         /* highlight lift, 0..1                     */
    float hdr_expansion_knee;    /* where the lift starts, 0.5..1            */

    /* The display grade.  Applies in SDR as well; all four at 1.0 is identity
     * and costs nothing, because the present pass is then skipped. */
    float grade_brightness;
    float grade_contrast;
    float grade_gamma;
    float grade_saturation;

    /* Screen effects the player may turn off.  See the block above
     * ReiPan_RendererSetFilmGrain() in rendering/reipan_renderer.h. */
    int   film_grain;            /* REIPAN_FILM_GRAIN_*                      */

    /* The viewfinder surround.  See the block above
     * ReiPan_RendererDrawFinderMask() in rendering/reipan_renderer.h. */
    int   finder_mask;           /* 0 off, 1 on                              */
    float finder_mask_blur;      /* defocus outside the frame, 0..1          */
    float finder_mask_darken;    /* 0 none, 1 black                          */
    float finder_mask_tint;      /* the cold indigo cast's strength, 0..1    */
    float finder_mask_scale;     /* clear rect against the ROM's 640x448     */

    /* Cut-out edge sharpening.  See the block above
     * ReiPan_RendererSetAlphaSharpen() in rendering/reipan_renderer.h. */
    float alpha_sharpen;         /* 0 off .. 1 full                          */
    float alpha_cutoff;          /* hard discard threshold, 0 off            */

    /* Static geometry kept on the GPU.  See the block above
     * ReiPan_RendererCreateResidentMesh() in rendering/reipan_renderer.h. */
    int   resident_meshes;       /* 0 off, 1 on                              */
    /* Model textures resolved once, their GS uploads then skipped.  See
     * ReiPan_RendererSetResidentTextures(). */
    int   resident_textures;     /* 0 off, 1 on                              */
} ReiPan_ConfigRenderer;

/*
 * Where the game's own files are.
 *
 * This is the one setting that is not a preference: without it the port has
 * nothing to load.  It is here rather than compiled in because on a phone or a
 * console the data cannot be beside the executable and there is no shell to set
 * an environment variable in -- pointing this key at a folder is the whole
 * mechanism.  Empty means "discover it", and what discovery finds is written
 * back, so the file always names the folder actually in use.
 */
typedef struct ReiPan_ConfigPaths
{
    char data_folder[1024];
} ReiPan_ConfigPaths;

/*
 * The title screen's backdrop and music.  outgame/title_room.h is what reads
 * it, and its banner says how it hangs off the ROM's title; the overlay's
 * Settings > Title screen window is what writes it.
 *
 * The backdrop is a list of scenes -- a room, two camera points and the path
 * between them -- and the title picks among the enabled ones at random, moving
 * on after `passes` runs of the path or each time the title comes up.  The
 * dim, the music and the attract movie belong to the title, not a scene.
 *
 * Camera points are world space as the game has it: Y grows downwards, and Rei
 * is about seventeen units tall.  Fields of view are in degrees here and
 * radians everywhere else.
 *
 * Every field is 4 bytes or a char array of a multiple of 4, so the overlay's
 * settle-and-save compare can memcmp it the way it does the renderer struct.
 */
#define REIPAN_TITLE_BACKDROP_CLASSIC 0     /* the original movie and picture  */
#define REIPAN_TITLE_BACKDROP_ROOM    1     /* one of the game's rooms         */

#define REIPAN_TITLE_MOTION_PINGPONG  0     /* A to B and back                 */
#define REIPAN_TITLE_MOTION_LOOP      1     /* A to B, then cut back to A      */
#define REIPAN_TITLE_MOTION_ONCE      2     /* A to B, then hold               */

#define REIPAN_TITLE_EASING_SMOOTH    0     /* eases out of each end and in    */
#define REIPAN_TITLE_EASING_LINEAR    1

#define REIPAN_TITLE_CHANGE_PASS      0     /* another scene after `passes`    */
#define REIPAN_TITLE_CHANGE_VISIT     1     /* another scene per title visit   */

#define REIPAN_TITLE_SCENE_MAX        64
#define REIPAN_TITLE_NAME_MAX         32

typedef struct ReiPan_ConfigTitleScene
{
    char  name[REIPAN_TITLE_NAME_MAX];
    int   enabled;           /* 0 keeps it out of the rotation              */
    int   room;              /* FF3 area, 1..52 (title_room.h)              */
    int   side;              /* 1 the house's night lighting                */
    int   camera_set;        /* 0: frame the room by itself when it loads   */
    float a_eye[3];
    float a_target[3];
    float a_fov;             /* degrees                                     */
    float b_eye[3];
    float b_target[3];
    float b_fov;             /* degrees                                     */
    float travel_seconds;    /* one pass, A to B                            */
    float hold_seconds;      /* at each end                                 */
    int   motion;            /* REIPAN_TITLE_MOTION_*                       */
    int   easing;            /* REIPAN_TITLE_EASING_*                       */
} ReiPan_ConfigTitleScene;

typedef struct ReiPan_ConfigTitle
{
    int   backdrop;          /* REIPAN_TITLE_BACKDROP_*                     */
    int   change;            /* REIPAN_TITLE_CHANGE_*                       */
    int   passes;            /* runs of the path before the next scene      */
    float dim;               /* black under the logo, 0 none .. 1 black     */
    int   music;             /* 0 the game's own; else a stream file, looped */
    float music_volume;      /* 0..1; 1 is the level the game plays music at */
    int   attract_movie;     /* room backdrop: let the idle movie start     */
    int   scene_count;       /* 1..REIPAN_TITLE_SCENE_MAX                   */
    ReiPan_ConfigTitleScene scenes[REIPAN_TITLE_SCENE_MAX];
} ReiPan_ConfigTitle;

/* A scene as a fresh one starts: the default room, framed by itself on load. */
void ReiPan_ConfigTitleSceneDefault(ReiPan_ConfigTitleScene *scene);

/*
 * The title settings as text, exactly as reipan.ini carries them, for the
 * overlay's copy and paste.  Both write NUL-terminated text and return the
 * length the whole of it needs, so a NULL or short buffer measures.
 *
 *   Title  [title] and every [title.sceneN] after it.
 *   Scene  one scene's section.
 */
int ReiPan_ConfigTitleText(char *buf, int size);
int ReiPan_ConfigTitleSceneText(int scene, char *buf, int size);

/* Append every scene section found in `text` -- [title.sceneN] blocks as the
 * two above write them, numbered or not -- after the scenes already there, up
 * to REIPAN_TITLE_SCENE_MAX.  With `replace` the list is cleared first and a
 * [title] section in the text sets the title's own settings too.  Returns the
 * number of scenes taken; 0 changes nothing. */
int ReiPan_ConfigTitlePaste(const char *text, int replace);

/*
 * The over-the-shoulder camera (gameplay/reipan_over_shoulder.h), section
 * [over_shoulder_camera].  Distances in world units (Rei stands about 27
 * tall).  Every field is 4 bytes, so the overlay's settle-and-save compare can
 * memcmp it.
 */
typedef struct ReiPan_ConfigCamera
{
    int   over_shoulder;     /* 0 the game's fixed cameras                  */
    int   side_left;         /* the left shoulder instead of the right      */
    int   collision;         /* pull in against the drawn scenery           */
    int   auto_follow;       /* drift back behind her while she walks       */
    int   invert_y;          /* on top of the game's own "Point of view"    */
    int   scare_camera;      /* REIPAN_OTS_SCARE_*, written by name         */
    float distance;
    float height;
    float side;
    float fov;               /* degrees, vertical                           */
    float sensitivity;
    float smoothing;
} ReiPan_ConfigCamera;

/*
 * The mouse in play (io/reipan_game_mouse.h), section [mouse]: looking, its
 * speed, how the Camera Obscura button works and which control each action is
 * on (REIPAN_MOUSE_SRC_*, written by name).  Every field is 4 bytes.
 */
typedef struct ReiPan_ConfigMouse
{
    int   look;
    float sensitivity;
    int   camera_toggle;     /* 0 hold, 1 toggle                            */
    int   bind[5];           /* REIPAN_MOUSE_ACT_* order                    */
} ReiPan_ConfigMouse;

/*
 * Gyro aiming (io/reipan_gyro.h), section [gyro].  Every field is 4 bytes.
 */
typedef struct ReiPan_ConfigGyro
{
    int   look;
    float sensitivity;
    int   turn;              /* REIPAN_GYRO_TURN_*, written by name         */
    int   invert_x;
    int   invert_y;
} ReiPan_ConfigGyro;

/*
 * The controller's motors (io/reipan_haptics.h), section [rumble].  The game's
 * own Vibration option is not here: it lives in the save.  Every field is 4
 * bytes.
 */
typedef struct ReiPan_ConfigRumble
{
    float strength;          /* 0..1                                        */
    int   triggers;
    float trigger_strength;  /* 0..1                                        */
    int   camera;            /* the Camera Obscura's cues too               */
} ReiPan_ConfigRumble;

typedef struct ReiPan_Config
{
    /* Order matters: reipan_config.cpp's defaults are a positional
     * initialiser in exactly this order. */
    ReiPan_ConfigRenderer renderer;
    ReiPan_ConfigPaths    paths;
    ReiPan_ConfigTitle    title;
    int profiler_summary;
    int profiler_fps_only;
    ReiPan_ConfigCamera   camera;
    ReiPan_ConfigMouse    mouse;
    ReiPan_ConfigGyro     gyro;
    ReiPan_ConfigRumble   rumble;
    int overlay_menu_bar;    /* the overlay's bar is up (F1), [overlay]     */
} ReiPan_Config;

extern ReiPan_Config reipan_config;

/* Absolute path of the settings file, or NULL if the user directory could not be
 * created. */
const char *ReiPan_ConfigPath(void);

/* Read the file over the defaults.  Idempotent -- the paths module calls it as
 * soon as anything asks where the data is, which is well before the renderer
 * exists, and the second call is a no-op.  Safe to call when no file exists:
 * that is the first run, and it leaves the defaults in place and writes the file
 * out so there is something to edit.  Does not touch the renderer; call
 * ReiPan_ConfigApply() for that. */
void ReiPan_ConfigLoad(void);

/* Record the data folder and persist it.  Called by the paths module with what
 * discovery found, so an unconfigured first run still ends up with the key
 * filled in, and by the settings UI when the player chooses one.  Returns
 * non-zero when the value is stored and on disk -- an unchanged value counts,
 * a refused write does not, which is what lets the UI say which happened. */
int ReiPan_ConfigSetDataFolder(const char *folder);

/* Write the current struct out.  Returns non-zero on success. */
int ReiPan_ConfigSave(void);

/* Push the struct into the renderer, and pull the renderer's live state back
 * into the struct, respectively.  Capture reads the *windowed* size even while
 * the window is fullscreen, so toggling fullscreen never overwrites the size the
 * window should return to. */
void ReiPan_ConfigApply(void);
void ReiPan_ConfigCapture(void);

/* Capture then save -- what the host UI calls after a settings change. */
int ReiPan_ConfigCaptureAndSave(void);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_CONFIG_H */
