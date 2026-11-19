"""Writes tests/fixtures/contract.json from the Okami HD apworld.

Rerun it whenever the world's main branch moves. Usage, from a Python 3.12 environment with the Archipelago
requirements installed:
    python scripts/gen_contract_fixture.py <path to the Archipelago checkout> [tests/fixtures/contract.json]
"""

import contextlib
import io
import json
import os
import random
import sys
from pathlib import Path

GAME = "Okami HD"
SEED = 1

# The options that reach slot_data, each away from its default.
NON_DEFAULT_OPTIONS = {
    "ProgressiveWeapons": 1,
    "KarmicTransformers": 2,
    "NightTimeChecksRequireCrescent": 0,
    "IngredientsInMoonCave": 0,
    "RandomizeContainers": 0,
    "RandomizeBrushes": 0,
    "FillerBattleItemsRate": 7,
    "FillerHealthInkItemsRate": 11,
    "FillerFeedBagsRate": 16,
    "FillerCheapTreasuresRate": 36,
    "FillerMediumTreasuresRate": 12,
    "FillerExpensiveTreasuresRate": 6,
    "FillerFishesRate": 3,
    "FillerFoodRate": 4,
}


def main() -> None:
    sys.dont_write_bytecode = True
    # The world's ModuleUpdate otherwise stops on an interactive pip prompt.
    os.environ.setdefault("SKIP_REQUIREMENTS_UPDATE", "1")
    world_dir = Path(sys.argv[1]).resolve()
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "tests/fixtures/contract.json"
    sys.path.insert(0, str(world_dir))

    # Importing worlds loads every bundled world, and the ones missing optional deps log tracebacks.
    with contextlib.redirect_stderr(io.StringIO()) as noise:
        from worlds.AutoWorld import AutoWorldRegister
        from test.general import setup_multiworld
    world_type = AutoWorldRegister.world_types.get(GAME)
    if world_type is None:
        sys.exit(noise.getvalue() + f"\n{GAME} did not load from {world_dir}")

    def slot_data(options: dict) -> dict:
        random.seed(SEED)
        multiworld = setup_multiworld(world_type, seed=SEED, options=options)
        return multiworld.worlds[1].fill_slot_data()

    fixture = {
        "items": world_type.item_name_to_id,
        "locations": world_type.location_name_to_id,
        "non_default_options": NON_DEFAULT_OPTIONS,
        "slot_data": {"default": slot_data({}), "non_default": slot_data(NON_DEFAULT_OPTIONS)},
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(fixture, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
