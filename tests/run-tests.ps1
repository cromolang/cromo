# Behavior tests: compiles every .cro under tests/ and checks the result against
# the expectations written in its comments:
#   // expect-exit: 42          exit code of the program
#   // expect-output: text      one line of standard output (repeatable)
#   // expect-error: text       text that must appear among the compiler errors
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 [-Cromo <path>]
param([string]$Cromo = (Join-Path $PSScriptRoot '..\build\cromo.exe'))

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8  # cromo writes UTF-8

if (-not (Test-Path $Cromo)) { throw "cromo not found: $Cromo (run build.ps1 first)" }
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null

$passed = 0
$failed = 0
foreach ($test in Get-ChildItem $PSScriptRoot -Recurse -Filter *.cro | Sort-Object FullName) {
    $expectExit = $null
    $expectOutput = @()
    $expectError = @()
    foreach ($line in Get-Content $test.FullName -Encoding UTF8) {
        if ($line -match '^\s*//\s*expect-exit:\s*(-?\d+)\s*$') { $expectExit = [int64]$matches[1] }
        elseif ($line -match '^\s*//\s*expect-output:\s?(.*)$') { $expectOutput += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-error:\s*(.+?)\s*$') { $expectError += $matches[1] }
    }

    $exe = Join-Path $out ($test.BaseName + '.exe')
    $ErrorActionPreference = 'Continue'
    $compileText = (& $Cromo $test.FullName -o $exe 2>&1 | ForEach-Object { "$_" }) -join "`n"
    $compileCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'

    $problems = @()
    if ($expectError.Count -gt 0) {
        if ($compileCode -eq 0) { $problems += 'compilation should have failed' }
        foreach ($e in $expectError) {
            if (-not $compileText.Contains($e)) { $problems += "missing error: $e" }
        }
    } elseif ($compileCode -ne 0) {
        $problems += "compilation failed:`n$compileText"
    } else {
        $ErrorActionPreference = 'Continue'
        $runText = (& $exe 2>&1 | ForEach-Object { "$_" }) -join "`n"
        $runCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($null -ne $expectExit -and $runCode -ne $expectExit) {
            $problems += "exit code $runCode, expected $expectExit"
        }
        if ($expectOutput.Count -gt 0) {
            $expected = $expectOutput -join "`n"
            if ($runText -ne $expected) { $problems += "output:`n$runText`nexpected:`n$expected" }
        }
    }

    $name = $test.FullName.Substring($PSScriptRoot.Length + 1)
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
