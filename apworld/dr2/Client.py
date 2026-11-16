"""Dead Rising 2 (PC) Archipelago client. TEST BUILD.

Bridges the Archipelago server and the DR2AP plugin through plain files in the game folder:

  dr2_ap_items.txt    written here (whole list every second): "seed <name>", "option <name> <value>" lines,
                      "tally <label>|<done>|<total>" lines, then "<index>|<item name>|<sender>" for every received item,
                      in order. The plugin only acts while this file is fresh, so nothing happens in a normal game.
  dr2_ap_seeds/<seed>/dr2_ap_outbox.txt  written by the plugin: one location name per line (and "GOAL: ..." lines).
                      Read here and sent as LocationChecks. Each multiworld has its own folder, so old runs never leak in.
  dr2_ap_deaths.txt   written by the plugin when Chuck dies; each line is sent as a DeathLink, then the file is deleted.
  dr2_ap_in.txt       the plugin's command file. A received DeathLink is written here as "deathlink <text>".

Start it from the Archipelago Launcher ("Dead Rising 2 Client") or run it directly. Set the game folder with
/gamedir <path> if the game is not in the default Steam folder.
"""
import asyncio
import os
import typing

import Utils
from CommonClient import CommonContext, ClientCommandProcessor, get_base_parser, gui_enabled, logger, server_loop
from MultiServer import mark_raw
from NetUtils import ClientStatus

from .Data import GAME_NAME, ZOMBREX_ITEM
from .Locations import location_name_to_id, location_table

DEFAULT_GAME_DIR = r"C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2"
GAME_DIR_HELP = ("Game folder not found. Type /gamedir followed by the Dead Rising 2 folder (the one with deadrising2.exe "
                 "and DR2AP.asi), for example: /gamedir D:\\SteamLibrary\\steamapps\\common\\Dead Rising 2")

ITEMS_FILE = "dr2_ap_items.txt"
IN_FILE = "dr2_ap_in.txt"
DEATHS_FILE = "dr2_ap_deaths.txt"
OUTBOX_FILE = "dr2_ap_outbox.txt"
SEEDS_DIR = "dr2_ap_seeds"

TALLY_LABELS = [("case", "Cases"), ("psychopath", "Psychopaths"), ("survivor", "Survivors"), ("gift", "Katey's Gifts"),
                ("poster", "Movie Posters"), ("kills", "Kill Milestones"), ("level", "Levels"), ("zombrex", "Zombrex")]
LOCATION_CATEGORY = {data.code: data.category for data in location_table.values()}
GOAL_LINES = {0: ("GOAL: The Facts", "GOAL: Overtime"), 1: ("GOAL: Overtime",)}   # Overtime can only follow The Facts


def game_folder_ok(folder: str) -> bool:
    """The game folder is set right when it has the game or the plugin in it."""
    return os.path.isfile(os.path.join(folder, "deadrising2.exe")) or os.path.isfile(os.path.join(folder, "DR2AP.asi"))


def clean_seed(seed: object) -> str:
    """The folder name the plugin uses for a multiworld (dr2_ap_seeds/<seed>/, same character rule as the plugin)."""
    return "".join(c if (c.isalnum() or c in "-_") else "_" for c in str(seed))


class DR2CommandProcessor(ClientCommandProcessor):
    @mark_raw
    def _cmd_gamedir(self, path: str = "") -> bool:
        """Show or set the game folder (the one with deadrising2.exe): /gamedir <path>"""
        # mark_raw: the whole rest of the line is the path, so folders with spaces and backslashes work with or without quotes
        path = path.strip().strip('"\'').strip()
        if path:
            if os.path.isfile(path):                                        # pasted the path to the exe: use its folder
                path = os.path.dirname(path)
            self.ctx.game_dir = os.path.normpath(path)
            Utils.persistent_store("client", "dr2_game_dir", self.ctx.game_dir)
            # the items file and the outbox position belong to the old folder: start over in the new one
            self.ctx.last_items_text = ""
            self.ctx.sent_lines = 0
            self.ctx.wrote_once = False
        folder = self.ctx.game_dir
        if not os.path.isdir(folder):
            state = "NOT FOUND"
        elif not os.path.isfile(os.path.join(folder, "DR2AP.asi")):
            state = "found, but DR2AP.asi is not in it"
        else:
            state = "found"
        logger.info(f"Game folder: {folder} ({state})")
        return True

    @mark_raw
    def _cmd_sendcheck(self, name: str = "") -> bool:
        """Send a check the game missed. Part of the name is enough: /sendcheck esther"""
        name = name.strip().strip('"\'').strip()
        if not name:
            logger.info("Usage: /sendcheck <check name>, for example /sendcheck Rescue Esther Alwin")
            return False
        names = list(location_name_to_id)
        if self.ctx.slot is not None:   # connected: only the checks that exist in this multiworld
            here = set(self.ctx.checked_locations) | set(self.ctx.missing_locations)
            names = [n for n in names if location_name_to_id[n] in here]
        low = name.lower()
        found = [n for n in names if n.lower() == low] or [n for n in names if low in n.lower()]
        if not found:
            logger.info(f"No check matches '{name}'.")
            return False
        if len(found) > 1:
            logger.info(f"'{name}' matches {len(found)} checks. Be more exact: " + "; ".join(sorted(found)[:8]))
            return False
        loc = found[0]
        self.ctx.game_command("sendcheck " + loc)   # the plugin keeps it in its own list (outbox)
        if self.ctx.slot is not None:
            asyncio.create_task(self.ctx.send_msgs([{"cmd": "LocationChecks", "locations": [location_name_to_id[loc]]}]))
            logger.info(f"Sent: {loc}")
        else:
            logger.info(f"{loc} will be sent once you connect and the game is running.")
        return True

    def _cmd_resync(self) -> bool:
        """Send every check the game has found again. Safe to use any time."""
        self.ctx.sent_lines = 0
        logger.info("Sending every check the game has found again.")
        return True

    def _cmd_regive(self, count: str = "") -> bool:
        """Hand out the last received items again, after loading an older save: /regive 5 (traps are skipped)"""
        if not count.isdigit() or int(count) < 1:
            logger.info("Usage: /regive <how many of the latest items>, for example /regive 5")
            return False
        self.ctx.game_command(f"regive {int(count)}")
        logger.info(f"Asked the game to hand out the last {int(count)} item(s) again.")
        return True


class DR2Context(CommonContext):
    game = GAME_NAME
    items_handling = 0b111  # every item, including our own and the starting items
    command_processor = DR2CommandProcessor

    def __init__(self, server_address: typing.Optional[str], password: typing.Optional[str]):
        super().__init__(server_address, password)
        self.game_dir: str = Utils.persistent_load().get("client", {}).get("dr2_game_dir", DEFAULT_GAME_DIR)
        self.slot_data: typing.Dict[str, typing.Any] = {}
        self.sent_lines = 0
        self.goal_sent = False
        self.goal_lines_seen: typing.Set[str] = set()
        self.last_items_text = ""
        self.wrote_once = False
        self.dr2_seed_name: typing.Optional[str] = None

    def path(self, name: str) -> str:
        return os.path.join(self.game_dir, name)

    def seed_path(self, name: str) -> str:
        return os.path.join(self.game_dir, SEEDS_DIR, clean_seed(self.room_seed), name)

    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict):
        if cmd == "RoomInfo":
            self.dr2_seed_name = args.get("seed_name")
        if cmd == "Connected":
            self.slot_data = args.get("slot_data", {}) or {}
            self.sent_lines = 0
            self.goal_sent = False
            self.goal_lines_seen = set()
            self.last_items_text = ""
            asyncio.create_task(self.update_death_link(bool(self.slot_data.get("death_link", False))))
            logger.info(f"Connected. Game folder: {self.game_dir}")
            if not self.room_seed:
                logger.error("The server did not report a seed name, so the game cannot be linked. Please report this.")
            elif not game_folder_ok(self.game_dir):
                logger.warning(GAME_DIR_HELP)

    # ------------------------------------------------------------------ game commands and "found" pop-ups

    def game_command(self, line: str) -> None:
        line = line.replace("\n", " ").replace("\r", " ")
        try:
            with open(self.path(IN_FILE), "a", encoding="utf-8") as f:
                f.write(line + "\n")
        except OSError as e:
            logger.error(f"Could not pass a message to the game: {e}")

    def on_print_json(self, args: dict) -> None:
        super().on_print_json(args)
        if args.get("type") != "ItemSend" or self.slot is None:
            return
        item = args.get("item")
        receiving = args.get("receiving")
        if item is None or receiving is None or getattr(item, "player", None) != self.slot or receiving == self.slot:
            return
        try:
            name = self.item_names.lookup_in_slot(item.item, receiving)
        except Exception:
            name = "an item"
        who = self.player_names.get(receiving, "someone")
        self.game_command(f"found Found {name} for {who}")

    def on_deathlink(self, data: typing.Dict[str, typing.Any]) -> None:
        super().on_deathlink(data)
        self.game_command("deathlink " + (data.get("cause") or f"{data.get('source', 'Someone')} died."))

    def leave_game_folder(self) -> None:
        """On exit: remove the items file, so the plugin stops acting and a normal game is left alone."""
        for name in (ITEMS_FILE,):
            try:
                os.remove(self.path(name))
            except OSError:
                pass

    # ------------------------------------------------------------------ file bridge

    @property
    def room_seed(self) -> typing.Optional[str]:
        # The multiworld's seed name. Released Archipelago (0.6.7 and older) never stores it for a client without a patch
        # file, so it is taken from the server's RoomInfo in on_package; newer Archipelago also keeps it in
        # server_seed_name.
        return self.dr2_seed_name or getattr(self, "server_seed_name", None) or self.seed_name

    def option_lines(self) -> typing.List[str]:
        data = self.slot_data

        def on_off(key: str, default: bool) -> str:
            return "on" if data.get(key, default) else "off"

        return [
            f"option goal {'overtime' if data.get('goal', 0) == 1 else 'the_facts'}",
            f"option timer_mode {data.get('timer_mode', 'vanilla')}",
            f"option case_gates {on_off('case_gate_items', False)}",
            f"option survivors_never_expire {on_off('survivors_never_expire', True)}",
            f"option psychopaths {on_off('include_psychopaths', True)}",
            f"option survivors {on_off('include_survivors', True)}",
            f"option katey_gifts {on_off('include_katey_gifts', True)}",
            f"option movie_posters {on_off('include_movie_posters', True)}",
            f"option kill_milestones {data.get('kill_milestones', 10)}",
            f"option level_checks {data.get('level_checks', 20)}",
            f"option zombrex_checks {data.get('zombrex_checks', 4)}",
        ]

    def write_items(self) -> None:
        if not self.wrote_once:
            logger.info(f"Linked to the game in {self.game_dir}")
            self.wrote_once = True
        lines = [f"seed {self.room_seed}"]
        lines += self.option_lines()
        lines += self.tally_lines()
        for index, item in enumerate(self.items_received, start=1):
            name = self.item_names.lookup_in_game(item.item)
            sender = self.player_names.get(item.player, "") if item.player != self.slot else ""
            lines.append(f"{index}|{name}|{sender}")
        text = "\n".join(lines) + "\n"
        if text == self.last_items_text:
            return
        tmp = self.path("dr2_ap_items.tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(tmp, self.path(ITEMS_FILE))  # atomic: the plugin never sees half a file
        self.last_items_text = text

    def tally_lines(self) -> typing.List[str]:
        """Checks done / total for each category of this seed, and all checks."""
        done = set(self.checked_locations)
        every = done | set(self.missing_locations)
        lines = []
        for category, label in TALLY_LABELS:
            ids = [i for i in every if LOCATION_CATEGORY.get(i) == category]
            if ids:
                lines.append(f"tally {label}|{sum(1 for i in ids if i in done)}|{len(ids)}")
        if every:
            lines.append(f"tally All checks|{len(done & every)}|{len(every)}")
        names = [self.item_names.lookup_in_game(item.item) for item in self.items_received]
        lines.append(f"tally Zombrex received|{names.count(ZOMBREX_ITEM)}|{max(names.count(ZOMBREX_ITEM), 4)}")
        return lines

    async def read_outbox(self) -> None:
        try:
            with open(self.seed_path(OUTBOX_FILE), encoding="utf-8") as f:
                lines = [line.strip() for line in f if line.strip()]
        except FileNotFoundError:
            return
        if len(lines) < self.sent_lines:
            self.sent_lines = 0  # the file was replaced: send everything again (the server ignores repeats)
        new = lines[self.sent_lines:]
        self.sent_lines = len(lines)
        ids = []
        for line in new:
            if line.startswith("GOAL:"):
                self.goal_lines_seen.add(line)
                continue
            loc = location_name_to_id.get(line)
            if loc is None:
                logger.info(f"Game reported '{line}', which is not a location (ignored).")
            elif loc in self.missing_locations:
                ids.append(loc)
        if ids:
            await self.send_msgs([{"cmd": "LocationChecks", "locations": ids}])
        await self.check_goal()

    async def check_goal(self) -> None:
        """Goal 0 The Facts, 1 Overtime. The outbox is read from the top after every connect, so a goal line from an
        earlier session counts too."""
        if self.goal_sent or not self.slot_data:
            return
        wanted = GOAL_LINES.get(self.slot_data.get("goal", 0), GOAL_LINES[0])
        if self.goal_lines_seen & set(wanted):
            self.goal_sent = True
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])
            logger.info("Goal complete!")

    async def send_deaths(self) -> None:
        path = self.path(DEATHS_FILE)
        if not os.path.exists(path):
            return
        try:
            with open(path, encoding="utf-8") as f:
                messages = [line.strip() for line in f if line.strip()]
            os.remove(path)
        except OSError:
            return
        if "DeathLink" in self.tags:
            for _message in messages:
                await self.send_death(f"{self.player_names.get(self.slot, 'Chuck')} died in Fortune City.")


async def game_loop(ctx: DR2Context) -> None:
    while not ctx.exit_event.is_set():
        try:
            if ctx.server and ctx.slot is not None and ctx.room_seed and os.path.isdir(ctx.game_dir):
                ctx.write_items()
                try:   # keep the items file fresh: the plugin only acts while a client is alive
                    os.utime(ctx.path(ITEMS_FILE))
                except OSError:
                    pass
                await ctx.read_outbox()
                await ctx.send_deaths()
        except Exception as e:  # keep the bridge alive whatever happens
            logger.exception(f"Bridge error: {e}")
        await asyncio.sleep(1.0)


def launch(*args: str) -> None:
    async def main(args) -> None:
        ctx = DR2Context(args.connect, args.password)
        ctx.server_task = asyncio.create_task(server_loop(ctx), name="server loop")
        loop = asyncio.create_task(game_loop(ctx), name="DR2 game loop")
        if gui_enabled:
            ctx.run_gui()
        ctx.run_cli()
        await ctx.exit_event.wait()
        loop.cancel()
        ctx.leave_game_folder()
        await ctx.shutdown()

    import colorama

    parser = get_base_parser(description="Dead Rising 2 Archipelago client (test build).")
    parsed = parser.parse_args(args)
    colorama.just_fix_windows_console()
    asyncio.run(main(parsed))
    colorama.deinit()
