# Builds cromo.exe with MSVC, CMake, Ninja and vcpkg from Visual Studio.
# Usage: powershell -ExecutionPolicy Bypass -File build.ps1 [-Config Release] [-LlvmRoot <dir>] [-Clean]
#        [-AndroidNdk <dir>] [-AndroidSdk <dir>]
# The NDK and the SDK of Android, for libSDL3.so and classes.dex of the APKs (D453),
# are remembered by CMake once given.
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4',
    [string]$AndroidNdk = $env:ANDROID_NDK_HOME,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$installer = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vs = & "$installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ tools was not found' }

# MinGW toolchains on PATH (for example Strawberry Perl) ship zlib/libxml2 builds
# that CMake would pick up instead of the MSVC ones from vcpkg.
$env:PATH = (($env:PATH -split ';') | Where-Object { $_ -and $_ -notmatch 'Strawberry|mingw' }) -join ';'
$env:PATH = "$installer;$env:PATH"

# Load the vcvars64 environment (cl.exe, cmake, ninja) into this session.
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] }
}

$root = $PSScriptRoot
$build = Join-Path $root 'build'
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

# LLVM was built with zlib, zstd and libxml2 but does not ship them: vcpkg builds
# them as static libraries with the static C runtime (/MT), like LLVM.
$toolchain = Join-Path $vs 'VC\vcpkg\scripts\buildsystems\vcpkg.cmake'
$android = @()
if ($AndroidNdk) { $android += "-DCROMO_ANDROID_NDK=$AndroidNdk" }
if ($AndroidSdk) { $android += "-DCROMO_ANDROID_SDK=$AndroidSdk" }
cmake -S $root -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCROMO_LLVM_ROOT=$LlvmRoot" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static' `
    '-DVCPKG_APPLOCAL_DEPS=OFF' @android
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# The pilot of the compiler in Cromo (D417, D419, D423): cromo fmt and the first part
# of sema, compiled by the cromo just built; they are not in the archives.
& (Join-Path $build 'cromo.exe') build --release (Join-Path $root 'compiler\main.cro') -o (Join-Path $build 'cromo-fmt.exe')
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& (Join-Path $build 'cromo.exe') build --release (Join-Path $root 'compiler\check.cro') -o (Join-Path $build 'cromo-check.exe')
exit $LASTEXITCODE
