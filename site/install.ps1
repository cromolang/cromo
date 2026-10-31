# Installs Cromo for the current user, without administrator rights:
#
#   irm https://cromolang.org/install.ps1 | iex
#
# It downloads the archive of the latest version for Windows x64, checks its
# SHA-256 against the manifest, unpacks it in %USERPROFILE%\.cromo\versions\<version>,
# makes it the active version (%USERPROFILE%\.cromo\current), adds its bin folder to
# the PATH of the user, and installs the extension for Visual Studio Code if the
# command code is there. CROMO_SITE can name another site, for tests, and
# CROMO_NO_MODIFY_PATH=1 leaves the PATH and Visual Studio Code as they are.

& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $site = if ($env:CROMO_SITE) { $env:CROMO_SITE.TrimEnd('/') } else { 'https://cromolang.org' }
    $root = Join-Path $env:USERPROFILE '.cromo'

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'Cromo needs a 64-bit Windows'
    }

    # The manifest of the latest version: its number, and the address and SHA-256 of each archive.
    $response = Invoke-WebRequest -UseBasicParsing "$site/latest"
    $text = [Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
    $manifest = $text | ConvertFrom-Json
    $version = $manifest.version
    $archive = $manifest.archive.win_x86_64
    if (-not $version -or -not $archive) { throw "the manifest of $site/latest has no archive for Windows x64" }

    $target = Join-Path $root "versions\$version"
    if (Test-Path (Join-Path $target 'bin\cromo.exe')) {
        Write-Host "Cromo $version is already installed."
    } else {
        $temp = Join-Path ([IO.Path]::GetTempPath()) "cromo-install-$PID"
        New-Item -ItemType Directory -Force $temp | Out-Null
        try {
            $zip = Join-Path $temp "win_x86_64-$version.zip"
            Write-Host "Downloading Cromo $version..."
            Invoke-WebRequest -UseBasicParsing $archive.package -OutFile $zip
            $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
            if ($hash -ne $archive.sha256) {
                throw "the archive is damaged: its SHA-256 is $hash instead of $($archive.sha256)"
            }
            $partial = "$target.partial"
            if (Test-Path $partial) { Remove-Item -Recurse -Force $partial }
            Expand-Archive -Path $zip -DestinationPath $partial
            Move-Item $partial $target
        } finally {
            Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $temp
        }
    }

    # The active version is the junction current, which needs no administrator.
    $current = Join-Path $root 'current'
    if (Test-Path $current) { cmd /c rmdir "$current" | Out-Null }
    New-Item -ItemType Junction -Path $current -Target $target | Out-Null

    # The bin folder of the active version, on the PATH of the user.
    $bin = Join-Path $current 'bin'
    $modify = -not $env:CROMO_NO_MODIFY_PATH
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $parts = @($userPath -split ';' | Where-Object { $_ })
    if ($modify -and $parts -notcontains $bin) {
        [Environment]::SetEnvironmentVariable('Path', (@($bin) + $parts) -join ';', 'User')
        Write-Host "Added $bin to the PATH of the user."
    }
    if (($env:Path -split ';') -notcontains $bin) { $env:Path = "$bin;$env:Path" }

    # The extension for Visual Studio Code.
    if ($modify -and (Get-Command code -ErrorAction SilentlyContinue)) {
        $vsix = Get-ChildItem (Join-Path $target 'extension') -Filter *.vsix -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($vsix) {
            & code --install-extension $vsix.FullName --force | Out-Null
            Write-Host 'Installed the extension for Visual Studio Code.'
            # Up to 0.27 the extension was cromo.cromo-lang: two would both start cromo lsp.
            if ($vsix.Name -notlike 'cromo-lang-*' -and ((& code --list-extensions) -contains 'cromo.cromo-lang')) {
                & code --uninstall-extension cromo.cromo-lang | Out-Null
            }
        }
    }

    Write-Host ''
    Write-Host "Cromo $version is installed in $target."
    Write-Host 'Open a new terminal and run: cromo version'
    Write-Host 'The Cromo Guide: https://cromolang.org/book/'
}
