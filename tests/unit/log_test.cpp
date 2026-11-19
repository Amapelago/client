#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from defining min, max and the rarely used APIs.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "support/log.hpp"
#include "support/race.hpp"

namespace
{

// A fresh folder per test, removed afterwards.
struct TempDir
{
    std::filesystem::path path;

    TempDir()
    {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / ("okami_apclient_log_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::vector<std::string> read_lines(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

} // namespace

TEST_CASE("log lines from many threads arrive whole")
{
    const TempDir tmp;
    const auto &dir = tmp.path;
    REQUIRE(support::log_start(dir));
    race_threads(8,
                 [](int t)
                 {
                     for (int i = 0; i < 2000; ++i)
                         support::log(support::Level::Info, "thread %d line %d", t, i);
                 });
    const auto lines = read_lines(dir / "client.log");
    REQUIRE(lines.size() == 16000);
    const std::regex shape(R"(^\d\d:\d\d:\d\d\.\d{3} +\d+ INFO  thread (\d) line (\d+)$)");
    std::set<std::pair<int, int>> seen;
    for (const auto &line : lines)
    {
        std::smatch m;
        REQUIRE(std::regex_match(line, m, shape));
        seen.emplace(std::stoi(m[1]), std::stoi(m[2]));
    }
    CHECK(seen.size() == 16000);
}
