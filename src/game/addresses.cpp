#include "game/addresses.hpp"

namespace game
{
namespace
{

// The preferred image base the virtual addresses below are written against
// in this case, Ghidra's default.
constexpr uintptr_t kImageBase = 0x180000000;

template <typename T> T at(uintptr_t main_base, uintptr_t va)
{
    return reinterpret_cast<T>(main_base + (va - kImageBase));
}

template <typename T> T at_rva(uintptr_t main_base, uintptr_t rva)
{
    return reinterpret_cast<T>(main_base + rva);
}

} // namespace

Addresses resolve(uintptr_t main_base)
{
    Addresses a;
    // flower_startup runs once per boot (cold) and again after a soft reset (warm).
    a.flower_startup = at<bool (*)(bool)>(main_base, 0x1804b6240);
    // flower_tick runs once per frame on the main thread.
    a.flower_tick = at<bool (*)()>(main_base, 0x1804b63b0);
    // g_currentRoomId holds the stage in the high byte and the room in the low byte.
    a.current_room = at<const volatile uint16_t *>(main_base, 0x180b6b240);
    // cTitle + 0x14 holds the title's state; the low byte is the major state.
    a.title_state = at<const volatile uint32_t *>(main_base, 0x180b74414);
    return a;
}

} // namespace game
