#ifndef REIPAN_CRASH_H
#define REIPAN_CRASH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Windows: on a crash on any thread, write crash.log (the exception, the
 * faulting address as module + offset, and every frame of the crashing
 * thread's stack with its function, file and line where the .pdb beside the
 * executable has them) and crash.dmp (a minidump) into the user folder, and
 * the same lines to reipan.log.  Elsewhere it does nothing (the PS5 build has
 * its own, reipan_ps5.cpp).  Call once, after the log is up. */
void ReiPan_CrashInstall(void);

#ifdef __cplusplus
}
#endif

#endif
