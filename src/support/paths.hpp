#pragma once

#include <filesystem>

namespace support
{

// %LOCALAPPDATA%\okami-apclient, read from the LOCALAPPDATA environment variable; empty when it is unset.
std::filesystem::path data_dir();

} // namespace support
