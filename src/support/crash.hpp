#pragma once

#include <filesystem>

namespace support
{

// Main thread, once, at the cold start. Creates dir, loads dbghelp.dll from the system directory, starts the reporter thread, installs the unhandled-exception
// filter and a first-chance handler for stack overflows, and makes std::terminate on the calling thread report 0xE0574F4C to that filter.
//
// The filter runs on the faulting thread and only hands the fault to the reporter, which logs a line and then writes dir\crash-YYYYMMDD-HHMMSS.dmp, so a crash
// on any thread gets a full dump. The filter waits up to 10 seconds for the report, then ends the process when it finished and passes the fault on when it did
// not, so a dump that hangs never hangs the game. A second crash during a report waits for that report and writes none of its own.
void crash_start(const std::filesystem::path &dir);

// Main thread: puts the unhandled-exception filter back, and logs when something else had replaced it.
void crash_reassert();

// Any thread the client starts: makes std::terminate on the calling thread report like the main thread's. Terminate handlers are per thread. On a process where
// crash_start never ran, the handler ends the process as the default one does.
void crash_watch_thread();

} // namespace support
