# Nexus Mods page: what to fill in

Upload page: https://www.nexusmods.com/newvegas/mods/add (log in first).

| Field | Value |
|---|---|
| Name | CaravanBot |
| Version | 1.0.0 |
| Category | Gameplay Effects and Changes (or Utilities) |
| Brief overview | Press F8 at any Caravan table and the bot plays the match for you. One xNVSE plugin, no fake input, no game code patched. Press F8 again to take back control. |
| Description | Paste `description.bbcode` (the editor's BBCode/source mode). |
| Requirements | xNVSE (Nexus mod 67883). Better Caravan (81002) as optional/recommended, with the note "developed and tested with it". |
| Permissions and credits | Credit lStewieAl (Better Caravan debug symbols). Source: the GitHub repo. |
| Main file | `build\CaravanBot-1.0.0.zip`, made with `python -I tools\package.py 1.0.0`. Category "Main files". |
| Screenshots | One at a Caravan table with the "Caravan bot: ON" corner message is enough. |

## Before uploading

1. Install `build\CaravanBot-1.0.0.zip` through Vortex and play one match with F8: the bot is now built into the DLL and hasn't run in game yet.
2. Check `Fallout New Vegas\CaravanBot\CaravanBot.log` starts with "CaravanBot 1.0.0 loaded" and shows "Turn ... hand [...]" lines and a "Result:" line.
3. Optional: ask lStewieAl whether they're fine with the credit wording.
