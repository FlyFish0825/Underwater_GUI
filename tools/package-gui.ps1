param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Split-Path -Parent $PSScriptRoot)).Path
$appDir = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build\gui'))
$testDir = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build\test'))
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build-gui.ps1') -BuildType Release -RunTests }
if ($appDir -ne (Join-Path $repoRoot 'build\gui') -or $testDir -ne (Join-Path $repoRoot 'build\test')) {
    throw 'Unexpected package paths.'
}
if ((Get-Content (Join-Path $testDir 'gui\CMakeCache.txt') -Raw) -notmatch 'CMAKE_BUILD_TYPE:STRING=Release') {
    throw 'Package requires a verified Release build under build/test/gui.'
}
$reparse = @(Get-Item -LiteralPath $appDir; Get-ChildItem -LiteralPath $appDir -Force -Recurse) |
    Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }
if ($reparse) { throw 'Reparse points are not allowed in the runtime tree.' }
if (@(Get-Process rov_ui -ErrorAction SilentlyContinue)) { throw 'Close the application before packaging.' }
$keep = @('rov_ui.exe', 'Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll', 'Qt5Network.dll',
          'Qt5Concurrent.dll', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll',
          'platforms\qwindows.dll', 'imageformats\qjpeg.dll', 'qt.conf')
$runtime = foreach ($relative in $keep) {
    $path = Join-Path $appDir $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing runtime file: $relative" }
    [pscustomobject]@{Path=$relative;Hash=(Get-FileHash -LiteralPath $path).Hash;Bytes=(Get-Item -LiteralPath $path).Length}
}
$stamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyyMMdd-HHmmss')
$archive = Join-Path $testDir "package-$stamp"
if (-not $archive.StartsWith(($testDir + '\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Archive outside test directory.'
}
New-Item -ItemType Directory -Path $archive -Force | Out-Null
$extras = @(Get-ChildItem -LiteralPath $appDir -File -Force -Recurse | Where-Object {
    $keep -notcontains $_.FullName.Substring($appDir.Length + 1)
})
$extraManifest = foreach ($file in $extras) {
    [pscustomobject]@{Path=$file.FullName.Substring($appDir.Length+1);Bytes=$file.Length;Hash=(Get-FileHash -LiteralPath $file.FullName).Hash}
}
$extraManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $archive 'removed-from-runtime.json') -Encoding utf8
# Archive whole generated directories, preserving all history/evidence and allowing recovery.
foreach ($item in @(Get-ChildItem -LiteralPath $appDir -Force)) {
    if (($item.PSIsContainer -and $item.Name -notin @('platforms','imageformats')) -or
        (-not $item.PSIsContainer -and $item.Name -notin $keep)) {
        $destination = Join-Path $archive ('old-runtime-extras\' + $item.Name)
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Move-Item -LiteralPath $item.FullName -Destination $destination
    }
}
foreach ($pluginDir in @('platforms','imageformats')) {
    foreach ($item in @(Get-ChildItem -LiteralPath (Join-Path $appDir $pluginDir) -Force)) {
        if (($pluginDir + '\' + $item.Name) -notin $keep) {
            $destination = Join-Path $archive ('old-runtime-extras\' + $pluginDir + '\' + $item.Name)
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Move-Item -LiteralPath $item.FullName -Destination $destination
        }
    }
}
foreach ($entry in $extraManifest) {
    $saved = Join-Path $archive ('old-runtime-extras\' + $entry.Path)
    if (-not (Test-Path -LiteralPath $saved) -or (Get-FileHash -LiteralPath $saved).Hash -ne $entry.Hash) {
        throw "Archived file mismatch: $($entry.Path)"
    }
}
foreach ($entry in $runtime) {
    if ((Get-FileHash -LiteralPath (Join-Path $appDir $entry.Path)).Hash -ne $entry.Hash) {
        throw "Runtime changed during cleanup: $($entry.Path)"
    }
}
if (@(Get-ChildItem -LiteralPath $appDir -File -Recurse -Force).Count -ne $keep.Count) {
    throw 'Unexpected runtime contents after cleanup.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipPath = Join-Path $repoRoot 'build\gui.zip'
if (Test-Path -LiteralPath $zipPath) { Move-Item -LiteralPath $zipPath -Destination (Join-Path $archive 'previous-gui.zip') }
[IO.Compression.ZipFile]::CreateFromDirectory($appDir, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $true)
$zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($zip.Entries.Count -ne $keep.Count) { throw 'Unexpected ZIP entries.' }
    foreach ($entry in $runtime) {
        $zipped = $zip.GetEntry('gui/' + $entry.Path.Replace('\','/'))
        if (-not $zipped) { throw "ZIP entry missing: $($entry.Path)" }
        $stream = $zipped.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
        finally { $sha.Dispose(); $stream.Dispose() }
        if ($hash -ne $entry.Hash) { throw "ZIP hash mismatch: $($entry.Path)" }
    }
} finally { $zip.Dispose() }
$extracted = Join-Path $archive 'extracted'
[IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $extracted)
foreach ($entry in $runtime) {
    if ((Get-FileHash -LiteralPath (Join-Path $extracted ('gui\' + $entry.Path))).Hash -ne $entry.Hash) {
        throw "Extracted runtime mismatch: $($entry.Path)"
    }
}
$smokeDir = Join-Path $archive 'startup'
New-Item -ItemType Directory -Path $smokeDir -Force | Out-Null
$savedPath = $env:Path; $savedPluginPath = $env:QT_PLUGIN_PATH; $savedPlatformPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = ''; $env:QT_QPA_PLATFORM_PLUGIN_PATH = ''
    $process = Start-Process -FilePath (Join-Path $extracted 'gui\rov_ui.exe') `
        -ArgumentList @('--smoke-test', ('"' + $smokeDir + '"')) `
        -WorkingDirectory (Join-Path $extracted 'gui') -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(20000)) { $process.Kill(); throw 'Extracted application startup timed out.' }
    if ($process.ExitCode -ne 0) { throw "Extracted application exited with $($process.ExitCode)." }
} finally {
    $env:Path = $savedPath; $env:QT_PLUGIN_PATH = $savedPluginPath; $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedPlatformPath
}
$smoke = Get-Content -LiteralPath (Join-Path $smokeDir 'startup-result.json') -Raw | ConvertFrom-Json
if (-not $smoke.passed -or $smoke.platform -ne 'windows') { throw 'Extracted GUI/JPEG startup verification failed.' }
$result = [ordered]@{BuildType='Release';RuntimeFiles=$keep.Count;ArchivedGeneratedFiles=$extras.Count;
    ArchivedMiB=[Math]::Round((($extras | Measure-Object Length -Sum).Sum / 1MB),2);
    Runtime=$runtime;Zip=$zipPath;ZipMiB=[Math]::Round((Get-Item -LiteralPath $zipPath).Length/1MB,2);
    ZipHash=(Get-FileHash -LiteralPath $zipPath).Hash;Archive=$archive;ExtractedStartup=$smoke}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $archive 'package-result.json') -Encoding utf8
$archive | Set-Content -LiteralPath (Join-Path $testDir 'current-package.txt') -Encoding utf8
$result | ConvertTo-Json -Depth 3
