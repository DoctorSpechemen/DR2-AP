from dataclasses import dataclass

from Options import (Accessibility, Choice, DeathLink, DefaultOnToggle, OptionGroup, PerGameCommonOptions, ProgressionBalancing,
                     Range, Toggle)

from .Data import (DEFAULT_KILL_MILESTONES, DEFAULT_LEVEL_CHECKS, DEFAULT_ZOMBREX_CHECKS, KILL_MILESTONES, MAX_LEVEL,
                   MAX_ZOMBREX_CHECKS)


class Goal(Choice):
    """What completes your game.

    The Facts: finish the story up to and including The Facts.
    Overtime: also finish Overtime."""
    display_name = "Goal"
    option_the_facts = 0
    option_overtime = 1
    default = 0


class TimerMode(Choice):
    """How the 72-hour clock runs.

    Vanilla: the clock runs as normal.
    Slowed: the clock runs at half speed.
    Frozen: the clock only moves when the story moves it.

    Not active in the current test build: the clock always runs as normal."""
    display_name = "Timer"
    option_vanilla = 0
    option_slowed = 1
    option_frozen = 2
    default = 0


class CaseGateItems(Toggle):
    """Adds one item per case from Case 2 on ("Case 2 Missions", "Case 3 Missions", ...). A case and everything that
    happens during it stay closed until its item arrives; the clock waits at the start of a case you do not have yet.
    Case 1 and the Zombrex missions are never gated.

    Not active in the current test build: the items exist and logic follows them, but the game does not hold cases back
    yet."""
    display_name = "Case Gate Items"


class SurvivorsNeverExpire(DefaultOnToggle):
    """On: a survivor mission's timer runs down to 20% and then waits there until you finish it, so nothing can be missed.
    Off: missions expire as in the normal game, and a missed survivor is a missed check.

    Not active in the current test build: missions expire as normal."""
    display_name = "Survivor Missions Never Expire"


class IncludePsychopaths(DefaultOnToggle):
    """Finishing a psychopath's mission is a check."""
    display_name = "Include Psychopaths"


class IncludeSurvivors(DefaultOnToggle):
    """Bringing a survivor back to the safe house is a check, once per survivor."""
    display_name = "Include Survivors"


class IncludeKateyGifts(Toggle):
    """Giving Katey each of her ten gifts in the safe house is a check (Beach Ball, Robot Bear, Stick Pony, ...).

    Leave this off for now: the mod cannot tell yet when a gift has been handed over, so these checks
    could never be sent and whatever is placed on them would be lost."""
    display_name = "Include Katey's Gifts"


class IncludeMoviePosters(Toggle):
    """Examining each of the seven movie posters that teach a combo card is a check. Chuck still learns the card.

    Leave this off for now: detecting an examined poster has not been proven in the game yet, so these
    checks might never be sent."""
    display_name = "Include Movie Posters"


class KillMilestones(Range):
    """Zombie kill checks, at 50, 100, 250, 500, 750, 1,000, 1,500, 2,000, 3,000, 4,000, 5,000, 7,500, 10,000, 15,000 and
    20,000 kills. This is how many of them are in the game; 0 turns them off."""
    display_name = "Kill Milestone Checks"
    range_start = 0
    range_end = len(KILL_MILESTONES)
    default = DEFAULT_KILL_MILESTONES


class LevelChecks(Range):
    """Level-up checks, starting with level 2. This is how many of them are in the game; 0 turns them off."""
    display_name = "Level-Up Checks"
    range_start = 0
    range_end = MAX_LEVEL - 1
    default = DEFAULT_LEVEL_CHECKS


class ZombrexChecks(Range):
    """Checks for the first Zombrex doses you find, buy or are handed in the game. 0 turns them off."""
    display_name = "Zombrex Checks"
    range_start = 0
    range_end = MAX_ZOMBREX_CHECKS
    default = DEFAULT_ZOMBREX_CHECKS


class TrapPercentage(Range):
    """Percentage of filler items replaced by traps."""
    display_name = "Trap Percentage"
    range_start = 0
    range_end = 100
    default = 20


class NakedTrapWeight(Range):
    """How often the naked trap is picked: Chuck is stripped to his underwear. He can get dressed again straight away."""
    display_name = "Naked Trap Weight"
    range_start = 0
    range_end = 100
    default = 6


class ButterfingersTrapWeight(Range):
    """How often the butterfingers trap is picked: Chuck drops what he is holding."""
    display_name = "Butterfingers Trap Weight"
    range_start = 0
    range_end = 100
    default = 8


class AmbushTrapWeight(Range):
    """How often the ambush trap is picked: enemies appear around Chuck. The game only keeps certain enemies ready in
    each area at each hour, so it is 2 mercenaries where they are around, otherwise 3 looters, otherwise 12 zombies."""
    display_name = "Ambush Trap Weight"
    range_start = 0
    range_end = 100
    default = 10


class WrongNumberTrapWeight(Range):
    """How often the wrong number trap is picked: the phone rings, and it is not for you."""
    display_name = "Wrong Number Trap Weight"
    range_start = 0
    range_end = 100
    default = 5


class SicknessTrapWeight(Range):
    """How often the sickness trap is picked: Chuck is sick for a while and stops to throw up."""
    display_name = "Sickness Trap Weight"
    range_start = 0
    range_end = 100
    default = 5


@dataclass
class DR2Options(PerGameCommonOptions):
    goal: Goal
    timer_mode: TimerMode
    case_gate_items: CaseGateItems
    survivors_never_expire: SurvivorsNeverExpire
    include_psychopaths: IncludePsychopaths
    include_survivors: IncludeSurvivors
    include_katey_gifts: IncludeKateyGifts
    include_movie_posters: IncludeMoviePosters
    kill_milestones: KillMilestones
    level_checks: LevelChecks
    zombrex_checks: ZombrexChecks
    trap_percentage: TrapPercentage
    naked_trap_weight: NakedTrapWeight
    butterfingers_trap_weight: ButterfingersTrapWeight
    ambush_trap_weight: AmbushTrapWeight
    wrong_number_trap_weight: WrongNumberTrapWeight
    sickness_trap_weight: SicknessTrapWeight
    death_link: DeathLink


# Sections of the template YAML and the WebHost options page. Options not listed here land in "Game Options".
dr2_option_groups = [
    OptionGroup("Game Options", [Goal, TimerMode, CaseGateItems, SurvivorsNeverExpire, ProgressionBalancing, Accessibility]),
    OptionGroup("Checks", [IncludePsychopaths, IncludeSurvivors, IncludeKateyGifts, IncludeMoviePosters, KillMilestones,
                           LevelChecks, ZombrexChecks]),
    OptionGroup("Traps", [TrapPercentage, NakedTrapWeight, ButterfingersTrapWeight, AmbushTrapWeight, WrongNumberTrapWeight, SicknessTrapWeight]),
    OptionGroup("Multiplayer", [DeathLink]),
]
