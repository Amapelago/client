#pragma once

#include <cstdint>
#include <optional>

namespace boot
{

struct Fingerprint
{
    uint32_t time_date_stamp = 0;
    uint32_t size_of_image = 0;
    bool operator==(const Fingerprint &) const = default;
};

// The one main.dll build the client's addresses describe.
inline constexpr Fingerprint kOkamiMain{0x60E54536, 0x1150000};

// The PE header values of the image mapped at image. Empty unless the first page holds an MZ header, a PE signature and a PE32+ optional header.
std::optional<Fingerprint> read_fingerprint(const void *image);

} // namespace boot
