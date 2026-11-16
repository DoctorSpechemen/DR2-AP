from typing import Any, Dict, List

from BaseClasses import ItemClassification, Region, Tutorial
from Options import OptionError
from worlds.AutoWorld import WebWorld, World
from worlds.LauncherComponents import Component, Type, components, icon_paths, launch_subprocess

from .Data import (ALL_FILLERS, AMBUSH_TRAP, BUTTERFINGERS_TRAP, CASE_GATE_ITEMS, FILLER_WEIGHTS, GAME_NAME, LAST_CASE,
                   NAKED_TRAP, SICKNESS_TRAP, THE_FACTS, WRONG_NUMBER_TRAP, ZOMBREX_IN_POOL,
                   ZOMBREX_ITEM)
from .Items import DR2Item, item_name_groups, item_name_to_id, item_table
from .Locations import DR2Location, location_name_groups, location_name_to_id, location_table
from .Options import DR2Options, dr2_option_groups


def launch_client(*args: str) -> None:
    from .Client import launch
    launch_subprocess(launch, name="DR2Client", args=args)


def _client_icon() -> str:
    """The client's icon is icon.png beside this file. It is not kept in the repository, so it may be missing."""
    import pkgutil
    try:
        if pkgutil.get_data(__name__, "icon.png"):
            icon_paths["dr2_icon"] = f"ap:{__name__}/icon.png"
            return "dr2_icon"
    except Exception:
        pass
    return "icon"   # Archipelago's own


components.append(Component("Dead Rising 2 Client", func=launch_client, component_type=Type.CLIENT,
                            game_name=GAME_NAME, supports_uri=True, icon=_client_icon()))


class DR2Web(WebWorld):
    theme = "partyTime"
    setup_en = Tutorial(
        "Multiworld Setup Guide",
        "A guide to setting up Dead Rising 2 (PC, Steam) for Archipelago.",
        "English",
        "setup_en.md",
        "setup/en",
        ["DoctorSpechemen"],
    )
    tutorials = [setup_en]
    option_groups = dr2_option_groups


class DR2World(World):
    """
    Dead Rising 2: former motocross champion Chuck Greene has 72 hours to clear his name in zombie-overrun Fortune City
    and keep his daughter supplied with Zombrex. Cases, psychopaths, rescued survivors, Katey's gifts, movie posters,
    zombie kill milestones, level-ups and Zombrex finds are checks; cash, PP, Zombrex and traps arrive from other worlds.
    """
    game = GAME_NAME
    web = DR2Web()
    options_dataclass = DR2Options
    options: DR2Options
    topology_present = False

    item_name_to_id = item_name_to_id
    location_name_to_id = location_name_to_id
    item_name_groups = item_name_groups
    location_name_groups = location_name_groups

    required_client_version = (0, 5, 0)

    # ------------------------------------------------------------------ helpers

    def _location_enabled(self, data) -> bool:
        o = self.options
        cat = data.category
        if cat == "case":
            return True
        if cat == "psychopath":
            return bool(o.include_psychopaths)
        if cat == "survivor":
            return bool(o.include_survivors)
        if cat == "gift":
            return bool(o.include_katey_gifts)
        if cat == "poster":
            return bool(o.include_movie_posters)
        if cat == "kills":
            return data.index <= o.kill_milestones.value
        if cat == "level":
            return data.index <= o.level_checks.value
        if cat == "zombrex":
            return data.index <= o.zombrex_checks.value
        return False

    def _gate_rule(self, case: int):
        """Reaching the stretch of the game that belongs to a case needs that case's gate item (the regions are chained,
        so the earlier gates are needed too). No rule when case gates are off."""
        if not self.options.case_gate_items or case < 2:
            return None
        item, player = CASE_GATE_ITEMS[case], self.player
        return lambda state: state.has(item, player)

    # ------------------------------------------------------------------ generation

    def create_regions(self) -> None:
        multiworld, player = self.multiworld, self.player

        menu = Region("Menu", player, multiworld)
        multiworld.regions.append(menu)

        # "Fortune City": checks that never wait for a case (kills, levels, Zombrex finds).
        city = Region("Fortune City", player, multiworld)
        multiworld.regions.append(city)
        menu.connect(city, "Menu -> Fortune City")

        # One region per case, each reached from the one before it: Case 1 -> Case 2 -> ... -> Case 7.
        case_regions: Dict[int, Region] = {}
        previous = menu
        for case in range(1, LAST_CASE + 1):
            region = Region(f"Case {case}", player, multiworld)
            multiworld.regions.append(region)
            case_regions[case] = region
            previous.connect(region, f"{previous.name} -> Case {case}", self._gate_rule(case))
            previous = region

        goal_is_overtime = self.options.goal == "overtime"
        for name, data in location_table.items():
            if not self._location_enabled(data):
                continue
            if name == THE_FACTS and not goal_is_overtime:
                continue   # The Facts is the goal itself (see below)
            parent = case_regions[data.band] if data.band else city
            parent.locations.append(DR2Location(player, name, data.code, parent))

        last = case_regions[LAST_CASE]
        goal_location = DR2Location(player, "Overtime Complete" if goal_is_overtime else "The Facts Complete", None, last)
        last.locations.append(goal_location)
        goal_location.place_locked_item(DR2Item("Victory", ItemClassification.progression, None, player))
        multiworld.completion_condition[player] = lambda state: state.has("Victory", player)

    def create_item(self, name: str) -> DR2Item:
        data = item_table[name]
        return DR2Item(name, data.classification, data.code, self.player)

    def get_filler_item_name(self) -> str:
        weights = [FILLER_WEIGHTS[name] for name in ALL_FILLERS]
        return self.random.choices(ALL_FILLERS, weights)[0]

    def _trap_weights(self) -> Dict[str, int]:
        o = self.options
        return {NAKED_TRAP: o.naked_trap_weight.value, BUTTERFINGERS_TRAP: o.butterfingers_trap_weight.value,
                AMBUSH_TRAP: o.ambush_trap_weight.value,
                WRONG_NUMBER_TRAP: o.wrong_number_trap_weight.value,
                SICKNESS_TRAP: o.sickness_trap_weight.value}

    def _random_trap(self) -> str:
        weights = self._trap_weights()
        if sum(weights.values()) == 0:
            return self.get_filler_item_name()
        return self.random.choices(list(weights), list(weights.values()))[0]

    def create_items(self) -> None:
        multiworld, player = self.multiworld, self.player

        pool: List[DR2Item] = []
        if self.options.case_gate_items:
            pool += [self.create_item(name) for name in CASE_GATE_ITEMS.values()]
        pool += [self.create_item(ZOMBREX_ITEM) for _ in range(ZOMBREX_IN_POOL)]

        unfilled = len(multiworld.get_unfilled_locations(player))
        remaining = unfilled - len(pool)
        if remaining < 0:
            raise OptionError(f"{self.player_name}: {len(pool)} items but only {unfilled} locations. "
                              f"Enable more check categories.")

        trap_count = round(remaining * self.options.trap_percentage.value / 100)
        for _ in range(trap_count):
            pool.append(self.create_item(self._random_trap()))
        for _ in range(remaining - trap_count):
            pool.append(self.create_item(self.get_filler_item_name()))

        multiworld.itempool += pool

    def fill_slot_data(self) -> Dict[str, Any]:
        o = self.options
        return {
            "goal": o.goal.value,
            "timer_mode": o.timer_mode.current_key,
            "case_gate_items": bool(o.case_gate_items),
            "survivors_never_expire": bool(o.survivors_never_expire),
            "include_psychopaths": bool(o.include_psychopaths),
            "include_survivors": bool(o.include_survivors),
            "include_katey_gifts": bool(o.include_katey_gifts),
            "include_movie_posters": bool(o.include_movie_posters),
            "kill_milestones": o.kill_milestones.value,
            "level_checks": o.level_checks.value,
            "zombrex_checks": o.zombrex_checks.value,
            "death_link": bool(o.death_link),
            "trap_weights": self._trap_weights(),
        }
