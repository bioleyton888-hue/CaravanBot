"""Exports saved turns and caravan_ai's choice for each, for tests/ai_parity.cpp.

Usage: python -I export_parity.py <captures dir> <output file>
Each line is whitespace-separated integers:
  opponentHand canDiscardCard
  nHand {value suit}...
  for each of the 6 tracks: nRows {value suit kings}...
  nLegal {card nTargets {track row}...}...
  nDiscardTracks {track}...
  expected: action(1 place, 2 discard, 3 discard_track) card track row
"""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import caravan_ai  # noqa: E402

ACTIONS = {"place": 1, "discard": 2, "discard_track": 3}

captures, out_path = sys.argv[1], sys.argv[2]
lines = []
for path in sorted(glob.glob(os.path.join(captures, "bot_*.json"))):
    with open(path, encoding="utf-8") as f:
        s = json.load(f)["state"]
    turn = s.get("turn", {})
    if not turn.get("awaiting"):
        continue
    move, _ = caravan_ai.choose_move(s)
    if move is None:
        continue
    nums = [s.get("opponentHandCount", 5), 1 if turn.get("canDiscardCard") else 0]
    hand = s["playerHand"]
    nums.append(len(hand))
    for c in hand:
        nums += [c["value"], c["suit"]]
    for t in s["tracks"]:
        rows = sorted(t["rows"], key=lambda r: r["row"])
        nums.append(len(rows))
        for r in rows:
            cards = r["cards"]
            nums += [cards[0]["value"], cards[0]["suit"], sum(1 for c in cards[1:] if c["value"] == caravan_ai.KING)]
    legal = turn.get("legalMoves", [])
    nums.append(len(legal))
    for lm in legal:
        nums += [lm["card"], len(lm["targets"])]
        for tg in lm["targets"]:
            nums += [tg["track"], tg["row"]]
    dts = turn.get("discardTracks", [])
    nums.append(len(dts))
    nums += dts
    nums += [ACTIONS[move["action"]], move.get("card", -1), move.get("track", -1), move.get("row", -1)]
    lines.append(" ".join(str(n) for n in nums))

with open(out_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print(f"{len(lines)} turns written to {out_path}")
