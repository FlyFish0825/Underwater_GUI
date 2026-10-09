param(
    [ValidateSet('Release', 'Debug')][string]$BuildType = 'Release',
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repoRoot 'toolchain\env\activate.ps1')
$buildDir = Join-Path $repoRoot 'build\test\gui'
$appDir = Join-Path $repoRoot 'build\gui'
$cmake = Join-Path $env:ROV_UI_CMAKE_DIR 'bin\cmake.exe'
$qtPath = $env:ROV_UI_QT_DIR.Replace('\', '/')
$gccPath = (Join-Path $env:ROV_UI_MINGW_DIR 'bin\gcc.exe').Replace('\', '/')
$gxxPath = (Join-Path $env:ROV_UI_MINGW_DIR 'bin\g++.exe').Replace('\', '/')
$runtimePath = $appDir.Replace('\', '/')
& $cmake -S $repoRoot -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=$BuildType" '-DBUILD_TESTING=ON' '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON' `
    "-DROV_UI_RUNTIME_DIR=$runtimePath" "-DCMAKE_PREFIX_PATH=$qtPath" `
    "-DCMAKE_C_COMPILER=$gccPath" "-DCMAKE_CXX_COMPILER=$gxxPath"
if ($LASTEXITCODE -ne 0) { throw 'GUI configuration failed.' }
& $cmake --build $buildDir --target rov_ui --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'GUI build failed.' }
foreach ($name in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Widgets.dll')) {
    Copy-Item -LiteralPath (Join-Path $env:ROV_UI_QT_DIR "bin\$name") -Destination $appDir -Force
}
foreach ($name in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $env:ROV_UI_MINGW_DIR "bin\$name") -Destination $appDir -Force
}
$platformDir = Join-Path $appDir 'platforms'
New-Item -ItemType Directory -Path $platformDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $env:ROV_UI_QT_DIR 'plugins\platforms\qwindows.dll') -Destination $platformDir -Force
& (Join-Path $PSScriptRoot 'deploy-camera-runtime.ps1') -AppDirectory $appDir
# Pin plugin lookup to the package, independent of the SDK installed on this PC.
"[Paths]`nPrefix=.`nPlugins=.`n" | Set-Content -LiteralPath (Join-Path $appDir 'qt.conf') -Encoding ascii
if ($RunTests) {
    $targets = [regex]::Matches((Get-Content (Join-Path $repoRoot 'CMakeLists.txt') -Raw),
                              'add_test\(NAME\s+(\w+)') | ForEach-Object { $_.Groups[1].Value }
    & $cmake --build $buildDir --target $targets --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
    & (Join-Path $repoRoot 'toolchain\env\deploy-qt.ps1') -Executable (Join-Path $buildDir 'rov_stereo_camera_page_test.exe')
    & (Join-Path $PSScriptRoot 'deploy-camera-runtime.ps1') -AppDirectory $buildDir
    & (Join-Path $env:ROV_UI_CMAKE_DIR 'bin\ctest.exe') --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
Write-Output "Built $BuildType runtime: $appDir (cache and tests: $buildDir)"
