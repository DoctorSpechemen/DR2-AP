"""Builds the Archipelago bridge mission and appends it to the game's missions.txt.

The mission runs for the whole game and does nothing by itself. Each part of it has a name ("AP_OnDrop", "AP_OnCall",
...) that the plugin runs on demand, and then does what the game's own missions do: make Chuck drop what he holds, ring the phone, and so on.

Usage: python3 build_ap_mission.py <missions.txt in> <missions.txt out>
"""
import sys

# Texts added to the game's text table for the Wrong Number trap (see strings_tool.py):
AP_CALLER_TEXT = 88801   # "Mr. Archipelago"
AP_CALL_LINES = [(88802, 88801),   # Mr. Archipelago: "Frank?"
                 (88803, 65167),   # Chuck: "No? This is Chuck."   (65167 is the game's own "Chuck")
                 (88804, 88801)]   # Mr. Archipelago: "My bad, wrong number."


POSTERS = ["Paddlesaw", "LaserSword", "HolyArms", "Tenderizer", "FreedomBear", "BlamBow", "SnowballCannon"]
# (event to listen for, name of the counter that records it). These used to be one small mission each, but adding 17
# missions made the game reject every existing save ("corrupt"), so they live inside the one bridge mission instead.
LISTENERS = [(f"PosterUsed_{p}", f"AP_Poster_{p}") for p in POSTERS]


def node(kind, name, props=None, children=None):
    return (kind, name, props or {}, children or [])


def write(n, depth=0):
    kind, name, props, children = n
    tab = "\t" * depth
    lines = [f"{tab}{kind} {name}", f"{tab}{{"]
    for key in sorted(props):
        lines.append(f'{tab}\t{key} = "{props[key]}"')
    for child in children:
        lines += write(child, depth + 1)
    lines += [f"{tab}}}", ""]
    return lines


def on_event(name, actions):
    return node("cMissionOnEvent", f"AP_On{name}", {"EventString": f"AP_{name}"}, actions)


def build():
    children = [
        on_event("Drop", [node("cMissionSetChuckState", "AP_DoDrop", {"ChuckState": "18"})]),
        on_event("Heal", [node("cMissionSetChuckState", "AP_DoHeal", {"ChuckState": "7"})]),
        # A high-priority call rings the transceiver like one of Stacey's (confirmed in game 2026-11-15).
        on_event("Call", [
            node("cMissionPhoneCall", "AP_DoCall", {"DeactivateOnLevelTransition": "false", "HighPriority": "true"}, [
                node("cMissionDialogue", "AP_CallText", dict(
                    [(f"Dialogue{i}", str(text)) for i, (text, _speaker) in enumerate(AP_CALL_LINES)]
                    + [(f"SpeakerID{i}", str(speaker)) for i, (_text, speaker) in enumerate(AP_CALL_LINES)])),
            ]),
        ]),
        # The spawn point is a placeholder the plugin overwrites with Chuck's position before running the part.
        # (11.5, 22.5, 33.5 and 0.75 are easy to recognise in memory.)
        on_event("Horde", [
            node("cMissionSpawnZombie", f"AP_Horde{i}", {"NumberOfZombies": "3", "Rotation": "0.75",
                                                          "SpawnPoint": "11.5,22.5,33.5", "ZombieType": kind})
            for i, kind in enumerate(["zombie_skinny", "zombie_fat", "zombie_fat_female", "zombie_skinny"], start=1)
        ]),
        on_event("Looters", [
            node("cMissionSpawnZombie", "AP_Looter1", {"NumberOfZombies": "3", "Rotation": "0.75",
                                                        "SpawnPoint": "11.5,22.5,33.5", "ZombieType": "looter"}),
        ]),
        on_event("Mercs", [
            node("cMissionSpawnZombie", "AP_Merc1", {"NumberOfZombies": "2", "Rotation": "0.75",
                                                      "SpawnPoint": "11.5,22.5,33.5", "ZombieType": "mercenary"}),
        ]),
        # Experiments (each is run by name from the plugin):
        on_event("PauseClock", [node("cMissionPauseClock", "AP_DoPauseClock")]),
        on_event("SetClock", [node("cMissionSetClock", "AP_DoSetClock", {"Time": "64800"})]),
        on_event("Gate2", [node("cMissionSendEvent", "AP_DoGate2", {"Value": "AP_Case2"})]),
    ]
    # Movie posters and Katey's gifts: when the event is heard, a half-second timer runs once. A timer counts how often
    # it has gone off, and the plugin reads that count to know the poster was examined or the gift handed over.
    for event, name in LISTENERS:
        children.append(node("cMissionOnEvent", name + "_On", {"EventString": event}, [
            node("cMissionTimer", name, {"DeltaTimeSecondsRealTime": "0.5", "TriggerXTimes": "1"}),
        ]))

    mission = node("cMissionDefinition", "AP_Bridge", {
        "Duration": "1209600", "ExplicitStart": "true", "MissionType": "12", "ShowInDebugMenu": "false",
        "ShowInHUD": "false", "StartTime": "0", "WaitUntilEnd": "true"}, children)
    # Test of a mission that waits for a named event (the mechanism case gates would use): it starts, and pays $777,
    # only once "AP_Case2" has been sent. Remove before release.
    gate_test = node("cMissionDefinition", "AP_GateTest", {
        "Duration": "1209600", "ExplicitStart": "true", "MissionType": "12", "ShowInDebugMenu": "false",
        "ShowInHUD": "false", "StartTime": "0", "WaitUntilEnd": "true"}, [
        node("cMissionPrereq", "AP_GateTestPrereq", {}, [
            node("cMissionObjectiveEvent", "AP_GateTestEvent", {"EventString": "AP_Case2"}),
        ]),
        node("cMissionOnEnter", "AP_GateTestEnter", {}, [
            node("cMissionSetChuckState", "AP_GateTestMoney", {"ChuckState": "9", "Value": "777"}),
        ]),
    ])

    return "\r\n".join(write(mission) + write(gate_test)) + "\r\n"


if __name__ == "__main__":
    original = open(sys.argv[1], "rb").read()
    assert b"cMissionDefinition AP_Bridge" not in original, "the bridge mission is already in this file"
    # The game's own missions are left exactly as they are: a save stops loading ("corrupt") when one of them
    # is changed (seen twice with an edit to GiftsForKatie, 2026-11-15).
    text = original.rstrip(b"\r\n") + b"\r\n\r\n" + build().encode("ascii") + b"\r\n"
    open(sys.argv[2], "wb").write(text)
    print(f"wrote {sys.argv[2]}: {len(text)} bytes (was {len(original)})")
