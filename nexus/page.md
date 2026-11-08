# Nexus Mods page: what to fill in

Upload page: https://www.nexusmods.com/newvegas/mods/add (log in first).

| Field | Value |
|---|---|
| Name | CaravanBot |
| Version | 1.0.5 |
| Category | Gameplay Effects and Changes (or Utilities) |
| Brief overview | Press F8 at any Caravan table and the bot plays the match for you. One xNVSE plugin, no fake input, no game code patched. Press F8 again to take back control. |
| Description | Paste `description.bbcode` (the editor's BBCode/source mode). |
| Requirements | xNVSE (Nexus mod 67883). Better Caravan (81002) as optional/recommended, with the note "developed and tested with it". |
| Permissions and credits | Credit lStewieAl (Better Caravan debug symbols). Source: the GitHub repo. |
| Main file | `build\CaravanBot-1.0.5.zip`, made with `python -I tools\package.py 1.0.5`. Category "Main files". |
| Screenshots | One at a Caravan table with the "Caravan bot: ON" corner message is enough. |

## Uploading an update

Mod page: https://www.nexusmods.com/newvegas/mods/100280

1. Build: `build.cmd`, then `python -I tools\package.py <version>`.
2. On the mod page, Manage > Files: add `build\CaravanBot-<version>.zip` as a Main file with the new version, and move the old file to "Old versions" (or archive it).
3. Set the mod's version to match, and add a changelog line.

## Changelog

- 1.0.5: faster moves. The bot now puts the cursor straight on the slot it wants instead of stepping there with the arrow keys, and falls back to the arrows if that ever fails.
- 1.0.4: more reliable moves. The bot now waits until the game will actually accept a click (no animation running, no tutorial popup, opponent's move finished) instead of clicking and hoping. If a move can't be finished, it puts the card back and tries the turn again, instead of leaving a card stuck in your hand. The plugin now only loads on the Steam version it was built for (1.4.0.525), so it can't misbehave on another version. When the game ignores a click, the log now says why.
- 1.0.3: when your deck has fewer than 30 cards, the bot fills it to 45 (or all you own) instead of stopping just past 30, so long matches against aggressive decks don't run you out of cards. The log shows both decks' remaining cards on every move.
- 1.0.2: the bot now tracks each caravan's direction and suit, and uses Queens to unblock its own caravans when its hand can't extend them, instead of playing them as filler.
- 1.0.1: retries the card click up to 6 times instead of 3 (the game can ignore it for about 6 s while a drawn card slides into the hand).
- 1.0.0: first release.
