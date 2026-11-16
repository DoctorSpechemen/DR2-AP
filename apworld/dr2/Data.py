"""Dead Rising 2 (PC) game data shared by the world, the client and (through tools/gen_tables.py) the plugin.

Everything here was read out of the game's own data (missions.txt and the English text table), so names match what the
game shows. Ids are handed out in the order of these lists: only ever append, never reorder or delete.
"""
GAME_NAME = "Dead Rising 2"
BASE_ID = 2_720_000

# ------------------------------------------------------------------ the story
# (location name, the game's text id for the case title, case number). The text id is how the plugin recognises the case.
CASES = [
    ("Case 1-1: Big News", 81551, 1),
    ("Case 1-2: Alive on Location", 81552, 1),
    ("Case 1-3: Insecurity", 81553, 1),
    ("Case 1-4: Alliance", 81554, 1),
    ("Case 2-1: Sign of Life", 81555, 2),
    ("Case 2-2: Ticket to Ride", 81556, 2),
    ("Case 3-1: Boom Town", 81557, 3),
    ("Case 3-2: Run for the Money", 81558, 3),
    ("Case 4-1: The Source", 81559, 4),
    ("Case 5-1: Stake Out", 81560, 5),
    ("Case 5-2: The Getaway", 81561, 5),
    ("Case 6-1: Help Arrives", 81562, 6),
    ("Case 6-2: Last Stand", 81563, 6),
    ("Case 6-3: Lean on Me", 81564, 6),
    ("Case 6-4: Breach", 81565, 6),
    ("Case 7-1: Bad to Worse", 81566, 7),
    ("Case 7-2: The Only Lead", 81567, 7),
    ("Case 7-3: What Lies Beneath", 81568, 7),
]
THE_FACTS = "The Facts"          # text id 81569; a location when the goal is Overtime, the goal itself otherwise
THE_FACTS_ID = 81569
OVERTIME_ID = 81570
LAST_CASE = 7

# Game clock (seconds since the outbreak started) at which each case begins. A mission that starts at or after the start of
# Case N belongs to "band" N: with case gates on it cannot be reached before the Case N gate item.
CASE_START = {1: 0, 2: 70200, 3: 122400, 4: 169200, 5: 241200, 6: 291600, 7: 291601}


def band_of(start_time: int) -> int:
    """Which case's stretch of the clock a mission start time falls in (Case 7 follows Case 6 with no gap, so 6 is the last)."""
    band = 1
    for case in range(2, 7):
        if start_time >= CASE_START[case]:
            band = case
    return band


# The game's internal mission name for each check that is "this mission finished". The game keeps a state for every
# mission (0 not started, 1 running, 2 finished, 3 failed or expired; seen in game 2026-11-15), and the plugin sends
# the check when the mission reaches 2.
CASE_MISSIONS = {name: "Case" + name.split(":")[0].split(" ")[1] for name, _text_id, _case in CASES}   # "Case1-1", ...
THE_FACTS_MISSION = "TheFacts"
OVERTIME_MISSION = "Overtime"
PSYCHOPATH_MISSIONS = {
    "Fresh Meat": "Snowflake",
    "Meet the Contestants": "TIRUltimateFan1",
    "Chuck the Role Model": "Protester",
    "Tastes Like Chicken": "Chef",
    "Everyone Knows Slappy": "Mascot",
    "Here Comes the Groom": "SexDeviant",
    "Mail Order Zombrex": "Postman",
    "WWJWD?": "Security_Guard",
    "Live Free or Die": "MilitiaMen",
    "One Hit Wonder": "Diva",
    "World's Most Dangerous Trick": "Magicians",
}

# One item per case from Case 2 on (option "Case Gate Items"). Case 1 and the Zombrex missions are never gated.
CASE_GATE_ITEMS = {case: f"Case {case} Missions" for case in range(2, LAST_CASE + 1)}

# ------------------------------------------------------------------ psychopaths
# (mission title, text id, clock time the mission opens)
PSYCHOPATHS = [
    ("Fresh Meat", 80035, 14400),
    ("Meet the Contestants", 80001, 52200),
    ("Chuck the Role Model", 80005, 61200),
    ("Tastes Like Chicken", 80024, 81000),
    ("Everyone Knows Slappy", 80058, 129600),
    ("Here Comes the Groom", 80040, 147600),
    ("Mail Order Zombrex", 80042, 169200),
    ("WWJWD?", 80011, 187200),
    ("Live Free or Die", 80009, 205200),
    ("One Hit Wonder", 80027, 226800),
    ("World's Most Dangerous Trick", 80057, 259200),
]


def psychopath_location(title: str) -> str:
    return f"Psychopath: {title}"


# ------------------------------------------------------------------ survivors
# (the game's internal name, full name, the mission they belong to, clock time that mission opens).
# A survivor is a check once, when they are brought back to the safe house.
SURVIVORS = [
    ("srv_denyce", "Denyce Calloway", "Zombrex 1", 10800),
    ("srv_lenny", "Lenny Mooney", "Fresh Meat", 14400),
    ("srv_gordon", "Gordon Dawkins", "Happy Ever After... Sort Of...", 20700),
    ("srv_lashawndra", "LaShawndra Dawkins", "Happy Ever After... Sort Of...", 20700),
    ("srv_doris", "Doris Elchart", "Lost...", 33000),
    ("srv_chad", "Chad Elchart", "Lost...", 33000),
    ("srv_esther", "Esther Alwin", "Short Sighted", 36000),
    ("srv_kenneth", "Kenneth Walsh", "Welcome to the Family", 39600),
    ("srv_jack", "Jack Ellis", "Welcome to the Family", 39600),
    ("srv_stuart", "Stuart Holmes", "Workers Compensation", 46800),
    ("srv_brittany", "Brittany Beck", "Workers Compensation", 46800),
    ("srv_kristin", "Kristin Harris", "Lush-ious Lady", 50400),
    ("srv_john", "John Boog", "Brains Over Brawn", 55800),
    ("srv_curtis", "Curtis Ellenton", "Brains Over Brawn", 55800),
    ("srv_kevin", "Kevin Meyers", "Brains Over Brawn", 55800),
    ("srv_brian", "Brian Scherbey", "Brains Over Brawn", 55800),
    ("srv_vikki", "Vikki Taylor", "Chuck the Role Model", 61200),
    ("srv_elrod", "Elrod Bumpkins", "Barn Burner", 61260),
    ("srv_trixielynn", "Trixie-Lynn Horton", "Barn Burner", 61260),
    ("srv_jared", "Jared Davis", "Once Bitten", 70500),
    ("srv_sven", "Sven Blaaborg", "Code Blue", 79200),
    ("srv_cinda", "Cinda Smith", "Tastes Like Chicken", 81000),
    ("srv_jasper", "Jasper Sanford", "Tastes Like Chicken", 81000),
    ("srv_terri", "Terri Glass", "An Industrial Fashion", 97200),
    ("srv_willa", "Willa Harris", "An Industrial Fashion", 97200),
    ("srv_jeanna", "Jeanna Slick", "Rock Heroes", 100800),
    ("srv_floyd", "Floyd Stone", "Rock Heroes", 100800),
    ("srv_allen", "Allen Ash", "Rock Heroes", 100800),
    ("srv_bessie", "Bessie Kent", "Shopping Spree", 115200),
    ("srv_erica", "Erica Mayes", "Shopping Spree", 115200),
    ("srv_rosa", "Rosa Collins", "Shopping Spree", 115200),
    ("srv_linnette", "Linette Watkins", "Wilted Flower", 131400),
    ("srv_danni", "Danni Bodine", "Here Comes the Groom", 147600),
    ("srv_janus", "Janus Razo", "Big Game Stakes", 154800),
    ("srv_luz", "Luz Palmer", "Par for the Course", 158400),
    ("srv_jessica", "Jessica Howe", "The High Rollers", 176400),
    ("srv_jacob", "Jacob Skinner", "The High Rollers", 176400),
    ("srv_nevada", "Nevada Slim", "The High Rollers", 176400),
    ("srv_richard", "Richard Kelly", "Hunger Pains", 180000),
    ("srv_cora", "Cora Russel", "Fetching Females", 183600),
    ("srv_summer", "Summer Chavez", "Fetching Females", 183600),
    ("srv_nina", "Nina Suhr", "Fetching Females", 183600),
    ("srv_ray", "Ray Teller", "WWJWD?", 187200),
    ("srv_woodrow", "Woodrow Rutherford", "Bank Run", 194400),
    ("srv_europa", "Europa Westinghouse", "Slave to Fashion", 206100),
    ("srv_randolph", "Randolph Allen", "Art Appreciation", 213000),
    ("srv_bill", "Bill Montagu", "Know When to Fold 'em", 219600),
    ("srv_dean", "Dean Wayne", "Shell Shocked", 220500),
    ("srv_allison", "Allison Perkins", "One Hit Wonder", 226800),
    ("srv_juan", "Juan Lee", "One Hit Wonder", 226800),
    ("srv_cameron", "Cameron Welch", "One Hit Wonder", 226800),
    ("srv_diva", "Bibi Love", "One Hit Wonder", 226800),
    ("srv_tammy", "Tammy Blaine", "Stranded Siren", 237780),
    ("srv_andy", "Andy Talbat", "Dead or Alive?", 244800),
    ("srv_walter", "Walter Morris", "Two's Company", 255600),
    ("srv_royce", "Royce St.John", "Two's Company", 255600),
    ("srv_madison", "Madison Lainey", "World's Most Dangerous Trick", 259200),
    ("srv_lillian", "Lillian Payne", "Family Feud", 266400),
    ("srv_camille", "Camille Payne", "Family Feud", 266400),
    ("srv_michael", "Michael Woo", "Delta Point 1", 291600),
    ("srv_matthew", "Matthew Kuss", "Delta Point 1", 291600),
    ("srv_mike", "Left Hand Lance", "Tape It Or Die", 292500),
    ("srv_meg", "Gretchen Peregrine", "Tape It Or Die", 292500),
    ("srv_jeff", "Wallace Hertzog", "Tape It Or Die", 292500),
    ("srv_matt", "Johnny Kilpatrick", "Tape It Or Die", 292500),
]


def survivor_location(full_name: str) -> str:
    return f"Rescue {full_name}"


# ------------------------------------------------------------------ counted checks
KILL_MILESTONES = [50, 100, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 5000, 7500, 10000, 15000, 20000]
DEFAULT_KILL_MILESTONES = 10
MAX_LEVEL = 50                    # "Reach Level 2" .. "Reach Level 50"
DEFAULT_LEVEL_CHECKS = 20
MAX_ZOMBREX_CHECKS = 8            # "Zombrex Found 1" .. : any Zombrex picked up, bought or handed to Chuck by the game
DEFAULT_ZOMBREX_CHECKS = 4


def kill_location(kills: int) -> str:
    return f"Kill {kills:,} Zombies"


def level_location(level: int) -> str:
    return f"Reach Level {level}"


def zombrex_location(n: int) -> str:
    return f"Zombrex Found {n}"


# ------------------------------------------------------------------ Katey's gifts and movie posters
# (the game's internal item name, the name the game shows). Each gift handed to Katey in the safe house is a
# check, counted in whatever order they are given: "Katey's Gift 1" to "Katey's Gift 10".
KATEY_GIFTS = [
    ("BeachBall", "Beach Ball"),
    ("GiantStuffedRabbit", "Giant Stuffed Rabbit"),
    ("GiantStuffedBear", "Robot Bear"),
    ("GiantStuffedDonkey", "Giant Stuffed Donkey"),
    ("WaterGun", "Water Gun"),
    ("GiantStuffedBull", "Giant Stuffed Bull"),
    ("GiantStuffedElephant", "Giant Stuffed Elephant"),
    ("FunnyPainting", "Funny Painting"),
    ("BagofMarbles", "Bag of Marbles"),
    ("PonyOnStick", "Stick Pony"),
]

# (the game's script event when the poster is examined, the combo weapon on the poster). Examining it is the check;
# Chuck still learns the combo card from it as in the normal game.
MOVIE_POSTERS = [
    ("PosterUsed_Paddlesaw", "Paddlesaw"),
    ("PosterUsed_LaserSword", "Laser Sword"),
    ("PosterUsed_HolyArms", "Holy Arms"),
    ("PosterUsed_Tenderizer", "Tenderizers"),
    ("PosterUsed_FreedomBear", "Freedom Bear"),
    ("PosterUsed_BlamBow", "Blambow"),
    ("PosterUsed_SnowballCannon", "Snowball Cannon"),
]


MAX_KATEY_GIFTS = len(KATEY_GIFTS)


def gift_location(number: int) -> str:
    return f"Katey's Gift {number}"


def poster_location(name: str) -> str:
    return f"Movie Poster: {name}"


# ------------------------------------------------------------------ items
ZOMBREX_ITEM = "Zombrex"
ZOMBREX_IN_POOL = 4
ZOMBREX_KEY_ITEM_ID = 85001       # the game's own id for Zombrex

MONEY_FILLERS = {"$1,000": 1000, "$5,000": 5000, "$10,000": 10000, "$25,000": 25000}
PP_FILLERS = {"1,000 PP": 1000, "5,000 PP": 5000, "10,000 PP": 10000}
FILLER_WEIGHTS = {"$1,000": 10, "$5,000": 8, "$10,000": 5, "$25,000": 2, "1,000 PP": 10, "5,000 PP": 8, "10,000 PP": 4}
ALL_FILLERS = list(MONEY_FILLERS) + list(PP_FILLERS)

NAKED_TRAP = "Naked Trap"                 # Chuck is stripped to his underwear; he can dress again straight away
BUTTERFINGERS_TRAP = "Butterfingers Trap"  # Chuck drops what he is holding
AMBUSH_TRAP = "Ambush Trap"               # enemies appear around Chuck: 2 mercenaries, else 3 looters, else 12 zombies,
                                          # whichever the area he is in has available at that hour
WRONG_NUMBER_TRAP = "Wrong Number Trap"   # the phone rings: "Frank?" "No? This is Chuck." "My bad, wrong number."
SICKNESS_TRAP = "Sickness Trap"           # Chuck is sick and stops to throw up now and then
TRAPS = [NAKED_TRAP, BUTTERFINGERS_TRAP, AMBUSH_TRAP, WRONG_NUMBER_TRAP, SICKNESS_TRAP]

# Id space kept free for item groups that are planned but not in the pool yet, so ids never move when they arrive.
RESERVED_COMBO_CARDS = 60
RESERVED_KEYS = 20
RESERVED_UPGRADES = 40
RESERVED_PAWNSHOP_LOCATIONS = 40
