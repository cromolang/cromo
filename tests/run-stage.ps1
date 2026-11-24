# Checks the two compilers of a build (D291, D576): build\stage1\cromo.exe, the sources
# compiled by the cromo of the version before, must build every program of tests\,
# examples\ and bench\ as build\cromo.exe does, the sources compiled by stage1: the same exit code and
# the same errors for those that are refused, and for the others an executable with
# the same bytes at the same path, apart from the time of the build that lld-link
# writes in an executable of Windows (two fields of four bytes).
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-stage.ps1 [-Filter <text>] [-Flags <options>]
#   -Flags passes extra options to every build, as -Flags '--release' or -Flags '--target linux'
#   -Stage names another cromo to compare, as build\stage2\cromo.exe
#   -Jobs is how many processes build at a time: by default half the processors, at most 8
param(
    [string]$Filter = '',
    [string]$Flags = '',
    [string]$Stage = '',
    [int]$Jobs = 0,
    # For the processes that this script starts: the programs of one of them.
    [int]$Shard = -1,
    [int]$Shards = 1
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cromo = Join-Path $root 'build\cromo.exe'
$stage = if ($Stage) { (Resolve-Path $Stage).Path } else { Join-Path $root 'build\stage1\cromo.exe' }
foreach ($needed in $cromo, $stage) {
    if (-not (Test-Path $needed)) { throw "$needed is missing: run build.ps1" }
}
# The programs in several processes, each with a folder of its own, where the two
# compilers write a program at the same path: the counts are added.
if ($Shard -lt 0 -and $Jobs -le 0) { $Jobs = [Math]::Max(1, [Math]::Min(8, [int]([Environment]::ProcessorCount / 2))) }
if ($Shard -lt 0 -and $Jobs -gt 1) {
    $logs = Join-Path ([IO.Path]::GetTempPath()) "cromo-stage-jobs-$PID"
    New-Item -ItemType Directory -Force $logs | Out-Null
    $shell = (Get-Process -Id $PID).Path
    $started = @()
    for ($i = 0; $i -lt $Jobs; $i++) {
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Stage `"$stage`" -Shard $i -Shards $Jobs"
        if ($Filter) { $arguments += " -Filter `"$Filter`"" }
        if ($Flags) { $arguments += " -Flags `"$Flags`"" }
        $started += Start-Process -FilePath $shell -ArgumentList $arguments -NoNewWindow -PassThru `
            -RedirectStandardOutput (Join-Path $logs "part$i.out") -RedirectStandardError (Join-Path $logs "part$i.err")
    }
    $started | Wait-Process
    $counts = @(0, 0, 0)
    $problems = @()
    for ($i = 0; $i -lt $Jobs; $i++) {
        $ended = $false
        foreach ($line in @(Get-Content (Join-Path $logs "part$i.out") -Encoding UTF8)) {
            if ($line -match '^(\d+) programs with the same executable, (\d+) refused with the same errors, (\d+) held by the antivirus$') {
                for ($k = 0; $k -lt 3; $k++) { $counts[$k] += [int]$matches[$k + 1] }
                $ended = $true
            } elseif ($line -match '^the (errors|executable) differ') {
                $problems += $line
            }
        }
        if (-not $ended) {
            throw "a process of the comparison did not end (part$i): $(Get-Content (Join-Path $logs "part$i.err") -Raw -ErrorAction SilentlyContinue)"
        }
    }
    Remove-Item -Recurse -Force $logs -ErrorAction SilentlyContinue
    '{0} programs with the same executable, {1} refused with the same errors, {2} held by the antivirus' -f $counts[0], $counts[1], $counts[2]
    if ($problems.Count -gt 0) {
        $problems | Select-Object -First 20
        'stage: FAILED'
        exit 1
    }
    'stage: the same results'
    exit 0
}

$work = Join-Path ([IO.Path]::GetTempPath()) "cromo-stage-$PID"
New-Item -ItemType Directory -Force $work | Out-Null
$out = Join-Path $work 'program.exe'

# The bytes of a file, or nothing when the antivirus holds it: that is left alone.
function Read-Program($path) {
    for ($try = 0; $try -lt 10; $try++) {
        try { return [IO.File]::ReadAllBytes($path) } catch [IO.IOException], [UnauthorizedAccessException] { Start-Sleep -Milliseconds 200 }
    }
    return $null
}

function Build-With($compiler, $relative) {
    Get-ChildItem $work | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    cmd /c "`"$compiler`" build -q $Flags `"$relative`" -o `"$out`" > `"$work.out`" 2> `"$work.err`""
    $code = $LASTEXITCODE
    $text = [IO.File]::ReadAllText("$work.err").Replace("`r`n", "`n") + [IO.File]::ReadAllText("$work.out").Replace("`r`n", "`n")
    $bytes = $null
    $held = $false
    if ($code -eq 0 -and (Test-Path $out)) {
        $bytes = Read-Program $out
        $held = $null -eq $bytes
    }
    return @{ Code = $code; Text = $text; Bytes = $bytes; Held = $held }
}

# How many bytes of two files of the same length differ, counted up to `most` and one more.
Add-Type -TypeDefinition 'public static class StageBytes { public static int Differences(byte[] a, byte[] b, int most) { int n = 0; for (int i = 0; i < a.Length && n <= most; i++) { if (a[i] != b[i]) { n++; } } return n; } }'

$same = 0
$refused = 0
$held = 0
$problems = @()
$index = -1
Push-Location $root
foreach ($file in Get-ChildItem 'tests', 'examples', 'bench' -Recurse -Filter *.cro) {
    if ($file.FullName -match '\\out2\\|\\modules\\|\\formatting\\|\\lsp\\|\\project\\' -or $file.Name -match '_test\.cro$') { continue }
    $relative = $file.FullName.Substring($root.Length + 1)
    if ($Filter -and $relative -notlike "*$Filter*") { continue }
    $index++
    if ($Shard -ge 0 -and ($index % $Shards) -ne $Shard) { continue }
    $a = Build-With $cromo $relative
    $b = Build-With $stage $relative
    if ($a.Held -or $b.Held) { $held++; continue }
    if ($null -eq $a.Bytes -or $null -eq $b.Bytes) {
        # A link that fails writes its errors in the order of the threads of the linker.
        $linked = $a.Text -match 'linking failed' -and $b.Text -match 'linking failed'
        if ($a.Code -eq $b.Code -and ($a.Text -eq $b.Text -or $linked)) { $refused++ } else { $problems += "the errors differ: $relative" }
        continue
    }
    $different = 0
    if ($a.Bytes.Length -ne $b.Bytes.Length) {
        $different = [Math]::Abs($a.Bytes.Length - $b.Bytes.Length) + 9
    } else {
        $different = [StageBytes]::Differences($a.Bytes, $b.Bytes, 8)
    }
    if ($different -gt 8) { $problems += "the executable differs: $relative" } else { $same++ }
}
Pop-Location
Remove-Item -Recurse -Force $work, "$work.out", "$work.err" -ErrorAction SilentlyContinue

'{0} programs with the same executable, {1} refused with the same errors, {2} held by the antivirus' -f $same, $refused, $held
if ($problems.Count -gt 0) {
    $problems | Select-Object -First 20
    'stage: FAILED'
    exit 1
}
'stage: the same results'
