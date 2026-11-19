#include "support/log.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <system_error>

namespace support
{
namespace
{

// nullptr while the log is off.
std::atomic<HANDLE> file{nullptr};
// End of the bytes handed out so far. Wine does not serialise appends from several threads, so each line reserves its range here and writes at that
// offset. Lines from several threads never overlap on Windows or under Proton, and no lock is needed.
std::atomic<uint64_t> file_end{0};

const char *level_name(Level level)
{
    switch (level)
    {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    default:
        return "ERROR";
    }
}

} // namespace

bool log_start(const std::filesystem::path &dir)
{
    // create_directories is linked into this DLL and calls only kernel32 file functions, so it loads no DLL under the loader lock.
    std::error_code ec;
    if (dir.empty() || !dir.is_absolute())
        return false;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return false;

    const std::wstring current = (dir / L"client.log").wstring();
    const std::wstring previous = (dir / L"client.prev.log").wstring();
    // A failed rotation (a viewer holding the old log, say) costs only client.prev.log; CREATE_ALWAYS below still opens this launch's log.
    MoveFileExW(current.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);

    const HANDLE opened = CreateFileW(current.c_str(), FILE_WRITE_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (opened == INVALID_HANDLE_VALUE)
        return false;
    file_end.store(0);
    file.store(opened, std::memory_order_release);
    return true;
}

void log(Level level, const char *format, ...)
{
    const HANDLE handle = file.load(std::memory_order_acquire);
    if (handle == nullptr)
        return;

    SYSTEMTIME time;
    GetLocalTime(&time);
    constexpr int size = 1024;
    char buffer[size];
    // The thread id and level are padded, so the levels share a column for thread ids up to six digits.
    int length = snprintf(buffer, size, "%02u:%02u:%02u.%03u %6lu %-5s ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, GetCurrentThreadId(),
                          level_name(level));
    va_list args;
    va_start(args, format);
    length += vsnprintf(buffer + length, size - length, format, args);
    va_end(args);

    // A cut line stops two bytes short of the buffer's end, leaving room for its own line break.
    if (length > size - 2)
        length = size - 2;
    buffer[length++] = '\r';
    buffer[length++] = '\n';

    const uint64_t offset = file_end.fetch_add(static_cast<uint64_t>(length));
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD written = 0;
    WriteFile(handle, buffer, static_cast<DWORD>(length), &written, &at);
}

} // namespace support
