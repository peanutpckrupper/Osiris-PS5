#!/usr/bin/env python3
"""Assemble the Halo 5 Campaign PS5 app (PPSA99905), the same way the Installation 08 app (PPSA99908) is made:
  --title     tools/build_native.sh's dist/PPSA99905 (eboot, libc.prx, lapy.elf, sce_sys), built from prospero-win
              644e32e + installation08-app.patch with -DPW_FIXED_PROFILE="halo5campaign" -DPW_DATA_NAME="halo5campaign"
  --template  the Installation 08 PS5 app folder (PPSA99908 from Installation08-PS5-1.3.zip): its Wine runtime (win/),
              licences, SOURCES.txt and its ready 64-bit prefix (Wine + DXVK; placeholders listed, not shipped)
  --vkd3d     a vkd3d-proton release folder (x64/d3d12.dll, x64/d3d12core.dll): Direct3D 12 on Vulkan
  --extra     this folder (bundle/profiles, bundle/Halo5Campaign, README-PS5.txt)
  --out       <dir>/PPSA99905; writes <dir>.zip beside it
The player's own game folder (built by Halo5CampaignSetup.exe from their dumps) is never part of the package: they
copy it to /data/halo5campaign/Halo5Campaign on the console."""
import argparse, shutil, zipfile
from pathlib import Path

ap = argparse.ArgumentParser()
for name in ("title", "template", "vkd3d", "extra", "patch", "out"):
    ap.add_argument("--" + name, type=Path, required=name != "patch")
a = ap.parse_args()
out = a.out
shutil.rmtree(out, ignore_errors=True)
shutil.copytree(a.title, out)
t = a.template
for name in ("LICENSE", "LICENSES", "SOURCES.txt", "mesa-zink-manifest.json"):
    src = t / name
    if src.exists(): (shutil.copytree if src.is_dir() else shutil.copy2)(src, out / name)
shutil.copytree(t / "win", out / "win")

# Prefix: Installation 08's, renamed, without Installation 08 itself.
prefix = out / "bundle/prefixes/halo5campaign"
shutil.copytree(t / "bundle/prefixes/installation08", prefix)
shutil.rmtree(prefix / "drive_c/Games/Installation08")
(prefix / "drive_c/Games/Halo5Campaign").mkdir(parents=True)   # restart.flag location (PW_RESTART_FLAG)
system32 = prefix / "drive_c/windows/system32"
for dll in ("d3d12.dll", "d3d12core.dll"):
    shutil.copy2(a.vkd3d / "x64" / dll, system32 / dll)
# Placeholders (Wine's stand-in DLLs, made on first start) under the new prefix name.
lines = [l.replace("prefixes/installation08/", "prefixes/halo5campaign/", 1) for l in (t / "placeholders.txt").read_text().splitlines(True)]
lines = [l for l in lines if "/drive_c/Games/Installation08/" not in l]
# Halo 5 also needs Wine's XAudio2 9 and (for d3dcompiler_47 -> wined3d) opengl32, which Installation 08 doesn't.
for dll in ("xaudio2_9.dll", "opengl32.dll"):
    entry = f"prefixes/halo5campaign/drive_c/windows/system32/{dll}\tx86_64-windows\n"
    if (out / "win/wine/lib/wine/x86_64-windows" / dll).exists() and entry not in lines: lines.append(entry)
(out / "placeholders.txt").write_text("".join(lines))

shutil.copytree(a.extra / "bundle", out / "bundle", dirs_exist_ok=True)
shutil.copy2(a.extra / "README-PS5.txt", out / "README.txt")
if a.patch: shutil.copy2(a.patch, out / a.patch.name)
vkd3d_name = a.vkd3d.name
src = (out / "SOURCES.txt").read_text()
src = src.replace("PPSA99908", "PPSA99905").replace('"installation08"', '"halo5campaign"').replace('"installation08-wine64"', '"halo5campaign-wine64"')
src = src.replace("drive_c/Games/Installation08/restart.flag", "drive_c/Games/Halo5Campaign/restart.flag")
src = "\n".join(l for l in src.splitlines() if "installation08.exe" not in l.lower() and "ucrtbase" not in l.lower()) + "\n"
src += (f"\nvkd3d-proton  {vkd3d_name}: x64/d3d12.dll and x64/d3d12core.dll in bundle/prefixes/halo5campaign/drive_c/windows/system32\n"
        "  (https://github.com/HansKristian-Work/vkd3d-proton, LGPL 2.1).\n"
        "The prefix in bundle/prefixes/halo5campaign is Installation 08 1.3's (Wine and DXVK as listed above) without\n"
        "Installation 08; Wine's placeholder files are made from win/ on first start (placeholders.txt).\n"
        "No game files: the player copies their own Halo5Campaign folder (made by Halo5CampaignSetup.exe) to the console.\n")
(out / "SOURCES.txt").write_text(src)
lic = out / "LICENSES"
(lic / "vkd3d-proton-LGPL-2.1.txt").write_text((a.vkd3d / "LICENSE").read_text() if (a.vkd3d / "LICENSE").exists() else
    "vkd3d-proton is licensed under the GNU Lesser General Public License 2.1: https://github.com/HansKristian-Work/vkd3d-proton/blob/master/LICENSE\n")

zpath = out.parent / (out.parent.name + ".zip")
zpath.unlink(missing_ok=True)
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for p in sorted(out.rglob("*")):
        arc = Path(out.name) / p.relative_to(out)
        z.write(p, arc.as_posix() + "/" if p.is_dir() else arc)
size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
print(f"{out}: {size >> 20} MB, {len(lines)} placeholders; {zpath} {zpath.stat().st_size >> 20} MB")
