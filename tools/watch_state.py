"""Watches CaravanBot's state.json while the game runs.

Every new version of the file is saved to captures/<seq>.json. One summary line is printed
per meaningful change (cursor moves alone are saved but not printed), plus lines when the
plugin log changes or the game process starts or stops.
Usage: python -I watch_state.py <game dir> <captures dir>
"""
import json
import os
import shutil
import subprocess
import sys
import time

game_dir, captures = sys.argv[1], sys.argv[2]
state_path = os.path.join(game_dir, "CaravanBot", "state.json")
log_path = os.path.join(game_dir, "CaravanBot", "CaravanBot.log")
os.makedirs(captures, exist_ok=True)


def say(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq FalloutNV.exe", "/NH"], capture_output=True, text=True).stdout
    return "FalloutNV.exe" in out


def summarize(doc):
    s = doc.get("state", {})
    if not s.get("active"):
        return "caravan menu closed"
    if "error" in s:
        return "ERROR " + s["error"]
    hand = " ".join(f"{i}:{c['card']}" for i, c in enumerate(s.get("playerHand", [])))
    tracks = []
    for t in s.get("tracks", []):
        cards = "+".join("/".join(c["card"] for c in row["cards"]) for row in t["rows"]) or "-"
        tracks.append(f"t{t['index']}={t['value']}[{cards}]")
    turn = s.get("turn", {})
    prefix = ""
    legal = ""
    if turn.get("awaiting"):
        prefix = (f"AWAITING turn {turn['id']} (match {turn.get('match', '?')} #{turn.get('number', '?')}"
                  + (", opening" if turn.get("opening") else "") + ") | ")
        moves = [f"{m['card']}:{m['code']}@" + ",".join(f"{x['track']}.{x['row']}" for x in m["targets"])
                 for m in turn.get("legalMoves", [])]
        legal = (" | legal: " + (" ".join(moves) or "none")
                 + f" | discardTracks={turn.get('discardTracks', [])} canDiscardCard={turn.get('canDiscardCard')}")
    return (prefix + f"{s['menu']} stage={s['stage']} ante={s['ante']} deck={s['playerDeckCount']}/{s['opponentDeckCount']} "
            f"npcHand={s['opponentHandCount']} | hand: {hand} | " + " ".join(tracks)
            + f" | last: {turn.get('lastResult', '-')}" + legal)


last_mtime = last_log_mtime = 0.0
last_summary = None
was_running = None
while True:
    try:
        running = game_running()
        if running != was_running:
            say("game process " + ("RUNNING" if running else "NOT running"))
            was_running = running
        if os.path.exists(log_path) and os.path.getmtime(log_path) != last_log_mtime:
            last_log_mtime = os.path.getmtime(log_path)
            with open(log_path, encoding="utf-8", errors="replace") as f:
                say("plugin log: " + f.read().strip().splitlines()[-1])
        if os.path.exists(state_path) and os.path.getmtime(state_path) != last_mtime:
            last_mtime = os.path.getmtime(state_path)
            with open(state_path, encoding="utf-8") as f:
                raw = f.read()
            doc = json.loads(raw)
            with open(os.path.join(captures, f"{doc['seq']:05d}.json"), "w", encoding="utf-8") as f:
                f.write(raw)
            summary = summarize(doc)
            if summary != last_summary:
                say(f"#{doc['seq']} {summary}")
                last_summary = summary
    except Exception as e:  # a half-written file or a tasklist hiccup should not stop the watch
        say(f"watcher error: {e!r}")
    time.sleep(1)
