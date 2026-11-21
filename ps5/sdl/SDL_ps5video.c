/*
 * SDL3's video driver for the PlayStation 5.
 *
 * Based on PS5_Dusklight's ps5/sdl/SDL_ps5video.c (griches/PS5_Dusklight,
 * commit 72d6885, Zlib). ReiPan adds Vulkan: the driver is PS5_Vulkan's
 * RADV, linked into the title, and the window's surface is VK_KHR_display on
 * the console's VideoOut, so SDL_GPU's Vulkan backend can render to it.
 *
 * The console has one output and no windows: SDL gets one display and keeps
 * its window bookkeeping for a window that is the whole screen.
 *
 * SDL_PS5_DISPLAY=WIDTHxHEIGHT in the environment sets the size (default
 * 1920x1080); the display mode closest to it, at the refresh rate closest to
 * 60 Hz, is the one the swapchain presents with.
 *
 * SPDX-License-Identifier: Zlib
 */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include "video/SDL_sysvideo.h"
#include "events/SDL_events_c.h"

#ifdef SDL_VIDEO_VULKAN
#include "video/SDL_vulkan_internal.h"

/* RADV's entry point, linked statically (PS5_Vulkan's libvulkan_radeon.ps5.a):
 * there is no Vulkan loader on the console. */
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
#endif

static int ps5_width = 1920;
static int ps5_height = 1080;

static bool PS5_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    const char *size = SDL_getenv("SDL_PS5_DISPLAY");
    int w, h;

    if (size && SDL_sscanf(size, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        ps5_width = w;
        ps5_height = h;
    }
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = ps5_width;
    mode.h = ps5_height;
    mode.refresh_rate = 60.0f;
    return SDL_AddBasicVideoDisplay(&mode) != 0;
}

static void PS5_VideoQuit(SDL_VideoDevice *_this)
{
}

static void PS5_PumpEvents(SDL_VideoDevice *_this)
{
}

static bool PS5_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    /* The window is the screen, and always has the pad's focus */
    SDL_SetKeyboardFocus(window);
    return true;
}

static bool PS5_SetWindowPosition(SDL_VideoDevice *_this, SDL_Window *window)
{
    return true;
}

static void PS5_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    /* The size stays the display's */
}

static SDL_FullscreenResult PS5_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    return SDL_FULLSCREEN_SUCCEEDED;
}

#ifdef SDL_VIDEO_VULKAN

static bool PS5_Vulkan_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr;

    if (path && *path) {
        return SDL_SetError("Vulkan is linked into the title: there is no library to load");
    }
    _this->vulkan_config.vkGetInstanceProcAddr = (SDL_FunctionPointer)get_proc;
    _this->vulkan_config.vkEnumerateInstanceExtensionProperties =
        (SDL_FunctionPointer)get_proc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!_this->vulkan_config.vkEnumerateInstanceExtensionProperties) {
        return SDL_SetError("RADV has no vkEnumerateInstanceExtensionProperties");
    }
    SDL_strlcpy(_this->vulkan_config.loader_path, "", SDL_arraysize(_this->vulkan_config.loader_path));
    return true;
}

static void PS5_Vulkan_UnloadLibrary(SDL_VideoDevice *_this)
{
}

static char const *const *PS5_Vulkan_GetInstanceExtensions(SDL_VideoDevice *_this, Uint32 *count)
{
    static const char *const extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME,
    };
    if (count) {
        *count = SDL_arraysize(extensions);
    }
    return extensions;
}

/* How far a mode is from the size asked for, then from 60 Hz: smaller is better */
static Uint64 PS5_ModeDistance(const VkDisplayModeParametersKHR *mode, int w, int h)
{
    const Sint64 dw = (Sint64)mode->visibleRegion.width - w;
    const Sint64 dh = (Sint64)mode->visibleRegion.height - h;
    const Sint64 dr = (Sint64)mode->refreshRate - 60000; // millihertz
    return ((Uint64)(dw * dw + dh * dh) << 24) + (Uint64)(dr < 0 ? -dr : dr);
}

static bool PS5_Vulkan_CreateSurface(SDL_VideoDevice *_this, SDL_Window *window, VkInstance instance,
                                     const struct VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)_this->vulkan_config.vkGetInstanceProcAddr;
#define PS5_VK_FUNCTION(name) PFN_##name name = (PFN_##name)get_proc(instance, #name)
    PS5_VK_FUNCTION(vkEnumeratePhysicalDevices);
    PS5_VK_FUNCTION(vkGetPhysicalDeviceDisplayPropertiesKHR);
    PS5_VK_FUNCTION(vkGetDisplayModePropertiesKHR);
    PS5_VK_FUNCTION(vkGetDisplayPlaneCapabilitiesKHR);
    PS5_VK_FUNCTION(vkCreateDisplayPlaneSurfaceKHR);
#undef PS5_VK_FUNCTION
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t count = 1;
    VkDisplayPropertiesKHR display;
    VkDisplayModePropertiesKHR *modes = NULL;
    uint32_t mode_count = 0, best = 0, i;
    VkDisplayPlaneCapabilitiesKHR caps;
    VkDisplaySurfaceCreateInfoKHR info;
    VkResult rc;

    if (!get_proc) {
        return SDL_SetError("Vulkan is not loaded");
    }
    if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceDisplayPropertiesKHR ||
        !vkGetDisplayModePropertiesKHR || !vkGetDisplayPlaneCapabilitiesKHR ||
        !vkCreateDisplayPlaneSurfaceKHR) {
        return SDL_SetError(VK_KHR_DISPLAY_EXTENSION_NAME " is not enabled in the Vulkan instance");
    }

    /* One GPU, one output */
    rc = vkEnumeratePhysicalDevices(instance, &count, &physical);
    if ((rc != VK_SUCCESS && rc != VK_INCOMPLETE) || count == 0) {
        return SDL_SetError("vkEnumeratePhysicalDevices: %s", SDL_Vulkan_GetResultString(rc));
    }
    count = 1;
    rc = vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &count, &display);
    if ((rc != VK_SUCCESS && rc != VK_INCOMPLETE) || count == 0) {
        return SDL_SetError("vkGetPhysicalDeviceDisplayPropertiesKHR: %s", SDL_Vulkan_GetResultString(rc));
    }

    rc = vkGetDisplayModePropertiesKHR(physical, display.display, &mode_count, NULL);
    if (rc != VK_SUCCESS || mode_count == 0) {
        return SDL_SetError("vkGetDisplayModePropertiesKHR: %s", SDL_Vulkan_GetResultString(rc));
    }
    modes = (VkDisplayModePropertiesKHR *)SDL_malloc(sizeof(*modes) * mode_count);
    if (!modes) {
        return false;
    }
    rc = vkGetDisplayModePropertiesKHR(physical, display.display, &mode_count, modes);
    if ((rc != VK_SUCCESS && rc != VK_INCOMPLETE) || mode_count == 0) {
        SDL_free(modes);
        return SDL_SetError("vkGetDisplayModePropertiesKHR: %s", SDL_Vulkan_GetResultString(rc));
    }
    for (i = 1; i < mode_count; i++) {
        if (PS5_ModeDistance(&modes[i].parameters, ps5_width, ps5_height) <
            PS5_ModeDistance(&modes[best].parameters, ps5_width, ps5_height)) {
            best = i;
        }
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "PS5: %u display modes, presenting at %ux%u %.2f Hz", mode_count,
                modes[best].parameters.visibleRegion.width, modes[best].parameters.visibleRegion.height,
                modes[best].parameters.refreshRate / 1000.0);

    SDL_zero(caps);
    rc = vkGetDisplayPlaneCapabilitiesKHR(physical, modes[best].displayMode, 0, &caps);
    if (rc != VK_SUCCESS) {
        SDL_zero(caps);
    }

    SDL_zero(info);
    info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    info.displayMode = modes[best].displayMode;
    info.planeIndex = 0;
    info.planeStackIndex = 0;
    info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    info.globalAlpha = 1.0f;
    info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedAlpha && !(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)) {
        info.alphaMode = (VkDisplayPlaneAlphaFlagBitsKHR)(caps.supportedAlpha & (~caps.supportedAlpha + 1));
    }
    info.imageExtent = modes[best].parameters.visibleRegion;
    SDL_free(modes);

    rc = vkCreateDisplayPlaneSurfaceKHR(instance, &info, allocator, surface);
    if (rc != VK_SUCCESS) {
        return SDL_SetError("vkCreateDisplayPlaneSurfaceKHR: %s", SDL_Vulkan_GetResultString(rc));
    }
    return true;
}

static void PS5_Vulkan_DestroySurface(SDL_VideoDevice *_this, VkInstance instance, VkSurfaceKHR surface,
                                      const struct VkAllocationCallbacks *allocator)
{
    if (_this->vulkan_config.vkGetInstanceProcAddr) {
        SDL_Vulkan_DestroySurface_Internal(_this->vulkan_config.vkGetInstanceProcAddr, instance, surface, allocator);
    }
}

#endif // SDL_VIDEO_VULKAN

static void PS5_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PS5_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }
    device->VideoInit = PS5_VideoInit;
    device->VideoQuit = PS5_VideoQuit;
    device->PumpEvents = PS5_PumpEvents;
    device->CreateSDLWindow = PS5_CreateWindow;
    device->SetWindowPosition = PS5_SetWindowPosition;
    device->SetWindowSize = PS5_SetWindowSize;
    device->SetWindowFullscreen = PS5_SetWindowFullscreen;
#ifdef SDL_VIDEO_VULKAN
    device->Vulkan_LoadLibrary = PS5_Vulkan_LoadLibrary;
    device->Vulkan_UnloadLibrary = PS5_Vulkan_UnloadLibrary;
    device->Vulkan_GetInstanceExtensions = PS5_Vulkan_GetInstanceExtensions;
    device->Vulkan_CreateSurface = PS5_Vulkan_CreateSurface;
    device->Vulkan_DestroySurface = PS5_Vulkan_DestroySurface;
#endif
    device->free = PS5_DeleteDevice;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap PS5_bootstrap = {
    "ps5", "PlayStation 5 video driver",
    PS5_CreateDevice,
    NULL, // no ShowMessageBox implementation
    false
};

#endif // SDL_VIDEO_DRIVER_PS5
