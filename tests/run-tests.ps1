# Behavior tests: compiles every .cro under tests/ and checks the result against
# the directives written in its comments. The files in a folder named `modules`
# are imported by the tests, and are not tests themselves (D50).
#   // cromo-flags: --release       extra options for cromo
#   // expect-exit: 42              exit code of the program
#   // expect-output: text          one line of standard output (repeatable, exact match)
#   // expect-stderr: text          text that must appear in the standard error
#   // expect-error: text           text that must appear among the compiler errors
#   // expect-warning: text         text that must appear among the compiler warnings
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 [-Cromo <path>] [-Filter <text>] [-Flags <options>]
#   -Flags passes extra options to every compilation, for example -Flags -O0
# The C libraries in m8\native are built first with the clang of LLVM (D136).
param(
    [string]$Cromo = (Join-Path $PSScriptRoot '..\build\cromo.exe'),
    [string]$Filter = '',
    [string]$Flags = '',
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Cromo)) { throw "cromo not found: $Cromo (run build.ps1 first)" }
$Cromo = (Resolve-Path $Cromo).Path
$out = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force $out | Out-Null

# A program that cannot find a DLL ends with an error code, instead of opening a
# window of Windows that would stop the tests. The programs inherit this mode.
Add-Type -Namespace Cromo -Name ErrorMode -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);'
[Cromo.ErrorMode]::SetErrorMode(0x8003) | Out-Null

# The libraries next to the modules of m8: a DLL, a static library, and a DLL
# for 32 bits that cromo must reject. Without the C library, like the runtime.
function Invoke-Tool([string]$Tool, [string[]]$Arguments) {
    & (Join-Path $LlvmRoot "bin\$Tool") @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Tool failed: $Arguments" }
}
$native = Join-Path $PSScriptRoot 'm8\native'
$modules = Join-Path $PSScriptRoot 'm8\modules'
$cflags = @('-ffreestanding', '-fno-builtin', '-nostdlib', '-O2', '-c')
Invoke-Tool 'clang.exe' ($cflags + @('--target=x86_64-pc-windows-msvc', "$native\adder.c", '-o', "$out\adder.obj"))
Invoke-Tool 'lld-link.exe' @('/nologo', '/dll', '/noentry', '/noimplib', '/nodefaultlib', '/machine:x64', "/out:$modules\adder.dll", "$out\adder.obj")
Invoke-Tool 'clang.exe' ($cflags + @('--target=i686-pc-windows-msvc', "$native\adder.c", '-o', "$out\adder32.obj"))
Invoke-Tool 'lld-link.exe' @('/nologo', '/dll', '/noentry', '/noimplib', '/nodefaultlib', '/machine:x86', "/out:$modules\old32.dll", "$out\adder32.obj")
Invoke-Tool 'clang.exe' ($cflags + @('--target=x86_64-pc-windows-msvc', "$native\scale.c", '-o', "$out\scale.obj"))
Invoke-Tool 'llvm-lib.exe' @('/nologo', "/out:$modules\scale.lib", "$out\scale.obj")

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
    if ($name -split '\\' -contains 'modules') { continue }
    if ($Filter -and -not $name.Contains($Filter)) { continue }

    # Not $flags: names are case-insensitive, it would overwrite the -Flags parameter.
    $compileFlags = @()
    if ($Flags) { $compileFlags += ($Flags -split '\s+') }
    $expectExit = $null
    $expectOutput = @()
    $expectStderr = @()
    $expectError = @()
    $expectWarning = @()
    foreach ($line in Get-Content $test.FullName -Encoding UTF8) {
        if ($line -match '^\s*//\s*cromo-flags:\s*(.+?)\s*$') { $compileFlags += ($matches[1] -split '\s+') }
        elseif ($line -match '^\s*//\s*expect-exit:\s*(-?\d+)\s*$') { $expectExit = [int64]$matches[1] }
        elseif ($line -match '^\s*//\s*expect-output:\s?(.*)$') { $expectOutput += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-stderr:\s*(.+?)\s*$') { $expectStderr += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-error:\s*(.+?)\s*$') { $expectError += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-warning:\s*(.+?)\s*$') { $expectWarning += $matches[1] }
    }

    $exe = Join-Path $out ($test.BaseName + '.exe')
    $compile = Invoke-Program $Cromo (@($test.FullName, '-o', $exe) + $compileFlags)
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
