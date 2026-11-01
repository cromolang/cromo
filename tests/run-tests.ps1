# Behavior tests: compiles every .cro under tests/ and checks the result against
# the directives written in its comments. The files in a folder named `modules`
# are imported by the tests, and are not tests themselves (D50); neither are the
# files name_test.cro, parts of the module name.cro for cromo test (D350). The files in a
# folder named `formatting` are cases of cromo fmt (D161): x.cro must become
# x.formatted.cro, which must not change again; without x.formatted.cro, cromo
# fmt must refuse x.cro. The files in a folder named `lsp` are opened in cromo
# lsp (D170), and checked with these directives, positions as line:column:
#   // lsp-hover: 9:5 text           the hover there contains the text
#   // lsp-definition: 9:5 3:9       the definition is there, or in another file: x.cro:3:9
#   // lsp-error: 4:13 text          an error there, with the text; no other error is allowed
#   // lsp-symbols: Point, Point.x   symbols of the file, the members after their type
#   // lsp-completion: 9:7 len, !data  proposed there, and not proposed with a '!'
#   // cromo-flags: --release       extra options for cromo
#   // expect-exit: 42              exit code of the program
#   // expect-output: text          one line of standard output (repeatable, exact match)
#   // expect-output-contains: text text that must appear in the standard output
#   // expect-stderr: text          text that must appear in the standard error
#   // expect-error: text           text that must appear among the compiler errors
#   // expect-warning: text         text that must appear among the compiler warnings
#   // target: windows              the test is only for that system (windows or linux)
#   // program-arg: text           an argument of the program (repeatable, exact)
#   // program-env: NAME=value     a variable of the environment of the program
#                                  (repeatable); {dir} is the folder of the test,
#                                  {cromo} the cromo of the tests, {port} a free port
#   // cromo-command: run          compiles and runs with cromo run, instead of build;
#                                  test runs cromo test, the program arguments being
#                                  the names of the tests
# Usage: powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1 [-Cromo <path>] [-Filter <text>] [-Flags <options>]
#   -Flags passes extra options to every compilation, for example -Flags -O0
#   -Flags '--target linux' compiles for Linux and runs the programs in WSL (D142),
#   with -Distro to choose the distribution instead of the default one
# The C libraries in m8\native are built first with the clang of LLVM (D136).
param(
    [string]$Cromo = (Join-Path $PSScriptRoot '..\build\cromo.exe'),
    [string]$Filter = '',
    [string]$Flags = '',
    [string]$LlvmRoot = 'C:\software\llvm-23.1.4',
    [string]$Distro = ''
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
# The same static library for Linux (D143).
Invoke-Tool 'clang.exe' ($cflags + @('--target=x86_64-unknown-linux-gnu', '-fno-pic', "$native\scale.c", '-o', "$out\scale.o"))
Remove-Item -Force -ErrorAction SilentlyContinue "$modules\libscale.a"
Invoke-Tool 'llvm-ar.exe' @('rcs', "$modules\libscale.a", "$out\scale.o")

# The system of an executable: the last --target among the options of cromo.
function Get-Target([string[]]$Options) {
    $target = 'windows'
    for ($i = 0; $i -lt $Options.Count - 1; $i++) {
        if ($Options[$i] -eq '--target') { $target = $Options[$i + 1] }
    }
    $target
}

# A path of Windows as WSL sees it: /mnt/c/...
function ConvertTo-LinuxPath([string]$Path) {
    '/mnt/' + $Path.Substring(0, 1).ToLower() + ($Path.Substring(2) -replace '\\', '/')
}

# A program for Linux runs in WSL, from the same folder; its variables go
# through env, because WSL does not pass those of Windows.
function Invoke-Linux([string]$Path, [string[]]$Arguments, [hashtable]$Environment = @{}) {
    $options = @()
    if ($Distro) { $options += @('-d', $Distro) }
    $variables = @()
    if ($Environment.Count -gt 0) {
        $variables = @('env') + ($Environment.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" })
    }
    Invoke-Program 'wsl.exe' ($options + @('-e') + $variables + @((ConvertTo-LinuxPath $Path)) + $Arguments)
}

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

# Runs a program and returns its exit code, standard output and standard error,
# read separately and decoded as UTF-8. $StandardInput, if given, are the bytes of
# its standard input. $Environment adds variables to its environment.
function Invoke-Program([string]$Path, [string[]]$Arguments, [byte[]]$StandardInput = $null, [hashtable]$Environment = @{}) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Path
    foreach ($variable in $Environment.GetEnumerator()) { $info.EnvironmentVariables[$variable.Key] = $variable.Value }
    $info.Arguments = ($Arguments | ForEach-Object { ConvertTo-Argument $_ }) -join ' '
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [System.Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    if ($null -ne $StandardInput) { $info.RedirectStandardInput = $true }
    $process = [System.Diagnostics.Process]::Start($info)
    if ($null -ne $StandardInput) {
        $process.StandardInput.BaseStream.Write($StandardInput, 0, $StandardInput.Length)
        $process.StandardInput.Close()
    }
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
$skipped = 0
foreach ($test in Get-ChildItem $PSScriptRoot -Recurse -Filter *.cro | Sort-Object FullName) {
    $name = $test.FullName.Substring($PSScriptRoot.Length + 1)
    $folders = $name -split '\\'
    if ($folders -contains 'modules' -or $folders -contains 'formatting' -or $folders -contains 'lsp') { continue }
    if ($test.BaseName.EndsWith('_test')) { continue }
    if ($Filter -and -not $name.Contains($Filter)) { continue }

    # Not $flags: names are case-insensitive, it would overwrite the -Flags parameter.
    $compileFlags = @()
    if ($Flags) { $compileFlags += ($Flags -split '\s+') }
    $expectExit = $null
    $expectOutput = @()
    $expectContains = @()
    $expectStderr = @()
    $expectError = @()
    $expectWarning = @()
    $only = ''
    $programArgs = @()
    $programEnv = @{}
    $command = 'build'
    foreach ($line in Get-Content $test.FullName -Encoding UTF8) {
        if ($line -match '^\s*//\s*cromo-flags:\s*(.+?)\s*$') { $compileFlags += ($matches[1] -split '\s+') }
        elseif ($line -match '^\s*//\s*expect-exit:\s*(-?\d+)\s*$') { $expectExit = [int64]$matches[1] }
        elseif ($line -match '^\s*//\s*expect-output-contains:\s?(.*?)\s*$') { $expectContains += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-output:\s?(.*)$') { $expectOutput += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-stderr:\s*(.+?)\s*$') { $expectStderr += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-error:\s*(.+?)\s*$') { $expectError += $matches[1] }
        elseif ($line -match '^\s*//\s*expect-warning:\s*(.+?)\s*$') { $expectWarning += $matches[1] }
        elseif ($line -match '^\s*//\s*target:\s*(\w+)\s*$') { $only = $matches[1] }
        elseif ($line -match '^\s*//\s*program-arg:\s?(.*)$') { $programArgs += $matches[1] }
        elseif ($line -match '^\s*//\s*program-env:\s*([^=\s]+)=(.*?)\s*$') { $programEnv[$matches[1]] = $matches[2] }
        elseif ($line -match '^\s*//\s*cromo-command:\s*(\w+)\s*$') { $command = $matches[1] }
    }
    $target = Get-Target $compileFlags
    if ($only -and $only -ne $target) {
        $skipped++
        continue
    }
    $folder = $test.DirectoryName
    if ($target -eq 'linux') { $folder = ConvertTo-LinuxPath $folder }
    $port = ''
    if (@($programEnv.Values | Where-Object { $_.Contains('{port}') }).Count -gt 0) {
        # A port free now, for a server of the test that the programs it starts reach.
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $listener.Start()
        $port = [string]$listener.LocalEndpoint.Port
        $listener.Stop()
    }
    foreach ($key in @($programEnv.Keys)) {
        $programEnv[$key] = $programEnv[$key].Replace('{dir}', $folder).Replace('{cromo}', $Cromo).Replace('{port}', $port)
    }

    $exe = Join-Path $out ($test.BaseName + $(if ($target -eq 'linux') { '' } else { '.exe' }))
    if ($command -eq 'run') {
        # cromo run compiles and runs: its result is the one of the program (D156).
        $compile = Invoke-Program $Cromo (@('run') + $compileFlags + @($test.FullName, '--') + $programArgs)
    } elseif ($command -eq 'test') {
        # cromo test compiles and runs the tests: its result is theirs (D352).
        $compile = Invoke-Program $Cromo (@('test') + $compileFlags + @($test.FullName) + $programArgs)
    } else {
        $compile = Invoke-Program $Cromo (@('build', $test.FullName, '-o', $exe) + $compileFlags)
    }
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
    } elseif ($compile.Code -ne 0 -and $command -eq 'build') {
        $problems += "compilation failed:`n$compileText"
    } else {
        if ($command -ne 'build') {
            $run = $compile
        } elseif ($target -eq 'linux') {
            $run = Invoke-Linux $exe $programArgs $programEnv
            # Linux keeps only the lowest byte of the exit code (D145).
            if ($null -ne $expectExit) { $expectExit = $expectExit -band 0xFF }
        } else {
            $run = Invoke-Program $exe $programArgs $null $programEnv
        }
        if ($null -ne $expectExit -and $run.Code -ne $expectExit) {
            $problems += "exit code $($run.Code), expected $expectExit"
        }
        if ($expectOutput.Count -gt 0) {
            $expected = $expectOutput -join "`n"
            $actual = $run.Out.TrimEnd("`n")
            if ($actual -ne $expected) { $problems += "output:`n$actual`nexpected:`n$expected" }
        }
        foreach ($e in $expectContains) {
            if (-not $run.Out.Contains($e)) { $problems += "missing in the output: $e`noutput was: $($run.Out)" }
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

# The cases of cromo fmt, with the text on the standard input (D164).
foreach ($case in Get-ChildItem $PSScriptRoot -Recurse -Filter *.cro | Sort-Object FullName) {
    $name = $case.FullName.Substring($PSScriptRoot.Length + 1)
    if (-not ($name -split '\\' -contains 'formatting') -or $case.Name.EndsWith('.formatted.cro')) { continue }
    if ($Filter -and -not $name.Contains($Filter)) { continue }
    $expectedPath = $case.FullName -replace '\.cro$', '.formatted.cro'
    $result = Invoke-Program $Cromo @('fmt', '-') ([System.IO.File]::ReadAllBytes($case.FullName))
    $problems = @()
    if (-not (Test-Path $expectedPath)) {
        if ($result.Code -ne 1 -or $result.Out) { $problems += "cromo fmt should refuse it, exit code $($result.Code)" }
    } else {
        $expected = [System.IO.File]::ReadAllText($expectedPath, [System.Text.Encoding]::UTF8) -replace "`r`n", "`n"
        if ($result.Code -ne 0) {
            $problems += "cromo fmt failed:`n$($result.Err)"
        } elseif ($result.Out -ne $expected) {
            $problems += "formatted:`n$($result.Out)expected:`n$expected"
        }
        $again = Invoke-Program $Cromo @('fmt', '-') ([System.IO.File]::ReadAllBytes($expectedPath))
        if ($again.Out -ne $expected) { $problems += 'formatting the expected text changes it' }
    }
    if ($problems.Count -eq 0) {
        $passed++
        Write-Host "ok    $name (fmt)"
    } else {
        $failed++
        Write-Host "FAIL  $name (fmt)" -ForegroundColor Red
        foreach ($p in $problems) { Write-Host "      $p" }
    }
}

# A session of cromo lsp: it opens one file and asks the requests, each a hashtable
# with method and params. Returns the responses and the last diagnostics of the file.
function Invoke-LanguageServer([string]$Path, [object[]]$Requests) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Cromo
    $info.Arguments = 'lsp'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $stdin = $process.StandardInput.BaseStream
    $stdout = $process.StandardOutput.BaseStream
    $uri = ([System.Uri]$Path).AbsoluteUri

    $send = {
        param($message)
        $body = [System.Text.Encoding]::UTF8.GetBytes(($message | ConvertTo-Json -Depth 20 -Compress))
        $header = [System.Text.Encoding]::ASCII.GetBytes("Content-Length: $($body.Length)`r`n`r`n")
        $stdin.Write($header, 0, $header.Length)
        $stdin.Write($body, 0, $body.Length)
        $stdin.Flush()
    }
    $readBytes = {
        param([int]$count)
        $buffer = New-Object byte[] $count
        $done = 0
        while ($done -lt $count) {
            $task = $stdout.ReadAsync($buffer, $done, $count - $done)
            if (-not $task.Wait(20000) -or $task.Result -le 0) { throw 'cromo lsp did not answer' }
            $done += $task.Result
        }
        , $buffer
    }
    $read = {
        $header = ''
        while (-not $header.EndsWith("`r`n`r`n")) { $header += [char](& $readBytes 1)[0] }
        $length = [int]([regex]::Match($header, 'Content-Length: *(\d+)').Groups[1].Value)
        [System.Text.Encoding]::UTF8.GetString((& $readBytes $length)) | ConvertFrom-Json
    }

    $diagnostics = @()
    $responses = @()
    $id = 0
    $all = @(@{ method = 'initialize'; params = @{ processId = $null; rootUri = ([System.Uri](Split-Path $PSScriptRoot)).AbsoluteUri; capabilities = @{} } })
    $all += $Requests
    $all += @{ method = 'shutdown'; params = $null }
    foreach ($request in $all) {
        $id++
        & $send @{ jsonrpc = '2.0'; id = $id; method = $request.method; params = $request.params }
        for (;;) {
            $message = & $read
            if ($message.method -eq 'textDocument/publishDiagnostics') {
                if ($message.params.uri -eq $uri) { $diagnostics = @($message.params.diagnostics) }
                continue
            }
            if ($message.id -eq $id) { break }
        }
        if ($id -eq 1) {
            & $send @{ jsonrpc = '2.0'; method = 'initialized'; params = @{} }
            $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
            & $send @{ jsonrpc = '2.0'; method = 'textDocument/didOpen'; params = @{ textDocument = @{ uri = $uri; languageId = 'cromo'; version = 1; text = $text } } }
        } elseif ($id -le $Requests.Count + 1) {
            $responses += , $message.result
        }
    }
    & $send @{ jsonrpc = '2.0'; method = 'exit'; params = $null }
    if (-not $process.WaitForExit(10000)) { $process.Kill() }
    [pscustomobject]@{ Responses = $responses; Diagnostics = $diagnostics; Code = $process.ExitCode; Uri = $uri }
}

# The cases of cromo lsp (D170-D173).
foreach ($case in Get-ChildItem $PSScriptRoot -Recurse -Filter *.cro | Sort-Object FullName) {
    $name = $case.FullName.Substring($PSScriptRoot.Length + 1)
    if (-not ($name -split '\\' -contains 'lsp')) { continue }
    if ($Filter -and -not $name.Contains($Filter)) { continue }
    $checks = @()
    foreach ($line in Get-Content $case.FullName -Encoding UTF8) {
        if ($line -match '^\s*//\s*lsp-(hover|definition|error):\s*(\d+):(\d+)\s+(.+?)\s*$') {
            $checks += [pscustomobject]@{ Kind = $matches[1]; Line = [int]$matches[2]; Column = [int]$matches[3]; Value = $matches[4] }
        } elseif ($line -match '^\s*//\s*lsp-completion:\s*(\d+):(\d+)\s+(.+?)\s*$') {
            $checks += [pscustomobject]@{ Kind = 'completion'; Line = [int]$matches[1]; Column = [int]$matches[2]; Value = $matches[3] }
        } elseif ($line -match '^\s*//\s*lsp-symbols:\s*(.+?)\s*$') {
            $checks += [pscustomobject]@{ Kind = 'symbols'; Value = $matches[1] }
        }
    }
    if ($checks.Count -eq 0) { continue }  # a file of a program, opened through another one

    $uri = ([System.Uri]$case.FullName).AbsoluteUri
    $requests = @()
    foreach ($check in $checks) {
        if ($check.Kind -eq 'error') { continue }
        $method = @{ hover = 'textDocument/hover'; definition = 'textDocument/definition'; symbols = 'textDocument/documentSymbol'; completion = 'textDocument/completion' }[$check.Kind]
        $params = @{ textDocument = @{ uri = $uri } }
        if ($check.Kind -ne 'symbols') { $params.position = @{ line = $check.Line - 1; character = $check.Column - 1 } }
        $requests += @{ method = $method; params = $params }
    }
    $problems = @()
    try {
        $session = Invoke-LanguageServer $case.FullName $requests
        $k = 0
        foreach ($check in $checks) {
            $at = "$($check.Line):$($check.Column)"
            if ($check.Kind -eq 'error') {
                $found = $session.Diagnostics | Where-Object {
                    $_.range.start.line + 1 -eq $check.Line -and $_.range.start.character + 1 -eq $check.Column -and $_.message.Contains($check.Value)
                }
                if (-not $found) { $problems += "missing error at ${at}: $($check.Value)" }
                continue
            }
            $result = $session.Responses[$k++]
            if ($check.Kind -eq 'hover') {
                if (-not $result -or -not $result.contents.value.Contains($check.Value)) {
                    $problems += "hover at ${at}: $(if ($result) { $result.contents.value } else { 'none' }), expected $($check.Value)"
                }
            } elseif ($check.Kind -eq 'definition') {
                $parts = $check.Value -split ':'
                $file = if ($parts.Count -eq 3) { $parts[0] } else { $case.Name }
                $target = "$($parts[-2]):$($parts[-1])"
                $actual = if ($result) { "$($result.uri.Split('/')[-1]) $($result.range.start.line + 1):$($result.range.start.character + 1)" } else { 'none' }
                if ($actual -ne "$file $target") { $problems += "definition at ${at}: $actual, expected $file $target" }
            } elseif ($check.Kind -eq 'completion') {
                $labels = @($result | ForEach-Object { $_.label })
                foreach ($expected in ($check.Value -split ',\s*')) {
                    if ($expected.StartsWith('!')) {
                        if ($labels -contains $expected.Substring(1)) { $problems += "completion at ${at} proposes $($expected.Substring(1))" }
                    } elseif ($labels -notcontains $expected) {
                        $problems += "completion at ${at} misses $expected (found: $($labels -join ', '))"
                    }
                }
            } else {
                $names = @()
                foreach ($symbol in $result) {
                    $names += $symbol.name
                    foreach ($child in $symbol.children) { $names += "$($symbol.name).$($child.name)" }
                }
                foreach ($expected in ($check.Value -split ',\s*')) {
                    if ($names -notcontains $expected) { $problems += "missing symbol $expected (found: $($names -join ', '))" }
                }
            }
        }
        $expectedErrors = @($checks | Where-Object { $_.Kind -eq 'error' }).Count
        if ($session.Diagnostics.Count -ne $expectedErrors) {
            $problems += "$($session.Diagnostics.Count) errors, expected ${expectedErrors}: $(($session.Diagnostics | ForEach-Object { $_.message }) -join '; ')"
        }
        if ($session.Code -ne 0) { $problems += "cromo lsp ended with $($session.Code)" }
    } catch {
        $problems += "$_"
    }
    if ($problems.Count -eq 0) {
        $passed++
        Write-Host "ok    $name (lsp)"
    } else {
        $failed++
        Write-Host "FAIL  $name (lsp)" -ForegroundColor Red
        foreach ($p in $problems) { Write-Host "      $p" }
    }
}

Write-Host ""
Write-Host "$passed passed, $failed failed, $skipped for the other system"
if ($failed -gt 0) { exit 1 }
