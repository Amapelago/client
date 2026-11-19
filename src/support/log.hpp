#pragma once

#include <filesystem>

namespace support
{

enum class Level
{
    Debug,
    Info,
    Warn,
    Error
};

// Creates dir, moves an existing client.log over client.prev.log, opens a fresh client.log.
// Returns false, leaving the log off, if dir is empty or relative or any step fails.
// Runs once, before any thread logs.
bool log_start(const std::filesystem::path &dir);

// printf-style. Bad format strings are build errors under clang. Safe to call from any thread. Each call writes one whole line; dropped while the log is off.
#ifdef __clang__
__attribute__((format(printf, 2, 3)))
#endif
void log(Level level, const char *format, ...);

} // namespace support
