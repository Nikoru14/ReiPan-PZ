/* ==========================================================================
 *  reipan_xbox_motors.cpp  (an Xbox pad's motors, through Windows.Gaming.Input)
 *
 *  See reipan_xbox_motors.h.  Windows only.
 * ======================================================================== */

#include "reipan_xbox_motors.h"

#if defined(_WIN32)

#include <stdint.h>

/* Declared by hand rather than through <windows.h> and the SDK's WinRT headers:
 * <windows.h> collides with the EE kernel shim every file here is compiled with
 * (iop_host.cpp has the details), and the WinRT headers of MSVC's SDK and of
 * MinGW spell the same interfaces differently.  Only the methods called are
 * given their real arguments; the ones before them hold their place. */

extern "C" __declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
extern "C" __declspec(dllimport) void *__stdcall GetProcAddress(void *module,
                                                                const char *name);

/* Not an anonymous namespace: no class here derives from these interfaces, and
 * of types nothing outside the file can see GCC concludes there are no objects
 * and drops the calls (-O2). */
namespace ReiPanGamingInput
{

typedef int32_t Hresult;        /* negative: failed */

struct Guid
{
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t  d[8];
};

struct Unknown
{
    virtual Hresult  __stdcall QueryInterface(const Guid &iid, void **out) = 0;
    virtual uint32_t __stdcall AddRef() = 0;
    virtual uint32_t __stdcall Release() = 0;
};

struct Inspectable : Unknown
{
    virtual Hresult __stdcall GetIids(uint32_t *count, Guid **iids) = 0;
    virtual Hresult __stdcall GetRuntimeClassName(void **name) = 0;
    virtual Hresult __stdcall GetTrustLevel(int32_t *level) = 0;
};

/* Windows.Gaming.Input.IRawGameController: any controller, with its USB ids. */
struct RawController : Inspectable
{
    virtual Hresult __stdcall get_AxisCount(int32_t *value) = 0;
    virtual Hresult __stdcall get_ButtonCount(int32_t *value) = 0;
    virtual Hresult __stdcall get_ForceFeedbackMotors(void **value) = 0;
    virtual Hresult __stdcall get_HardwareProductId(uint16_t *value) = 0;
    virtual Hresult __stdcall get_HardwareVendorId(uint16_t *value) = 0;
};

/* Windows.Foundation.Collections.IVectorView<RawGameController>. */
struct RawControllerList : Inspectable
{
    virtual Hresult __stdcall GetAt(uint32_t index, RawController **item) = 0;
    virtual Hresult __stdcall get_Size(uint32_t *size) = 0;
};

/* Windows.Gaming.Input.IRawGameControllerStatics. */
struct RawControllerStatics : Inspectable
{
    virtual Hresult __stdcall add_Added(void *handler, int64_t *token) = 0;
    virtual Hresult __stdcall remove_Added(int64_t token) = 0;
    virtual Hresult __stdcall add_Removed(void *handler, int64_t *token) = 0;
    virtual Hresult __stdcall remove_Removed(int64_t token) = 0;
    virtual Hresult __stdcall get_RawGameControllers(RawControllerList **list) = 0;
};

struct Vibration
{
    double left_motor;
    double right_motor;
    double left_trigger;
    double right_trigger;
};

struct Reading
{
    uint64_t timestamp;
    uint32_t buttons;
    double   left_trigger;
    double   right_trigger;
    double   left_x;
    double   left_y;
    double   right_x;
    double   right_y;
};

/* Windows.Gaming.Input.IGamepad: the same controller as a gamepad. */
struct Gamepad : Inspectable
{
    virtual Hresult __stdcall get_Vibration(Vibration *value) = 0;
    virtual Hresult __stdcall put_Vibration(Vibration value) = 0;
    virtual Hresult __stdcall GetCurrentReading(Reading *value) = 0;
};

/* Windows.Gaming.Input.IGamepadStatics2. */
struct GamepadStatics : Inspectable
{
    virtual Hresult __stdcall FromGameController(Unknown *controller,
                                                 Gamepad **gamepad) = 0;
};

} // namespace ReiPanGamingInput

namespace
{

using namespace ReiPanGamingInput;

constexpr Guid kIidRawControllerStatics = {
    0xeb8d0792, 0xe95a, 0x4b19, {0xaf, 0xc7, 0x0a, 0x59, 0xf8, 0xbf, 0x75, 0x9e}};
constexpr Guid kIidGameController = {
    0x1baf6522, 0x5f64, 0x42c5, {0x82, 0x67, 0xb9, 0xfe, 0x22, 0x15, 0xbf, 0xbd}};
constexpr Guid kIidGamepadStatics = {
    0x42676dc5, 0x0856, 0x47c4, {0x92, 0x13, 0xb3, 0x95, 0x50, 0x4c, 0x3a, 0x3c}};

/* A WinRT string over a literal; `header` is HSTRING_HEADER, which the string
 * points into. */
struct StringHeader
{
    void *reserved[4];
};

typedef Hresult(__stdcall *RoGetActivationFactoryFn)(void *name, const Guid &iid,
                                                     void **factory);
typedef Hresult(__stdcall *WindowsCreateStringReferenceFn)(const wchar_t *text,
                                                           uint32_t length,
                                                           StringHeader *header,
                                                           void **string);
typedef Hresult(__stdcall *CoIncrementMTAUsageFn)(void **cookie);

constexpr int      kMaxPads = 8;
constexpr unsigned kButtonMask = 0x3fffu;       /* REIPAN_XBOX_BTN_*; not the paddles */

/* Loaded once and kept: 0 not tried, 1 there, -1 not on this Windows. */
int                            g_state;
RoGetActivationFactoryFn       g_get_factory;
WindowsCreateStringReferenceFn g_make_string;
RawControllerStatics          *g_controllers;
GamepadStatics                *g_gamepads;

template <size_t N>
void *Factory(const wchar_t (&name)[N], const Guid &iid)
{
    StringHeader header = {};
    void        *string = nullptr;
    void        *factory = nullptr;

    if (g_make_string(name, (uint32_t)(N - 1), &header, &string) < 0 ||
        g_get_factory(string, iid, &factory) < 0)
    {
        return nullptr;
    }
    return factory;
}

bool Load()
{
    if (g_state != 0)
    {
        return g_state > 0;
    }
    g_state = -1;

    void *combase = LoadLibraryA("combase.dll");
    if (combase == nullptr)
    {
        return false;
    }
    g_get_factory =
        (RoGetActivationFactoryFn)GetProcAddress(combase, "RoGetActivationFactory");
    g_make_string = (WindowsCreateStringReferenceFn)GetProcAddress(
        combase, "WindowsCreateStringReference");
    CoIncrementMTAUsageFn keep_mta =
        (CoIncrementMTAUsageFn)GetProcAddress(combase, "CoIncrementMTAUsage");
    if (g_get_factory == nullptr || g_make_string == nullptr || keep_mta == nullptr)
    {
        return false;
    }

    /* Puts every thread that has not chosen otherwise into COM's shared
     * apartment, and keeps the DLLs under Windows.Gaming.Input loaded: let go
     * of, they can unload before it and crash its teardown at exit (SDL issue
     * 5552).  Never given back. */
    void *cookie = nullptr;
    if (keep_mta(&cookie) < 0)
    {
        return false;
    }

    g_controllers = (RawControllerStatics *)Factory(
        L"Windows.Gaming.Input.RawGameController", kIidRawControllerStatics);
    if (g_controllers == nullptr)
    {
        return false;
    }
    g_state = 1;
    return true;
}

/* The controller as a gamepad, or null when Windows does not know it as one. */
Gamepad *GamepadOf(RawController *controller)
{
    /* Not before an Xbox pad needs it: bringing this class up makes Windows put
     * a DualShock 4 on Bluetooth into its extended mode. */
    if (g_gamepads == nullptr)
    {
        g_gamepads = (GamepadStatics *)Factory(L"Windows.Gaming.Input.Gamepad",
                                               kIidGamepadStatics);
        if (g_gamepads == nullptr)
        {
            return nullptr;
        }
    }

    void    *game_controller = nullptr;
    Gamepad *gamepad = nullptr;
    if (controller->QueryInterface(kIidGameController, &game_controller) < 0 ||
        game_controller == nullptr)
    {
        return nullptr;
    }
    if (g_gamepads->FromGameController((Unknown *)game_controller, &gamepad) < 0)
    {
        gamepad = nullptr;
    }
    ((Unknown *)game_controller)->Release();
    return gamepad;
}

/* Whether a pad of this vendor's may be an Xbox pad under another id. */
bool MayBeXbox(uint16_t vendor)
{
    return vendor != 0x054c &&      /* Sony */
           vendor != 0x057e &&      /* Nintendo */
           vendor != 0x28de;        /* Valve */
}

unsigned ButtonsHeld(Gamepad *gamepad)
{
    Reading reading = {};
    return gamepad->GetCurrentReading(&reading) < 0
               ? 0u
               : (unsigned)reading.buttons & kButtonMask;
}

} // namespace

extern "C" {

int ReiPan_XboxMotorsSupported(void)
{
    return Load() ? 1 : 0;
}

ReiPan_XboxMotors *ReiPan_XboxMotorsFind(unsigned vendor, unsigned product,
                                         unsigned buttons)
{
    RawControllerList *list = nullptr;
    if (!Load() || g_controllers->get_RawGameControllers(&list) < 0 ||
        list == nullptr)
    {
        return nullptr;
    }

    /* The pads with the ids asked for, and the rest that could be it. */
    Gamepad *same[kMaxPads];
    Gamepad *other[kMaxPads];
    int      same_count = 0;
    int      other_count = 0;

    uint32_t count = 0;
    list->get_Size(&count);
    for (uint32_t i = 0; i < count; i++)
    {
        RawController *controller = nullptr;
        if (list->GetAt(i, &controller) < 0 || controller == nullptr)
        {
            continue;
        }

        uint16_t vid = 0;
        uint16_t pid = 0;
        controller->get_HardwareVendorId(&vid);
        controller->get_HardwareProductId(&pid);
        const bool is_same = vid == vendor && pid == product;
        if (is_same ? same_count < kMaxPads
                    : MayBeXbox(vid) && other_count < kMaxPads)
        {
            Gamepad *gamepad = GamepadOf(controller);
            if (gamepad != nullptr)
            {
                if (is_same)
                {
                    same[same_count++] = gamepad;
                }
                else
                {
                    other[other_count++] = gamepad;
                }
            }
        }
        controller->Release();
    }
    list->Release();

    Gamepad *found = nullptr;
    if (same_count == 1)
    {
        found = same[0];
    }
    else if (buttons != 0)
    {
        Gamepad **pads = same_count != 0 ? same : other;
        const int pad_count = same_count != 0 ? same_count : other_count;
        int       matches = 0;
        for (int i = 0; i < pad_count; i++)
        {
            if (ButtonsHeld(pads[i]) == buttons)
            {
                matches++;
                found = pads[i];
            }
        }
        if (matches != 1)
        {
            found = nullptr;
        }
    }

    for (int i = 0; i < same_count; i++)
    {
        if (same[i] != found)
        {
            same[i]->Release();
        }
    }
    for (int i = 0; i < other_count; i++)
    {
        if (other[i] != found)
        {
            other[i]->Release();
        }
    }
    return (ReiPan_XboxMotors *)found;
}

int ReiPan_XboxMotorsSet(ReiPan_XboxMotors *motors, float low, float high,
                         float left_trigger, float right_trigger)
{
    if (motors == nullptr)
    {
        return 0;
    }
    const Vibration v = {low, high, left_trigger, right_trigger};
    return ((Gamepad *)motors)->put_Vibration(v) >= 0 ? 1 : 0;
}

void ReiPan_XboxMotorsRelease(ReiPan_XboxMotors *motors)
{
    if (motors != nullptr)
    {
        ((Gamepad *)motors)->Release();
    }
}

}

#else /* !_WIN32 */

extern "C" {

int ReiPan_XboxMotorsSupported(void)
{
    return 0;
}

ReiPan_XboxMotors *ReiPan_XboxMotorsFind(unsigned vendor, unsigned product,
                                         unsigned buttons)
{
    (void)vendor;
    (void)product;
    (void)buttons;
    return 0;
}

int ReiPan_XboxMotorsSet(ReiPan_XboxMotors *motors, float low, float high,
                         float left_trigger, float right_trigger)
{
    (void)motors;
    (void)low;
    (void)high;
    (void)left_trigger;
    (void)right_trigger;
    return 0;
}

void ReiPan_XboxMotorsRelease(ReiPan_XboxMotors *motors)
{
    (void)motors;
}

}

#endif
