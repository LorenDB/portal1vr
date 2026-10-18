"""Package a built x86 runtime with its installer, bindings and materials."""
from pathlib import Path
import argparse,hashlib,json,zipfile

repo=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();args.output.parent.mkdir(parents=True,exist_ok=True)
files={}
for name in ['Release/d3d9.dll','Launch Portal VR.cmd','Launch Rexaura VR.cmd','rexaura-vr-launch.sh','Uninstall.ps1','Uninstall.cmd','L4D2VR/copy-to-portal.ps1',
             'L4D2VR/config.txt','L4D2VR/manifest.vrmanifest',
             'L4D2VR/portal1vr_capsule_main.png','L4D2VR/portal1vr_portrait_main.png',
             'thirdparty/openvr/bin/win32/openvr_api.dll',
             'thirdparty/openvr/LICENSE','thirdparty/minhook/LICENSE.txt','dxvk/LICENSE',
             'L4D2VR/sdk/LICENSE.txt','L4D2VR/sdk/thirdpartylegalnotices.txt']:
    files[name]=(repo/name).read_bytes()
for folder in ['L4D2VR/SteamVRActionManifest','L4D2VR/materials','L4D2VR/resource','L4D2VR/rexaura_vr']:
    for path in (repo/folder).rglob('*'):
        if path.is_file():files[path.relative_to(repo).as_posix()]=path.read_bytes()
files['Install.ps1']=b'''param([string]$PortalDirectory, [string]$RexauraDirectory)
$ErrorActionPreference = 'Stop'
if (Get-Process hl2 -ErrorAction SilentlyContinue) {
    throw 'Close Portal before installing this update.'
}
$options = @{ SourceDll = (Join-Path $PSScriptRoot 'Release/d3d9.dll') }
if ($PortalDirectory) { $options.PortalDirectory = $PortalDirectory }
if ($RexauraDirectory) { $options.RexauraDirectory = $RexauraDirectory }
& (Join-Path $PSScriptRoot 'L4D2VR/copy-to-portal.ps1') @options
'''
files['Install.cmd']=b'@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*\r\npause\r\n'
files['README.txt']='''Portal1VR - Windows x86

Source: https://github.com/LorenDB/portal1vr
d3d9.dll is a modified DXVK (fholger/dxvk_l4d2vr msaa, plus the Portal
changes in that repo). It is not an upstream DXVK build. The version
resource identifies it as Portal1VR.

Requires the Steam Windows x86 version of Portal and SteamVR.
No compiler is required.

On Linux, install with Kaon: https://github.com/LorenDB/kaon

On Windows:
1. Close Portal and extract this zip.
2. Run Install.cmd. It finds a Steam install of Portal.
   Custom location:
   .\\Install.ps1 -PortalDirectory "D:\\SteamLibrary\\steamapps\\common\\Portal"
3. Start SteamVR, then run Launch Portal VR.cmd in the Portal folder.
   Or launch with:
   -insecure -fullscreen -novid +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0

To uninstall, close Portal and run Uninstall.cmd. It deletes bin/d3d9.dll
and the other files this mod installed, so Portal uses system Direct3D 9
again. bin/VR/InstallBackups is left in place. A d3d9.dll proxy from
something else, installed at that same path, is removed too.

The installer copies d3d9.dll, openvr_api.dll, bindings, and the gun and
arm material overrides. Hands, gun, body, and the radio song are Portal's. An older
replacement pack (portal/custom/bowman_portal1.vpk) is moved to
bin/VR/InstallBackups. Existing config.txt values are kept.

Game build this was made against:
  client.dll  0x68362d89  (2025-05-27)
  engine.dll  0x675781e6  (2024-12-09)
  server.dll  0x67578384  (2024-12-09)
A newer Portal can still boot. A missed signature is logged and that
feature stays off. The portal-environment camera trace checks the client
timestamp and disables itself on any other build. Verify game files after
a Steam update.

Tested with a Quest 3 through SteamVR, on the Windows Portal build
under Proton, with -insecure. Other headsets and controllers were not
tested; rebind them in SteamVR.

Confirmed in a headset: stereo, head tracking, movement, portal gun,
pickup and carry, roomscale walking, the ending lift, and the credits.
Not confirmed in a headset: map cameras in testchmb_a_00 and escape_02.

On Linux, SteamVR does not reproject missed frames. Under Proton the
desktop present can stall the headset. RenderWindow=2 (default) mirrors
the left eye; 0 turns the desktop view off during play. Not tested on
native Windows.

Portal with RTX and Portal: Prelude RTX are not supported. They replace
d3d9.dll with RTX Remix.

Left stick moves, right stick turns, left-stick click recenters. Gun-hand
trigger fires blue, grip fires orange. Pause menu: "VR: use left-handed
controls" / "VR: use right-handed controls". ShowArms=false draws only
the gun. Settings: bin/VR/config.txt. Log: portalvr.log.

Licenses in this zip: L4D2VR/sdk/LICENSE.txt (Source 1 SDK; free
distribution only), L4D2VR/sdk/thirdpartylegalnotices.txt,
thirdparty/openvr/LICENSE, thirdparty/minhook/LICENSE.txt, dxvk/LICENSE.
'''.encode()
files['SHA256SUMS.txt']=''.join(f'{hashlib.sha256(v).hexdigest()}  {k}\n' for k,v in sorted(files.items())).encode()
with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,6) as z:
    for name,data in sorted(files.items()):z.writestr(name,data)
with zipfile.ZipFile(args.output) as z:
    assert z.testzip() is None
    for name,data in files.items():assert z.read(name)==data
print(json.dumps({'path':str(args.output),'files':len(files),'bytes':args.output.stat().st_size,'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest()}))
