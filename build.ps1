# Builds cromo.exe with MSVC, CMake, Ninja and vcpkg from Visual Studio.
# Usage: powershell -ExecutionPolicy Bypass -File build.ps1 [-Config Release] [-LlvmRoot <dir>] [-Clean]
param(
    [ValidateSet('RelWithDebInfo', 'Release')]
    [string]$Config = 'RelWithDebInfo',
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4',
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
cmake -S $root -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCROMO_LLVM_ROOT=$LlvmRoot" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static' `
    '-DVCPKG_APPLOCAL_DEPS=OFF'
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build
exit $LASTEXITCODE
