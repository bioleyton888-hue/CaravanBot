"""Scenario tests for caravan_ai. Run: python -I test_caravan_ai.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import caravan_ai  # noqa: E402

SUITS = {"H": 1, "S": 2, "D": 3, "C": 4}
VALUES = {"A": 1, "J": 12, "Q": 13, "K": 14}


def card(code):
    if code == "JOKER":
        return {"card": code, "value": 15, "suit": 5}
    v = VALUES.get(code[:-1]) or int(code[:-1])
    return {"card": code, "value": v, "suit": SUITS[code[-1]]}


def state(tracks, hand, legal, discard_tracks=(0, 1, 2), can_discard=True):
    """tracks: {t: [["10H", "KC"], ["6H"]]} rows of codes; legal: {hand idx: [(t, r), ...]}."""
    ts = []
    for t in range(6):
        rows = [{"row": r, "cards": [card(c) for c in cells]} for r, cells in enumerate(tracks.get(t, []))]
        ts.append({"index": t, "rows": rows, "value": 0})
    return {
        "playerHand": [card(c) for c in hand],
        "tracks": ts,
        "turn": {
            "legalMoves": [{"card": i, "targets": [{"track": t, "row": r} for t, r in tg]} for i, tg in legal.items()],
            "discardTracks": list(discard_tracks),
            "canDiscardCard": can_discard,
        },
    }


def check(name, s, expect):
    move, reason = caravan_ai.choose_move(s)
    ok = all(move.get(k) == v for k, v in expect.items())
    print(("PASS " if ok else "FAIL ") + name + ": " + reason + " " + str(move))
    return ok


results = [
    # Ringo's track 3 is sold at 26 via a King on its 9; the Jack on that 9 removes 18 points.
    check("jack the sold caravan",
          state({0: [["10S"]], 1: [["10H", "KC"]], 2: [["7H"]], 3: [["8H"], ["9D", "KC"]], 4: [["6H"]], 5: [["6S"]]},
                ["2D", "3C", "6H", "JH", "8H"],
                {0: [(0, 1), (1, 1), (2, 1)], 2: [(0, 1), (1, 1), (2, 1)], 3: [(t, 0) for t in range(6)] + [(3, 1)],
                 4: [(0, 1), (1, 1), (2, 1)]}),
          {"action": "place", "card": 3, "track": 3, "row": 1}),
    # Selling caravan 2 would decide every pair while Ringo leads two of them: never do that.
    check("avoid closing into a loss",
          state({0: [["5S"]], 1: [["8C"]], 2: [["7C"], ["4C"], ["2S"]], 3: [["4H"]],
                 4: [["6S"], ["7D"], ["9S"]], 5: [["6D"], ["9C"], ["4C"], ["2H"]]},
                ["10C", "3H", "QD"],
                {0: [(2, 3), (1, 1)], 1: [(0, 1), (1, 1)], 2: [(0, 0), (1, 0), (2, 2)]}),
          {}),  # checked below: must not be 10C on caravan 2
    # A 6 on 10+K (20) makes 26: the best finish.
    check("finish at 26",
          state({0: [["10S"]], 1: [["10H", "KC"]], 2: [["7H"]], 3: [["8H"], ["7D"]], 4: [["6H"]], 5: [["6S"]]},
                ["2D", "6H", "8H"],
                {0: [(0, 1), (1, 1), (2, 1)], 1: [(0, 1), (1, 1), (2, 1)], 2: [(0, 1), (1, 1), (2, 1)]}),
          {"action": "place", "card": 1, "track": 1}),
    # A King on my 9 sells caravan 0 at 23; on Ringo's 10 it would sell theirs (4 + 20 = 24) instead.
    check("king sells my caravan, not theirs",
          state({0: [["9D"], ["5S"]], 1: [["8C"]], 2: [["7C"]], 3: [["4H"], ["10D"]], 4: [["6S"], ["9S"]], 5: [["6D"], ["9C"]]},
                ["KH", "2D"],
                {0: [(t, r) for t, rows in {0: 2, 1: 1, 2: 1, 3: 2, 4: 2, 5: 2}.items() for r in range(rows)], 1: [(1, 1)]}),
          {"action": "place", "card": 0}),
]

# Lookahead: Ringo's caravan 3 (10D 9D, going down) sells with any 2-6, which it very likely
# holds. Growing my caravan 0 looks best one move deep; with Ringo's reply, the Jack is better.
threat = state({0: [["10S"]], 1: [["8C"]], 2: [["7H"]], 3: [["10D"], ["9D"]], 4: [["6H"]], 5: [["6S"]]},
               ["JH", "9C", "QS"],
               {0: [(t, 0) for t in range(6)] + [(3, 1)], 1: [(0, 1), (1, 1), (2, 1)],
                2: [(0, 0), (1, 0), (2, 0), (3, 1), (4, 0), (5, 0)]})
one_ply, why1 = caravan_ai.choose_move(threat, lookahead=False)
two_ply, why2 = caravan_ai.choose_move(threat)
ok = two_ply.get("card") == 0 and two_ply.get("track") == 3 and two_ply.get("row") == 0
results.append(ok)
print(("PASS " if ok else "FAIL ") + "lookahead jacks the threat: " + why2 + " | one move deep: " + why1)

# The trap test: the chosen move must not end the game.
trap = state({0: [["5S"]], 1: [["8C"]], 2: [["7C"], ["4C"], ["2S"]], 3: [["4H"]],
              4: [["6S"], ["7D"], ["9S"]], 5: [["6D"], ["9C"], ["4C"], ["2H"]]},
             ["10C", "3H", "QD"],
             {0: [(2, 3), (1, 1)], 1: [(0, 1), (1, 1)], 2: [(0, 0), (1, 0), (2, 2)]})
move, reason = caravan_ai.choose_move(trap)
results[1] = not (move.get("card") == 0 and move.get("track") == 2)
print(("PASS" if results[1] else "FAIL") + " trap really avoided: " + reason)

print(f"{sum(results)}/{len(results)} passed")
sys.exit(0 if all(results) else 1)
