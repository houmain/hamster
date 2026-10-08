param(
  [string]$OutputDirectory = ""
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
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
  $_.Name -notin @('manifest.json', 'manifest-chromium.json')
} | Copy-Item -Destination $output -Recurse
Copy-Item -LiteralPath (Join-Path $source 'manifest-chromium.json') -Destination (Join-Path $output 'manifest.json')

Write-Host "Chromium extension written to $output"
