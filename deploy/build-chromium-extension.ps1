param(
  [string]$OutputDirectory = "",
  [string]$Version = "",
  [string]$ArchivePath = ""
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
if ($ArchivePath -and -not $Version) {
  throw 'Version is required when creating a Chromium extension archive'
}
if ($Version) {
  if ($Version -notmatch '^(0|[1-9]\d*)(\.(0|[1-9]\d*)){0,3}$') {
    throw "Invalid Chromium extension version: $Version"
  }
  $parts = $Version.Split('.') | ForEach-Object { [int64]$_ }
  if ($parts | Where-Object { $_ -gt 65535 }) {
    throw "Each Chromium extension version component must not exceed 65535: $Version"
  }
}

if (-not $OutputDirectory) {
  $OutputDirectory = Join-Path $repository '_build/chromium-extension'
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $repository '_build'))
if (-not $output.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar,
    [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "OutputDirectory must be below $buildRoot"
}

if (Test-Path -LiteralPath $output) {
  Remove-Item -LiteralPath $output -Recurse -Force
}
New-Item -ItemType Directory -Path $output | Out-Null

$source = Join-Path $repository 'webext'
Get-ChildItem -LiteralPath $source | Where-Object {
  $_.Name -notin @('.eslintrc.js', 'manifest.json', 'manifest-chromium.json')
} | Copy-Item -Destination $output -Recurse
$manifestPath = Join-Path $output 'manifest.json'
Copy-Item -LiteralPath (Join-Path $source 'manifest-chromium.json') -Destination $manifestPath

if ($Version) {
  $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
  $manifest.version = $Version
  $json = $manifest | ConvertTo-Json -Depth 100
  $encoding = New-Object System.Text.UTF8Encoding($false)
  [System.IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine, $encoding)
}

Write-Host "Chromium extension written to $output"

if ($ArchivePath) {
  $archive = [System.IO.Path]::GetFullPath($ArchivePath)
  $archiveParent = Split-Path -Parent $archive
  if ($archiveParent) {
    New-Item -ItemType Directory -Path $archiveParent -Force | Out-Null
  }
  if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
  }
  Compress-Archive -Path (Join-Path $output '*') -DestinationPath $archive
  Write-Host "Chromium extension archive written to $archive"
}
