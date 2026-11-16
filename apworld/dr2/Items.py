from typing import Dict, NamedTuple

from BaseClasses import Item, ItemClassification

from .Data import (ALL_FILLERS, BASE_ID, CASE_GATE_ITEMS, GAME_NAME, RESERVED_COMBO_CARDS, RESERVED_KEYS, RESERVED_UPGRADES,
                   TRAPS, ZOMBREX_ITEM)


class DR2Item(Item):
    game = GAME_NAME


class ItemData(NamedTuple):
    code: int
    classification: ItemClassification


def _build() -> Dict[str, ItemData]:
    table: Dict[str, ItemData] = {}
    code = BASE_ID

    def add(name: str, classification: ItemClassification) -> None:
        nonlocal code
        table[name] = ItemData(code, classification)
        code += 1

    # Order here defines item ids: only ever append new groups at the end.
    for name in CASE_GATE_ITEMS.values():
        add(name, ItemClassification.progression)
    add(ZOMBREX_ITEM, ItemClassification.useful)
    for name in ALL_FILLERS:
        add(name, ItemClassification.filler)
    for name in TRAPS:
        add(name, ItemClassification.trap)
    code += RESERVED_COMBO_CARDS + RESERVED_KEYS + RESERVED_UPGRADES   # planned groups; keeps later ids from moving
    # Event item used for the goal ("Victory") has no id.
    return table


item_table: Dict[str, ItemData] = _build()
item_name_to_id: Dict[str, int] = {name: data.code for name, data in item_table.items()}

item_name_groups = {
    "Case Gates": set(CASE_GATE_ITEMS.values()),
    "Traps": set(TRAPS),
    "Filler": set(ALL_FILLERS),
}
