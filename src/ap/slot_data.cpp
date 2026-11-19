#include "ap/slot_data.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>

namespace ap
{
namespace
{

enum class Type
{
    String,
    Integer,
    Array
};

// A key's JSON type, and the integer values the client plays. An empty set accepts any value of the type.
struct Rule
{
    const char *key;
    Type type;
    std::initializer_list<std::int64_t> accepted;
};

constexpr Rule rules[] = {
    {"SeedNumber", Type::String, {}},
    {"SeedName", Type::String, {}},
    {"supported_client_version", Type::String, {}},
    {"TotalLocations", Type::Integer, {}},
    {"ShopSlots", Type::Integer, {}},
    {"EnemiesLogic", Type::Integer, {}},
    {"IncludeFishes", Type::Integer, {}},
    {"FillerBattleItemsRate", Type::Integer, {}},
    {"FillerHealthInkItemsRate", Type::Integer, {}},
    {"FillerFeedBagsRate", Type::Integer, {}},
    {"FillerCheapTreasuresRate", Type::Integer, {}},
    {"FillerMediumTreasuresRate", Type::Integer, {}},
    {"FillerExpensiveTreasuresRate", Type::Integer, {}},
    {"FillerFishesRate", Type::Integer, {}},
    {"FillerFoodRate", Type::Integer, {}},
    {"RandomizeContainers", Type::Integer, {0, 1}},
    {"RandomizeBrushes", Type::Integer, {0, 1}},
    {"NightTimeChecksRequireCrescent", Type::Integer, {0, 1}},
    {"IngredientsInMoonCave", Type::Integer, {0, 1}},
    {"ProgressiveWeapons", Type::Integer, {0, 1}},
    {"KarmicTransformers", Type::Integer, {0, 1, 2}},
    {"DebugMode", Type::Array, {}},
    {"RandomizeShops", Type::Integer, {0}},
    {"OpenGameStart", Type::Integer, {0}},
    {"RemoveBlockHead", Type::Integer, {0}},
    {"RequiredDoggorbs", Type::Integer, {8}},
    {"CanineRewards", Type::Integer, {0}},
    {"MoonCaveAccess", Type::Integer, {1}},
    {"BloomGuardianSaplings", Type::Integer, {0}},
};

bool has_type(const nlohmann::json &value, Type type)
{
    switch (type)
    {
    case Type::String:
        return value.is_string();
    case Type::Integer:
        return value.is_number_integer();
    case Type::Array:
        return value.is_array();
    }
    return false;
}

} // namespace

SlotDataCheck check_slot_data(const nlohmann::json &slot_data)
{
    SlotDataCheck check;
    if (!slot_data.is_object())
    {
        check.refusal = "slot_data isn't an object";
        return check;
    }
    for (const auto &[key, value] : slot_data.items())
    {
        const Rule *rule = std::find_if(std::begin(rules), std::end(rules), [&](const Rule &r) { return key == r.key; });
        if (rule == std::end(rules))
        {
            check.unknown_keys.push_back(key);
            continue;
        }
        if (!has_type(value, rule->type))
        {
            check.refusal = "slot_data key " + key + " has the wrong type";
            check.unknown_keys.clear();
            return check;
        }
        if (rule->accepted.size() != 0 && std::find(rule->accepted.begin(), rule->accepted.end(), value.get<std::int64_t>()) == rule->accepted.end())
        {
            check.refusal = "okami-apclient doesn't support " + key + " = " + value.dump();
            check.unknown_keys.clear();
            return check;
        }
    }
    std::sort(check.unknown_keys.begin(), check.unknown_keys.end());
    return check;
}

} // namespace ap
