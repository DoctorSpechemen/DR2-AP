"""Patches your own copy of Dead Rising 2 for DR2AP, or puts it back.

    python patch_game.py                      patch the game in the usual Steam folder
    python patch_game.py "<game folder>"      patch the game somewhere else
    python patch_game.py --restore ["<game folder>"]   put the original files back

What it changes, with the game closed:
    data/datafile.big           the mod's hidden mission (AP_Bridge) is added to missions.txt
    data/frontend/str_en.bcs    four lines of text for the phone call

The originals are kept beside them as "<name>.original" the first time, and every later run starts from
those originals, so running this twice is harmless. Nothing of the game's is copied anywhere else.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2"
TEXTS = ["88801=Mr. Archipelago", "88802=Frank?", "88803=No? This is Chuck.", "88804=My bad, wrong number."]
MARKER = b"cMissionDefinition AP_Bridge"


def run(script, *args):
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True, stdout=subprocess.DEVNULL)


def original_of(path):
    """Returns the untouched copy of a game file, making it from the file itself the first time."""
    original = path + ".original"
    if not os.path.exists(original):
        shutil.copy2(path, original)
        print(f"  kept the original as {os.path.basename(original)}")
    return original


def is_patched_archive(path, work):
    """True if this datafile.big already holds the mod's mission."""
    probe = os.path.join(work, "probe")
    os.makedirs(probe, exist_ok=True)
    run("unpack.py", path, probe)
    with open(os.path.join(probe, "missions.txt"), "rb") as handle:
        return MARKER in handle.read()


def patch(game):
    datafile = os.path.join(game, "data", "datafile.big")
    strings = os.path.join(game, "data", "frontend", "str_en.bcs")
    for path in (datafile, strings):
        if not os.path.exists(path):
            sys.exit(f"Not found: {path}\nIs this the Dead Rising 2 folder? Give the folder as an argument.")

    with tempfile.TemporaryDirectory() as work:
        if not os.path.exists(datafile + ".original") and is_patched_archive(datafile, work):
            sys.exit("data/datafile.big is already patched and there is no datafile.big.original to start from.\n"
                     "Use Steam's \"Verify integrity of game files\" to get the original back, then run this again.")

        print("Game data (data/datafile.big):")
        source = original_of(datafile)
        unpacked = os.path.join(work, "unpacked")
        os.makedirs(unpacked)
        run("unpack.py", source, unpacked)
        missions = os.path.join(work, "missions_ap.txt")
        run("build_ap_mission.py", os.path.join(unpacked, "missions.txt"), missions)
        built = os.path.join(work, "datafile_ap.big")
        run("repack.py", source, built, "missions.txt=" + missions)

        print("Text (data/frontend/str_en.bcs):")
        text_source = original_of(strings)
        built_text = os.path.join(work, "str_en_ap.bcs")
        run("strings_tool.py", text_source, built_text, *TEXTS)

        # Only now, with both built, is anything of the game's replaced.
        shutil.copyfile(built, datafile)
        shutil.copyfile(built_text, strings)
    print("Done. The game is patched for DR2AP.")


def restore(game):
    put_back = 0
    for path in (os.path.join(game, "data", "datafile.big"), os.path.join(game, "data", "frontend", "str_en.bcs")):
        if os.path.exists(path + ".original"):
            shutil.copyfile(path + ".original", path)
            put_back += 1
            print(f"  restored {os.path.relpath(path, game)}")
    print("Done." if put_back else "No .original files were found; nothing was changed.")


if __name__ == "__main__":
    arguments = [a for a in sys.argv[1:] if a != "--restore"]
    game_folder = arguments[0] if arguments else DEFAULT_GAME
    try:
        restore(game_folder) if "--restore" in sys.argv[1:] else patch(game_folder)
    except PermissionError as error:
        sys.exit(f"Could not write a game file ({error}).\nClose the game first. If it is closed, run this as administrator.")
    except subprocess.CalledProcessError as error:
        sys.exit(f"A step failed: {error}\nNothing of the game's was changed.")
