# Checks the pilot of the compiler in Cromo (D417, D423): build\cromo-fmt.exe must give
# the same results as cromo fmt in C++ on every .cro file of the repository: the same
# files written, the same names printed and the same errors, apart from the line
# endings of the output, which the runtime of Cromo writes as \n; and
# build\cromo-check.exe the same errors as cromo build --check-names and as cromo build
# --list-declarations, --list-types and --check-moves on every program, and the same
# lists (P30, P89, P90); and the same IR of LLVM as cromo build --emit-ir, byte for
# byte, with and without --release, and for Linux on x86-64 and on ARM64 (D572).
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-pilot.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cromo = Join-Path $root 'build\cromo.exe'
$pilot = Join-Path $root 'build\cromo-fmt.exe'
$check = Join-Path $root 'build\cromo-check.exe'
foreach ($needed in $cromo, $pilot, $check) {
    if (-not (Test-Path $needed)) { throw "$needed is missing: run build.ps1" }
}

$work = Join-Path ([IO.Path]::GetTempPath()) "cromo-pilot-$PID"
$folders = 'library', 'examples', 'tests', 'bench', 'compiler', 'tools'
$count = 0
foreach ($side in 'cpp', 'cromo') {
    foreach ($folder in $folders) {
        foreach ($file in Get-ChildItem (Join-Path $root $folder) -Recurse -Filter *.cro) {
            if ($file.FullName -match '\\tests\\out2\\') { continue }
            $target = Join-Path (Join-Path $work $side) $file.FullName.Substring($root.Length + 1)
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item $file.FullName $target
            if ($side -eq 'cpp') { $count++ }
        }
    }
}

# The same work for both: they format every file of their copy, which the
# folders of the tests leave unformatted or with errors on purpose.
$times = @{}
foreach ($side in 'cpp', 'cromo') {
    $program = if ($side -eq 'cpp') { "`"$cromo`" fmt" } else { "`"$pilot`"" }
    $start = Get-Date
    cmd /c "cd /d `"$work\$side`" && $program > ..\$side.out 2> ..\$side.err"
    $times[$side] = ((Get-Date) - $start).TotalMilliseconds
}

$problems = @()
foreach ($stream in 'out', 'err') {
    $a = [IO.File]::ReadAllText("$work\cpp.$stream").Replace("`r`n", "`n")
    $b = [IO.File]::ReadAllText("$work\cromo.$stream").Replace("`r`n", "`n")
    if ($a -ne $b) { $problems += "the standard $(if ($stream -eq 'out') { 'output' } else { 'error' }) differs" }
}
foreach ($file in Get-ChildItem (Join-Path $work 'cpp') -Recurse -Filter *.cro) {
    $other = $file.FullName.Replace((Join-Path $work 'cpp'), (Join-Path $work 'cromo'))
    if ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash $other).Hash) {
        $problems += "differs: $($file.FullName.Substring($work.Length + 5))"
    }
}

# `cromo fmt -`, for the editors (D164): the same bytes on the standard output, and
# the same exit code.
$piped = 0
foreach ($folder in 'library', 'examples', 'tests\m11') {
    foreach ($file in Get-ChildItem (Join-Path $root $folder) -Recurse -Filter *.cro) {
        $piped++
        $results = @()
        foreach ($program in "`"$cromo`" fmt -", "`"$pilot`" -") {
            cmd /c "$program < `"$($file.FullName)`" > `"$work\piped.out`" 2> NUL"
            $results += "$LASTEXITCODE " + (Get-FileHash "$work\piped.out").Hash
        }
        if ($results[0] -ne $results[1]) { $problems += "differs with -: $($file.FullName.Substring($root.Length + 1))" }
    }
}

# Sema (D423, P30, P89, P90): the same exit code and the same errors as the C++ stopped
# after the names of the modules, after the types of the declarations, after the types
# of the expressions and after the moves, on every program; and the same lists of what
# the analysis knows: the structs with their layout, their instances and the
# signatures, then every function checked with its variables and its expressions.
$programs = 0
$libraries = 0
Push-Location $root
foreach ($file in Get-ChildItem 'tests', 'examples', 'bench' -Recurse -Filter *.cro) {
    if ($file.FullName -match '\\out2\\|\\modules\\|\\formatting\\|\\lsp\\|\\project\\' -or $file.Name -match '_test\.cro$') { continue }
    $programs++
    $relative = $file.FullName.Substring($root.Length + 1)
    $results = @()
    foreach ($program in "`"$cromo`" build --check-names", "`"$check`" --names", "`"$cromo`" build --list-declarations",
        "`"$check`" --list", "`"$cromo`" build --list-types", "`"$check`" --list-types",
        "`"$cromo`" build --check-moves", "`"$check`"", "`"$cromo`" build --emit-ir", "`"$check`" --emit-ir",
        "`"$cromo`" build --emit-ir --release", "`"$check`" --emit-ir --release",
        "`"$cromo`" build --emit-ir --target linux", "`"$check`" --emit-ir --target linux",
        "`"$cromo`" build --emit-ir --target linux-arm64", "`"$check`" --emit-ir --target linux-arm64") {
        cmd /c "$program `"$relative`" > `"$work\names.out`" 2> `"$work\names.err`""
        $results += "$LASTEXITCODE " + [IO.File]::ReadAllText("$work\names.err").Replace("`r`n", "`n") +
            [IO.File]::ReadAllText("$work\names.out").Replace("`r`n", "`n")
    }
    if ($results[0] -ne $results[1]) { $problems += "the names differ: $relative" }
    if ($results[2] -ne $results[3]) { $problems += "the declarations differ: $relative" }
    if ($results[4] -ne $results[5]) { $problems += "the types differ: $relative" }
    if ($results[6] -ne $results[7]) { $problems += "the moves differ: $relative" }
    # A program that passes the analysis and stops at its C libraries (D136, D137), which
    # cromo looks for before it generates the code: the pilot does not look for them yet.
    if ($results[6].StartsWith('0 ') -and -not $results[8].StartsWith('0 ')) {
        $libraries++
        continue
    }
    if ($results[8] -ne $results[9]) { $problems += "the IR differs: $relative" }
    if ($results[10] -ne $results[11]) { $problems += "the IR of --release differs: $relative" }
    # For another system a program can stop at its C libraries, or at a module that the
    # system has not: only the IR of those that the C++ compiles is compared.
    if ($results[12].StartsWith('0 ') -and $results[12] -ne $results[13]) { $problems += "the IR for Linux differs: $relative" }
    if ($results[14].StartsWith('0 ') -and $results[14] -ne $results[15]) { $problems += "the IR for Linux ARM64 differs: $relative" }
}
Pop-Location
Remove-Item -Recurse -Force $work

'{0} files, C++ {1:N0} ms, Cromo {2:N0} ms; {3} through the standard input; {4} programs for the analysis and the IR, {5} without the IR for their C libraries' -f $count, $times['cpp'], $times['cromo'], $piped, $programs, $libraries
if ($problems.Count -gt 0) {
    $problems | Select-Object -First 20
    'pilot: FAILED'
    exit 1
}
'pilot: the same results'
