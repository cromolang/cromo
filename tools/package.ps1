# Makes the archives of a version of Cromo (D237, D285-D287), in dist/ with the
# layout of the site, so that publishing is copying it:
#   dist/package/win_x86_64-<version>.zip     bin/cromo.exe, lib/, extension/, the licenses
#   dist/package/linux_x86_64-<version>.tar.gz  bin/cromo, lib/, extension/, the licenses
#   dist/release/<version>, dist/latest       the manifest (D285)
#   dist/release/<version>.sig, dist/latest.sig  its signature, with -Key (D286)
# It needs the build of Windows (build.ps1), which makes lib/ for both systems, and
# the one of Linux (build-linux.sh in WSL).
# Usage: powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Key <private key>] [-Site <address>]
param(
    [string]$Key = '',
    [string]$Site = 'https://cromolang.org'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cromo = Join-Path $root 'build\cromo.exe'
$library = Join-Path $root 'build\lib'
$linuxCromo = Join-Path $root 'build-linux\cromo'
foreach ($needed in $cromo, $library, $linuxCromo) {
    if (-not (Test-Path $needed)) { throw "$needed is missing: run build.ps1, and build-linux.sh in WSL" }
}

# A path of Windows as WSL sees it: /mnt/c/...
function ConvertTo-LinuxPath([string]$Path) {
    '/mnt/' + $Path.Substring(0, 1).ToLower() + ($Path.Substring(2) -replace '\\', '/')
}

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $Arguments" }
}

# The version comes from CMakeLists.txt; the two builds must have it.
$version = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(cromo VERSION ([0-9.]+)').Groups[1].Value
$windowsVersion = (& $cromo version) -replace '^cromo ([0-9.]+).*$', '$1'
$linuxVersion = (wsl.exe -e (ConvertTo-LinuxPath $linuxCromo) version) -replace '^cromo ([0-9.]+).*$', '$1'
if ($windowsVersion -ne $version -or $linuxVersion -ne $version) {
    throw "the builds are not of version ${version}: cromo.exe is $windowsVersion, cromo for Linux is $linuxVersion"
}

$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'
$packages = Join-Path $dist 'package'
$releases = Join-Path $dist 'release'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $packages, $releases | Out-Null

# The extension for VS Code, the same in both archives.
& (Join-Path $root 'tools\vscode-cromo\build-vsix.ps1')
$extension = Get-ChildItem (Join-Path $root 'tools\vscode-cromo\dist') -Filter *.vsix | Sort-Object LastWriteTime | Select-Object -Last 1

# The contents of an archive: bin/, lib/ and extension/ at its root, which the
# installation unpacks into ~/.cromo/versions/<version> (D288).
function New-Stage([string]$Name, [string]$Compiler, [string]$Executable) {
    $folder = Join-Path $stage $Name
    New-Item -ItemType Directory -Force (Join-Path $folder 'bin'), (Join-Path $folder 'extension') | Out-Null
    Copy-Item $Compiler (Join-Path $folder "bin\$Executable")
    Copy-Item -Recurse $library (Join-Path $folder 'lib')
    Copy-Item $extension.FullName (Join-Path $folder 'extension')
    # The licenses of Cromo and of the software of others it contains.
    foreach ($file in 'LICENSE-MIT', 'LICENSE-APACHE', 'THIRD-PARTY-NOTICES.md') { Copy-Item (Join-Path $root $file) $folder }
    New-Item -ItemType Directory -Force (Join-Path $folder 'licenses') | Out-Null
    Copy-Item (Join-Path $root 'third_party\mbedtls\LICENSE') (Join-Path $folder 'licenses\mbedtls.txt')
    Copy-Item (Join-Path $root 'third_party\monocypher\LICENCE.md') (Join-Path $folder 'licenses\monocypher.md')
    $folder
}
$contents = @('bin', 'lib', 'extension', 'licenses', 'LICENSE-MIT', 'LICENSE-APACHE', 'THIRD-PARTY-NOTICES.md')

$windows = New-Stage 'win_x86_64' $cromo 'cromo.exe'
$zip = Join-Path $packages "win_x86_64-$version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Invoke-Checked 'tar.exe' (@('-a', '-c', '-f', $zip, '-C', $windows) + $contents)

# On Linux the archive is made in a folder of WSL, where the permissions can be
# set (not in /mnt/c); the symbols of cromo are stripped, as the build has no
# debug information to keep.
$linux = New-Stage 'linux_x86_64' $linuxCromo 'cromo'
$gz = Join-Path $packages "linux_x86_64-$version.tar.gz"
$script = "set -e; s=`$(mktemp -d); cp -r '$(ConvertTo-LinuxPath $linux)'/. `$s; cd `$s; strip bin/cromo; " +
          "find . -type d -exec chmod 755 {} +; find . -type f -exec chmod 644 {} +; chmod 755 bin/cromo; " +
          "tar -czf '$(ConvertTo-LinuxPath $gz)' --owner=0 --group=0 --numeric-owner $($contents -join ' '); rm -rf `$s"
Invoke-Checked 'wsl.exe' @('-e', 'sh', '-c', $script)
Remove-Item -Recurse -Force $stage

# The manifest (D285): its exact bytes are what the signature covers.
function Get-Sha256([string]$Path) { (Get-FileHash -Algorithm SHA256 $Path).Hash.ToLower() }
$manifest = @"
{
    "version": "$version",
    "archive": {
        "win_x86_64": {"package": "$Site/package/win_x86_64-$version.zip", "sha256": "$(Get-Sha256 $zip)"},
        "linux_x86_64": {"package": "$Site/package/linux_x86_64-$version.tar.gz", "sha256": "$(Get-Sha256 $gz)"}
    }
}
"@ -replace "`r`n", "`n"
$release = Join-Path $releases $version
[System.IO.File]::WriteAllText($release, $manifest + "`n", (New-Object System.Text.UTF8Encoding($false)))
Copy-Item $release (Join-Path $dist 'latest')

if ($Key) {
    Invoke-Checked $cromo @('run', (Join-Path $root 'tools\sign.cro'), '--', 'sign', $Key, $release)
    Copy-Item "$release.sig" (Join-Path $dist 'latest.sig')
} else {
    Remove-Item -ErrorAction SilentlyContinue "$release.sig", (Join-Path $dist 'latest.sig')
    Write-Warning 'without -Key the manifest is not signed: cromo update would refuse it (D286)'
}

foreach ($file in $zip, $gz) { '{0,8:N1} MB  {1}' -f ((Get-Item $file).Length / 1MB), $file }
"manifest: $release"
