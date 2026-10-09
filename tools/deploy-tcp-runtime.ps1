param([string]$AppDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($AppDirectory)) {
    $AppDirectory = Join-Path $repoRoot 'build\gui'
}
$targetDir = (Resolve-Path -LiteralPath $AppDirectory).Path
$runtime = Join-Path $repoRoot 'toolchain\qt\5.15.19\bin\Qt5Network.dll'
Copy-Item -LiteralPath $runtime -Destination (Join-Path $targetDir 'Qt5Network.dll') -Force
Write-Output "TCP runtime deployed to $targetDir"
