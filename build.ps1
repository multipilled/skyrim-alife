# Build ALife.dll. Usage: powershell -File build.ps1 [-Config Release] [-Clean] [-Src <checkout>] [-Build <dir>]
# Env: ALIFE_ROOT (default: the folder above this checkout; builds go to <root>/build), ALIFE_CLIB (CommonLibSSE-NG),
# VCPKG_ROOT, CMAKE (default: cmake on PATH, else Visual Studio 2022's).
param([string]$Config = 'Release', [switch]$Clean, [string]$Src = '', [string]$Build = '')
$ErrorActionPreference = 'Continue'
$root  = if ($env:ALIFE_ROOT) { $env:ALIFE_ROOT } else { Split-Path $PSScriptRoot -Parent }
$src   = if ($Src) { $Src } else { $PSScriptRoot }
$bld   = if ($Build) { $Build } else { Join-Path $root 'build' }
$vcpkg = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $root 'deps\vcpkg' }
if (-not $env:ALIFE_CLIB) { $env:ALIFE_CLIB = Join-Path $root 'deps\CommonLibSSE-NG' }
$env:VCPKG_ROOT = $vcpkg
$cm = if ($env:CMAKE) { $env:CMAKE } elseif (Get-Command cmake -ErrorAction SilentlyContinue) { 'cmake' } else {
  'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
$log = Join-Path $bld 'build.log'
if ($Clean -and (Test-Path $bld)) { Remove-Item -Recurse -Force $bld }
New-Item -ItemType Directory -Force $bld | Out-Null
if (-not (Test-Path (Join-Path $bld 'CMakeCache.txt'))) {
    & $cm -S $src -B $bld -G 'Visual Studio 17 2022' -A x64 `
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
        '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' '-DVCPKG_MANIFEST_MODE=OFF' `
        "-DVCPKG_INSTALLED_DIR=$vcpkg/installed" 2>&1 | Tee-Object -FilePath (Join-Path $bld 'cfg.log')
}
& $cm --build $bld --config $Config --target ALife -- /m:2 /v:minimal 2>&1 | Tee-Object -FilePath $log
$code = $LASTEXITCODE
Select-String -Path $log -Pattern ': error|: fatal|warning C4' | Select-Object -First 60 | ForEach-Object { $_.Line }
"EXIT $code"
if ($code -eq 0) { Get-Item (Join-Path $bld "$Config\ALife.dll") | Select-Object FullName, Length, LastWriteTime | Format-List }
