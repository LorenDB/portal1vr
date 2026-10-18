param(
    [string]$PortalDirectory
)

$ErrorActionPreference = 'Stop'

if (Get-Process hl2 -ErrorAction SilentlyContinue) {
    throw 'Close Portal before uninstalling.'
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

function Get-PortalPath([string]$Root, [string]$Relative) {
    $portalRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $full = [IO.Path]::GetFullPath((Join-Path $portalRoot $Relative))
    $prefix = $portalRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to delete outside Portal: $full"
    }
    return $full
}

function Remove-ModFile([string]$Root, [string]$Relative) {
    $full = Get-PortalPath $Root $Relative
    if (-not (Test-Path -LiteralPath $full)) {
        return $false
    }
    Remove-Item -LiteralPath $full -Force
    Write-Host "Removed $Relative"
    return $true
}

function Remove-ModTree([string]$Root, [string]$Relative) {
    $full = Get-PortalPath $Root $Relative
    if (-not (Test-Path -LiteralPath $full)) {
        return $false
    }
    Remove-Item -LiteralPath $full -Recurse -Force
    Write-Host "Removed $Relative"
    return $true
}

function Remove-EmptyDirectory([string]$Root, [string]$Relative) {
    $full = Get-PortalPath $Root $Relative
    if (-not (Test-Path -LiteralPath $full)) {
        return
    }
    if (@(Get-ChildItem -LiteralPath $full -Force).Count -eq 0) {
        Remove-Item -LiteralPath $full -Force
        Write-Host "Removed $Relative"
    }
}

$portalDir = $PortalDirectory
if (-not $portalDir) {
    foreach ($library in @(Get-SteamLibraryPaths)) {
        $candidate = Join-Path $library "steamapps\\common\\Portal"
        if (Test-Path -LiteralPath $candidate) {
            $portalDir = $candidate
            break
        }
    }
}

if (-not $portalDir) {
    throw 'Portal install not found. Pass -PortalDirectory.'
}

if (-not (Test-Path -LiteralPath (Join-Path $portalDir 'hl2.exe'))) {
    throw "Not a Portal installation: $portalDir"
}

$removed = $false
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
    'rexaura_vr\gameinfo.txt',
    'rexaura_vr\resource\GameMenu.res'
)) {
    if (Remove-ModFile $portalDir $relative) { $removed = $true }
}

foreach ($relative in @(
    'portal\custom\portal1vr\materials',
    'portal\custom\portal1vr\resource'
)) {
    if (Remove-ModTree $portalDir $relative) { $removed = $true }
}

Remove-EmptyDirectory $portalDir 'bin\VR\SteamVRActionManifest'
Remove-EmptyDirectory $portalDir 'portal\custom\portal1vr'
Remove-EmptyDirectory $portalDir 'rexaura_vr\resource'
Remove-EmptyDirectory $portalDir 'rexaura_vr'

$vrDir = Get-PortalPath $portalDir 'bin\VR'
if ((Test-Path -LiteralPath $vrDir) -and -not (Get-ChildItem -LiteralPath $vrDir -Force | Where-Object { $_.Name -ne 'InstallBackups' })) {
    # InstallBackups holds content the installer moved out of portal\custom.
    if (-not (Test-Path -LiteralPath (Join-Path $vrDir 'InstallBackups'))) {
        Remove-Item -LiteralPath $vrDir -Force
        Write-Host 'Removed bin\VR'
    }
}

if (-not $removed) {
    Write-Host "No Portal1VR files found in $portalDir"
}
else {
    Write-Host "Uninstalled from $portalDir. Portal will use the system Direct3D 9."
    Write-Host 'bin\d3d9.dll is gone. Another proxy that was installed at that path is gone too.'
    Write-Host 'bin\VR\InstallBackups was left in place.'
}
