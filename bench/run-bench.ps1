# Benchmarks: compiles each program of bench\ with cromo and with the reference
# compilers, runs it, checks that every version prints the same result, and
# writes the compile and run times in a table, in Markdown and in CSV, in
# bench\results.
#
# A benchmark is a folder with the same program in four languages: name.cro,
# name.cpp, name.go and name.rs, which prints a checksum of its work; a language
# without its file is left out of that benchmark, as tasks and http, which only
# Go has (a dash in the table). The
# programs run on Linux, in WSL, where the reference compilers are: cromo
# compiles on Windows with --target linux, the others in WSL. The times are
# the median of several runs, taken inside WSL by linux.sh, after a first run
# that warms up the caches and gives the output to check.
#
# Usage: powershell -ExecutionPolicy Bypass -File bench\run-bench.ps1 [options]
#   -Cromo <paths>       the cromo.exe to measure, separated by commas, each a
#                        column: path, or label=path (default build\cromo.exe)
#   -Filter <text>       only the benchmarks whose name contains the text
#   -Runs <n>            timed runs of each program (default 5)
#   -CompileRuns <n>     timed compilations of each program (default 3)
#   -References <names>  the reference compilers, separated by commas (default
#                        g++,clang++,go,rustc); a missing one is left out
#   -Gxx, -Clangxx, -Go  the compilers in WSL (default g++, clang++, and go in
#                        the PATH or /opt/go/bin/go)
#   -RustToolchain       the toolchain of rustup for rustc (default stable)
#   -Distro              the WSL distribution (default: the default one)
#   -Name                the name of the result files (default: date and time)
param(
    [string]$Cromo = (Join-Path $PSScriptRoot '..\build\cromo.exe'),
    [string]$Filter = '',
    [int]$Runs = 5,
    [int]$CompileRuns = 3,
    [string]$References = 'g++,clang++,go,rustc',
    [string]$Gxx = 'g++',
    [string]$Clangxx = 'clang++',
    [string]$Go = '',
    [string]$RustToolchain = 'stable',
    [string]$Distro = '',
    [string]$Name = (Get-Date -Format 'yyyy-MM-dd-HHmm')
)

$ErrorActionPreference = 'Stop'
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$work = '/tmp/cromo-bench'  # in WSL, out of /mnt/c, which is slow

# Writes an argument in a command line so that the program reads it back as it
# is, with the rules of the C runtime of Microsoft.
function ConvertTo-Argument([string]$Text) {
    if ($Text -and $Text -notmatch '[\s"]') { return $Text }
    $quoted = '"'
    $backslashes = 0
    foreach ($c in $Text.ToCharArray()) {
        if ($c -eq '\') {
            $backslashes++
            continue
        }
        if ($c -eq '"') { $quoted += '\' * ($backslashes * 2 + 1) } else { $quoted += '\' * $backslashes }
        $backslashes = 0
        $quoted += $c
    }
    $quoted + ('\' * ($backslashes * 2)) + '"'
}

# Runs a program and returns its exit code, standard output and standard error.
function Invoke-Program([string]$Path, [string[]]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Path
    $info.Arguments = ($Arguments | ForEach-Object { ConvertTo-Argument $_ }) -join ' '
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

# A folder of Windows as WSL sees it.
function ConvertTo-LinuxPath([string]$Path) {
    '/mnt/' + $Path.Substring(0, 1).ToLower() + ($Path.Substring(2) -replace '\\', '/')
}

# Runs linux.sh in WSL.
$linuxScript = ConvertTo-LinuxPath (Join-Path $PSScriptRoot 'linux.sh')
function Invoke-Linux([string[]]$Arguments) {
    $options = @()
    if ($Distro) { $options += @('-d', $Distro) }
    Invoke-Program 'wsl.exe' ($options + @('-e', 'bash', $linuxScript) + $Arguments)
}

# Runs a command in WSL with `linux.sh measure`: the exit code, the times in
# milliseconds, the standard output and the standard error of the first run.
function Measure-Linux([int]$Count, [string]$Folder, [string]$Output, [string]$Vary, [string[]]$Command) {
    $arguments = @('measure', $Count, $Output, $Folder)
    if ($Vary) { $arguments += @('--vary', $Vary) }
    $result = Invoke-Linux ($arguments + @('--') + $Command)
    $lines = $result.Out -split "`n", 2
    $numbers = @($lines[0].Trim() -split '\s+')
    if ($result.Code -ne 0 -or $numbers[0] -notmatch '^\d+$') { throw "linux.sh measure failed: $($result.Out)$($result.Err)" }
    $text = if ($lines.Count -gt 1) { $lines[1] } else { '' }
    $parts = $text -split "(?m)^--- stderr`n", 2
    [pscustomobject]@{
        Code  = [int]$numbers[0]
        Times = @($numbers | Select-Object -Skip 1 | ForEach-Object { [double]$_ / 1000.0 })
        Out   = $parts[0]
        Err   = $(if ($parts.Count -gt 1) { $parts[1] } else { '' })
    }
}

function Get-Median([double[]]$Values) {
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    $middle = [math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return $sorted[$middle] }
    ($sorted[$middle - 1] + $sorted[$middle]) / 2
}

function Format-Milliseconds($Value) {
    if ($null -eq $Value) { return '' }
    $format = if ($Value -ge 100) { '{0:0}' } elseif ($Value -ge 10) { '{0:0.0}' } else { '{0:0.00}' }
    [string]::Format($invariant, $format, $Value)
}

# The benchmarks: the folders with a program in Cromo, and what each measures,
# from its first line: "// Benchmark name: what it measures."
$benchmarks = @()
foreach ($folder in Get-ChildItem $PSScriptRoot -Directory | Sort-Object Name) {
    $source = Join-Path $folder.FullName "$($folder.Name).cro"
    if (-not (Test-Path $source)) { continue }
    if ($Filter -and -not $folder.Name.Contains($Filter)) { continue }
    $first = Get-Content $source -TotalCount 1
    $what = if ($first -match '^//\s*Benchmark\s+\w+:\s*(.+?)\.?\s*$') { $matches[1] } else { '' }
    $benchmarks += [pscustomobject]@{ Name = $folder.Name; Folder = $folder.FullName; What = $what }
}
if ($benchmarks.Count -eq 0) { throw "no benchmark matches '$Filter'" }

# The columns of the table: the builds of cromo, then the reference compilers.
$columns = @()
$index = 0
foreach ($entry in ($Cromo -split ',')) {
    $label = ''
    $path = $entry.Trim()
    if (-not (Test-Path $path) -and $path -match '^([^=]+)=(.+)$') {
        $label = $matches[1]
        $path = $matches[2]
    }
    if (-not (Test-Path $path)) { throw "cromo not found: $path" }
    $path = (Resolve-Path $path).Path
    if ($path.StartsWith([System.IO.Path]::GetFullPath($env:TEMP), [System.StringComparison]::OrdinalIgnoreCase)) {
        # Seen on Windows 11: about one second more for every start of cromo.exe.
        Write-Host "$path is in the temporary folder, whose programs Windows checks at every start: the compile times will be too high" -ForegroundColor Yellow
    }
    $version = (Invoke-Program $path @('version')).Out.Trim()
    if (-not $label) { $label = if ($version -match '^cromo (\S+)') { "cromo $($matches[1])" } else { 'cromo' } }
    $columns += [pscustomobject]@{
        Key = "cromo$index"; Kind = 'cromo'; Label = $label; Path = $path; Version = $version
        Command = 'cromo build --release --target linux (on Windows)'; Extension = 'cro'
    }
    $index++
}
# Two builds with the same label are told apart by their folder.
foreach ($group in $columns | Group-Object Label | Where-Object { $_.Count -gt 1 }) {
    foreach ($column in $group.Group) { $column.Label += " ($(Split-Path (Split-Path $column.Path) -Leaf))" }
}

$wanted = @($References -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$toolchains = Invoke-Linux @('toolchains', $Gxx, $Clangxx, $Go, $RustToolchain)
if ($toolchains.Code -ne 0) { throw "WSL is not available: $($toolchains.Out)$($toolchains.Err)" }
$system = ''
$missing = @()
foreach ($line in $toolchains.Out.Trim() -split "`n") {
    $fields = $line -split '\|'
    if ($fields[0] -eq 'system') {
        $system = "$($fields[1]); $($fields[2])"
        continue
    }
    if ($wanted -notcontains $fields[0]) { continue }
    if (-not $fields[1]) {
        $missing += $fields[0]
        continue
    }
    $short = if ($fields[2] -match '(\d+\.\d+(\.\d+)?)') { $matches[1] } else { '' }
    $column = [pscustomobject]@{ Key = ''; Kind = $fields[0]; Label = "$($fields[0]) $short"; Path = $fields[1]; Version = $fields[2]; Command = ''; Extension = '' }
    switch ($fields[0]) {
        'g++' {
            $column.Key = 'gxx'
            $column.Extension = 'cpp'
            $column.Command = 'g++ -O2 -std=c++20 -pthread -static-libstdc++ -static-libgcc'
        }
        'clang++' {
            $column.Key = 'clang'
            $column.Extension = 'cpp'
            $column.Command = 'clang++ -O2 -std=c++20 -pthread -static-libstdc++ -static-libgcc'
            if ($fields[3]) { $column.Command += " --gcc-toolchain=$($fields[3])" }  # the libstdc++ of g++, with <format>
        }
        'go' {
            $column.Key = 'go'
            $column.Extension = 'go'
            $column.Command = 'go build'
        }
        'rustc' {
            $column.Key = 'rust'
            $column.Extension = 'rs'
            $column.Command = 'rustc -C opt-level=3 --edition 2021 (as cargo --release)'
        }
    }
    $columns += $column
}

# The command that compiles a benchmark with a reference compiler, in its folder.
function Get-CompileCommand($Column, [string]$Benchmark) {
    $source = "$Benchmark.$($Column.Extension)"
    $program = "$Benchmark-$($Column.Key)"
    $options = ($Column.Command -replace ' \(.*\)$', '') -split ' ' | Select-Object -Skip 1
    switch ($Column.Kind) {
        'go' { @($Column.Path, 'build', '-o', $program, $source) }
        default { @($Column.Path) + $options + @($source, '-o', $program) }
    }
}

Write-Host "System: $system"
foreach ($column in $columns) { Write-Host ("{0,-16} {1}" -f $column.Label, $column.Version) }
foreach ($m in $missing) { Write-Host "$m is not installed in WSL: its column is left out" -ForegroundColor Yellow }
Write-Host ''

# Cromo compiles on Windows: the times are taken here.
$results = @{}
$programs = Join-Path $PSScriptRoot 'out'
foreach ($benchmark in $benchmarks) {
    $folder = Join-Path $programs $benchmark.Name
    New-Item -ItemType Directory -Force $folder | Out-Null
    foreach ($column in $columns | Where-Object { $_.Kind -eq 'cromo' }) {
        $program = Join-Path $folder "$($benchmark.Name)-$($column.Key)"
        Remove-Item -Force -ErrorAction SilentlyContinue $program
        $arguments = @('build', (Join-Path $benchmark.Folder "$($benchmark.Name).cro"), '--release', '--target', 'linux', '-o', $program)
        $first = Invoke-Program $column.Path $arguments
        $times = @()
        if ($first.Code -eq 0) {
            for ($i = 0; $i -lt $CompileRuns; $i++) {
                $watch = [System.Diagnostics.Stopwatch]::StartNew()
                Invoke-Program $column.Path $arguments | Out-Null
                $watch.Stop()
                $times += $watch.Elapsed.TotalMilliseconds
            }
        }
        $results["$($benchmark.Name)|$($column.Key)"] = [pscustomobject]@{
            Compiled = $first.Code -eq 0; CompileTimes = $times; CompileText = $first.Out + $first.Err
            RunTimes = @(); Output = $null; Problem = ''
        }
    }
}

# The sources and the programs of cromo go to a folder of WSL; the other
# compilers compile there, and all the programs run there.
$prepare = Invoke-Linux (@('prepare', (ConvertTo-LinuxPath $PSScriptRoot), (ConvertTo-LinuxPath $programs), $work) + ($benchmarks | ForEach-Object { $_.Name }))
if ($prepare.Code -ne 0) { throw "cannot copy the benchmarks to WSL: $($prepare.Err)" }

foreach ($benchmark in $benchmarks) {
    $folder = "$work/$($benchmark.Name)"
    foreach ($column in $columns) {
        $key = "$($benchmark.Name)|$($column.Key)"
        if ($column.Kind -ne 'cromo' -and -not (Test-Path (Join-Path $PSScriptRoot "$($benchmark.Name)\$($benchmark.Name).$($column.Extension)"))) {
            $results[$key] = [pscustomobject]@{
                Compiled = $true; Missing = $true; CompileTimes = @(); CompileText = ''; RunTimes = @(); Output = $null; Problem = ''
            }
            continue
        }
        if ($column.Kind -ne 'cromo') {
            $vary = if ($column.Kind -eq 'go') { "$($benchmark.Name).go" } else { '' }
            $compile = Measure-Linux $CompileRuns $folder "compile-$($column.Key).txt" $vary (Get-CompileCommand $column $benchmark.Name)
            $results[$key] = [pscustomobject]@{
                Compiled = $compile.Code -eq 0; CompileTimes = $(if ($compile.Code -eq 0) { $compile.Times } else { @() })
                CompileText = $compile.Out + $compile.Err; RunTimes = @(); Output = $null; Problem = ''
            }
        }
        $result = $results[$key]
        if (-not $result.Compiled) {
            $result.Problem = "compilation failed: $($result.CompileText.Trim())"
        } else {
            $run = Measure-Linux $Runs $folder "output-$($column.Key).txt" '' @("./$($benchmark.Name)-$($column.Key)")
            $result.Output = $run.Out
            if ($run.Code -ne 0) {
                $result.Problem = "exit code $($run.Code): $($run.Err.Trim())"
            } else {
                $result.RunTimes = $run.Times
            }
        }
        $compileText = Format-Milliseconds (Get-Median $result.CompileTimes)
        $runText = Format-Milliseconds (Get-Median $result.RunTimes)
        Write-Host ("{0,-14} {1,-16} compile {2,8} ms   run {3,8} ms" -f $benchmark.Name, $column.Label, $compileText, $runText)
        if ($result.Problem) { Write-Host "    $($result.Problem)" -ForegroundColor Red }
    }

    # The output that most versions print is the right one; the others are wrong.
    $outputs = @($columns | ForEach-Object { $results["$($benchmark.Name)|$($_.Key)"] } | Where-Object { -not $_.Problem -and -not $_.Missing })
    $expected = ($outputs | Group-Object Output -CaseSensitive | Sort-Object Count -Descending | Select-Object -First 1).Name
    foreach ($column in $columns) {
        $result = $results["$($benchmark.Name)|$($column.Key)"]
        if (-not $result.Problem -and -not $result.Missing -and $result.Output -cne $expected) {
            # The first line that differs, then the whole outputs.
            $mine = @($result.Output.TrimEnd() -split "`n")
            $right = @($expected.TrimEnd() -split "`n")
            $line = 0
            while ($line -lt $mine.Count -and $line -lt $right.Count -and $mine[$line] -ceq $right[$line]) { $line++ }
            $result.Problem = "wrong output: '$(if ($line -lt $mine.Count) { $mine[$line] })' instead of '$(if ($line -lt $right.Count) { $right[$line] })'" +
                "`n$($result.Output.TrimEnd())`nexpected:`n$($expected.TrimEnd())"
            Write-Host "    $($column.Label): $($result.Problem)" -ForegroundColor Red
        }
    }
}

# A cell of the table: the median, or what went wrong.
function Get-Cell($Result, [string]$What) {
    if ($Result.Missing) { return '-' }
    if (-not $Result.Compiled) { return 'error' }
    if ($What -eq 'compile') { return Format-Milliseconds (Get-Median $Result.CompileTimes) }
    if ($Result.Problem -like 'wrong output*') { return 'wrong' }
    if ($Result.Problem) { return 'failed' }
    Format-Milliseconds (Get-Median $Result.RunTimes)
}

# The geometric mean of the ratios to a column, over the benchmarks that both
# have.
$baseline = $columns | Where-Object { $_.Kind -eq 'g++' } | Select-Object -First 1
if (-not $baseline) { $baseline = $columns[0] }
function Get-Ratio($Column, [string]$What) {
    $logs = @()
    foreach ($benchmark in $benchmarks) {
        $mine = $results["$($benchmark.Name)|$($Column.Key)"]
        $base = $results["$($benchmark.Name)|$($baseline.Key)"]
        $times = if ($What -eq 'compile') { 'CompileTimes' } else { 'RunTimes' }
        if ($mine.$times.Count -eq 0 -or $base.$times.Count -eq 0 -or ($What -eq 'run' -and ($mine.Problem -or $base.Problem))) { continue }
        $logs += [math]::Log((Get-Median $mine.$times) / (Get-Median $base.$times))
    }
    if ($logs.Count -eq 0) { return '' }
    [string]::Format($invariant, '{0:0.00}', [math]::Exp(($logs | Measure-Object -Sum).Sum / $logs.Count))
}

$md = @()
$md += '# Cromo benchmarks'
$md += ''
$md += "$(Get-Date -Format 'yyyy-MM-dd HH:mm'). $system."
$md += ''
$md += "The programs run in WSL. The times are in milliseconds: the median of $Runs runs, after one that warms up, and of $CompileRuns compilations. Every program prints a checksum of its work, and all the versions of a benchmark must print the same."
$md += ''
$md += '## Compilers'
$md += ''
$md += '| Column | Version | Command |'
$md += '|---|---|---|'
foreach ($column in $columns) { $md += "| $($column.Label) | $($column.Version) | ``$($column.Command)`` |" }
foreach ($m in $missing) { $md += "| $m | not installed in WSL | |" }
foreach ($what in @('run', 'compile')) {
    $md += ''
    $md += $(if ($what -eq 'run') { '## Run time (ms)' } else { '## Compile time (ms)' })
    $md += ''
    $header = '| Benchmark |'
    $rule = '|---|'
    if ($what -eq 'run') {
        $header += ' Measures |'
        $rule += '---|'
    }
    foreach ($column in $columns) {
        $header += " $($column.Label) |"
        $rule += '--:|'
    }
    $md += $header
    $md += $rule
    foreach ($benchmark in $benchmarks) {
        $line = "| $($benchmark.Name) |"
        if ($what -eq 'run') { $line += " $($benchmark.What) |" }
        foreach ($column in $columns) { $line += " $(Get-Cell $results["$($benchmark.Name)|$($column.Key)"] $what) |" }
        $md += $line
    }
    $line = "| **relative to $($baseline.Label)** |"
    if ($what -eq 'run') { $line += ' geometric mean |' }
    foreach ($column in $columns) { $line += " $(Get-Ratio $column $what) |" }
    $md += $line
}
$problems = @()
foreach ($benchmark in $benchmarks) {
    foreach ($column in $columns) {
        $result = $results["$($benchmark.Name)|$($column.Key)"]
        if ($result.Problem) { $problems += "- **$($benchmark.Name)**, $($column.Label): $(($result.Problem -split "`n")[0])" }
    }
}
if ($problems.Count -gt 0) {
    $md += ''
    $md += '## Problems'
    $md += ''
    $md += $problems
}

$csv = @()
foreach ($benchmark in $benchmarks) {
    foreach ($column in $columns) {
        $result = $results["$($benchmark.Name)|$($column.Key)"]
        $csv += [pscustomobject][ordered]@{
            benchmark    = $benchmark.Name
            compiler     = $column.Label
            version      = $column.Version
            command      = $column.Command
            compile_ms   = Format-Milliseconds (Get-Median $result.CompileTimes)
            run_ms       = Format-Milliseconds (Get-Median $result.RunTimes)
            run_min_ms   = Format-Milliseconds $(if ($result.RunTimes.Count -gt 0) { ($result.RunTimes | Measure-Object -Minimum).Minimum } else { $null })
            compile_runs = ($result.CompileTimes | ForEach-Object { Format-Milliseconds $_ }) -join ' '
            runs         = ($result.RunTimes | ForEach-Object { Format-Milliseconds $_ }) -join ' '
            problem      = ($result.Problem -split "`n")[0]
        }
    }
}

$folder = Join-Path $PSScriptRoot 'results'
New-Item -ItemType Directory -Force $folder | Out-Null
$markdown = Join-Path $folder "$Name.md"
$utf8 = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($markdown, ($md -join "`n") + "`n", $utf8)
[System.IO.File]::WriteAllText((Join-Path $folder "$Name.csv"), (($csv | ConvertTo-Csv -NoTypeInformation) -join "`n") + "`n", $utf8)
Write-Host ''
Write-Host ($md -join "`n")
Write-Host ''
Write-Host "Written $markdown and $Name.csv"
if ($problems.Count -gt 0) { exit 1 }
