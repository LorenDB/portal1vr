$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('portal1vr-uninstall-test-' + [Guid]::NewGuid().ToString('N'))
$portalDir = Join-Path $fixture 'Portal'
$uninstall = Join-Path $PSScriptRoot '..\Uninstall.ps1'

function Write-Fixture([string]$Relative, [string]$Text) {
    $path = Join-Path $portalDir $Relative
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    [IO.File]::WriteAllText($path, $Text)
}

Write-Fixture 'hl2.exe' 'not-a-real-exe'
foreach ($relative in @(
    'bin\d3d9.dll',
    'bin\openvr_api.dll',
    'Launch Portal VR.cmd',
    'Launch Rexaura VR.cmd',
    'rexaura-vr-launch.sh',
    'portalvr.log',
    'bin\VR\config.txt',
    'bin\VR\manifest.vrmanifest',
    'bin\VR\portal1vr_capsule_main.png',
    'bin\VR\portal1vr_portrait_main.png',
    'bin\VR\SteamVRActionManifest\action_manifest.json',
    'bin\VR\SteamVRActionManifest\bindings_knuckles.json',
    'bin\VR\SteamVRActionManifest\bindings_oculus_touch.json',
    'bin\VR\SteamVRActionManifest\bindings_vive_cosmos_controller.json',
    'bin\VR\SteamVRActionManifest\bindings_custom.json',
    'portal\custom\portal1vr\materials\models\weapons\v_models\v_hands\v_hands.vmt',
    'portal\custom\portal1vr\resource\gamemenu.res',
    'portal\custom\portal1vr\sound\keep.wav',
    'portal\custom\other_mod.vpk',
    'rexaura_vr\gameinfo.txt',
    'rexaura_vr\resource\GameMenu.res',
    'rexaura_vr\save\keep.sav',
    'bin\VR\InstallBackups\replaced-content-test\bowman_portal1.vpk'
)) {
    Write-Fixture $relative "mod:$relative"
}

& $uninstall -PortalDirectory $portalDir

foreach ($relative in @(
    'bin\d3d9.dll',
    'bin\openvr_api.dll',
    'Launch Portal VR.cmd',
    'Launch Rexaura VR.cmd',
    'rexaura-vr-launch.sh',
    'portalvr.log',
    'bin\VR\config.txt',
    'bin\VR\manifest.vrmanifest',
    'bin\VR\SteamVRActionManifest\action_manifest.json',
    'portal\custom\portal1vr\materials',
    'portal\custom\portal1vr\resource',
    'rexaura_vr\gameinfo.txt',
    'rexaura_vr\resource'
)) {
    if (Test-Path -LiteralPath (Join-Path $portalDir $relative)) {
        throw "Still installed: $relative"
    }
}

foreach ($relative in @(
    'hl2.exe',
    'bin\VR\SteamVRActionManifest\bindings_custom.json',
    'portal\custom\portal1vr\sound\keep.wav',
    'portal\custom\other_mod.vpk',
    'rexaura_vr\save\keep.sav',
    'bin\VR\InstallBackups\replaced-content-test\bowman_portal1.vpk'
)) {
    $path = Join-Path $portalDir $relative
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing kept file: $relative" }
    if ($relative -ne 'hl2.exe' -and [IO.File]::ReadAllText($path) -ne "mod:$relative") {
        throw "Kept file changed: $relative"
    }
}

& $uninstall -PortalDirectory $portalDir
if (-not (Test-Path -LiteralPath (Join-Path $portalDir 'hl2.exe'))) { throw 'Second run deleted the game' }
Write-Output "PASS: uninstall removes mod files, keeps backups, saves, and unrelated content. Fixture: $fixture"
