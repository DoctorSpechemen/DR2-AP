from . import DR2TestBase
from ..Data import CASE_GATE_ITEMS


class TestDefaults(DR2TestBase):
    """Default options: The Facts goal, no case gates."""


class TestOvertimeEverything(DR2TestBase):
    options = {"goal": "overtime", "case_gate_items": True, "kill_milestones": 15, "level_checks": 49, "zombrex_checks": 8,
               "trap_percentage": 100}


class TestMinimal(DR2TestBase):
    options = {"include_psychopaths": False, "include_survivors": False, "kill_milestones": 0, "level_checks": 0,
               "zombrex_checks": 0, "include_katey_gifts": False, "include_movie_posters": False,
               "case_gate_items": True}


class TestCaseGates(DR2TestBase):
    options = {"case_gate_items": True}

    def test_each_case_needs_its_gate_and_the_ones_before(self):
        self.collect_all_but(list(CASE_GATE_ITEMS.values()))
        self.assertTrue(self.can_reach_location("Case 1-4: Alliance"))
        self.assertTrue(self.can_reach_location("Rescue Esther Alwin"))          # Case 1 stretch of the clock
        self.assertFalse(self.can_reach_location("Case 2-1: Sign of Life"))
        self.assertFalse(self.can_reach_location("Rescue Sven Blaaborg"))        # opens during Case 2
        self.collect_by_name("Case 3 Missions")                                  # out of order: still stuck behind Case 2
        self.assertFalse(self.can_reach_location("Case 3-1: Boom Town"))
        self.collect_by_name("Case 2 Missions")
        self.assertTrue(self.can_reach_location("Case 2-2: Ticket to Ride"))
        self.assertTrue(self.can_reach_location("Case 3-2: Run for the Money"))
        self.assertFalse(self.can_reach_location("Case 4-1: The Source"))
        self.assertFalse(self.can_reach_location("The Facts Complete"))
        for case in range(4, 8):
            self.collect_by_name(f"Case {case} Missions")
        self.assertTrue(self.can_reach_location("The Facts Complete"))

    def test_counted_checks_never_wait_for_a_case(self):
        self.collect_all_but(list(CASE_GATE_ITEMS.values()))
        self.assertTrue(self.can_reach_location("Kill 1,000 Zombies"))
        self.assertTrue(self.can_reach_location("Reach Level 5"))
        self.assertTrue(self.can_reach_location("Zombrex Found 1"))


class TestNoGates(DR2TestBase):
    options = {"case_gate_items": False}

    def test_no_gate_items_in_pool(self):
        names = {item.name for item in self.multiworld.itempool}
        self.assertFalse(names & set(CASE_GATE_ITEMS.values()))
        self.assertTrue(self.can_reach_location("Case 7-3: What Lies Beneath"))
