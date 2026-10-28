# Behavior tests: compiles every .cro under tests/ and checks the result against
# the directives written in its comments:
#   // cromo-flags: --release       extra options for cromo
#   // expect-exit: 42              exit code of the program
#   // expect-output: text          one line of standard output (repeatable, exact match)
#   // expect-stderr: text          text that must appear in the standard error
#   // expect-error: text           text that must appear among the compiler errors
#   // expect-warning: text         text that must appear among the compiler warnings
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 [-Cromo <path>] [-Filter <text>] [-Flags <options>]
#   -Flags passes extra options to every compilation, for example -Flags -O0
param(
    [string]$Cromo = (Join-Path $PSScriptRoot '..\build\cromo.exe'),
    [string]$Filter = '',
    [string]$Flags = ''
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Cromo)) { throw "cromo not found: $Cromo (run build.ps1 first)" }
$Cromo = (Resolve-Path $Cromo).Path
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null

# Runs a program and returns its exit code, standard output and standard error,
# read separately and decoded as UTF-8.
function Invoke-Program([string]$Path, [string[]]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Path
    $info.Arguments = ($Arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    $process = [System.Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    [pscustomobject]@{
        Code = $process.ExitCode
        Out  = $stdout.Result -replace "`r`n", "`n"
        Err  = $stderr -replace "`r`n", "`n"
    }
}

$passed = 0
$failed = 0
foreach ($test in Get-ChildItem $PSScriptRoot -Recurse -Filter *.cro | Sort-Object FullName) {
    $name = $test.FullName.Substring($PSScriptRoot.Length + 1)
    if ($Filter -and -not $name.Contains($Filter)) { continue }

    $flags = @()
    if ($Flags) { $flags += ($Flags -split '\s+') }
    $expectExit = $null
    $expectOutput = @()
    $expectStderr = @()
    $expectError = @()
    $expectWarning = @()
    foreach ($line in Get-Content $test.FullName -Encoding UTF8) {
        if ($line -match '^\s*//\s*cromo-flags:\s*(.+?)\s*$') { $flags += ($matches[1] -split '\s+') }
        elseif ($line -match '^\s*//\s*expect-exit:\s*(-?\d+)\s*$') { $expectExit = [int64]$matches[1] }
        elseif ($line -match '^\s*//\s*expect-output:\s?(.*)$') { $expectOutput += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-stderr:\s*(.+?)\s*$') { $expectStderr += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-error:\s*(.+?)\s*$') { $expectError += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-warning:\s*(.+?)\s*$') { $expectWarning += $matches[1] }
    }

    $exe = Join-Path $out ($test.BaseName + '.exe')
    $compile = Invoke-Program $Cromo (@($test.FullName, '-o', $exe) + $flags)
    $compileText = $compile.Out + $compile.Err

    $problems = @()
    foreach ($w in $expectWarning) {
        if (-not $compileText.Contains($w)) { $problems += "missing warning: $w" }
    }
    if ($expectError.Count -gt 0) {
        if ($compile.Code -eq 0) { $problems += 'compilation should have failed' }
        foreach ($e in $expectError) {
            if (-not $compileText.Contains($e)) { $problems += "missing error: $e" }
        }
    } elseif ($compile.Code -ne 0) {
        $problems += "compilation failed:`n$compileText"
    } else {
        $run = Invoke-Program $exe @()
        if ($null -ne $expectExit -and $run.Code -ne $expectExit) {
            $problems += "exit code $($run.Code), expected $expectExit"
        }
        if ($expectOutput.Count -gt 0) {
            $expected = $expectOutput -join "`n"
            $actual = $run.Out.TrimEnd("`n")
            if ($actual -ne $expected) { $problems += "output:`n$actual`nexpected:`n$expected" }
        }
        foreach ($e in $expectStderr) {
            if (-not $run.Err.Contains($e)) { $problems += "missing in stderr: $e`nstderr was: $($run.Err)" }
        }
    }

    if ($problems.Count -eq 0) {
        $passed++
        Write-Host "ok    $name"
    } else {
        $failed++
        Write-Host "FAIL  $name" -ForegroundColor Red
        foreach ($p in $problems) { Write-Host "      $p" }
    }
}

Write-Host ""
Write-Host "$passed passed, $failed failed"
if ($failed -gt 0) { exit 1 }
