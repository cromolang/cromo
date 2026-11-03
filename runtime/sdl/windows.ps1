# SDL3.lib for Windows (D429), from the static library that the build of SDL
# writes without the C runtime: one archive with
# - SDL, whose memcpy, memmove and memset get other names, since the runtime of
#   Cromo has them (D19);
# - the GUIDs of COM of guids.c, which SDL takes from uuid.lib of the Windows SDK;
# - the import libraries of the DLLs of the system that SDL calls, written from
#   the functions that they export, as cromo does for the extern blocks (D136).
# Usage: windows.ps1 -Static <SDL3-static.lib> -Out <SDL3.lib> -Work <folder> -Llvm <root of LLVM>
param(
    [Parameter(Mandatory)] [string]$Static,
    [Parameter(Mandatory)] [string]$Out,
    [Parameter(Mandatory)] [string]$Work,
    [Parameter(Mandatory)] [string]$Llvm
)

$ErrorActionPreference = 'Stop'
$bin = Join-Path $Llvm 'bin'
function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    $output = & (Join-Path $bin $Tool) @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed: $Arguments" }
    $output
}

if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force $Work | Out-Null
$library = Join-Path $Work 'sdl.lib'
Copy-Item $Static $library

Invoke-Tool 'clang-cl.exe' @('-nologo', '-c', '-O2', '-GS-', '-Zl', "-Fo$Work\guids.obj", (Join-Path $PSScriptRoot 'guids.c'))

foreach ($name in 'memcpy', 'memmove', 'memset') {
    $member = Invoke-Tool 'llvm-ar.exe' @('t', $library) | Where-Object { $_ -match "SDL_$name\.c\.obj$" }
    if (-not $member) { throw "SDL_$name.c.obj is not in $Static" }
    Invoke-Tool 'llvm-ar.exe' @('x', $library, $member, '--output', $Work)
    $object = Join-Path $Work (Split-Path -Leaf $member)
    Invoke-Tool 'llvm-objcopy.exe' @('--redefine-sym', "$name=SDL_unused_$name", $object)
    Invoke-Tool 'llvm-ar.exe' @('d', $library, $member)
    Invoke-Tool 'llvm-lib.exe' @('-nologo', "-out:$library", $library, $object)
}

# The names that SDL leaves undefined, without __imp_, and the DLL of each.
$defined = @{}
foreach ($symbol in Invoke-Tool 'llvm-nm.exe' @('--defined-only', '--format=just-symbols', $library, "$Work\guids.obj")) {
    $defined[$symbol] = $true
}
$needed = @{}
foreach ($symbol in Invoke-Tool 'llvm-nm.exe' @('--undefined-only', '--format=just-symbols', $library)) {
    if ($symbol -and -not $defined[$symbol]) { $needed[($symbol -replace '^__imp_', '')] = $true }
}
$dlls = 'kernel32', 'user32', 'gdi32', 'imm32', 'ole32', 'oleaut32', 'shell32', 'setupapi', 'version', 'advapi32', 'winmm'
$system = [Environment]::GetFolderPath('System')
$definitions = @{}
$owner = @{}
foreach ($dll in $dlls) {
    $definitions[$dll] = [System.Collections.Generic.List[string]]::new()
    foreach ($line in Invoke-Tool 'llvm-readobj.exe' @('--coff-exports', (Join-Path $system "$dll.dll"))) {
        if ($line -match '^\s+Name: (\S+)$' -and -not $owner.ContainsKey($matches[1])) { $owner[$matches[1]] = $dll }
    }
}
$unknown = @()
foreach ($name in $needed.Keys | Sort-Object) {
    if ($owner.ContainsKey($name)) { $definitions[$owner[$name]].Add("    $name") } else { $unknown += $name }
}
# The others are the functions of the runtime of Cromo (D19).
$runtime = 'memcpy', 'memmove', 'memset', 'memcmp', '__chkstk', '_fltused'
$missing = $unknown | Where-Object { $runtime -notcontains $_ }
if ($missing) { throw "SDL calls functions that are not in the DLLs of the system nor in the runtime: $($missing -join ' ')" }

$parts = @($library, (Join-Path $Work 'guids.obj'))
foreach ($dll in $dlls) {
    if ($definitions[$dll].Count -eq 0) { continue }
    $def = Join-Path $Work "$dll.def"
    Set-Content -Encoding ascii $def (@("LIBRARY $dll.dll", 'EXPORTS') + $definitions[$dll])
    Invoke-Tool 'llvm-lib.exe' @('-nologo', '-machine:x64', "-def:$def", "-out:$Work\$dll.lib")
    $parts += Join-Path $Work "$dll.lib"
}
Invoke-Tool 'llvm-lib.exe' (@('-nologo', "-out:$Out") + $parts)
