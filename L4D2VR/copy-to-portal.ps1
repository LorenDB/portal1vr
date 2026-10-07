param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDll,
    [string]$PortalDirectory,
    # Rexaura's install folder (the one holding rexaura\gameinfo.txt). Found
    # in the Steam libraries when omitted; Rexaura support is skipped when absent.
    [string]$RexauraDirectory
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SourceDll)) {
    throw "Build output not found: $SourceDll"
}

function Get-SteamLibraryPaths {
    $roots = @(
        "${env:ProgramFiles(x86)}\\Steam",
        "${env:ProgramFiles}\\Steam",
        "${env:ProgramW6432}\\Steam"
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique

    $libraries = New-Object System.Collections.Generic.List[string]

    foreach ($root in $roots) {
        $libraries.Add($root)

        $vdfPath = Join-Path $root "steamapps\\libraryfolders.vdf"
        if (-not (Test-Path -LiteralPath $vdfPath)) {
            continue
        }

        $content = Get-Content -LiteralPath $vdfPath -Raw
        $matches = [regex]::Matches($content, '"path"\s+"([^"]+)"')
        foreach ($match in $matches) {
            $libraryPath = $match.Groups[1].Value -replace "\\\\", "\"
            if ($libraryPath) {
                $libraries.Add($libraryPath)
            }
        }
    }

    foreach ($drive in (Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Root)) {
        foreach ($suffix in @("", "Steam", "SteamLibrary", "Games\\Steam", "Games\\SteamLibrary")) {
            $candidate = if ($suffix) { Join-Path $drive $suffix } else { $drive }
            if (Test-Path -LiteralPath $candidate) {
                $libraries.Add($candidate)
            }
        }
    }

    return $libraries | Select-Object -Unique
}

$portalDir = $PortalDirectory
$steamLibraries = $null
if (-not $PortalDirectory -or -not $RexauraDirectory) {
    $steamLibraries = @(Get-SteamLibraryPaths)
}
foreach ($library in $(if (-not $portalDir) { $steamLibraries })) {
    $candidate = Join-Path $library "steamapps\\common\\Portal"
    if (Test-Path -LiteralPath $candidate) {
        $portalDir = $candidate
        break
    }
}

if (-not $portalDir) {
    Write-Host "Portal install not found. Skipping DLL copy."
    exit 0
}

if (-not (Test-Path -LiteralPath (Join-Path $portalDir "hl2.exe"))) {
    throw "Not a Portal installation: $portalDir"
}

$binDir = Join-Path $portalDir "bin"
if (-not (Test-Path -LiteralPath $binDir)) {
    New-Item -ItemType Directory -Path $binDir | Out-Null
}

$vrDir = Join-Path $binDir "VR"
if (-not (Test-Path -LiteralPath $vrDir)) {
    New-Item -ItemType Directory -Path $vrDir | Out-Null
}

$vrActionDir = Join-Path $vrDir "SteamVRActionManifest"
if (-not (Test-Path -LiteralPath $vrActionDir)) {
    New-Item -ItemType Directory -Path $vrActionDir | Out-Null
}

$openVrSource = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\thirdparty\openvr\bin\win32\openvr_api.dll"))
$manifestSource = Join-Path $PSScriptRoot "manifest.vrmanifest"
$capsuleSource = Join-Path $PSScriptRoot "portal1vr_capsule_main.png"
$portraitSource = Join-Path $PSScriptRoot "portal1vr_portrait_main.png"
$runtimeFiles = @(
    @{
        Source = Join-Path $PSScriptRoot '..\Launch Portal VR.cmd'
        Destination = Join-Path $portalDir 'Launch Portal VR.cmd'
        Label = 'Launch Portal VR.cmd'
    },
    @{
        Source = Join-Path $PSScriptRoot '..\Launch Rexaura VR.cmd'
        Destination = Join-Path $portalDir 'Launch Rexaura VR.cmd'
        Label = 'Launch Rexaura VR.cmd'
    },
    @{
        Source = $SourceDll
        Destination = Join-Path $binDir "d3d9.dll"
        Label = "d3d9.dll"
    },
    @{
        Source = $openVrSource
        Destination = Join-Path $binDir "openvr_api.dll"
        Label = "openvr_api.dll"
    },
    @{
        Source = $manifestSource
        Destination = Join-Path $vrDir "manifest.vrmanifest"
        Label = "manifest.vrmanifest"
    },
    @{
        Source = $capsuleSource
        Destination = Join-Path $vrDir "portal1vr_capsule_main.png"
        Label = "portal1vr_capsule_main.png"
    },
    @{
        Source = $portraitSource
        Destination = Join-Path $vrDir "portal1vr_portrait_main.png"
        Label = "portal1vr_portrait_main.png"
    }
)

foreach ($file in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath $file.Source)) {
        throw "Required runtime file was not found: $($file.Source)"
    }

    try {
        Copy-Item -LiteralPath $file.Source -Destination $file.Destination -Force
        Write-Host "Copied $($file.Label) to $($file.Destination)"
    }
    catch {
        if ($_.Exception.Message -like "*being used by another process*") {
            throw "Close Portal before installing: '$($file.Destination)' is in use."
        }

        throw
    }
}

$actionManifestSourceDir = Join-Path $PSScriptRoot "SteamVRActionManifest"
if (Test-Path -LiteralPath $actionManifestSourceDir) {
    Get-ChildItem -LiteralPath $actionManifestSourceDir -File | Copy-Item -Destination $vrActionDir -Force
    Write-Host "Copied SteamVRActionManifest to $vrActionDir"
}
else {
    throw "SteamVRActionManifest source directory was not found: $actionManifestSourceDir"
}

$configDestination = Join-Path $vrDir "config.txt"
if (-not (Test-Path -LiteralPath $configDestination)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "config.txt") -Destination $configDestination
}
else {
    # Preserve chosen settings and append every missing shipped default. A
    # hand-maintained list missed the grip and body-offset settings, causing
    # one blocking startup dialog for each absent entry.
    $configText = [System.IO.File]::ReadAllText($configDestination)
    $defaultConfigLines = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot "config.txt") |
        Where-Object { $_ -match '^[A-Za-z0-9_]+=' })
    $missingConfigLines = @($defaultConfigLines | Where-Object {
        $key = ($_ -split '=', 2)[0]
        # Runtime keys are case-sensitive, including when checking presence.
        $configText -cnotmatch ('(?m)^' + [regex]::Escape($key) + '=')
    })
    if ($missingConfigLines.Count -gt 0) {
        $newline = [Environment]::NewLine
        $prefix = if ($configText.EndsWith("`n") -or $configText.EndsWith("`r")) { '' } else { $newline }
        [System.IO.File]::AppendAllText(
            $configDestination,
            $prefix + ($missingConfigLines -join $newline) + $newline)
        Write-Host "Added $($missingConfigLines.Count) missing default settings to $configDestination"
    }
}

$materialSource = Join-Path $PSScriptRoot "materials"
$resourceSource = Join-Path $PSScriptRoot 'resource'
if (Test-Path -LiteralPath $resourceSource) {
    $resourceDestination = Join-Path $portalDir 'portal\custom\portal1vr\resource'
    New-Item -ItemType Directory -Force -Path $resourceDestination | Out-Null
    Get-ChildItem -LiteralPath $resourceSource -File | Copy-Item -Destination $resourceDestination -Force
    Write-Host 'Installed VR handedness and recenter menu options'
}
$materialDestination = Join-Path $portalDir "portal\custom\portal1vr\materials"
if (Test-Path -LiteralPath $materialSource) {
    New-Item -ItemType Directory -Force -Path $materialDestination | Out-Null
    Get-ChildItem -LiteralPath $materialSource -Directory | Copy-Item -Destination $materialDestination -Recurse -Force
    Write-Host "Installed gun and arm materials"
}

# Rexaura runs as a mod of this Portal installation (-game rexaura_vr), on the
# Portal engine and game DLLs this runtime hooks. Rexaura's own install ships
# a 2013 engine and is left unchanged; only its content folder is mounted.
$rexauraContent = $null
$rexauraCandidates = if ($RexauraDirectory) { @($RexauraDirectory) } else {
    @($steamLibraries | ForEach-Object { Join-Path $_ 'steamapps\common\Rexaura' })
}
foreach ($candidate in $rexauraCandidates) {
    foreach ($content in @((Join-Path $candidate 'rexaura'), $candidate)) {
        if (Test-Path -LiteralPath (Join-Path $content 'maps\rex_00_intro.bsp')) {
            $rexauraContent = [IO.Path]::GetFullPath($content)
            break
        }
    }
    if ($rexauraContent) { break }
}
if ($RexauraDirectory -and -not $rexauraContent) {
    throw "Not a Rexaura installation: $RexauraDirectory"
}
if ($rexauraContent) {
    $rexauraVrSource = Join-Path $PSScriptRoot 'rexaura_vr'
    $rexauraVrDir = Join-Path $portalDir 'rexaura_vr'
    New-Item -ItemType Directory -Force -Path (Join-Path $rexauraVrDir 'resource') | Out-Null
    Copy-Item -LiteralPath (Join-Path $rexauraVrSource 'resource\GameMenu.res') `
        -Destination (Join-Path $rexauraVrDir 'resource\GameMenu.res') -Force
    # Paths in gameinfo.txt are relative to Portal's folder unless absolute.
    $portalParent = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($portalDir).TrimEnd('\', '/'))
    $rexauraInstall = [IO.Path]::GetDirectoryName($rexauraContent)
    $sameLibrary = ([IO.Path]::GetDirectoryName($rexauraInstall) -ieq $portalParent) -and
        ((Split-Path $rexauraContent -Leaf) -ieq 'rexaura')
    $rexauraPath = $rexauraContent -replace '\\', '/'
    if ($sameLibrary) {
        $rexauraPath = '../' + (Split-Path $rexauraInstall -Leaf) + '/rexaura'
    }
    $gameInfo = [IO.File]::ReadAllText((Join-Path $rexauraVrSource 'gameinfo.txt'))
    $gameInfo = $gameInfo.Replace('../Rexaura/rexaura', $rexauraPath)
    [IO.File]::WriteAllText((Join-Path $rexauraVrDir 'gameinfo.txt'), $gameInfo)
    Write-Host "Installed Rexaura VR (content: $rexauraContent). Start it with 'Launch Rexaura VR.cmd'."
}
else {
    Write-Host 'Rexaura not found; skipped Rexaura VR. Pass -RexauraDirectory to add it.'
}

# Portal's own hands, portal gun, player body and radio song are used. Earlier
# releases installed replacements; move them and their caches outside custom/
# so Source falls back to the stock content.
$customRoot = [IO.Path]::GetFullPath((Join-Path $portalDir 'portal\custom'))
$retiredFiles = @(
    'bowman_portal1.vpk',
    'bowman_portal1.vpk.sound.cache',
    'portal1vr\sound\ambient\music\looping_radio_mix.wav',
    'portal1vr\portal1vr_streamer_warning.txt',
    'portal1vr\sound\sound.cache'
)
$backupRoot = Join-Path $portalDir ('bin\VR\InstallBackups\replaced-content-' +
    [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fffffff'))
foreach ($relativePath in $retiredFiles) {
    $oldFile = [IO.Path]::GetFullPath((Join-Path $customRoot $relativePath))
    if (-not $oldFile.StartsWith($customRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Retired content path escaped the custom directory.'
    }
    if (Test-Path -LiteralPath $oldFile -PathType Leaf) {
        $backupFile = Join-Path $backupRoot $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path $backupFile) | Out-Null
        Move-Item -LiteralPath $oldFile -Destination $backupFile
        Write-Host "Backed up replaced model/radio content: $backupFile"
    }
}
