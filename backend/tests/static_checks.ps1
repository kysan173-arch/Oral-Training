param([string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path)

$ErrorActionPreference = 'Stop'
$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node) { throw 'node is required for JavaScript syntax validation.' }

$javascriptFiles = Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File -Filter '*.js' |
  Where-Object { $_.FullName -notmatch '[\\/](backend[\\/]build|tmp|node_modules)[\\/]' }
foreach ($file in $javascriptFiles) {
  & $node.Source --check $file.FullName
  if ($LASTEXITCODE -ne 0) { throw "JavaScript syntax check failed: $($file.FullName)" }
}

$jsonFiles = Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File -Filter '*.json' |
  Where-Object { $_.FullName -notmatch '[\\/](backend[\\/]build|tmp|node_modules)[\\/]' }
foreach ($file in $jsonFiles) {
  try {
    Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json | Out-Null
  } catch {
    throw "JSON validation failed: $($file.FullName): $($_.Exception.Message)"
  }
}

[pscustomobject]@{
  Result = 'passed'
  JavaScriptFiles = $javascriptFiles.Count
  JsonFiles = $jsonFiles.Count
} | ConvertTo-Json -Compress
