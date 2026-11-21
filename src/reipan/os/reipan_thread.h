#ifndef REIPAN_THREAD_H
#define REIPAN_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

// Host threading/synchronisation wrapper.  Confines SDL threads/mutex/sema/TLS
// to the reipan layer so the SDK's EE-kernel shim (and libgraph's vblank
// thread) don't call SDL directly.  These are neutral primitives; the SDK keeps
// its own sce*/EE naming, slot tables and scheduling policy on top.

typedef struct ReiPan_Thread ReiPan_Thread;
typedef struct ReiPan_Sema   ReiPan_Sema;
typedef struct ReiPan_Mutex  ReiPan_Mutex;
typedef struct ReiPan_Tls    ReiPan_Tls;

// Threads.  entry(arg) runs on the new thread; the handle is owned by the
// caller until ReiPan_ThreadDetach.  Returns NULL on failure.
typedef void (*ReiPan_ThreadEntry)(void *arg);
ReiPan_Thread *ReiPan_ThreadCreate(ReiPan_ThreadEntry entry, void *arg,
                                   const char *name);
void           ReiPan_ThreadDetach(ReiPan_Thread *t);

// SDL_CreateThread for the port's own SDL threads (the IOP, its timers, the
// mixer), with a stack sized for the game rather than the system's default:
// the PS5's is far smaller than a desktop's.
struct SDL_Thread;
struct SDL_Thread *ReiPan_SdlThreadCreate(int (*fn)(void *), const char *name,
                                          void *data);

// Counting semaphores.
ReiPan_Sema *ReiPan_SemaCreate(int initial_count);
void         ReiPan_SemaDestroy(ReiPan_Sema *s);
void         ReiPan_SemaSignal(ReiPan_Sema *s);
void         ReiPan_SemaWait(ReiPan_Sema *s);
int          ReiPan_SemaTryWait(ReiPan_Sema *s); // 1 if acquired, 0 otherwise

// Mutexes.
ReiPan_Mutex *ReiPan_MutexCreate(void);
void          ReiPan_MutexDestroy(ReiPan_Mutex *m);
void          ReiPan_MutexLock(ReiPan_Mutex *m);
void          ReiPan_MutexUnlock(ReiPan_Mutex *m);

// Thread-local storage of a single void* per key.
ReiPan_Tls *ReiPan_TlsCreate(void);
void        ReiPan_TlsSet(ReiPan_Tls *key, void *value);
void       *ReiPan_TlsGet(ReiPan_Tls *key);

#ifdef __cplusplus
}
#endif

#endif /* REIPAN_THREAD_H */
