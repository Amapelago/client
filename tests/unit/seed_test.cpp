// The bound seed raced between main, which publishes and unpublishes it, and the hook threads that read it.
#include <atomic>

#include <catch2/catch_test_macros.hpp>

#include "ap/seed.hpp"
#include "support/race.hpp"

namespace
{

// A seed whose locations[i].location is i. Like every published seed, it is never freed.
const ap::Seed *make_seed(int count)
{
    auto *s = new ap::Seed{"seed", 1, {}, {}};
    for (int i = 0; i < count; ++i)
        s->locations.push_back({0, i, 1, 0});
    return s;
}

} // namespace

TEST_CASE("readers see a whole seed or none while seeds are published and unpublished")
{
    std::atomic<bool> done{false}, torn{false};
    race_threads(5,
                 [&](int t)
                 {
                     if (t == 0)
                     {
                         for (int n = 0; n < 2000; ++n)
                             ap::publish(n % 2 ? nullptr : make_seed(64));
                         ap::publish(nullptr);
                         done = true;
                         return;
                     }
                     while (!done)
                         if (const ap::Seed *s = ap::seed(); s && (s->locations.size() != 64 || s->locations[63].location != 63))
                             torn = true;
                 });
    CHECK_FALSE(torn); // Catch2 assertions aren't thread-safe, so the readers only set a flag
}
