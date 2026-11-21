#ifndef REIPAN_PS5_H
#define REIPAN_PS5_H

/*
 * The PlayStation 5 build's own support (src/reipan/reipan_ps5.cpp), ported
 * from MikuPan's: where the title keeps its files, the console's
 * notifications, the crash report, leaving the title, and the pause while the
 * home screen has the controller.  Everything here exists only when building
 * for the console (cmake/ps5.cmake).
 */

#ifdef __PROSPERO__

#ifdef __cplusplus
extern "C" {
#endif

/* The writable folder for reipan.ini, the memory card, the log and the crash
 * report, with a trailing separator: /data/ReiPan/ when the title can write
 * there, like other homebrew, otherwise the title's own /download0/ReiPan/.
 * It is also where the game's own files are looked for first.  Never NULL. */
const char *ReiPan_Ps5UserDir(void);

/* The title's read-only folder (eboot.bin and resources/): "/app0/". */
const char *ReiPan_Ps5AppDir(void);

/* Whether a file or folder exists, found by opening it (fopen, then opendir):
 * a title has no working directory, and its stat is not relied on. */
int ReiPan_Ps5PathExists(const char *path);

/* Whether `path` is a file that opens (and not a folder). */
int ReiPan_Ps5IsFile(const char *path);

/* `path` with each part spelt as its folder spells it, ignoring case: the
 * console's /data is case-sensitive, and a disc's files are often copied as
 * img_bd.bin.  A part that matches nothing keeps its own spelling.  Writes
 * the result to `out`; returns 1 if the path (so spelt) exists. */
int ReiPan_Ps5ResolveCase(const char *path, char *out, unsigned long out_size);

/* Shows a system notification (the pop-up in the corner of the screen) and
 * writes the same text to the log. */
void ReiPan_Ps5Notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Leaves the title: a title that calls exit() gets SIGSYS and a crash
 * dialog, so the shell is asked to close it instead.  Never returns. */
[[noreturn]] void ReiPan_Ps5Exit(int status);

/* Whether the game should stay paused this frame: true while the console's
 * home screen or a system menu has the controller.  Pauses the sound on the
 * way in and resumes it on the way out; sleeps a little while paused. */
int ReiPan_Ps5ShouldPause(void);

/* Once a game frame, from WaitVBlank: holds the game while the home screen
 * has the controller, and writes the log out about once a second. */
void ReiPan_Ps5FrameTick(void);

/* Walks through what SDL_GPU's Vulkan backend needs from RADV and logs each
 * answer, to say why SDL_GPU refused Vulkan. */
void ReiPan_Ps5VulkanReport(void);

#ifdef __cplusplus
}
#endif

#endif /* __PROSPERO__ */

#endif /* REIPAN_PS5_H */
