#include "reipan_thread.h"

#include <stdlib.h>

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>

// The opaque handle types wrap the SDL primitives.  Threads carry a small
// trampoline so the caller's void(*)(void*) entry runs under SDL's SDLCALL
// int(void*) signature, with TLS-based identity handled by the SDK on top.

struct ReiPan_Thread
{
    SDL_Thread        *sdl;
    ReiPan_ThreadEntry entry;
    void              *arg;
};

struct ReiPan_Sema
{
    SDL_Semaphore *sdl;
};

struct ReiPan_Mutex
{
    SDL_Mutex *sdl;
};

struct ReiPan_Tls
{
    SDL_TLSID id;
};

static int SDLCALL ThreadTrampoline(void *data)
{
    ReiPan_Thread *t = (ReiPan_Thread *)data;
    if (t != 0 && t->entry != 0)
    {
        t->entry(t->arg);
    }
    return 0;
}

extern "C" {

SDL_Thread *ReiPan_SdlThreadCreate(int (*fn)(void *), const char *name, void *data)
{
#ifdef __PROSPERO__
    /* The console's default thread stack is small; the game's frames are not. */
    const Sint64 stack_bytes = 2 * 1024 * 1024;
#else
    const Sint64 stack_bytes = 0; /* the system's default */
#endif
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                           (void *)fn);
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, name);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, data);
    if (stack_bytes > 0)
    {
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER,
                              stack_bytes);
    }
    SDL_Thread *thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    return thread;
}

ReiPan_Thread *ReiPan_ThreadCreate(ReiPan_ThreadEntry entry, void *arg,
                                   const char *name)
{
    if (entry == 0)
    {
        return 0;
    }

    ReiPan_Thread *t = (ReiPan_Thread *)malloc(sizeof(ReiPan_Thread));
    if (t == 0)
    {
        return 0;
    }

    t->entry = entry;
    t->arg   = arg;
    t->sdl   = ReiPan_SdlThreadCreate(ThreadTrampoline, name != 0 ? name : "reipan-thread", t);
    if (t->sdl == 0)
    {
        free(t);
        return 0;
    }

    return t;
}

void ReiPan_ThreadDetach(ReiPan_Thread *t)
{
    if (t == 0)
    {
        return;
    }
    if (t->sdl != 0)
    {
        SDL_DetachThread(t->sdl);
    }
    free(t);
}

ReiPan_Sema *ReiPan_SemaCreate(int initial_count)
{
    ReiPan_Sema *s = (ReiPan_Sema *)malloc(sizeof(ReiPan_Sema));
    if (s == 0)
    {
        return 0;
    }
    s->sdl = SDL_CreateSemaphore((Uint32)(initial_count < 0 ? 0 : initial_count));
    if (s->sdl == 0)
    {
        free(s);
        return 0;
    }
    return s;
}

void ReiPan_SemaDestroy(ReiPan_Sema *s)
{
    if (s == 0)
    {
        return;
    }
    if (s->sdl != 0)
    {
        SDL_DestroySemaphore(s->sdl);
    }
    free(s);
}

void ReiPan_SemaSignal(ReiPan_Sema *s)
{
    if (s != 0 && s->sdl != 0)
    {
        SDL_SignalSemaphore(s->sdl);
    }
}

void ReiPan_SemaWait(ReiPan_Sema *s)
{
    if (s != 0 && s->sdl != 0)
    {
        SDL_WaitSemaphore(s->sdl);
    }
}

int ReiPan_SemaTryWait(ReiPan_Sema *s)
{
    if (s == 0 || s->sdl == 0)
    {
        return 0;
    }
    return SDL_TryWaitSemaphore(s->sdl) ? 1 : 0;
}

ReiPan_Mutex *ReiPan_MutexCreate(void)
{
    ReiPan_Mutex *m = (ReiPan_Mutex *)malloc(sizeof(ReiPan_Mutex));
    if (m == 0)
    {
        return 0;
    }
    m->sdl = SDL_CreateMutex();
    if (m->sdl == 0)
    {
        free(m);
        return 0;
    }
    return m;
}

void ReiPan_MutexDestroy(ReiPan_Mutex *m)
{
    if (m == 0)
    {
        return;
    }
    if (m->sdl != 0)
    {
        SDL_DestroyMutex(m->sdl);
    }
    free(m);
}

void ReiPan_MutexLock(ReiPan_Mutex *m)
{
    if (m != 0 && m->sdl != 0)
    {
        SDL_LockMutex(m->sdl);
    }
}

void ReiPan_MutexUnlock(ReiPan_Mutex *m)
{
    if (m != 0 && m->sdl != 0)
    {
        SDL_UnlockMutex(m->sdl);
    }
}

ReiPan_Tls *ReiPan_TlsCreate(void)
{
    ReiPan_Tls *k = (ReiPan_Tls *)malloc(sizeof(ReiPan_Tls));
    if (k == 0)
    {
        return 0;
    }
    // SDL_TLSID is zero-initialised until first use; SDL lazily assigns it.
    SDL_TLSID zero = {0};
    k->id = zero;
    return k;
}

void ReiPan_TlsSet(ReiPan_Tls *key, void *value)
{
    if (key != 0)
    {
        SDL_SetTLS(&key->id, value, 0);
    }
}

void *ReiPan_TlsGet(ReiPan_Tls *key)
{
    if (key == 0)
    {
        return 0;
    }
    return SDL_GetTLS(&key->id);
}

}
