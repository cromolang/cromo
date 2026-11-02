# Checks the pilot of the compiler in Cromo (D417, D423): build\cromo-fmt.exe must give
# the same results as cromo fmt in C++ on every .cro file of the repository: the same
# files written, the same names printed and the same errors, apart from the line
# endings of the output, which the runtime of Cromo writes as \n; and
# build\cromo-check.exe the same errors as cromo build --check-names on every program
# (P30).
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
            if ($file.FullName -match '\\tests\\out\\') { continue }
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

# The first part of sema (D423, P30): the same exit code and the same errors as the
# C++ stopped after the names of the modules, on every program.
$programs = 0
Push-Location $root
foreach ($file in Get-ChildItem 'tests', 'examples', 'bench' -Recurse -Filter *.cro) {
    if ($file.FullName -match '\\out\\|\\modules\\|\\formatting\\|\\lsp\\|\\project\\' -or $file.Name -match '_test\.cro$') { continue }
    $programs++
    $relative = $file.FullName.Substring($root.Length + 1)
    $results = @()
    foreach ($program in "`"$cromo`" build --check-names", "`"$check`"") {
        cmd /c "$program `"$relative`" > NUL 2> `"$work\names.err`""
        $results += "$LASTEXITCODE " + [IO.File]::ReadAllText("$work\names.err").Replace("`r`n", "`n")
    }
    if ($results[0] -ne $results[1]) { $problems += "the names differ: $relative" }
}
Pop-Location
Remove-Item -Recurse -Force $work

'{0} files, C++ {1:N0} ms, Cromo {2:N0} ms; {3} through the standard input; {4} programs for the names' -f $count, $times['cpp'], $times['cromo'], $piped, $programs
if ($problems.Count -gt 0) {
    $problems | Select-Object -First 20
    'pilot: FAILED'
    exit 1
}
'pilot: the same results'
