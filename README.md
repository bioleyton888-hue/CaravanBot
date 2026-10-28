# CaravanBot

A bot that plays **Caravan**, the card minigame in *Fallout: New Vegas*, for you.

Sit down at a Caravan table, press **F8**, and the bot plays the rest of the match on its own. Press F8 again to take back control. It sends no fake mouse or keyboard input: an xNVSE plugin reads the table straight from the game's memory and plays through the Caravan menu's own cursor and buttons.

## Requirements

- *Fallout: New Vegas*, Steam version, runtime **1.4.0.525**. The plugin reads fixed memory addresses, so other game versions won't work.
- [xNVSE](https://github.com/xNVSE/NVSE)
- **Python 3.10 or newer** on Windows. The bot uses only the standard library. It was tested with Python 3.11.
- Recommended: [Better Caravan](https://www.nexusmods.com/newvegas/mods/81002) by lStewieAl. The bot was developed and tested with it installed.
- To build the plugin yourself: Visual Studio 2022 Build Tools (C++).

## Install

1. **Build the plugin.** Run `build.cmd`. It creates `build\CaravanBot.dll` (32-bit).
2. **Copy the DLL.** Close the game first, because Windows locks the DLL while the game runs. Then copy `build\CaravanBot.dll` into `Fallout New Vegas\Data\NVSE\Plugins\`.
3. **Start the game once.** The plugin creates a `Fallout New Vegas\CaravanBot\` folder.
4. **Create the settings file.** In that folder, create `CaravanBot.ini` and point it at your Python and at this repo:

   ```ini
   [Bot]
   Python=C:\Path\To\Python311\python.exe
   Script=C:\Path\To\CaravanBot\tools\bot.py
   Captures=C:\Path\To\CaravanBot\captures
   ; DirectInput key code: 0x42 = F8 (0x43 = F9, 0x44 = F10, 0x57 = F11)
   ToggleKey=0x42
   ```

   Use the full path to `python.exe`, not a launcher shim.

## Usage

- **Start a Caravan game.** The first time each session, a corner message tells you which key turns the bot on.
- **Press F8 to turn the bot on.** You'll see "Caravan bot: ON" and hear a menu sound. The bot starts hidden, with no window, and plays every turn.
- **Press F8 again to turn it off** and play yourself.
- **Closing the game also stops the bot.** It never keeps running in the background.
- **Deck screen:** with the bot on, the plugin accepts your deck and presses Play for you. If your deck has fewer than 30 cards, it tries to fill it, but that part hasn't been tested yet. With the bot off, you build your deck yourself.

The bot writes what it's doing to `Fallout New Vegas\CaravanBot\bot.log`. The plugin's own log is `CaravanBot.log` in the same folder.

## How it works

```
Fallout: New Vegas ──(CaravanBot.dll reads memory)──> CaravanBot\state.json
                                                              │
                                                     tools/bot.py decides
                                                              │
Caravan menu  <──(plugin moves the cursor, clicks)──  CaravanBot\move.json
```

- **The plugin** (`src/CaravanBot.cpp`) runs on xNVSE's main-loop message and patches no game code.
  - While the Caravan menu is open, it writes the table to `state.json`: your hand, every caravan and its total, and your opponent's hand size only, never their cards.
  - When it's your turn, it adds a turn id and the list of legal moves, taken from the game's own rule check.
- **The bot** (`tools/bot.py`) reads `state.json`, picks a move with `tools/caravan_ai.py`, and writes it to `move.json`.
- **Carrying out the move:** the plugin checks it against the legal moves, moves the menu's cursor to the card and the target, and presses the menu's Select or Discard button.

### move.json

```json
{"turn": 17, "action": "place", "card": 3, "track": 2, "row": -1}
{"turn": 17, "action": "discard", "card": 0}
{"turn": 17, "action": "discard_track", "track": 1}
```

- **Fields:**
  - `card` is the index in your hand.
  - Tracks 0 to 2 are your caravans, left to right. Tracks 5, 4 and 3 are your opponent's, left to right.
  - `row: -1` means any legal row on that track.
- **Rejected moves:** an illegal move is refused and the turn stays open.
- **Other players:** anything that can read and write these two JSON files can play, including a script of your own or an AI agent.

### Strategy

`caravan_ai.choose_move` looks one move ahead over every legal move and discard. It also weighs the opponent's most damaging likely reply, estimated from their hand size, since their cards are hidden.

It aims for caravans worth 21 to 26 and avoids going over. It uses Jacks to remove threats and Kings to push an opponent's caravan past 26.

## Tools

| File | What it does |
|---|---|
| `tools/bot.py` | The auto-player. F8 starts it, or run it yourself: `python -I bot.py <game dir> <captures dir>`. Add `--suggest` to print its choice without playing it. |
| `tools/caravan_ai.py` | The strategy engine. |
| `tools/test_caravan_ai.py` | Strategy tests. Run with `python -I test_caravan_ai.py`. |
| `tools/watch_state.py` | Prints a line for each change at the table and saves every state to the captures folder. |
| `tools/send_move.py` | Sends one move by hand: `python -I send_move.py <game dir> next place <card> <track> [row]`. |

## Known limitations

- Queens are treated as filler. The engine doesn't plan around changing a caravan's direction.
- If the cursor can't reach its target while a card is picked up, the plugin can't cancel. Finish that move by hand.
- **Alt-tabbing:** on some setups, switching out of the game crashes it. The bot doesn't need you to leave the game.

## Credits

The Caravan menu's memory layout and engine function addresses come from the debug symbols that lStewieAl ships with [Better Caravan](https://www.nexusmods.com/newvegas/mods/81002).
