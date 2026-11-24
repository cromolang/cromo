# Builds cromo.exe, which is written in Cromo (D291, D576), with the cromo of the
# version before, and the runtime and lib\ with the clang of LLVM, CMake and Ninja
# from Visual Studio, whose C++ compiles only compiler\shim\shim.cpp.
# Usage: powershell -ExecutionPolicy Bypass -File build.ps1 [-LlvmRoot <dir>] [-Clean] [-Bootstrap <cromo.exe>]
#        [-AndroidNdk <dir>] [-AndroidSdk <dir>]
# The cromo of the version before is downloaded once from cromolang.org into
# bootstrap\, and its SHA-256 checked; -Bootstrap names another one, as the cromo of
# an installation. The NDK and the SDK of Android, for libSDL3.so and classes.dex of
# the APKs (D453), are remembered by CMake once given.
param(
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4',
    [string]$AndroidNdk = $env:ANDROID_NDK_HOME,
    [string]$AndroidSdk = $env:ANDROID_HOME,
    [string]$Bootstrap = '',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

# The version that builds this one, and the SHA-256 of its archive for Windows, as
# in its manifest, https://cromolang.org/release/<version> (D285).
$bootstrapVersion = '0.98.0'
$bootstrapSum = '34133d4da3c1e90b2d4693e6e2e643ef4ffc597e961ddb969fbfff585032d74b'

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

# The cromo of the version before, with its own lib\ next to it.
if (-not $Bootstrap) {
    $folder = Join-Path $root "bootstrap\win_x86_64-$bootstrapVersion"
    $Bootstrap = Join-Path $folder 'bin\cromo.exe'
    if (-not (Test-Path $Bootstrap)) {
        $archive = Join-Path $root "bootstrap\win_x86_64-$bootstrapVersion.zip"
        New-Item -ItemType Directory -Force (Join-Path $root 'bootstrap') | Out-Null
        "Downloading cromo $bootstrapVersion, which builds this one"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        (New-Object Net.WebClient).DownloadFile("https://cromolang.org/package/win_x86_64-$bootstrapVersion.zip", $archive)
        $sum = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLower()
        if ($sum -ne $bootstrapSum) { throw "the archive of cromo $bootstrapVersion has another SHA-256: $sum" }
        Expand-Archive -Path $archive -DestinationPath $folder -Force
        Remove-Item $archive
    }
}
if (-not (Test-Path $Bootstrap)) { throw "$Bootstrap is missing: the cromo that builds this one" }

# LLVM was built with zlib, zstd and libxml2 but does not ship them: vcpkg builds
# them as static libraries with the static C runtime (/MT), like LLVM.
$toolchain = Join-Path $vs 'VC\vcpkg\scripts\buildsystems\vcpkg.cmake'
$android = @()
if ($AndroidNdk) { $android += "-DCROMO_ANDROID_NDK=$AndroidNdk" }
if ($AndroidSdk) { $android += "-DCROMO_ANDROID_SDK=$AndroidSdk" }
cmake -S $root -B $build -G Ninja `
    '-DCMAKE_BUILD_TYPE=Release' `
    "-DCROMO_LLVM_ROOT=$LlvmRoot" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static' `
    '-DVCPKG_APPLOCAL_DEPS=OFF' @android
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# The library in build\lib, and LLVM, LLD and the shim in compiler\cromollvm.lib.
cmake --build $build --target cromo_library
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& (Join-Path $root 'tools\llvm-archive.ps1') -LlvmRoot $LlvmRoot
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# stage1: the compiler of these sources, built by the cromo of the version before with
# its own library. It finds build\lib, and compiles the runtime written in Cromo, which
# CMake puts in lib\ with the one in C.
New-Item -ItemType Directory -Force (Join-Path $build 'stage1') | Out-Null
$stage1 = Join-Path $build 'stage1\cromo.exe'
& $Bootstrap build --release (Join-Path $root 'compiler\cromo.cro') -o $stage1
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $build --target cromo_runtime
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# cromo.exe: these sources compiled by themselves, with the library and the runtime of
# these sources. tests\run-stage.ps1 checks that stage1 and it build the same programs.
& $stage1 build --release (Join-Path $root 'compiler\cromo.cro') -o (Join-Path $build 'cromo.exe')
exit $LASTEXITCODE
