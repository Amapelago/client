#include <cstring>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// NOMINMAX and WIN32_LEAN_AND_MEAN keep windows.h from defining min, max and the rarely used APIs.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "boot/fingerprint.hpp"

namespace
{

// A zeroed page holding a DOS header and, at lfanew when it fits, NT headers carrying the given values.
std::vector<unsigned char> make_pe(uint32_t stamp, uint32_t size, WORD magic, LONG lfanew)
{
    std::vector<unsigned char> page(4096, 0);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE;
    dos.e_lfanew = lfanew;
    std::memcpy(page.data(), &dos, sizeof(dos));
    if (lfanew >= 0 && static_cast<size_t>(lfanew) + sizeof(IMAGE_NT_HEADERS64) <= page.size())
    {
        IMAGE_NT_HEADERS64 nt{};
        nt.Signature = IMAGE_NT_SIGNATURE;
        nt.FileHeader.TimeDateStamp = stamp;
        nt.OptionalHeader.Magic = magic;
        nt.OptionalHeader.SizeOfImage = size;
        std::memcpy(page.data() + lfanew, &nt, sizeof(nt));
    }
    return page;
}

} // namespace

TEST_CASE("read_fingerprint reads a PE32+ header")
{
    const auto image = make_pe(0x60E54536, 0x1150000, IMAGE_NT_OPTIONAL_HDR64_MAGIC, 0x80);
    CHECK(boot::read_fingerprint(image.data()) == boot::kOkamiMain);
}

TEST_CASE("read_fingerprint rejects images that aren't PE32+")
{
    auto no_mz = make_pe(0x60E54536, 0x1150000, IMAGE_NT_OPTIONAL_HDR64_MAGIC, 0x80);
    no_mz[0] = 0;
    CHECK_FALSE(boot::read_fingerprint(no_mz.data()));
    CHECK_FALSE(boot::read_fingerprint(make_pe(0x60E54536, 0x1150000, IMAGE_NT_OPTIONAL_HDR32_MAGIC, 0x80).data()));
    CHECK_FALSE(boot::read_fingerprint(make_pe(0x60E54536, 0x1150000, IMAGE_NT_OPTIONAL_HDR64_MAGIC, 0x2000).data()));
}

TEST_CASE("the test executable is not the supported main.dll")
{
    const auto own = boot::read_fingerprint(GetModuleHandleW(nullptr));
    REQUIRE(own);
    CHECK(*own != boot::kOkamiMain);
}
