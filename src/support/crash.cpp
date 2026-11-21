#include "support/crash.hpp"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>

#include "support/log.hpp"

namespace support
{
namespace
{

constexpr DWORD kTerminateCode = 0xE0574F4C;
constexpr DWORD kReportWaitMs = 10000;

using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                           PMINIDUMP_CALLBACK_INFORMATION);

MiniDumpWriteDumpFn g_write_dump = nullptr;
DWORD g_dbghelp_error = 0;
wchar_t g_dir[MAX_PATH] = {};
HANDLE g_wake = nullptr; // set by the filter for the reporter
HANDLE g_done = nullptr; // manual reset: set once the report is written, for every waiting filter

// The thread inside the filter, 0 until the first crash. Only that thread hands a fault to the reporter. A plain LONG with interlocked intrinsics, because an
// overflowed thread runs the handoff on the little stack it has left, and debug builds call through std::atomic's functions.
volatile LONG g_faulting = 0;
DWORD g_reporter_thread = 0;
EXCEPTION_POINTERS *g_fault = nullptr;

LONG wait_for_report()
{
    return WaitForSingleObject(g_done, kReportWaitMs) == WAIT_OBJECT_0 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// Hands the fault to the reporter and waits for the report. The thread that faults first reports; one faulting later waits for that report; a fault inside
// this function or on the reporter thread passes straight on, since waiting would wait on itself.
LONG hand_off(EXCEPTION_POINTERS *pointers)
{
    const DWORD self = GetCurrentThreadId();
    if (self == g_reporter_thread)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (const auto first = static_cast<DWORD>(InterlockedCompareExchange(&g_faulting, static_cast<LONG>(self), 0)); first != 0)
    {
        return first == self ? EXCEPTION_CONTINUE_SEARCH : wait_for_report();
    }
    g_fault = pointers;
    SetEvent(g_wake);
    return wait_for_report();
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS *pointers)
{
    return hand_off(pointers);
}

// A stack overflow can end its thread before the unhandled-exception filter runs, as it does under Wine, so it is reported at first chance. The process then
// ends with the overflow's code once the report is written.
LONG WINAPI overflow_handler(EXCEPTION_POINTERS *pointers)
{
    if (pointers->ExceptionRecord->ExceptionCode != EXCEPTION_STACK_OVERFLOW)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (hand_off(pointers) == EXCEPTION_EXECUTE_HANDLER)
    {
        TerminateProcess(GetCurrentProcess(), EXCEPTION_STACK_OVERFLOW);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// The line goes out before the dump, so the code and address are on record even when the dump then blocks on a lock the faulting thread holds.
void report()
{
    const EXCEPTION_RECORD &record = *g_fault->ExceptionRecord;
    const auto thread = static_cast<DWORD>(g_faulting);

    SYSTEMTIME time;
    GetLocalTime(&time);
    char name[64];
    std::snprintf(name, sizeof(name), "crash-%04u%02u%02u-%02u%02u%02u.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    char outcome[96];
    if (g_write_dump != nullptr)
    {
        std::snprintf(outcome, sizeof(outcome), "dump crashes\\%s", name);
    }
    else
    {
        std::snprintf(outcome, sizeof(outcome), "dump failed with error %lu", g_dbghelp_error);
    }

    const auto address = reinterpret_cast<uintptr_t>(record.ExceptionAddress);
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(record.ExceptionAddress),
                           &module) &&
        GetModuleFileNameW(module, path, MAX_PATH) != 0)
    {
        const wchar_t *base = std::wcsrchr(path, L'\\');
        log(Level::Error, "crash: exception 0x%08lX at %ls+0x%llX on thread %lu, %s", record.ExceptionCode, base != nullptr ? base + 1 : path,
            static_cast<unsigned long long>(address - reinterpret_cast<uintptr_t>(module)), thread, outcome);
    }
    else
    {
        log(Level::Error, "crash: exception 0x%08lX at 0x%llX on thread %lu, %s", record.ExceptionCode, static_cast<unsigned long long>(address), thread,
            outcome);
    }
    if (g_write_dump == nullptr)
    {
        return;
    }

    wchar_t dump_path[MAX_PATH];
    std::swprintf(dump_path, MAX_PATH, L"%ls\\%hs", g_dir, name);
    const HANDLE file = CreateFileW(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD error = GetLastError();
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{.ThreadId = thread, .ExceptionPointers = g_fault, .ClientPointers = FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
        error = g_write_dump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr) != 0 ? 0 : GetLastError();
        CloseHandle(file);
    }
    if (error != 0)
    {
        log(Level::Error, "crash: dump crashes\\%s failed with error %lu", name, error);
    }
}

// The terminate handler. It reports through UnhandledExceptionFilter, which runs the client's filter, instead of raising: MSVC's terminate calls its handler
// inside an exception guard and then aborts, so an exception raised here would never reach the filter. Without crash_start there is no filter to report to.
[[noreturn]] void report_terminate()
{
    if (g_wake == nullptr)
    {
        std::abort();
    }
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kTerminateCode;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    record.ExceptionAddress = reinterpret_cast<void *>(&report_terminate); // the dump holds the stack that called terminate
    EXCEPTION_POINTERS pointers{.ExceptionRecord = &record, .ContextRecord = &context};
    UnhandledExceptionFilter(&pointers);
    TerminateProcess(GetCurrentProcess(), kTerminateCode);
    __assume(0);
}

// Takes no client lock: the faulting thread may hold any of them.
DWORD WINAPI reporter(void *)
{
    WaitForSingleObject(g_wake, INFINITE);
    report();
    SetEvent(g_done);
    return 0;
}

} // namespace

void crash_start(const std::filesystem::path &dir)
{
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    std::swprintf(g_dir, MAX_PATH, L"%ls", dir.c_str());

    wchar_t path[MAX_PATH];
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    std::swprintf(path + length, MAX_PATH - length, L"\\dbghelp.dll");
    if (const HMODULE dbghelp = LoadLibraryW(path); dbghelp != nullptr)
    {
        g_write_dump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    if (g_write_dump == nullptr)
    {
        g_dbghelp_error = GetLastError();
        log(Level::Error, "dbghelp.dll did not load; crashes write no dump");
    }

    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (const HANDLE thread = CreateThread(nullptr, 0, &reporter, nullptr, 0, &g_reporter_thread); thread != nullptr)
    {
        CloseHandle(thread);
    }
    AddVectoredExceptionHandler(1, &overflow_handler);
    SetUnhandledExceptionFilter(&crash_filter);
    crash_watch_thread();
}

void crash_reassert()
{
    if (g_wake == nullptr)
    {
        return;
    }
    if (SetUnhandledExceptionFilter(&crash_filter) != &crash_filter)
    {
        log(Level::Warn, "the unhandled exception filter was replaced during flower_startup; the client's is back");
    }
}

void crash_watch_thread()
{
    std::set_terminate(&report_terminate);
}

} // namespace support
