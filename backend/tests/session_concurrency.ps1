param(
  [Parameter(Mandatory = $true)] [string]$DatabaseUrl,
  [string]$BaseUrl = 'http://127.0.0.1:8080/api',
  [string]$PsqlPath = 'C:\Program Files\PostgreSQL\18\bin\psql.exe'
)

$ErrorActionPreference = 'Stop'
$databaseName = ([Uri]$DatabaseUrl).AbsolutePath.Trim('/')
if ($databaseName -notmatch '(?i)(test|ci)') {
  throw 'Session concurrency test requires a disposable database whose name contains test or ci.'
}
Add-Type -AssemblyName System.Net.Http
$trainingSessionId = $null
$roleplaySessionId = $null
$trainingResults = @()
$roleplayResults = @()
$jobs = @()

function Invoke-JsonApi {
  param([string]$Method, [string]$Path, [object]$Body, [string]$Token)
  $arguments = @{ Method = $Method; Uri = "$BaseUrl$Path"; TimeoutSec = 60 }
  if ($Token) { $arguments.Headers = @{ Authorization = "Bearer $Token" } }
  if ($null -ne $Body) {
    $arguments.ContentType = 'application/json'
    $arguments.Body = $Body | ConvertTo-Json -Compress
  }
  (Invoke-RestMethod @arguments).data
}

function Invoke-Health {
  $client = [System.Net.Http.HttpClient]::new()
  try {
    $response = $client.GetAsync("$BaseUrl/health").GetAwaiter().GetResult()
    $payload = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult() | ConvertFrom-Json
    if ([int]$response.StatusCode -notin @(200, 503) -or $payload.code -ne 0) {
      throw "Health API failed: HTTP $([int]$response.StatusCode) / $($payload.code)"
    }
    return $payload.data
  } finally {
    $client.Dispose()
  }
}

function Invoke-ConcurrentCreate {
  param([string]$Url, [string]$Token, [string]$ScenarioId)
  $script:jobs = 1..20 | ForEach-Object {
    Start-Job -ScriptBlock {
      param($RequestUrl, $AccessToken, $RequestedScenarioId)
      Add-Type -AssemblyName System.Net.Http
      $client = [System.Net.Http.HttpClient]::new()
      try {
        $client.DefaultRequestHeaders.Authorization =
          [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $AccessToken)
        $payload = @{ scenarioId = $RequestedScenarioId } | ConvertTo-Json -Compress
        $body = [System.Net.Http.StringContent]::new(
          $payload, [System.Text.Encoding]::UTF8, 'application/json')
        $response = $client.PostAsync($RequestUrl, $body).GetAwaiter().GetResult()
        [pscustomobject]@{
          Status = [int]$response.StatusCode
          Body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        }
      } finally {
        $client.Dispose()
      }
    } -ArgumentList $Url, $Token, $ScenarioId
  }
  $results = @($script:jobs | Wait-Job | Receive-Job)
  $script:jobs | Remove-Job -Force
  $script:jobs = @()
  $results
}

function Assert-CreateResults {
  param([object[]]$Results, [string]$ConflictCode)
  if ($Results.Count -ne 20) {
    throw "Expected 20 concurrent responses, got $($Results.Count)."
  }
  $unexpected = @($Results | Where-Object { $_.Status -notin @(201, 409) })
  if ($unexpected.Count -gt 0) {
    throw "Unexpected concurrent create status: $($unexpected.Status -join ',')"
  }
  $created = @($Results | Where-Object { $_.Status -eq 201 })
  if ($created.Count -ne 1) {
    throw "Expected exactly one successful create, got $($created.Count)."
  }
  foreach ($conflict in @($Results | Where-Object { $_.Status -eq 409 })) {
    $payload = $conflict.Body | ConvertFrom-Json
    if ($payload.code -ne $ConflictCode) {
      throw "Expected conflict code $ConflictCode, got $($payload.code)."
    }
  }
  ($created[0].Body | ConvertFrom-Json).data.session.id
}

try {
  $login = Invoke-JsonApi POST '/auth/wechat' @{ code = 'session-concurrency-test' } ''
  $token = $login.accessToken

  $scenarios = Invoke-JsonApi GET '/scenarios' $null $token
  $scenario = $scenarios.items | Where-Object { $null -eq $_.activeSession } | Select-Object -First 1
  if (-not $scenario) { throw 'No idle training scenario is available.' }
  $trainingResults = Invoke-ConcurrentCreate "$BaseUrl/sessions" $token $scenario.id
  $trainingSessionId = Assert-CreateResults $trainingResults 'SESSION_IN_PROGRESS'

  $roleplayScenarios = Invoke-JsonApi GET '/roleplay/scenarios' $null $token
  $roleplayScenario = $roleplayScenarios.items |
    Where-Object { $null -eq $_.activeSession } | Select-Object -First 1
  if (-not $roleplayScenario) { throw 'No idle roleplay scenario is available.' }
  $roleplayResults = Invoke-ConcurrentCreate "$BaseUrl/roleplay/sessions" $token $roleplayScenario.id
  $roleplaySessionId = Assert-CreateResults $roleplayResults 'ROLEPLAY_SESSION_IN_PROGRESS'

  $health = Invoke-Health
  if (-not $health.database -or -not $health.workerRunning -or
      $health.databasePool.maximum -lt 4 -or
      $health.databasePool.open -gt $health.databasePool.maximum -or
      $health.databasePool.waiting -ne 0) {
    throw 'Connection pool was not healthy and bounded after concurrent requests.'
  }

  [pscustomobject]@{
    Result = 'passed'
    RequestsPerMode = 20
    PoolMaximum = $health.databasePool.maximum
    PoolOpen = $health.databasePool.open
    TrainingSessionId = $trainingSessionId
    RoleplaySessionId = $roleplaySessionId
  } | ConvertTo-Json -Compress
} finally {
  if ($jobs) { $jobs | Remove-Job -Force -ErrorAction SilentlyContinue }
  $trainingIds = @($trainingResults | Where-Object { $_.Status -eq 201 } | ForEach-Object {
    ($_.Body | ConvertFrom-Json).data.session.id
  } | Sort-Object -Unique)
  if ($trainingIds.Count -gt 0) {
    $quotedIds = ($trainingIds | ForEach-Object { "'$_'" }) -join ','
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "DELETE FROM sessions WHERE id IN ($quotedIds);"
  }
  $roleplayIds = @($roleplayResults | Where-Object { $_.Status -eq 201 } | ForEach-Object {
    ($_.Body | ConvertFrom-Json).data.session.id
  } | Sort-Object -Unique)
  if ($roleplayIds.Count -gt 0) {
    $quotedIds = ($roleplayIds | ForEach-Object { "'$_'" }) -join ','
    & $PsqlPath --dbname=$DatabaseUrl -v ON_ERROR_STOP=1 -X -q -c "DELETE FROM roleplay_sessions WHERE id IN ($quotedIds);"
  }
}
