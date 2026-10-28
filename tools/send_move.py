"""Waits until the plugin awaits a turn, then writes move.json for it.

Usage: python -I send_move.py <game dir> <turn|next> place|discard|discard_track [card] [track] [row]
Turn ids keep counting across matches; "next" takes whatever turn the plugin is awaiting.
"""
import json
import os
import sys
import time

game_dir, turn, action, *rest = sys.argv[1:]
turn = None if turn == "next" else int(turn)
nums = [int(x) for x in rest] + [-1] * 3
card, track, row = nums[:3]
out_dir = os.path.join(game_dir, "CaravanBot")

deadline = time.time() + 110
while time.time() < deadline:
    try:
        with open(os.path.join(out_dir, "state.json"), encoding="utf-8") as f:
            t = json.load(f)["state"].get("turn", {})
        if t.get("awaiting") and (turn is None or t.get("id") == turn):
            turn = t["id"]
            break
    except (OSError, ValueError):
        pass
    time.sleep(0.3)
else:
    sys.exit(f"turn {turn} was not awaited within the time limit")

move = {"turn": turn, "action": action, "card": card, "track": track, "row": row}
tmp = os.path.join(out_dir, "move.tmp")
with open(tmp, "w", encoding="utf-8") as f:
    json.dump(move, f)
os.replace(tmp, os.path.join(out_dir, "move.json"))
print("sent", move)
