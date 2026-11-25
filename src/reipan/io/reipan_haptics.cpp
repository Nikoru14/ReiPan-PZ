/* ==========================================================================
 *  reipan_haptics.cpp  (the controller's motors)
 *
 *  See reipan_haptics.h.  The game and the menus call in on the game thread;
 *  the timer that plays the cues runs on SDL's.  One lock covers everything
 *  here.  SDL's own joystick lock is taken under it and never the other way
 *  round, which holds because nothing SDL calls back reaches this file.
 * ======================================================================== */

#include "reipan_haptics.h"

#include "reipan_xbox_motors.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <math.h>
#include <stdlib.h>

namespace
{

/* The game sets its motors every frame, 33 or 40 ms apart; they stop this long
 * after the last one that did, so a load or a pause does not leave them on. */
constexpr Uint32 kMotorsMs = 120;

constexpr Uint32 kTickMs = 16;          /* the timer, while something plays */
constexpr Uint32 kSearchMs = 250;       /* ...and while looking for an Xbox pad */

/* A kept cue ends this long after it was last raised -- two frames and some, so
 * one late frame does not cut it -- and then fades. */
constexpr Uint64 kKeptMs = 100;
constexpr Uint64 kReleaseMs = 80;

constexpr Uint64 kPreviewMs = 1200;     /* a kept cue, from the settings */
constexpr Uint64 kTestStepMs = 450;
constexpr Uint64 kTestOnMs = 300;
constexpr float  kTestLevel = 0.8f;

constexpr ReiPan_HapticsSettings kDefaults = {1.0f, 1, 1.0f, 0};

struct Motors
{
    float low;                          /* the big one, left grip */
    float high;                         /* the small one, right grip */
    float left;                         /* the triggers */
    float right;
};

bool Same(const Motors &a, const Motors &b)
{
    return a.low == b.low && a.high == b.high && a.left == b.left &&
           a.right == b.right;
}

bool Any(const Motors &m)
{
    return m.low > 0.0f || m.high > 0.0f || m.left > 0.0f || m.right > 0.0f;
}

/* ---------------------------------------------------------------------------
 *  How each cue feels: the trigger motors `t` milliseconds in, at strength `s`.
 *
 *  These are small motors that take some 30 ms to come up and say nothing below
 *  about a tenth, so a beat is a block with a short tail rather than a curve,
 *  and nothing that lasts goes far past half: two of them at full are loud.
 * ------------------------------------------------------------------------ */

/* 1 from `at` for `length`, falling away over the last of it. */
float Pulse(float t, float at, float length)
{
    t -= at;
    if (t < 0.0f || t >= length)
    {
        return 0.0f;
    }
    const float tail = length * 0.4f;
    return t < length - tail ? 1.0f : (length - t) / tail;
}

/* 1 for `hold`, then dying away. */
float Decay(float t, float hold, float tau)
{
    return t < hold ? 1.0f : expf((hold - t) / tau);
}

float Sway(float t, float period)
{
    return sinf(t * (2.0f * SDL_PI_F / period));
}

float Hash(Uint32 n)
{
    n = (n ^ 61u) ^ (n >> 16);
    n *= 9u;
    n ^= n >> 4;
    n *= 0x27d4eb2du;
    n ^= n >> 15;
    return (float)(n & 0xffffu) / 65535.0f;
}

/* Smooth noise, 0..1, a new value every `step`. */
float Noise(float t, float step, Uint32 seed)
{
    const float  x = t / step;
    const Uint32 i = (Uint32)x;
    const float  f = x - (float)i;
    return Hash(seed + i) * (1.0f - f) + Hash(seed + i + 1u) * f;
}

/* Lub, dub. */
void PlayHeartbeat(float t, float s, Motors *m)
{
    m->left = m->right =
        s * fmaxf(0.65f * Pulse(t, 0.0f, 90.0f), 0.45f * Pulse(t, 190.0f, 100.0f));
}

void PlayHit(float t, float s, Motors *m)
{
    m->left = m->right = s * 0.95f * Decay(t, 70.0f, 130.0f);
}

/* A struggle: from one hand to the other and back, seven times a second. */
void PlayHeld(float t, float s, Motors *m)
{
    const float sway = Sway(t, 143.0f);
    m->left = s * (0.35f + 0.2f * sway);
    m->right = s * (0.35f - 0.2f * sway);
}

void PlayStricken(float t, float s, Motors *m)
{
    m->left = m->right = s * (0.4f + 0.15f * Sway(t, 90.0f));
}

/* The right hand took the picture. */
void PlayShot(float t, float s, Motors *m)
{
    m->right = s * Decay(t, 90.0f, 150.0f);
    m->left = 0.55f * m->right;
}

void PlayThunder(float t, float s, Motors *m)
{
    m->left = s * (0.15f + 0.35f * Noise(t, 70.0f, 17u));
    m->right = s * (0.15f + 0.35f * Noise(t, 70.0f, 4211u));
}

void PlayScene(float t, float s, Motors *m)
{
    (void)t;
    m->left = m->right = 0.5f * s;
}

void PlayShutter(float t, float s, Motors *m)
{
    m->right = (0.35f + 0.45f * s) * Pulse(t, 0.0f, 55.0f);
}

void PlayCharge(float t, float s, Motors *m)
{
    m->right = (0.2f + 0.15f * s) * Pulse(t, 0.0f, 45.0f);
}

/* Two ticks; in both hands for a fatal frame. */
void PlayChance(float t, float s, Motors *m)
{
    m->right = s * 0.8f * fmaxf(Pulse(t, 0.0f, 55.0f), Pulse(t, 105.0f, 55.0f));
    m->left = s >= 1.0f ? m->right : 0.0f;
}

struct CueSpec
{
    const char *name;
    bool        kept;
    float       length;                 /* of one that plays through */
    void (*play)(float t, float s, Motors *m);
};

const CueSpec kCues[] = {
    {"Heartbeat", false, 290.0f, PlayHeartbeat},
    {"Hit", false, 520.0f, PlayHit},
    {"In a ghost's grip", true, 0.0f, PlayHeld},
    {"Shaking it off", true, 0.0f, PlayStricken},
    {"Heavy hit on a ghost", false, 620.0f, PlayShot},
    {"Thunder", true, 0.0f, PlayThunder},
    {"Cutscene", true, 0.0f, PlayScene},
    {"Shutter", false, 55.0f, PlayShutter},
    {"Charge step", false, 45.0f, PlayCharge},
    {"Shutter chance", false, 160.0f, PlayChance},
};
static_assert(sizeof(kCues) / sizeof(kCues[0]) == REIPAN_CUE_COUNT,
              "one entry per REIPAN_CUE_*");

struct Voice
{
    bool   on;
    float  strength;
    Uint64 start;
    Uint64 last;                        /* raised last */
    Uint64 preview_until;
};

SDL_Mutex *g_lock;

ReiPan_HapticsSettings g_set = kDefaults;

SDL_Gamepad *g_pad;
Uint32       g_pad_serial;              /* counts pads, for a search that ends late */
int          g_triggers;                /* REIPAN_TRIGGERS_* */

ReiPan_XboxMotors *g_xbox;
Uint64             g_search_next;

Motors g_game;                          /* the game's two, until g_game_until */
Uint64 g_game_until;
Voice  g_voice[REIPAN_CUE_COUNT];
Uint64 g_test_start;                    /* 0: none running */

Motors g_out;                           /* what the pad was last told */

SDL_TimerID g_timer;
Uint32      g_timer_ms;
bool        g_closed;                   /* the program is leaving */

/* The first call is the game thread's, opening a pad or loading the settings;
 * the timer does not exist before it. */
void Lock()
{
    if (g_lock == nullptr)
    {
        g_lock = SDL_CreateMutex();
    }
    SDL_LockMutex(g_lock);
}

void Unlock()
{
    SDL_UnlockMutex(g_lock);
}

float Clamp01(float value)
{
    /* Written so a NaN from a hand-edited file lands on the floor too. */
    return value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
}

/* In hundredths, the steps an Xbox pad's motors have: a fading motor is only
 * told when it would notice. */
float Hundredths(float value)
{
    return floorf(Clamp01(value) * 100.0f + 0.5f) / 100.0f;
}

Uint16 Motor16(float value)
{
    return (Uint16)(Clamp01(value) * 65535.0f + 0.5f);
}

bool TriggersReached()
{
    return g_triggers == REIPAN_TRIGGERS_SDL ||
           g_triggers == REIPAN_TRIGGERS_WINDOWS;
}

/* How loud a voice still is, 0 once it is over. */
float VoiceLevel(const CueSpec &cue, const Voice &v, Uint64 now)
{
    if (!cue.kept)
    {
        return (float)(now - v.start) < cue.length ? 1.0f : 0.0f;
    }
    const Uint64 end = SDL_max(v.last + kKeptMs, v.preview_until);
    if (now < end)
    {
        return 1.0f;
    }
    return now - end < kReleaseMs
               ? 1.0f - (float)(now - end) / (float)kReleaseMs
               : 0.0f;
}

/* The settings' test: each motor in turn. */
void MixTest(Uint64 now, Motors *out)
{
    if (g_test_start == 0)
    {
        return;
    }
    const Uint64 t = now - g_test_start;
    const Uint64 step = t / kTestStepMs;
    if (step >= (TriggersReached() ? 4u : 2u))
    {
        g_test_start = 0;
        return;
    }
    if (t % kTestStepMs < kTestOnMs)
    {
        float *motor = step == 0   ? &out->low
                       : step == 1 ? &out->high
                       : step == 2 ? &out->left
                                   : &out->right;
        *motor = fmaxf(*motor, kTestLevel);
    }
}

/* Every motor now, as the pad is to be told. */
Motors Mix(Uint64 now)
{
    Motors out = {};

    if (now < g_game_until)
    {
        out.low = g_game.low;
        out.high = g_game.high;
    }

    for (int i = 0; i < REIPAN_CUE_COUNT; i++)
    {
        Voice &v = g_voice[i];
        if (!v.on)
        {
            continue;
        }
        const float level = VoiceLevel(kCues[i], v, now);
        if (level <= 0.0f)
        {
            v.on = false;
            continue;
        }
        Motors m = {};
        kCues[i].play((float)(now - v.start), v.strength, &m);
        out.left = fmaxf(out.left, m.left * level);
        out.right = fmaxf(out.right, m.right * level);
    }

    MixTest(now, &out);

    out.low *= g_set.strength;
    out.high *= g_set.strength;
    if (TriggersReached())
    {
        out.left = Hundredths(out.left * g_set.strength * g_set.trigger_strength);
        out.right = Hundredths(out.right * g_set.strength * g_set.trigger_strength);
    }
    else
    {
        out.left = 0.0f;
        out.right = 0.0f;
    }
    return out;
}

void ReleaseXbox()
{
    ReiPan_XboxMotorsRelease(g_xbox);
    g_xbox = nullptr;
}

/* Run the timer at least this often; below, with the timer. */
void Wake(Uint32 ms);

void Send(const Motors &want)
{
    if (g_pad == nullptr)
    {
        g_out = Motors{};
        return;
    }

    if (g_triggers == REIPAN_TRIGGERS_WINDOWS)
    {
        if (Same(want, g_out))
        {
            return;
        }
        if (ReiPan_XboxMotorsSet(g_xbox, want.low, want.high, want.left,
                                 want.right))
        {
            g_out = want;
            return;
        }
        /* Windows has lost the pad.  SDL may not have: the grips go back to it,
         * and the search starts over. */
        ReleaseXbox();
        g_triggers = REIPAN_TRIGGERS_SEARCHING;
        g_search_next = 0;
        g_out = Motors{};
        Wake(kTickMs);
    }

    /* SDL only writes to the pad when a value changes; the same one again moves
     * the time it would stop the motors by itself. */
    const bool grips = want.low > 0.0f || want.high > 0.0f;
    if (grips || g_out.low > 0.0f || g_out.high > 0.0f)
    {
        SDL_RumbleGamepad(g_pad, Motor16(want.low), Motor16(want.high),
                          grips ? kMotorsMs : 0);
    }
    if (g_triggers == REIPAN_TRIGGERS_SDL)
    {
        const bool triggers = want.left > 0.0f || want.right > 0.0f;
        if (triggers || g_out.left > 0.0f || g_out.right > 0.0f)
        {
            SDL_RumbleGamepadTriggers(g_pad, Motor16(want.left),
                                      Motor16(want.right),
                                      triggers ? kMotorsMs : 0);
        }
    }
    g_out = want;
    if (g_triggers != REIPAN_TRIGGERS_SDL)
    {
        g_out.left = 0.0f;
        g_out.right = 0.0f;
    }
}

void Apply(Uint64 now)
{
    Send(Mix(now));
}

bool Busy(Uint64 now)
{
    if (Any(g_out) || g_test_start != 0 ||
        (now < g_game_until && Any(g_game)))
    {
        return true;
    }
    for (int i = 0; i < REIPAN_CUE_COUNT; i++)
    {
        if (g_voice[i].on)
        {
            return true;
        }
    }
    return false;
}

void Silence()
{
    for (int i = 0; i < REIPAN_CUE_COUNT; i++)
    {
        g_voice[i].on = false;
    }
    g_test_start = 0;
}

/* ---------------------------------------------------------------------------
 *  The timer.
 * ------------------------------------------------------------------------ */

Uint32 SDLCALL Tick(void *userdata, SDL_TimerID id, Uint32 interval);

void Wake(Uint32 ms)
{
    if (g_closed || (g_timer != 0 && g_timer_ms <= ms))
    {
        return;
    }
    if (g_timer != 0)
    {
        /* It may be on its way in, waiting for the lock: it sees it is no
         * longer g_timer and goes. */
        SDL_RemoveTimer(g_timer);
    }
    g_timer = SDL_AddTimer(ms, Tick, nullptr);
    g_timer_ms = ms;
}

/* What the pad is held on, as Windows.Gaming.Input would say it. */
unsigned ButtonsHeld(SDL_Gamepad *pad)
{
    static const struct
    {
        SDL_GamepadButton button;
        unsigned          bit;
    } kButtons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, REIPAN_XBOX_BTN_A},
        {SDL_GAMEPAD_BUTTON_EAST, REIPAN_XBOX_BTN_B},
        {SDL_GAMEPAD_BUTTON_WEST, REIPAN_XBOX_BTN_X},
        {SDL_GAMEPAD_BUTTON_NORTH, REIPAN_XBOX_BTN_Y},
        {SDL_GAMEPAD_BUTTON_START, REIPAN_XBOX_BTN_MENU},
        {SDL_GAMEPAD_BUTTON_BACK, REIPAN_XBOX_BTN_VIEW},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, REIPAN_XBOX_BTN_LB},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, REIPAN_XBOX_BTN_RB},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, REIPAN_XBOX_BTN_LS},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, REIPAN_XBOX_BTN_RS},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, REIPAN_XBOX_BTN_UP},
        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, REIPAN_XBOX_BTN_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, REIPAN_XBOX_BTN_LEFT},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, REIPAN_XBOX_BTN_RIGHT},
    };

    unsigned held = 0;
    for (const auto &b : kButtons)
    {
        if (SDL_GetGamepadButton(pad, b.button))
        {
            held |= b.bit;
        }
    }
    return held;
}

/* Ask Windows for the open pad.  The lock is let go of meanwhile: the game does
 * not wait on it. */
void Search(Uint64 now)
{
    const Uint32   serial = g_pad_serial;
    const unsigned vendor = SDL_GetGamepadVendor(g_pad);
    const unsigned product = SDL_GetGamepadProduct(g_pad);
    const unsigned buttons = ButtonsHeld(g_pad);

    g_search_next = now + kSearchMs;

    Unlock();
    ReiPan_XboxMotors *found = ReiPan_XboxMotorsFind(vendor, product, buttons);
    Lock();

    if (found == nullptr)
    {
        return;
    }
    if (serial != g_pad_serial || g_triggers != REIPAN_TRIGGERS_SEARCHING ||
        g_closed)
    {
        ReiPan_XboxMotorsRelease(found);
        return;
    }

    /* From here on all four motors go through Windows.  SDL's way to the grips
     * sets the whole pad as well, and two writers would undo each other. */
    SDL_RumbleGamepad(g_pad, 0, 0, 0);
    g_out = Motors{};
    g_xbox = found;
    g_triggers = REIPAN_TRIGGERS_WINDOWS;
    SDL_Log("ReiPan: trigger rumble on, through Windows");
}

Uint32 SDLCALL Tick(void *userdata, SDL_TimerID id, Uint32 interval)
{
    (void)userdata;
    (void)interval;

    Lock();
    Uint64 now = SDL_GetTicks();
    if (id == g_timer && !g_closed &&
        g_triggers == REIPAN_TRIGGERS_SEARCHING && now >= g_search_next)
    {
        Search(now);
        /* A cue raised meanwhile must not lie ahead of the clock. */
        now = SDL_GetTicks();
    }
    /* Replaced, perhaps while the lock was free. */
    if (id != g_timer || g_closed)
    {
        Unlock();
        return 0;
    }

    Apply(now);

    Uint32 next = 0;
    if (Busy(now))
    {
        next = kTickMs;
    }
    else if (g_triggers == REIPAN_TRIGGERS_SEARCHING)
    {
        next = kSearchMs;
    }
    g_timer_ms = next;
    if (next == 0)
    {
        g_timer = 0;
    }
    Unlock();
    return next;
}

/* ---------------------------------------------------------------------------
 *  The pad.
 * ------------------------------------------------------------------------ */

/* How to reach the trigger motors of the open pad, if at all. */
int WantedTriggers()
{
    if (g_pad == nullptr || !g_set.triggers)
    {
        return REIPAN_TRIGGERS_NONE;
    }
    if (SDL_GetBooleanProperty(SDL_GetGamepadProperties(g_pad),
                               SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false))
    {
        return REIPAN_TRIGGERS_SDL;
    }
    /* Not behind Steam Input's back: the pad it shows is not the one in the
     * player's hands, and it shakes that one itself. */
    if (SDL_GetRealGamepadType(g_pad) == SDL_GAMEPAD_TYPE_XBOXONE &&
        SDL_GetGamepadSteamHandle(g_pad) == 0 && ReiPan_XboxMotorsSupported())
    {
        return g_xbox != nullptr ? REIPAN_TRIGGERS_WINDOWS
                                 : REIPAN_TRIGGERS_SEARCHING;
    }
    return REIPAN_TRIGGERS_NONE;
}

/* After the pad or the setting changed. */
void SyncTriggers()
{
    const int want = WantedTriggers();
    if (want == g_triggers)
    {
        return;
    }

    /* Let go of the motors the old way first. */
    Send(Motors{});
    if (want != REIPAN_TRIGGERS_WINDOWS)
    {
        ReleaseXbox();
    }
    g_triggers = want;
    Silence();

    if (want == REIPAN_TRIGGERS_SEARCHING)
    {
        g_search_next = 0;
        Wake(kTickMs);
    }
}

void Closing()
{
    Lock();
    const bool moving = Any(g_out);
    Silence();
    Send(Motors{});
    g_closed = true;
    Unlock();
    if (moving)
    {
        /* Windows passes the stop on from a thread of its own, which the end of
         * the program does not wait for. */
        SDL_Delay(30);
    }
}

void Raise(int cue, float strength, Uint64 now, Uint64 preview_until)
{
    Voice       &v = g_voice[cue];
    const float  s = Clamp01(strength);
    const bool   again = v.on && kCues[cue].kept;

    if (!again)
    {
        v.start = now;
    }
    /* Raised twice in a frame, the stronger. */
    v.strength = again && now - v.last < 10 ? fmaxf(v.strength, s) : s;
    v.last = now;
    v.preview_until = preview_until;
    v.on = true;

    Apply(now);
    Wake(kTickMs);
}

} // namespace

extern "C" {

const char *ReiPan_HapticsCueName(int cue)
{
    return cue >= 0 && cue < REIPAN_CUE_COUNT ? kCues[cue].name : "";
}

void ReiPan_HapticsDefaults(ReiPan_HapticsSettings *out)
{
    *out = kDefaults;
}

void ReiPan_HapticsGetSettings(ReiPan_HapticsSettings *out)
{
    Lock();
    *out = g_set;
    Unlock();
}

void ReiPan_HapticsSetSettings(const ReiPan_HapticsSettings *in)
{
    Lock();
    g_set.strength = Clamp01(in->strength);
    g_set.triggers = in->triggers ? 1 : 0;
    g_set.trigger_strength = Clamp01(in->trigger_strength);
    g_set.camera = in->camera ? 1 : 0;
    SyncTriggers();
    Apply(SDL_GetTicks());
    Unlock();
}

int ReiPan_HapticsSettingsAreDefault(void)
{
    Lock();
    const int same = g_set.strength == kDefaults.strength &&
                     g_set.triggers == kDefaults.triggers &&
                     g_set.trigger_strength == kDefaults.trigger_strength &&
                     g_set.camera == kDefaults.camera;
    Unlock();
    return same;
}

int ReiPan_HapticsMotors(unsigned char small_on, unsigned char big_power)
{
    Lock();
    const Uint64 now = SDL_GetTicks();
    g_game.low = (float)big_power / 255.0f;
    g_game.high = small_on != 0 ? 1.0f : 0.0f;
    g_game_until = now + kMotorsMs;
    Apply(now);
    if (TriggersReached() && Busy(now))
    {
        Wake(kTickMs);
    }
    const int pad = g_pad != nullptr;
    Unlock();
    return pad;
}

void ReiPan_HapticsCue(int cue, float strength)
{
    if (cue < 0 || cue >= REIPAN_CUE_COUNT)
    {
        return;
    }
    Lock();
    if (TriggersReached() && (cue < REIPAN_CUE_SHUTTER || g_set.camera))
    {
        Raise(cue, strength, SDL_GetTicks(), 0);
    }
    Unlock();
}

void ReiPan_HapticsTest(void)
{
    Lock();
    if (g_pad != nullptr)
    {
        Silence();
        g_test_start = SDL_GetTicks();
        Apply(g_test_start);
        Wake(kTickMs);
    }
    Unlock();
}

void ReiPan_HapticsPreview(int cue)
{
    if (cue < 0 || cue >= REIPAN_CUE_COUNT)
    {
        return;
    }
    Lock();
    if (TriggersReached())
    {
        const Uint64 now = SDL_GetTicks();
        Silence();
        Raise(cue, 1.0f, now, kCues[cue].kept ? now + kPreviewMs : 0);
    }
    Unlock();
}

void ReiPan_HapticsGetStatus(ReiPan_HapticsStatus *out)
{
    Lock();
    out->has_gamepad = g_pad != nullptr;
    out->has_rumble =
        g_pad != nullptr &&
        SDL_GetBooleanProperty(SDL_GetGamepadProperties(g_pad),
                               SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    out->triggers = g_triggers;
    out->low = g_out.low;
    out->high = g_out.high;
    out->left_trigger = g_out.left;
    out->right_trigger = g_out.right;
    Unlock();
}

}

void ReiPan_HapticsSetGamepad(SDL_Gamepad *gamepad)
{
    static bool at_exit;

    Lock();
    Silence();
    g_game = Motors{};
    Send(Motors{});
    ReleaseXbox();
    g_triggers = REIPAN_TRIGGERS_NONE;
    g_pad = gamepad;
    g_pad_serial++;
    g_out = Motors{};
    SyncTriggers();
    if (gamepad != nullptr && g_triggers == REIPAN_TRIGGERS_SDL)
    {
        SDL_Log("ReiPan: trigger rumble on, the pad's own");
    }
    Unlock();

    if (gamepad != nullptr && !at_exit)
    {
        at_exit = true;
        atexit(Closing);
    }
}
