# Checks the pilot of the compiler in Cromo (D417): build\cromo-fmt.exe must give the
# same results as cromo fmt in C++ on every .cro file of the repository: the same
# files written, the same names printed and the same errors, apart from the line
# endings of the output, which the runtime of Cromo writes as \n.
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-pilot.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cromo = Join-Path $root 'build\cromo.exe'
$pilot = Join-Path $root 'build\cromo-fmt.exe'
foreach ($needed in $cromo, $pilot) {
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
Remove-Item -Recurse -Force $work

'{0} files, C++ {1:N0} ms, Cromo {2:N0} ms' -f $count, $times['cpp'], $times['cromo']
if ($problems.Count -gt 0) {
    $problems | Select-Object -First 20
    'pilot: FAILED'
    exit 1
}
'pilot: the same results'
