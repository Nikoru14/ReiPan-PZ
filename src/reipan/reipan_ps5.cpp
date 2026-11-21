/* ==========================================================================
 *  reipan_ps5.cpp  (the PlayStation 5 title's own support)
 *
 *  See reipan_ps5.h.  Ported from MikuPan's src/mikupan/mikupan_ps5.cpp; the
 *  console's platform layer itself is under ps5/ (see ps5/NOTICE.md).
 * ======================================================================== */

#ifdef __PROSPERO__

#include "reipan/reipan_ps5.h"

#include "reipan/io/reipan_log.h"
#include "sdk/iop_voice.h"
#include "reipan/os/reipan_thread.h"

#include "platform.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <strings.h>
#include <system_error>
#include <unordered_map>

extern "C" int sceKernelSendNotificationRequest(int device, void *request, size_t size,
                                                int blocking);
extern "C" int sceKernelUsleep(unsigned int microseconds);
extern "C" int sceSystemServiceLoadExec(const char *path, const char *const *argv);
extern "C" void ReiPan_Ps5InstallCrashHandler(void);

namespace
{
std::atomic<unsigned> g_frames{0}; /* game frames, from ReiPan_Ps5FrameTick */
std::atomic<int> g_paused{0};      /* held for the home screen */
void TakePlatformMessages();
} // namespace

/* ------------------------------------------------------------------ files */

namespace
{
/* PS5 homebrew keeps its files under /data.  A title's sandbox may not reach
 * it, so it is only used once a real write there succeeds; otherwise ReiPan
 * falls back to the title's own storage. */
constexpr const char *kDataFolder = "/data/ReiPan/";
constexpr const char *kTitleStorageFolder = "/download0/ReiPan/";

bool FolderIsWritable(const char *folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec))
    {
        return false;
    }

    const std::string probe = std::string(folder) + ".reipan_write_test";
    FILE *file = std::fopen(probe.c_str(), "wb");
    if (file == nullptr)
    {
        return false;
    }
    const bool written = std::fputc('1', file) != EOF;
    const bool closed = std::fclose(file) == 0;
    std::remove(probe.c_str());
    return written && closed;
}

bool OpensAsFile(const char *path)
{
    FILE *file = std::fopen(path, "rb");
    if (file == nullptr)
    {
        return false;
    }
    std::fclose(file);
    return true;
}

bool OpensAsFolder(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == nullptr)
    {
        return false;
    }
    closedir(dir);
    return true;
}

std::string ResolveCaseUncached(const std::string &path)
{
    if (ReiPan_Ps5PathExists(path.c_str()))
    {
        return path;
    }
    std::string resolved = path.empty() || path[0] != '/' ? "" : "/";
    size_t start = resolved.size();
    while (start < path.size())
    {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
        {
            end = path.size();
        }
        const std::string part = path.substr(start, end - start);
        if (!part.empty())
        {
            std::string candidate = resolved + part;
            if (!ReiPan_Ps5PathExists(candidate.c_str()) && !resolved.empty())
            {
                if (DIR *dir = opendir(resolved.c_str()))
                {
                    while (struct dirent *entry = readdir(dir))
                    {
                        if (strcasecmp(entry->d_name, part.c_str()) == 0)
                        {
                            candidate = resolved + entry->d_name;
                            break;
                        }
                    }
                    closedir(dir);
                }
            }
            resolved = candidate;
            if (end < path.size())
            {
                resolved += '/';
            }
        }
        start = end + 1;
    }
    return resolved;
}
} // namespace

extern "C" const char *ReiPan_Ps5UserDir(void)
{
    static const std::string dir = [] {
        if (FolderIsWritable(kDataFolder))
        {
            return std::string(kDataFolder);
        }
        std::error_code ec;
        std::filesystem::create_directories(kTitleStorageFolder, ec);
        return std::string(kTitleStorageFolder);
    }();
    /* No logging here: the log asks for this folder while it is opened. */
    return dir.c_str();
}

extern "C" const char *ReiPan_Ps5AppDir(void)
{
    return "/app0/";
}

extern "C" int ReiPan_Ps5PathExists(const char *path)
{
    return path != nullptr && path[0] != '\0' && (OpensAsFile(path) || OpensAsFolder(path));
}

extern "C" int ReiPan_Ps5IsFile(const char *path)
{
    return path != nullptr && path[0] != '\0' && !OpensAsFolder(path) && OpensAsFile(path);
}

extern "C" int ReiPan_Ps5ResolveCase(const char *path, char *out, unsigned long out_size)
{
    if (path == nullptr || out == nullptr || out_size == 0)
    {
        return 0;
    }
    /* Each path is matched once: the folders do not change spelling while
     * the game runs, and the same names are asked for again and again. */
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> resolved;
    std::string result;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = resolved.find(path);
        if (found != resolved.end())
        {
            result = found->second;
        }
    }
    if (result.empty())
    {
        result = ResolveCaseUncached(path);
        std::lock_guard<std::mutex> lock(mutex);
        if (resolved.size() > 4096)
        {
            resolved.clear();
        }
        resolved.emplace(path, result);
    }
    if (result.size() + 1 > out_size)
    {
        out[0] = '\0';
        return 0;
    }
    std::memcpy(out, result.c_str(), result.size() + 1);
    return ReiPan_Ps5PathExists(out);
}

/* ---------------------------------------------------- notification, exit */

namespace
{
/* Layout used by the ps5-payload-sdk's notification samples. */
struct NotifyRequest
{
    char reserved[45];
    char message[3075];
};
} // namespace

extern "C" void ReiPan_Ps5Notify(const char *fmt, ...)
{
    NotifyRequest request;
    std::memset(&request, 0, sizeof(request));

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(request.message, sizeof(request.message), fmt, args);
    va_end(args);

    SDL_Log("PS5 notification: %s", request.message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

extern "C" [[noreturn]] void ReiPan_Ps5Exit(int status)
{
    if (status != 0)
    {
        ReiPan_Ps5Notify("ReiPan has stopped (status %d). See %sreipan.log", status,
                         ReiPan_Ps5UserDir());
    }
    TakePlatformMessages();
    SDL_Log("PS5: closing the title (status %d)", status);
    ReiPan_LogFlush();
    std::fflush(nullptr);
    /* The shell closes the title asynchronously: wait, and never return. */
    (void)sceSystemServiceLoadExec("exit", nullptr);
    for (;;)
    {
        sceKernelUsleep(100000);
    }
}

/* Called by the title's entry point (ps5/native/app_crt.cpp) when main
 * returns. */
extern "C" [[noreturn]] void catchReturnFromMain(int status)
{
    ReiPan_Ps5Exit(status);
}

/* Before main (and before SDL starts): standard error into klog and the
 * shell's splash screen dismissed, which otherwise covers every frame
 * (ps5/platform/platform.c). */
__attribute__((constructor)) static void ReiPan_Ps5Startup()
{
    platform_init(REIPAN_PS5_TITLE_NAME);
    /* RADV's own messages (Mesa's log) beside ReiPan's, rather than only in
     * the console's klog. */
    static std::string mesa_log;
    mesa_log = std::string(ReiPan_Ps5UserDir()) + "mesa.log";
    setenv("MESA_LOG_FILE", mesa_log.c_str(), 1);
    setenv("MESA_LOG_LEVEL", "debug", 1);
    /* RADV's compiled shaders, kept between launches with the rest of
     * ReiPan's files (its default is the title's own /app0/radv-shader-cache,
     * which is read-only). */
    static std::string shader_cache;
    shader_cache = std::string(ReiPan_Ps5UserDir()) + "shader-cache";
    setenv("MESA_SHADER_CACHE_DIR", shader_cache.c_str(), 1);
    /* Nothing reads standard output: the log is the file. */
    setenv("REIPAN_LOG_CONSOLE", "0", 0);
    ReiPan_Ps5InstallCrashHandler();
}

/* ------------------------------------------------------- home screen pause */

extern "C" int ReiPan_Ps5ShouldPause(void)
{
    /* The console marks each controller reading it keeps for itself (the
     * home screen, the control centre, a system dialog) with
     * PAD_INTERCEPTED; the latest reading says who has the controller now.
     * SDL's controller driver polls the pads while the events are pumped. */
    static bool paused = false;
    const struct pad_reading *readings = nullptr;
    const int count = pad_player_readings(0, &readings);
    bool shell_has_pad = paused;
    if (count > 0 && readings != nullptr)
    {
        shell_has_pad = (readings[count - 1].buttons & PAD_INTERCEPTED) != 0;
    }

    if (shell_has_pad != paused)
    {
        paused = shell_has_pad;
        g_paused.store(paused ? 1 : 0);
        ReiPan_VoiceSetPaused(paused ? 1 : 0);
        SDL_Log("PS5: %s (pad buttons 0x%08x)",
                paused ? "the system has the controller: game paused"
                       : "back in the game: resumed",
                count > 0 && readings != nullptr ? (unsigned)readings[count - 1].buttons : 0u);
        ReiPan_LogFlush();
    }
    if (paused)
    {
        SDL_Delay(50);
    }
    return paused ? 1 : 0;
}

namespace
{
/* The console layer's own reports (ps5/platform: the pad, the splash) into
 * this log, a line at a time. */
void TakePlatformMessages()
{
    static char text[8192];
    if (platform_take_messages(text, sizeof(text)) == 0)
    {
        return;
    }
    char *line = text;
    while (*line != '\0')
    {
        char *end = std::strchr(line, '\n');
        if (end != nullptr)
        {
            *end = '\0';
        }
        if (*line != '\0')
        {
            SDL_Log("PS5 platform: %s", line);
        }
        if (end == nullptr)
        {
            break;
        }
        line = end + 1;
    }
}

/* What the controllers look like from each side -- the console's pad layer,
 * SDL's joysticks and SDL's gamepads -- whenever it changes, and every few
 * seconds while there is no gamepad at all. */
void ReportPads(unsigned frames)
{
    static Uint64 next_at = 0;
    static std::string last;
    static unsigned last_frames = 0;
    const Uint64 now = SDL_GetTicks();

    int joysticks = 0;
    int gamepads = 0;
    if (SDL_JoystickID *ids = SDL_GetJoysticks(&joysticks))
    {
        SDL_free(ids);
    }
    if (SDL_JoystickID *ids = SDL_GetGamepads(&gamepads))
    {
        SDL_free(ids);
    }
    const struct pad_reading *readings = nullptr;
    const int count = pad_player_readings(0, &readings);
    char line[256];
    std::snprintf(line, sizeof(line),
                  "pad players 0x%x, player 0 %s; SDL joysticks %d, gamepads %d%s",
                  (unsigned)pad_players(), count > 0 ? "reading" : "no readings",
                  joysticks, gamepads, g_paused.load() ? "; PAUSED" : "");
    const bool changed = last != line;
    /* Every few seconds while something is wrong: no gamepad, or the game's
     * frames not moving. */
    const bool stuck = frames == last_frames;
    last_frames = frames;
    if (changed || ((gamepads == 0 || stuck) && now >= next_at))
    {
        SDL_Log("PS5: game frames %u%s", frames, stuck ? " (not advancing)" : "");
        if (count > 0 && readings != nullptr)
        {
            const struct pad_reading &r = readings[count - 1];
            SDL_Log("PS5: %s (buttons 0x%08x, connected %d)", line, (unsigned)r.buttons,
                    (int)r.connected);
        }
        else
        {
            SDL_Log("PS5: %s", line);
        }
        last = line;
        next_at = now + 3000;
    }
}

/* The watch: copies the console layer's reports into the log, says how the
 * controllers and the game's frames are doing, and writes the log out -- from
 * a thread of its own, so it still does when the game's thread is stuck. */
int SDLCALL WatchThread(void *)
{
    for (;;)
    {
        SDL_Delay(1000);
        TakePlatformMessages();
        ReportPads(g_frames.load(std::memory_order_relaxed));
        ReiPan_LogFlush();
    }
    return 0;
}
} // namespace

extern "C" void ReiPan_Ps5StartWatch(void)
{
    static bool started = false;
    if (started)
    {
        return;
    }
    started = true;
    if (SDL_Thread *thread = ReiPan_SdlThreadCreate(WatchThread, "ps5-watch", nullptr))
    {
        SDL_DetachThread(thread);
    }
}

extern "C" void ReiPan_Ps5FrameTick(void)
{
    g_frames.fetch_add(1, std::memory_order_relaxed);
    while (ReiPan_Ps5ShouldPause())
    {
    }
}

/* ------------------------------------------------------------ diagnostics */

#include <vulkan/vulkan.h>

#include <vector>

extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

extern "C" void ReiPan_Ps5VulkanReport(void)
{
    SDL_Log("Vulkan report: start");
    auto get = [](VkInstance instance, const char *name) {
        return vk_icdGetInstanceProcAddr(instance, name);
    };
#define PS5_VK(instance, name) auto name = reinterpret_cast<PFN_##name>(get(instance, #name))
    PS5_VK(VK_NULL_HANDLE, vkEnumerateInstanceExtensionProperties);
    PS5_VK(VK_NULL_HANDLE, vkEnumerateInstanceVersion);
    PS5_VK(VK_NULL_HANDLE, vkCreateInstance);
    if (!vkEnumerateInstanceExtensionProperties || !vkCreateInstance)
    {
        SDL_Log("Vulkan report: RADV gave no global entry points");
        return;
    }
    uint32_t version = 0;
    if (vkEnumerateInstanceVersion)
    {
        vkEnumerateInstanceVersion(&version);
    }
    SDL_Log("Vulkan report: instance version %u.%u.%u", VK_API_VERSION_MAJOR(version),
             VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version));

    uint32_t count = 0;
    VkResult rc = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> instance_extensions(count);
    if (count)
    {
        rc = vkEnumerateInstanceExtensionProperties(nullptr, &count, instance_extensions.data());
    }
    std::string list;
    for (const auto &extension : instance_extensions)
    {
        list += extension.extensionName;
        list += ' ';
    }
    SDL_Log("Vulkan report: %u instance extensions (%d): %s", count, (int)rc, list.c_str());

    const char *enabled[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ReiPan";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = enabled;
    VkInstance instance = VK_NULL_HANDLE;
    rc = vkCreateInstance(&info, nullptr, &instance);
    SDL_Log("Vulkan report: vkCreateInstance(surface, display) = %d", (int)rc);
    ReiPan_LogFlush();
    if (rc != VK_SUCCESS)
    {
        return;
    }

    PS5_VK(instance, vkDestroyInstance);
    PS5_VK(instance, vkEnumeratePhysicalDevices);
    PS5_VK(instance, vkGetPhysicalDeviceProperties);
    PS5_VK(instance, vkGetPhysicalDeviceFeatures);
    PS5_VK(instance, vkEnumerateDeviceExtensionProperties);
    PS5_VK(instance, vkGetPhysicalDeviceQueueFamilyProperties);
    PS5_VK(instance, vkGetPhysicalDeviceDisplayPropertiesKHR);
    PS5_VK(instance, vkGetDisplayModePropertiesKHR);

    count = 0;
    rc = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    SDL_Log("Vulkan report: vkEnumeratePhysicalDevices = %d, %u GPUs", (int)rc, count);
    std::vector<VkPhysicalDevice> devices(count);
    if (count)
    {
        rc = vkEnumeratePhysicalDevices(instance, &count, devices.data());
    }
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(device, &properties);
        SDL_Log("Vulkan report: GPU '%s' API %u.%u.%u type %d", properties.deviceName,
                 VK_API_VERSION_MAJOR(properties.apiVersion),
                 VK_API_VERSION_MINOR(properties.apiVersion),
                 VK_API_VERSION_PATCH(properties.apiVersion), (int)properties.deviceType);

        VkPhysicalDeviceFeatures features = {};
        vkGetPhysicalDeviceFeatures(device, &features);
        SDL_Log("Vulkan report: features independentBlend %u imageCubeArray %u depthClamp %u "
                 "shaderClipDistance %u drawIndirectFirstInstance %u sampleRateShading %u "
                 "samplerAnisotropy %u",
                 features.independentBlend, features.imageCubeArray, features.depthClamp,
                 features.shaderClipDistance, features.drawIndirectFirstInstance,
                 features.sampleRateShading, features.samplerAnisotropy);

        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> device_extensions(extension_count);
        if (extension_count)
        {
            vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                                 device_extensions.data());
        }
        bool swapchain = false, maintenance1 = false;
        for (const auto &extension : device_extensions)
        {
            swapchain |= std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            maintenance1 |= std::strcmp(extension.extensionName, VK_KHR_MAINTENANCE_1_EXTENSION_NAME) == 0;
        }
        SDL_Log("Vulkan report: %u device extensions, swapchain %d, maintenance1 %d",
                 extension_count, swapchain, maintenance1);

        uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        if (families)
        {
            vkGetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data());
        }
        for (uint32_t i = 0; i < families; i++)
        {
            SDL_Log("Vulkan report: queue family %u flags 0x%x count %u", i,
                     (unsigned)queues[i].queueFlags, queues[i].queueCount);
        }

        if (vkGetPhysicalDeviceDisplayPropertiesKHR && vkGetDisplayModePropertiesKHR)
        {
            uint32_t displays = 0;
            rc = vkGetPhysicalDeviceDisplayPropertiesKHR(device, &displays, nullptr);
            SDL_Log("Vulkan report: vkGetPhysicalDeviceDisplayPropertiesKHR = %d, %u displays",
                     (int)rc, displays);
            std::vector<VkDisplayPropertiesKHR> display_list(displays);
            if (displays)
            {
                vkGetPhysicalDeviceDisplayPropertiesKHR(device, &displays, display_list.data());
            }
            for (const auto &display : display_list)
            {
                uint32_t modes = 0;
                rc = vkGetDisplayModePropertiesKHR(device, display.display, &modes, nullptr);
                std::vector<VkDisplayModePropertiesKHR> mode_list(modes);
                if (modes)
                {
                    vkGetDisplayModePropertiesKHR(device, display.display, &modes, mode_list.data());
                }
                for (const auto &mode : mode_list)
                {
                    SDL_Log("Vulkan report: display mode %ux%u %u mHz",
                             mode.parameters.visibleRegion.width,
                             mode.parameters.visibleRegion.height, mode.parameters.refreshRate);
                }
            }
        }
        ReiPan_LogFlush();
    }
    vkDestroyInstance(instance, nullptr);
    SDL_Log("Vulkan report: done");
    ReiPan_LogFlush();
#undef PS5_VK
}

/* ------------------------------------------------------------ crash report */

#include <ps5platform/context.h>

#include <exception>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

namespace
{
char g_crash_path[512];

/* One line to crash.log, opened for each line so nothing is lost if the
 * report itself faults. snprintf is not async-signal-safe, but the title is
 * going down anyway. */
void CrashLine(const char *fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    const int length = std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (length <= 0)
    {
        return;
    }
    const int fd = open(g_crash_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
        return;
    }
    (void)write(fd, line, (size_t)(length < (int)sizeof(line) ? length : (int)sizeof(line) - 1));
    (void)write(fd, "\n", 1);
    close(fd);
}

void CrashHeader(const char *what)
{
    CrashLine("ReiPan crashed: %s", what);
    /* Its link-time address is in the ELF's symbol table: the difference is
     * where the console loaded the title. */
    CrashLine("anchor ReiPan_Ps5Notify = %p", (void *)&ReiPan_Ps5Notify);
    CrashLine("thread %p", (void *)pthread_self());
}

void CrashHandler(int signal_number, siginfo_t *info, void *context)
{
    const auto *uc = static_cast<const ucontext_t *>(context);
    const mcontext_t &mc = uc->uc_mcontext;
    char what[96];
    std::snprintf(what, sizeof(what), "signal %d, code %d, address %p", signal_number,
                  info ? info->si_code : 0, info ? info->si_addr : nullptr);
    CrashHeader(what);
    CrashLine("rip %#llx rsp %#llx rbp %#llx", (unsigned long long)mc.mc_rip,
              (unsigned long long)mc.mc_rsp, (unsigned long long)mc.mc_rbp);
    CrashLine("rax %#llx rbx %#llx rcx %#llx rdx %#llx", (unsigned long long)mc.mc_rax,
              (unsigned long long)mc.mc_rbx, (unsigned long long)mc.mc_rcx,
              (unsigned long long)mc.mc_rdx);
    CrashLine("rsi %#llx rdi %#llx r8 %#llx r9 %#llx", (unsigned long long)mc.mc_rsi,
              (unsigned long long)mc.mc_rdi, (unsigned long long)mc.mc_r8,
              (unsigned long long)mc.mc_r9);

    /* The frame-pointer chain, kept to frames that lie above the stack
     * pointer and grow upwards, so a broken chain ends the walk. */
    CrashLine("frame 0 %#llx", (unsigned long long)mc.mc_rip);
    const uintptr_t stack = (uintptr_t)mc.mc_rsp;
    uintptr_t frame = (uintptr_t)mc.mc_rbp;
    for (int i = 1; i < 64; i++)
    {
        if (frame == 0 || (frame & 7) != 0 || frame < stack || frame - stack > (64u << 20))
        {
            break;
        }
        const uintptr_t *slot = reinterpret_cast<const uintptr_t *>(frame);
        const uintptr_t return_address = slot[1];
        if (return_address == 0)
        {
            break;
        }
        CrashLine("frame %d %#llx", i, (unsigned long long)return_address);
        const uintptr_t next = slot[0];
        if (next <= frame)
        {
            break;
        }
        frame = next;
    }
    CrashLine("end");

    /* Let the console report it as usual */
    struct sigaction fallback = {};
    fallback.sa_handler = SIG_DFL;
    sigemptyset(&fallback.sa_mask);
    sigaction(signal_number, &fallback, nullptr);
    raise(signal_number);
}

void TerminateHandler()
{
    const char *what = "std::terminate (no active exception)";
    std::string message;
    if (std::exception_ptr current = std::current_exception())
    {
        try
        {
            std::rethrow_exception(current);
        }
        catch (const std::exception &exception)
        {
            message = std::string("uncaught exception: ") + exception.what();
            what = message.c_str();
        }
        catch (...)
        {
            what = "uncaught exception of an unknown type";
        }
    }
    CrashHeader(what);
    CrashLine("end");
    SDL_Log("ReiPan crashed: %s", what);
    ReiPan_LogFlush();
    std::abort();
}

alignas(16) char g_crash_stack[128 * 1024];
} // namespace

extern "C" void ReiPan_Ps5InstallCrashHandler(void)
{
    std::snprintf(g_crash_path, sizeof(g_crash_path), "%scrash.log",
                  ReiPan_Ps5UserDir());
    unlink(g_crash_path);

    /* Its own stack, so a stack overflow can still be reported */
    stack_t alternate = {};
    alternate.ss_sp = g_crash_stack;
    alternate.ss_size = sizeof(g_crash_stack);
    sigaltstack(&alternate, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = CrashHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal_number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP})
    {
        sigaction(signal_number, &action, nullptr);
    }
    std::set_terminate(TerminateHandler);
}

#endif /* __PROSPERO__ */
