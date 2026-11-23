# Makes compiler\cromollvm.lib, the C library of the compiler written in Cromo (D291):
# compiler\shim\shim.cpp, the libraries of LLVM and of LLD that cromo.exe is linked
# with, and the runtime of C and C++ of MSVC, in one archive, which `extern
# "cromollvm"` of compiler\llvm.cro finds next to its module (D136). It is called by
# build.ps1, in the environment of vcvars64, after CMake wrote build\build.ninja.
# Usage: powershell -ExecutionPolicy Bypass -File tools\llvm-archive.ps1 [-LlvmRoot <dir>]
param(
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'build'
$shim = Join-Path $root 'compiler\shim\shim.cpp'
$archive = Join-Path $root 'compiler\cromollvm.lib'
$ninja = Join-Path $build 'build.ninja'
if (-not (Test-Path $ninja)) { throw "$ninja is missing: run build.ps1" }
if ((Test-Path $archive) -and (Get-Item $archive).LastWriteTime -gt (Get-Item $shim).LastWriteTime -and
    (Get-Item $archive).LastWriteTime -gt (Get-Item $PSCommandPath).LastWriteTime) {
    exit 0
}

$object = Join-Path $build 'shim.obj'
cl /nologo /c /MT /O2 /EHsc /GR- /std:c++17 /utf-8 "/I$LlvmRoot\include" "/Fo$object" $shim
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# The libraries of the link of cromo.exe, in its order, each once.
$line = (Select-String -Path $ninja -Pattern 'LINK_LIBRARIES = .*lldCOFF' | Select-Object -First 1).Line
$libraries = @()
foreach ($match in [regex]::Matches($line.Substring($line.IndexOf('=') + 1), '"[^"]+"|\S+')) {
    $item = $match.Value.Trim('"')
    if ($item -notmatch '\.lib$') { continue }
    if ($item -match '^vcpkg_installed') { $item = Join-Path $build $item }
    if (-not [IO.Path]::IsPathRooted($item)) {
        $item = ($env:LIB -split ';' | Where-Object { $_ -and (Test-Path (Join-Path $_ $item)) } | Select-Object -First 1 |
            ForEach-Object { Join-Path $_ $item })
        if (-not $item) { continue }
    }
    if ($libraries -notcontains $item) { $libraries += $item }
}
# The runtime of C and C++, static as that of LLVM (/MT), which cromo.exe gets by
# default and a program of Cromo does not: its linker has no default libraries.
foreach ($name in 'libcmt.lib', 'libcpmt.lib', 'libvcruntime.lib', 'libucrt.lib', 'oldnames.lib', 'comsuppw.lib') {
    $found = $env:LIB -split ';' | Where-Object { $_ -and (Test-Path (Join-Path $_ $name)) } | Select-Object -First 1
    if (-not $found) { throw "$name was not found in the libraries of MSVC" }
    $libraries += (Join-Path $found $name)
}
$response = Join-Path $build 'cromollvm.rsp'
[IO.File]::WriteAllLines($response, @('/nologo', "/out:`"$archive`"", "`"$object`"") + ($libraries | ForEach-Object { "`"$_`"" }))
# LNK4006 and LNK4221: the same function in two libraries, as malloc of LLVM and of
# the C runtime; the first is kept, as the linker of cromo.exe does.
lib.exe "@$response" | Select-String -NotMatch 'LNK4006|LNK4221'
if ($LASTEXITCODE) { exit $LASTEXITCODE }
"cromollvm.lib: $($libraries.Count) libraries, $([int]((Get-Item $archive).Length / 1MB)) MB"
