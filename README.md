# CaravanBot

A bot that plays **Caravan**, the card minigame in *Fallout: New Vegas*, for you.

Sit down at a Caravan table, press **F8**, and the bot plays the rest of the match on its own. Press F8 again to take back control. It sends no fake mouse or keyboard input: an xNVSE plugin reads the table straight from the game's memory, decides the move itself, and plays it through the Caravan menu's own cursor and buttons. Everything runs inside the one DLL; there is nothing else to install or run.

**Download:** [CaravanBot on Nexus Mods](https://www.nexusmods.com/newvegas/mods/100280)

## Requirements

- *Fallout: New Vegas*, Steam version, runtime **1.4.0.525**. The plugin reads fixed memory addresses, so other game versions won't work.
- [xNVSE](https://github.com/xNVSE/NVSE)
- Recommended: [Better Caravan](https://www.nexusmods.com/newvegas/mods/81002) by lStewieAl. The bot was developed and tested with it installed.

## Install

**With Vortex (or Mod Organizer 2):** install the `CaravanBot-<version>.zip` from the [Nexus Mods page](https://www.nexusmods.com/newvegas/mods/100280) like any other mod. Drag it onto Vortex's Mods page, or use "Mod Manager Download", then enable and deploy it.

**By hand:** unzip it into the game's `Data` folder, so that you end up with:

```
Fallout New Vegas\Data\NVSE\Plugins\CaravanBot.dll
Fallout New Vegas\Data\NVSE\Plugins\CaravanBot.ini
```

## Usage

- **Start a Caravan game.** The first time each session, a corner message tells you which key turns the bot on.
- **Press F8 to turn the bot on.** You'll see "Caravan bot: ON" and hear a menu sound. It plays every one of your turns.
- **Press F8 again to turn it off** and play yourself. You can switch at any point of a match.
- **Deck screen:** with the bot on, the plugin accepts your deck and presses Play for you. If your deck has fewer than 30 cards, it fills it from your collection to 45 cards (or all you own), so long matches don't run it dry: running out of cards loses the match. A deck of 30 or more that you built is played as it is. With the bot off, you build your deck yourself.

The plugin writes what it's doing, including every move and why it chose it, to `Fallout New Vegas\CaravanBot\CaravanBot.log`. The log starts fresh each time you launch the game.

## Settings

`Data\NVSE\Plugins\CaravanBot.ini`:

```ini
[Bot]
; DirectInput key code: 0x42 = F8, 0x43 = F9, 0x44 = F10, 0x57 = F11
ToggleKey=0x42
; 1 = also let an outside program play (see "External control" below)
ExternalControl=0
```

## Uninstall

Remove the mod in Vortex, or delete the two files from `Data\NVSE\Plugins`. The `Fallout New Vegas\CaravanBot` folder only holds the log and can be deleted too.

## How it works

- **The plugin** (`src/CaravanBot.cpp`) runs on xNVSE's main-loop message and patches no game code.
  - While the Caravan menu is open, it reads the table: your hand, every caravan and its rows, and your opponent's hand size only, never their cards.
  - It detects your turn from the engine's own input flags, and asks the engine's own rule check which placements are legal.
- **The strategy** (`src/caravan_ai.h`) picks the move.
- **Carrying out the move:** the plugin moves the menu's cursor to the card and the target, and presses the menu's Select or Discard button. If the game ignores a click (it sometimes does on a card that is still sliding into your hand), it clicks again.

### Strategy

The engine looks one move ahead over every legal move and discard. It also weighs the opponent's most damaging likely reply, estimated from their hand size, since their cards are hidden.

It aims for caravans worth 21 to 26 and avoids going over. It uses Jacks to remove threats and Kings to push an opponent's caravan past 26.

It also tracks each caravan's direction and suit, the way the game does, so it knows which of your cards still fit where. It prefers moves that leave each of your unsold caravans a card from your hand to grow with. That's what Queens are for: when a caravan is stuck going the wrong way for your hand, the bot turns it with a Queen instead of wasting the Queen as filler.

`tools/caravan_ai.py` is the original engine in Python, without directions or Queens. `tests\run_parity.cmd` replays every saved turn in `captures\` through both and lists the turns where they pick different moves, which is a handy way to review what a strategy change does.

## External control

With `ExternalControl=1`, anything that can read and write two JSON files can play instead of the built-in bot (while the bot is off):

```
Fallout: New Vegas ──(CaravanBot.dll)──> CaravanBot\state.json
                                                  │
                                     your program decides
                                                  │
Caravan menu  <──(plugin plays it)──  CaravanBot\move.json
```

- `state.json` holds the table, and on your turn `turn.awaiting`, a turn id and the legal moves.
- `move.json` holds one move for that turn id:

  ```json
  {"turn": 17, "action": "place", "card": 3, "track": 2, "row": -1}
  {"turn": 17, "action": "discard", "card": 0}
  {"turn": 17, "action": "discard_track", "track": 1}
  ```

  - `card` is the index in your hand.
  - Tracks 0 to 2 are your caravans, left to right. Tracks 5, 4 and 3 are your opponent's, left to right.
  - `row: -1` means any legal row on that track.
  - An illegal move is refused and the turn stays open.

| File | What it does |
|---|---|
| `tools/bot.py` | The Python auto-player: `python -I bot.py <game dir> <captures dir>`. Add `--suggest` to print its choice without playing it. |
| `tools/watch_state.py` | Prints a line for each change at the table and saves every state to the captures folder. |
| `tools/send_move.py` | Sends one move by hand: `python -I send_move.py <game dir> next place <card> <track> [row]`. |

## Building

You need Visual Studio 2022 Build Tools (C++).

- `build.cmd` builds `build\CaravanBot.dll` (32-bit).
- `tests\run_tests.cmd` runs the C++ engine's strategy tests on hand-built tables.
- `tests\run_parity.cmd` lists the saved turns where the C++ and Python engines differ (needs Python 3.10+). Add a line number to `build\ai_parity.exe build\parity.txt <line>` to see every candidate move's score for that turn.
- `python -I tools\test_caravan_ai.py` runs the Python engine's tests.
- `python -I tools\package.py <version>` builds the Nexus archive, `build\CaravanBot-<version>.zip`.

## Known limitations

- Queens are only used on your own caravans. The bot doesn't use them to block the opponent's caravans.
- If the cursor can't reach its target while a card is picked up, the plugin can't cancel. Finish that move by hand.
- **Alt-tabbing:** on some setups, switching out of the game crashes it. The bot doesn't need you to leave the game.

## Changelog

- **1.0.3:** the deck auto-fill builds a 45-card deck instead of stopping just past 30. Against opponents full of Jacks, Jokers and Kings, a 34-card deck ran out before the match ended. The log also shows how many cards are left in both decks on every move.
- **1.0.2:** the bot tracks each caravan's direction and suit, and uses Queens to unblock its own caravans when its hand can't extend them, instead of playing them as filler.
- **1.0.1:** retries a card click up to 6 times instead of 3. The game can ignore it for a few seconds while a drawn card slides into the hand.
- **1.0.0:** first release. The strategy engine runs inside the plugin; no Python needed.

## Credits

The Caravan menu's memory layout and engine function addresses come from the debug symbols that lStewieAl ships with [Better Caravan](https://www.nexusmods.com/newvegas/mods/81002).
