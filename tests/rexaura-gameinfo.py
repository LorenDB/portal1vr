"""Resolve rexaura_vr/gameinfo.txt against real Portal and Rexaura installs.

Checks, without starting the game, that the search paths reach Portal's
supported client/server DLLs, put the VR menu and materials first, and find
every file Rexaura ships in Rexaura's folder (as Rexaura's own gameinfo.txt
does), with nothing it needs missing from Portal's install.

    python3 tests/rexaura-gameinfo.py <Portal folder> <Rexaura folder>
"""
import re
import struct
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
portal = Path(sys.argv[1])
rexaura = Path(sys.argv[2])
template = (root / "L4D2VR/rexaura_vr/gameinfo.txt").read_text()


def search_paths(text):
    body = re.sub(r"//[^\n]*", "", text)
    block = body[body.index("SearchPaths"):]
    block = block[block.index("{") + 1:block.index("}")]
    return [tuple(line.split(None, 1)) for line in block.splitlines() if line.strip()]


def resolve(location, gameinfo_dir):
    # FileSystem_LoadSearchPaths: |gameinfo_path| is the mod folder; all
    # other paths, |all_source_engine_paths| included, are relative to the
    # folder holding hl2.exe unless absolute.
    if location.startswith("|gameinfo_path|"):
        return (gameinfo_dir / location[len("|gameinfo_path|"):]).resolve()
    location = location.replace("|all_source_engine_paths|", "")
    path = Path(location)
    return (path if path.is_absolute() else portal / path).resolve()


def vpk_files(path):
    data = path.read_bytes()
    version, tree = struct.unpack_from("<II", data, 4)
    pos = 12 if version == 1 else 28

    def read():
        nonlocal pos
        end = data.index(b"\0", pos)
        text = data[pos:end].decode("latin1")
        pos = end + 1
        return text

    files = set()
    while ext := read():
        while directory := read():
            while name := read():
                preload = struct.unpack_from("<H", data, pos + 4)[0]
                pos += 18 + preload
                files.add((("" if directory == " " else directory + "/") + name
                           + ("" if ext == " " else "." + ext)).lower())
    return files


def mounts(gameinfo_dir, text):
    result = []
    for ids, location in search_paths(text):
        path = resolve(location, gameinfo_dir)
        if path.suffix == ".vpk":
            directory = path.with_name(path.stem + "_dir.vpk")
            if directory.exists():
                result.append((ids, directory, vpk_files(directory)))
        elif path.is_dir():
            files = {p.relative_to(path).as_posix().lower() for p in path.rglob("*") if p.is_file()}
            result.append((ids, path, files))
    return result


def find(mounted, name, path_id="game"):
    for ids, where, files in mounted:
        if path_id in ids.split("+") and name.lower() in files:
            return where
    return None


# Installed layout: <Portal>/rexaura_vr, with the installer's path rewrite.
gameinfo_dir = portal / "rexaura_vr"
content = rexaura / "rexaura"
same_library = portal.resolve().parent == rexaura.resolve().parent
location = "../" + rexaura.name + "/rexaura" if same_library else content.resolve().as_posix()
text = template.replace("../Rexaura/rexaura", location)
mounted = mounts(gameinfo_dir, text)
vr_menu = root / "L4D2VR/rexaura_vr"
# The VR menu is installed into rexaura_vr, and materials into portal/custom.
mounted.insert(0, ("game+mod", vr_menu, {"resource/gamemenu.res"}))
gamebin = [resolve(loc, gameinfo_dir) for ids, loc in search_paths(text) if ids == "gamebin"]
assert gamebin == [(portal / "portal/bin").resolve()], gamebin


def pe_timestamp(path):
    data = path.read_bytes()
    return struct.unpack_from("<I", data, struct.unpack_from("<I", data, 0x3C)[0] + 8)[0]


# portaltrace.h and nativepose.h pin these builds.
assert pe_timestamp(portal / "portal/bin/client.dll") == 0x68362D89
assert pe_timestamp(portal / "portal/bin/server.dll") == 0x67578384

assert find(mounted, "resource/gamemenu.res") == vr_menu
materials = portal / "portal/custom/portal1vr"
if materials.is_dir():
    for vmt in ("materials/models/weapons/v_models/v_portalgun/v_portalgun.vmt",
                "materials/models/weapons/v_models/v_hands/v_hands.vmt"):
        assert find(mounted, vmt) == materials.resolve(), vmt

# Rexaura's own gameinfo.txt lists its folder first, so each Rexaura file
# must resolve to Rexaura here too, except the menu the VR copy replaces.
own = [p.relative_to(content).as_posix().lower() for p in content.rglob("*") if p.is_file()]
for name in own:
    if name in ("resource/gamemenu.res", "gameinfo.txt"):
        continue
    assert find(mounted, name) == content.resolve(), name
    assert find(mounted, name, "mod") == content.resolve(), name

# Everything Rexaura's gameinfo.txt mounts from its bundled Portal copy must
# still be found here. (particles/fire_01.pcf left Portal's pack in Valve's
# update; Half-Life 2's hl2_misc pack, also mounted, provides it.)
bundled = vpk_files(rexaura / "portal/portal_pak_dir.vpk")
missing = sorted(name for name in bundled if not find(mounted, name))
assert not missing, missing
manifest = re.sub(r"//[^\n]*", "", (content / "particles/particles_manifest.txt").read_text())
for name in re.findall(r'"file"\s+"([^"]+)"', manifest):
    assert find(mounted, name.lower()), name

maps = sorted(p.name for p in (content / "maps").glob("*.bsp"))
for name in maps:
    assert find(mounted, "maps/" + name) == content.resolve(), name
# The Linux launch wrapper swaps Steam's "-game portal" for the mod folder
# and keeps everything else in order (printf stands in for the game).
wrapper = [str(root / "rexaura-vr-launch.sh"), "printf", "%s\n"]
steam = ["/proton", "waitforexitandrun", "hl2.exe", "-game", "portal", "-steam", "-novid", "+mat_queue_mode", "0"]
ran = subprocess.run(["sh"] + wrapper + steam, capture_output=True, text=True, check=True)
assert ran.stdout.split("\n")[:-1] == [a if a != "portal" else "rexaura_vr" for a in steam], ran.stdout
assert subprocess.run(["sh"] + wrapper + ["hl2.exe", "-steam"], capture_output=True).returncode == 1

print({"maps": len(maps), "rexaura_files": len(own), "gamebin": str(gamebin[0]),
       "rexaura_path": location, "passed": True})
