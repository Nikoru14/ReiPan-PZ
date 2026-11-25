#include "reipan_crash.h"

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))

#include "../io/reipan_paths.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

/* The EE kernel shim, force-included into every file, renames these to its
 * ReiPan_* versions (eekernel.h); <windows.h> declares the Win32 functions of
 * the same names, which the renames would turn into clashing redeclarations
 * of the shim's.  Nothing below calls the shim's, so the renames go. */
#undef CreateThread
#undef TerminateThread
#undef SuspendThread
#undef ResumeThread
#undef GetThreadId

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{

/* Both the file and the log get each line: the log may be what a tester
 * sends, the file is there even when the log's own machinery was what broke. */
void Emit(FILE *f, const char *line)
{
    if (f != nullptr)
    {
        std::fputs(line, f);
        std::fputc('\n', f);
        std::fflush(f);
    }
    SDL_Log("CRASH: %s", line);
}

/* "ReiPan.exe+0x1234" for an address, so a build's .pdb can name it later. */
std::string ModuleOffset(DWORD64 address)
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        module != nullptr)
    {
        char path[MAX_PATH];
        if (GetModuleFileNameA(module, path, sizeof(path)) != 0)
        {
            const char *slash = std::strrchr(path, '\\');
            std::snprintf(name, sizeof(name), "%s", slash != nullptr ? slash + 1 : path);
        }
    }
    char text[MAX_PATH + 32];
    std::snprintf(text, sizeof(text), "%s+0x%llx", name,
                  (unsigned long long)(address - (DWORD64)(uintptr_t)module));
    return text;
}

const char *ExceptionName(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "float divide by zero";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case 0xC0000409: return "stack buffer overrun / fast fail";
        default: return "exception";
    }
}

LONG WINAPI OnCrash(EXCEPTION_POINTERS *info)
{
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0 || info == nullptr ||
        info->ExceptionRecord == nullptr || info->ContextRecord == nullptr)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const std::string dir = ReiPan_PathUserDir();
    FILE *f = std::fopen((dir + "crash.log").c_str(), "w");
    char line[1024];

    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    const DWORD64 at = (DWORD64)(uintptr_t)record->ExceptionAddress;
    std::snprintf(line, sizeof(line), "ReiPan crashed: %s (0x%08lx) at %s, thread %lu",
                  ExceptionName(record->ExceptionCode), (unsigned long)record->ExceptionCode,
                  ModuleOffset(at).c_str(), (unsigned long)GetCurrentThreadId());
    Emit(f, line);
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2)
    {
        const ULONG_PTR kind = record->ExceptionInformation[0];
        std::snprintf(line, sizeof(line), "  %s address 0x%llx",
                      kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                      (unsigned long long)record->ExceptionInformation[1]);
        Emit(f, line);
    }

    /* The stack, named where the .pdb is found (next to ReiPan.exe). */
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;

    CONTEXT context = *info->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    Emit(f, "Stack:");
    for (int depth = 0; depth < 64; depth++)
    {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64,
                         nullptr) ||
            frame.AddrPC.Offset == 0)
        {
            break;
        }
        const DWORD64 pc = frame.AddrPC.Offset;
        char function[512] = "?";
        char where[600] = "";
        if (symbols)
        {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;
            DWORD64 displacement = 0;
            if (SymFromAddr(process, pc, &displacement, symbol))
            {
                std::snprintf(function, sizeof(function), "%s+0x%llx", symbol->Name,
                              (unsigned long long)displacement);
            }
            IMAGEHLP_LINE64 source{};
            source.SizeOfStruct = sizeof(source);
            DWORD column = 0;
            if (SymGetLineFromAddr64(process, pc, &column, &source))
            {
                const char *file = source.FileName;
                const char *src = file != nullptr ? std::strstr(file, "\\src\\") : nullptr;
                std::snprintf(where, sizeof(where), "  %s:%lu",
                              src != nullptr ? src + 1 : (file != nullptr ? file : "?"),
                              (unsigned long)source.LineNumber);
            }
        }
        std::snprintf(line, sizeof(line), "  #%02d %s  %s%s", depth,
                      ModuleOffset(pc).c_str(), function, where);
        Emit(f, line);
    }
    if (symbols)
    {
        SymCleanup(process);
    }

    /* A minidump beside it, for a debugger. */
    HANDLE dump = CreateFileA((dir + "crash.dmp").c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION exception{};
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = info;
        exception.ClientPointers = FALSE;
        MiniDumpWriteDump(process, GetCurrentProcessId(), dump,
                          (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo |
                                          MiniDumpWithIndirectlyReferencedMemory),
                          &exception, nullptr, nullptr);
        CloseHandle(dump);
        Emit(f, "Minidump: crash.dmp in the user folder.");
    }

    if (f != nullptr)
    {
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

extern "C" void ReiPan_CrashInstall(void)
{
    SetUnhandledExceptionFilter(OnCrash);
}

#else

extern "C" void ReiPan_CrashInstall(void)
{
}

#endif
