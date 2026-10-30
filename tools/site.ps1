# Assembles the site cromolang.org in dist\site, ready to be copied to the server:
# the pages of site\, the Cromo Guide with its PDFs (tools\book.py), and the
# archives and manifests that tools\package.ps1 wrote in dist\.
# Usage: powershell -ExecutionPolicy Bypass -File tools\site.ps1 [-NoCheck]
#   -NoCheck skips compiling and running the examples of the guide
param([switch]$NoCheck)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
$out = Join-Path $dist 'site'
$version = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(cromo VERSION ([0-9.]+)').Groups[1].Value

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

# The pages, with the version of the archives they link.
Copy-Item (Join-Path $root 'site\*') $out -Recurse
$index = Join-Path $out 'index.html'
$text = [System.IO.File]::ReadAllText($index) -replace '@VERSION@', $version
[System.IO.File]::WriteAllText($index, $text, (New-Object System.Text.UTF8Encoding($false)))

# The guide, in every language, with its PDFs.
$arguments = @((Join-Path $root 'tools\book.py'), '--out', $out, '--pdf')
if (-not $NoCheck) { $arguments += '--check' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'the guide was not built' }

# The archives and the manifests of this version (D285-D287).
New-Item -ItemType Directory -Force (Join-Path $out 'package'), (Join-Path $out 'release') | Out-Null
foreach ($item in "package\win_x86_64-$version.zip", "package\linux_x86_64-$version.tar.gz", "release\$version", "release\$version.sig", 'latest', 'latest.sig') {
    $path = Join-Path $dist $item
    if (Test-Path $path) { Copy-Item $path (Join-Path $out $item) }
}
foreach ($archive in "package\win_x86_64-$version.zip", "package\linux_x86_64-$version.tar.gz") {
    if (-not (Test-Path (Join-Path $out $archive))) { Write-Warning "$archive is missing: run tools\package.ps1 first" }
}
$latest = Join-Path $out 'latest'
if ((Test-Path $latest) -and -not ((Get-Content $latest -Raw) -match "`"version`": `"$version`"")) {
    Write-Warning "latest is not the manifest of ${version}: run tools\package.ps1 first"
}

$size = (Get-ChildItem $out -Recurse -File | Measure-Object -Sum Length).Sum / 1MB
'site: {0} ({1:N1} MB)' -f $out, $size
