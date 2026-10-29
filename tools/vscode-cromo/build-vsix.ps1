# Builds the .vsix package of the Cromo VS Code extension.
# With -Install it also installs it with "code --install-extension".
param([switch]$Install)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = $PSScriptRoot
$pkg = Get-Content (Join-Path $root 'package.json') -Raw | ConvertFrom-Json
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$vsix = Join-Path $dist "$($pkg.name)-$($pkg.version).vsix"
if (Test-Path $vsix) { Remove-Item $vsix }

$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="$($pkg.name)" Version="$($pkg.version)" Publisher="$($pkg.publisher)" />
    <DisplayName>$($pkg.displayName)</DisplayName>
    <Description xml:space="preserve">$($pkg.description)</Description>
    <Categories>Programming Languages</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($pkg.engines.vscode)" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
  </Assets>
</PackageManifest>
"@

$contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json" />
  <Default Extension=".js" ContentType="application/javascript" />
  <Default Extension=".md" ContentType="text/markdown" />
  <Default Extension=".vsixmanifest" ContentType="text/xml" />
</Types>
"@

function Add-TextEntry($zip, $name, $text) {
    $entry = $zip.CreateEntry($name)
    $writer = New-Object System.IO.StreamWriter($entry.Open(), (New-Object System.Text.UTF8Encoding($false)))
    $writer.Write($text)
    $writer.Dispose()
}

# Entry names must use forward slashes, or VS Code rejects the package.
$files = 'package.json', 'extension.js', 'language-configuration.json', 'README.md', 'syntaxes/cromo.tmLanguage.json'

$zip = [System.IO.Compression.ZipFile]::Open($vsix, 'Create')
try {
    Add-TextEntry $zip 'extension.vsixmanifest' $manifest
    Add-TextEntry $zip '[Content_Types].xml' $contentTypes
    foreach ($f in $files) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $root $f), "extension/$f") | Out-Null
    }
} finally {
    $zip.Dispose()
}

Write-Host "Created $vsix"

if ($Install) {
    & code --install-extension $vsix --force
}
