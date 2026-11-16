from typing import Dict, NamedTuple

from BaseClasses import Location

from .Data import (BASE_ID, CASES, GAME_NAME, KATEY_GIFTS, KILL_MILESTONES, MAX_LEVEL, MAX_ZOMBREX_CHECKS, MOVIE_POSTERS,
                   PSYCHOPATHS, RESERVED_PAWNSHOP_LOCATIONS, SURVIVORS, THE_FACTS, LAST_CASE, band_of, gift_location,
                   kill_location, level_location, poster_location, psychopath_location, survivor_location,
                   zombrex_location)


class DR2Location(Location):
    game = GAME_NAME


class LocData(NamedTuple):
    code: int
    category: str   # case, psychopath, survivor, kills, level, zombrex, gift, poster
    band: int       # the case whose gate item opens it (1 = open from the start, 0 = never gated)
    index: int = 0  # 1-based position inside its group (used by "up to N" options)


def _build() -> Dict[str, LocData]:
    table: Dict[str, LocData] = {}
    code = BASE_ID

    def add(name: str, category: str, band: int, index: int = 0) -> None:
        nonlocal code
        table[name] = LocData(code, category, band, index)
        code += 1

    # Order here defines location ids: only ever append new groups at the end.
    for name, _text_id, case in CASES:
        add(name, "case", case)
    add(THE_FACTS, "case", LAST_CASE)
    for title, _text_id, start in PSYCHOPATHS:
        add(psychopath_location(title), "psychopath", band_of(start))
    for _internal, full_name, _mission, start in SURVIVORS:
        add(survivor_location(full_name), "survivor", band_of(start))
    for i, kills in enumerate(KILL_MILESTONES, start=1):
        add(kill_location(kills), "kills", 0, i)
    for level in range(2, MAX_LEVEL + 1):
        add(level_location(level), "level", 0, level - 1)
    for i in range(1, MAX_ZOMBREX_CHECKS + 1):
        add(zombrex_location(i), "zombrex", 0, i)
    code += RESERVED_PAWNSHOP_LOCATIONS   # planned: pawnshop purchases
    for number in range(1, len(KATEY_GIFTS) + 1):
        add(gift_location(number), "gift", 0, number)
    for _event, name in MOVIE_POSTERS:
        add(poster_location(name), "poster", 0)
    return table


location_table: Dict[str, LocData] = _build()
location_name_to_id: Dict[str, int] = {name: data.code for name, data in location_table.items()}

_GROUP_NAMES = {"case": "Cases", "psychopath": "Psychopaths", "survivor": "Survivors", "kills": "Kill Milestones",
                "level": "Levels", "zombrex": "Zombrex", "gift": "Katey's Gifts", "poster": "Movie Posters"}
location_name_groups: Dict[str, set] = {}
for _name, _data in location_table.items():
    location_name_groups.setdefault(_GROUP_NAMES[_data.category], set()).add(_name)
