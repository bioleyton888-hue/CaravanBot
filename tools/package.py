"""Builds the Nexus Mods / Vortex archive from build\\CaravanBot.dll and data\\.

Usage: python -I package.py <version>
The archive mirrors the game's Data folder (NVSE\\Plugins\\...), which is what Vortex, MO2 and a
manual install into Data\\ all expect.
"""
import os
import sys
import zipfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
version = sys.argv[1]
files = {
    "NVSE/Plugins/CaravanBot.dll": os.path.join(root, "build", "CaravanBot.dll"),
    "NVSE/Plugins/CaravanBot.ini": os.path.join(root, "data", "NVSE", "Plugins", "CaravanBot.ini"),
}
out = os.path.join(root, "build", f"CaravanBot-{version}.zip")
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for name, path in files.items():
        z.write(path, name)
with zipfile.ZipFile(out) as z:
    for info in z.infolist():
        print(f"{info.file_size:>8}  {info.filename}")
print(out)
