param([string]$PortalClient = '', [string]$PortalGunModel = '', [string]$PortalServer = '', [string]$PortalEngine = '')
$ErrorActionPreference='Stop'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Run this from an x86 Native Tools Command Prompt for Visual Studio.' }
$repository=Split-Path -Parent $PSScriptRoot
$output=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $output | Out-Null
& cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'abi.cpp') "/Fe$output\abi.exe" "/Fo$output\abi.obj"
if ($LASTEXITCODE -ne 0) { throw 'ABI test build failed.' }
& (Join-Path $output 'abi.exe')
if ($LASTEXITCODE -ne 0) { throw 'ABI tests failed.' }
& cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'overhead-carry-calibration.cpp') "/Fe$output\overhead-carry-calibration.exe" "/Fo$output\overhead-carry-calibration.obj"
if ($LASTEXITCODE -ne 0) { throw 'Overhead/carry/calibration test build failed.' }
if ($PortalClient -and $PortalServer) {
    & (Join-Path $output 'overhead-carry-calibration.exe') $PortalClient $PortalServer
} else { & (Join-Path $output 'overhead-carry-calibration.exe') }
if ($LASTEXITCODE -ne 0) { throw 'Overhead/carry/calibration regressions failed.' }
& cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'portal-camera.cpp') "/Fe$output\portal-camera.exe" "/Fo$output\portal-camera.obj"
if ($LASTEXITCODE -ne 0) { throw 'Portal camera test build failed.' }
if ($PortalClient) { & (Join-Path $output 'portal-camera.exe') $PortalClient }
else { & (Join-Path $output 'portal-camera.exe') }
if ($LASTEXITCODE -ne 0) { throw 'Portal camera tests failed.' }
& cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'roomscale-aim-marker.cpp') "/Fe$output\roomscale-aim-marker.exe" "/Fo$output\roomscale-aim-marker.obj"
if ($LASTEXITCODE -ne 0) { throw 'Roomscale/aim marker test build failed.' }
& (Join-Path $output 'roomscale-aim-marker.exe')
if ($LASTEXITCODE -ne 0) { throw 'Roomscale follow, aim marker, or log regressions failed.' }
& cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'map-camera.cpp') "/Fe$output\map-camera.exe" "/Fo$output\map-camera.obj"
if ($LASTEXITCODE -ne 0) { throw 'Map camera test build failed.' }
& (Join-Path $output 'map-camera.exe')
if ($LASTEXITCODE -ne 0) { throw 'Map camera regressions failed.' }
if ($PortalEngine) {
    & cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'engine-view-entity.cpp') "/Fe$output\engine-view-entity.exe" "/Fo$output\engine-view-entity.obj" psapi.lib
    if ($LASTEXITCODE -ne 0) { throw 'Engine view-entity test build failed.' }
    & (Join-Path $output 'engine-view-entity.exe') $PortalEngine
    if ($LASTEXITCODE -ne 0) { throw 'Installed engine.dll does not match the map-camera view-entity guard.' }
}
if ($PortalClient) {
    & cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'portal-client-layout.cpp') "/Fe$output\portal-client-layout.exe" "/Fo$output\portal-client-layout.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Portal client layout test build failed.' }
    & (Join-Path $output 'portal-client-layout.exe') $PortalClient
    if ($LASTEXITCODE -ne 0) { throw 'Installed Portal client layout is not supported by the portal-aware trace.' }
    & cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'credits-flag.cpp') "/Fe$output\credits-flag.exe" "/Fo$output\credits-flag.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Credits flag test build failed.' }
    & (Join-Path $output 'credits-flag.exe') $PortalClient
    if ($LASTEXITCODE -ne 0) { throw 'Installed Portal client no longer publishes the end-credits flags the VR runtime reads.' }
}
if ($PortalGunModel) {
    & cl.exe /nologo /std:c++17 /EHsc /RTC1 /Od /DWIN32 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$repository\L4D2VR" "/I$repository\L4D2VR\sdk" (Join-Path $PSScriptRoot 'gun-model-attachments.cpp') "/Fe$output\gun-model-attachments.exe" "/Fo$output\gun-model-attachments.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Gun attachment test build failed.' }
    & (Join-Path $output 'gun-model-attachments.exe') $PortalGunModel
    if ($LASTEXITCODE -ne 0) { throw 'Compiled gun attachment tests failed.' }
}
