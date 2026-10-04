$ErrorActionPreference = 'Stop'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('portal1vr-stock-content-test-' + [Guid]::NewGuid().ToString('N'))
$portalDir = Join-Path $fixture 'Portal'
$installer = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\L4D2VR\copy-to-portal.ps1'))
$start = $installer.IndexOf('$customRoot =')
if ($start -lt 0) { throw 'Missing retired-content block' }
$block = [scriptblock]::Create($installer.Substring($start))
$retired = @('bowman_portal1.vpk', 'bowman_portal1.vpk.sound.cache',
    'portal1vr\sound\ambient\music\looping_radio_mix.wav',
    'portal1vr\portal1vr_streamer_warning.txt', 'portal1vr\sound\sound.cache')
$kept = @('portal1vr\sound\unrelated.wav', 'other_mod.vpk',
    'portal1vr\materials\models\weapons\v_models\v_hands\v_hands.vmt')
foreach ($relative in $retired + $kept) {
    $path = Join-Path $portalDir ('portal\custom\' + $relative)
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    [IO.File]::WriteAllText($path, 'previous:' + $relative)
}
& $block
$backup = @(Get-ChildItem (Join-Path $portalDir 'bin\VR\InstallBackups') -Directory)
if ($backup.Count -ne 1) { throw 'Expected one backup folder' }
foreach ($relative in $retired) {
    if (Test-Path -LiteralPath (Join-Path $portalDir ('portal\custom\' + $relative))) { throw 'Replaced content still active' }
    if ([IO.File]::ReadAllText((Join-Path $backup[0].FullName $relative)) -ne ('previous:' + $relative)) { throw 'Backup changed' }
}
foreach ($relative in $kept) {
    if ([IO.File]::ReadAllText((Join-Path $portalDir ('portal\custom\' + $relative))) -ne ('previous:' + $relative)) { throw 'Unrelated content changed' }
}
& $block
if (@(Get-ChildItem (Join-Path $portalDir 'bin\VR\InstallBackups') -Directory).Count -ne 1) { throw 'Redundant backup created' }
Write-Output "PASS: reversible model/radio/cache migration, unrelated files, and repeated installation. Fixture: $fixture"
