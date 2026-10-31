# Builds the .vsix package of the Cromo VS Code extension, without npm: the same
# package that vsce makes, which the Visual Studio Marketplace and Open VSX accept.
# With -Install it also installs it with "code --install-extension", in place of
# cromo.cromo-lang, the name of the extension up to 0.9. With -Icon it first draws
# images/icon.png from images/icon.svg with Microsoft Edge.
param([switch]$Install, [switch]$Icon)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = $PSScriptRoot
$repository = Split-Path (Split-Path $root -Parent) -Parent

if ($Icon) {
    $edge = "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
    $edgeProfile = Join-Path ([System.IO.Path]::GetTempPath()) 'cromo-icon-edge'
    $svg = 'file:///' + (Join-Path $root 'images\icon.svg').Replace('\', '/')
    # Edge reports the file it wrote on stderr, which PowerShell 5.1 would take for an error.
    $ErrorActionPreference = 'Continue'
    & $edge --headless=new --disable-gpu "--user-data-dir=$edgeProfile" --hide-scrollbars --default-background-color=00000000 `
        --window-size=256,256 "--screenshot=$(Join-Path $root 'images\icon.png')" $svg 2>&1 | Out-Null
    $ErrorActionPreference = 'Stop'
    Remove-Item -Recurse -Force $edgeProfile -ErrorAction SilentlyContinue
}

$pkg = Get-Content (Join-Path $root 'package.json') -Raw | ConvertFrom-Json
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$vsix = Join-Path $dist "$($pkg.name)-$($pkg.version).vsix"
if (Test-Path $vsix) { Remove-Item $vsix }

function Escape([string]$text) { [System.Security.SecurityElement]::Escape($text) }

# The tags of the gallery: the keywords, the language and its extension, which
# VS Code reads to propose the extension for a .cro file.
$tags = @($pkg.keywords) + @($pkg.contributes.languages | ForEach-Object { $_.id }) +
        @($pkg.contributes.languages | ForEach-Object { $_.extensions } | ForEach-Object { '__ext_' + $_.TrimStart('.') })
$tags = ($tags | Select-Object -Unique) -join ','
$source = $pkg.repository.url

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($pkg.name)" Version="$($pkg.version)" Publisher="$($pkg.publisher)" />
    <DisplayName>$(Escape $pkg.displayName)</DisplayName>
    <Description xml:space="preserve">$(Escape $pkg.description)</Description>
    <Tags>$(Escape $tags)</Tags>
    <Categories>$(Escape ($pkg.categories -join ','))</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Badges></Badges>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($pkg.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Source" Value="$source" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Getstarted" Value="$source" />
      <Property Id="Microsoft.VisualStudio.Services.Links.GitHub" Value="$source" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Support" Value="$($pkg.bugs.url)" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Learn" Value="$($pkg.homepage)" />
      <Property Id="Microsoft.VisualStudio.Services.Branding.Color" Value="$($pkg.galleryBanner.color)" />
      <Property Id="Microsoft.VisualStudio.Services.Branding.Theme" Value="$($pkg.galleryBanner.theme)" />
      <Property Id="Microsoft.VisualStudio.Services.GitHubFlavoredMarkdown" Value="true" />
      <Property Id="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free" />
    </Properties>
    <License>extension/LICENSE.txt</License>
    <Icon>extension/$($pkg.icon)</Icon>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Changelog" Path="extension/CHANGELOG.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.License" Path="extension/LICENSE.txt" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Icons.Default" Path="extension/$($pkg.icon)" Addressable="true" />
  </Assets>
</PackageManifest>
"@

$contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json" />
  <Default Extension=".js" ContentType="application/javascript" />
  <Default Extension=".md" ContentType="text/markdown" />
  <Default Extension=".txt" ContentType="text/plain" />
  <Default Extension=".png" ContentType="image/png" />
  <Default Extension=".vsixmanifest" ContentType="text/xml" />
</Types>
"@

# The license of the extension is the one of Cromo: MIT or Apache 2.0.
$license = "Cromo, and this extension, are licensed under either of the MIT license or the Apache License 2.0, at your option.`n`n" +
    "------------------------------------------------------------------------------`n`n" +
    (Get-Content (Join-Path $repository 'LICENSE-MIT') -Raw -Encoding UTF8) +
    "`n------------------------------------------------------------------------------`n`n" +
    (Get-Content (Join-Path $repository 'LICENSE-APACHE') -Raw -Encoding UTF8)

function Add-TextEntry($zip, $name, $text) {
    $entry = $zip.CreateEntry($name)
    $writer = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
    $writer.Write($text)
    $writer.Dispose()
}

# Entry names must use forward slashes, or VS Code rejects the package.
$files = 'package.json', 'extension.js', 'language-configuration.json', 'README.md', 'CHANGELOG.md', 'syntaxes/cromo.tmLanguage.json', $pkg.icon

$zip = [System.IO.Compression.ZipFile]::Open($vsix, 'Create')
try {
    Add-TextEntry $zip 'extension.vsixmanifest' $manifest
    Add-TextEntry $zip '[Content_Types].xml' $contentTypes
    Add-TextEntry $zip 'extension/LICENSE.txt' $license
    foreach ($f in $files) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $root $f), "extension/$f") | Out-Null
    }
} finally {
    $zip.Dispose()
}

Write-Host "Created $vsix"

if ($Install) {
    $old = 'cromo.cromo-lang'
    if ((& code --list-extensions) -contains $old) {
        & code --uninstall-extension $old | Out-Null
        Write-Host "Removed $old, the old name of the extension"
    }
    & code --install-extension $vsix --force
}
