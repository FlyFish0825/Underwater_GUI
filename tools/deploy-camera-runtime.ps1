param([string]$AppDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($AppDirectory)) {
    $AppDirectory = Join-Path $repoRoot 'build\gui'
}
$targetDir = (Resolve-Path -LiteralPath $AppDirectory).Path
$qtDir = Join-Path $repoRoot 'toolchain\qt\5.15.19'
foreach ($name in @('Qt5Network.dll', 'Qt5Concurrent.dll')) {
    Copy-Item -LiteralPath (Join-Path $qtDir "bin\$name") -Destination (Join-Path $targetDir $name) -Force
}
$imageDir = Join-Path $targetDir 'imageformats'
New-Item -ItemType Directory -Path $imageDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $qtDir 'plugins\imageformats\qjpeg.dll') -Destination $imageDir -Force
Write-Output "Camera runtime deployed to $targetDir"
