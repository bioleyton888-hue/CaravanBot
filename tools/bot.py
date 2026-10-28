"""Plays Caravan through CaravanBot using caravan_ai, as fast as the plugin allows.

Run it as a Monitor command: every line it prints is one event (match start, each move with
its reason, errors, the result). It also saves each state it acts on to captures/.
Usage: python -I bot.py <game dir> <captures dir> [--suggest]
  --suggest  only print the engine's choice instead of playing it (Claude plays by hand).
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import caravan_ai  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith("--")]
game_dir, captures = args[0], args[1]
suggest_only = "--suggest" in sys.argv
out_dir = os.path.join(game_dir, "CaravanBot")
state_path = os.path.join(out_dir, "state.json")
os.makedirs(captures, exist_ok=True)


def say(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def read_state():
    try:
        with open(state_path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def send(turn_id, move):
    tmp = os.path.join(out_dir, "move.tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(dict(move, turn=turn_id), f)
    os.replace(tmp, os.path.join(out_dir, "move.json"))


last_seq = None
acted = {}            # turn id -> list of moves already sent for it
last_menu = None
last_result = None
say("bot started (" + ("suggest only" if suggest_only else "auto-play") + ")")
while True:
    doc = read_state()
    if not doc or doc.get("seq") == last_seq:
        time.sleep(0.15)
        continue
    last_seq = doc["seq"]
    s = doc["state"]
    if not s.get("active"):
        if last_menu is not None:
            say("caravan menu closed")
        last_menu = None
        continue

    menu = s.get("menu")
    turn = s.get("turn", {})
    if menu != last_menu:
        if menu == "game":
            say(f"match {turn.get('match')} started, ante {s.get('ante')}")
        elif menu == "results":
            won = bool(int(s.get("flags", "0x0"), 16) & 0x400)
            say(f"RESULT: {'WON' if won else 'LOST'} | final mine "
                + "/".join(str(t['value']) for t in s['tracks'][:3]) + " vs npc "
                + "/".join(str(s['tracks'][i]['value']) for i in (5, 4, 3)))
        last_menu = menu

    result = turn.get("lastResult", "")
    if result != last_result:
        if result.startswith(("error", "rejected")) or "did not change" in result:
            say("PLUGIN: " + result)
        last_result = result

    if menu != "game" or not turn.get("awaiting"):
        continue
    tid = turn["id"]
    tried = acted.setdefault(tid, [])
    if len(tried) >= 3:
        continue
    if tried and not result.startswith("rejected"):
        continue

    with open(os.path.join(captures, f"bot_{tid:05d}.json"), "w", encoding="utf-8") as f:
        json.dump(doc, f)
    move, reason = caravan_ai.choose_move(s, exclude=tried)
    if move is None:
        say(f"turn {turn.get('number')}: {reason}")
        tried.append(None)
        continue
    hand = " ".join(c["card"] for c in s["playerHand"])
    if suggest_only:
        say(f"SUGGEST turn {tid} (#{turn.get('number')}) hand [{hand}]: {reason} | {json.dumps(move)}")
        tried.append(move)
        continue
    send(tid, move)
    tried.append(move)
    say(f"turn #{turn.get('number')} hand [{hand}]: {reason}")
