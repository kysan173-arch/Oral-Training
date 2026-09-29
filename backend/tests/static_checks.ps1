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

& $node.Source (Join-Path $PSScriptRoot 'client_recovery_test.js')
if ($LASTEXITCODE -ne 0) { throw 'Client recovery tests failed.' }

& $node.Source (Join-Path $PSScriptRoot 'patient_client_test.js')
if ($LASTEXITCODE -ne 0) { throw 'Patient client tests failed.' }

& $node.Source (Join-Path $PSScriptRoot 'knowledge_report_client_test.js')
if ($LASTEXITCODE -ne 0) { throw 'Knowledge report client tests failed.' }

& $node.Source (Join-Path $PSScriptRoot 'audit_client_regression_test.js')
if ($LASTEXITCODE -ne 0) { throw 'Audit client regression tests failed.' }

[pscustomobject]@{
  Result = 'passed'
  JavaScriptFiles = $javascriptFiles.Count
  JsonFiles = $jsonFiles.Count
} | ConvertTo-Json -Compress

# Parse the opt-in real-model harness without executing it or making network calls.
$parseErrors = $null
$parseTokens = $null
[System.Management.Automation.Language.Parser]::ParseFile(
  (Join-Path $PSScriptRoot 'rag_controlled_smoke.ps1'), [ref]$parseTokens, [ref]$parseErrors) | Out-Null
if ($parseErrors.Count -ne 0) {
  $parseErrors | ForEach-Object { Write-Output ($_.ToString()) }
  throw 'Controlled smoke PowerShell syntax invalid.'
}
